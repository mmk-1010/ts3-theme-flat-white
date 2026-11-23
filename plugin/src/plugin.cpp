// Flat White UI plugin for TeamSpeak 3
//
// Stage 1: probe. Dumps the live widget tree so we can design a row delegate
// and a new panel layout against the real structure instead of guessing.
//
// This is an in-process Qt plugin: it links the same Qt 5.15.2 DLLs the client
// already loaded, so widget pointers are shared with the client.

#include <QApplication>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QMetaMethod>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegion>
#include <QScopedValueRollback>
#include <QTime>
#include <QLabel>
#include <QMainWindow>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QToolBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QGridLayout>
#include <QSizePolicy>
#include <QWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QVariantAnimation>

#include <windows.h>
#include <dwmapi.h>

#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "channelpanel.h"
#include "foundation_motion.h"

#include <array>
#include <chrono>
#include <functional>
#include <thread>

void cleanupUi();

namespace {

using foundation::Color;

TS3Functions g_funcs{};
QByteArray g_pluginId;
QPointer<ChannelSidebar> g_panel;
QPointer<UserPanel> g_userPanel;
QPointer<ChatHeader> g_chatHeader;
QPointer<QWidget> g_panelHost;
QPointer<QObject> g_paneFilter;
QPointer<QScrollArea> g_panelScroll;
QPointer<QWidget> g_stockToolbar;
void logLine(const QString& line);
QPointer<QObject> g_guard;
QPointer<QTimer> g_timer;
QPointer<QTimer> g_stateTimer;
QPointer<QWidget> g_banner;
QPointer<QWidget> g_bannerHome;
int g_bannerIndex = -1;
int g_bannerStretch = 0;
int g_bannerMaximumHeight = QWIDGETSIZE_MAX;
bool g_bannerWasVisible = false;
QPointer<QTreeView> g_paneTree;
QPointer<QWidget> g_infoFrame;
QPointer<QWidget> g_infoPage;
QPointer<QWidget> g_cardHost;
QPointer<QWidget> g_contentHost;
QPointer<QWidget> g_nativeContent;
QPointer<QStackedWidget> g_chatStack;
QPointer<QWidget> g_infoBar;
QPointer<QPushButton> g_infoBack;
QPointer<QLabel> g_infoTitle;
QPointer<QLabel> g_infoStatus;
QPointer<QWidget> g_infoHome;
QPointer<QSplitter> g_infoSplitter;
int g_infoIndex = -1;
int g_infoStretch = 0;
Qt::Alignment g_infoAlignment;
QPointer<QTreeView> g_infoTree;
QMetaObject::Connection g_infoConn;
QPointer<ModernChat> g_modernChat;
QPointer<NotifyToast> g_notifyToast;
QPointer<QTabWidget> g_chatTab;
QPointer<QObject> g_chatFilter;
QHash<QString, QVector<ChatMessage>> g_chatHistory;
QHash<uint64, int> g_connectionStatus;
// Saved channel and private chat offsets; -1 means the reader was at the bottom.
// The server log opens at the newest event.
QHash<QString, int> g_chatScroll;
QString g_overlayKey;
// Conversation the visible notification card belongs to. Cleared as soon as
// the card is dismissed, so a later tab change cannot hide a newer card.
QString g_notifyKey;
anyID g_notifyMode = 0;
QString g_notifyTabName;
bool g_infoShown = false;
QMetaObject::Connection g_chatTabConn;
QPointer<QTreeView> g_styledTree;
QPointer<NativeTreeRows> g_treeRows;
QPointer<QWidget> g_userPanelHost;
QHash<QTreeView*, QPointer<QAbstractItemDelegate>> g_oldDelegates;
bool g_installed = false;
bool g_firstPaintDone = false;
// -1 = not detected yet, 0 = light stylesheet, 1 = dark stylesheet.
int g_darkMode = -1;

void openPrivateChat(unsigned short clientId, const QString& name);
void openNotificationTarget(uint64 schid, anyID targetMode, anyID clientId,
                            const QString& tabName);

// Keeps the custom sidebar covering the stock tree pane without taking part in
// its layout, so the tree keeps valid geometry for synthetic events.
class PaneFilter : public QObject {
public:
    explicit PaneFilter(QObject* parent = nullptr) : QObject(parent) {}
    QPointer<QWidget> overlay;
    std::function<void()> onResize;
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::Resize) {
            if (auto* w = qobject_cast<QWidget*>(obj)) {
                if (overlay) overlay->setGeometry(w->rect());
            }
            if (onResize) onResize();
        }
        return QObject::eventFilter(obj, ev);
    }
};

class OverlayFilter : public QObject {
public:
    explicit OverlayFilter(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void()> onLayout;
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (!m_pending && (ev->type() == QEvent::Resize || ev->type() == QEvent::Move ||
                           ev->type() == QEvent::Show || ev->type() == QEvent::LayoutRequest)) {
            m_pending = true;
            QTimer::singleShot(0, this, [this] {
                m_pending = false;
                if (onLayout) onLayout();
            });
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    bool m_pending = false;
};

QFile* g_log = nullptr;
QPointer<QTreeView> g_tree;

void openLog() {
    if (g_log && g_log->isOpen()) return;
    if (!g_log) g_log = new QFile();
    const QString path = QDir::homePath() + "/AppData/Roaming/TS3Client/flat_white_plugin.log";
    g_log->setFileName(path);
    g_log->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
}

void logLine(const QString& line) {
    openLog();
    if (!g_log || !g_log->isOpen()) return;
    QTextStream ts(g_log);
    ts << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " | " << line << "\n";
    ts.flush();
}

QString widgetPath(QWidget* w) {
    QStringList parts;
    for (QWidget* p = w; p; p = p->parentWidget()) {
        QString s = QString::fromLatin1(p->metaObject()->className());
        if (!p->objectName().isEmpty()) s += "#" + p->objectName();
        parts.prepend(s);
    }
    return parts.join(" > ");
}

void dumpItem(QTextStream& ts, const QModelIndex& idx, int depth, int maxDepth) {
    if (depth > maxDepth) return;
    const QString indent(depth * 2, ' ');
    QString text = idx.data(Qt::DisplayRole).toString();
    text.replace('\n', ' ');
    if (text.size() > 40) text = text.left(40) + "...";
    ts << indent << "- [" << idx.row() << "] \"" << text << "\""
       << " children=" << idx.model()->rowCount(idx)
       << " cols=" << idx.model()->columnCount(idx) << "\n";
    const int n = idx.model()->rowCount(idx);
    for (int i = 0; i < n && i < 30; ++i) {
        dumpItem(ts, idx.model()->index(i, 0, idx), depth + 1, maxDepth);
    }
}

void dumpTree(QTextStream& ts, QTreeView* tree) {
    ts << "\n--- TREE " << widgetPath(tree) << "\n";
    ts << "  geometry=" << tree->geometry().x() << "," << tree->geometry().y()
       << " " << tree->width() << "x" << tree->height() << "\n";
    ts << "  indentation=" << tree->indentation()
       << " iconSize=" << tree->iconSize().width() << "x" << tree->iconSize().height()
       << " rootIsDecorated=" << tree->rootIsDecorated()
       << " alternating=" << tree->alternatingRowColors() << "\n";
    QAbstractItemModel* m = tree->model();
    ts << "  model=" << (m ? m->metaObject()->className() : "null")
       << " delegate=" << (tree->itemDelegate() ? tree->itemDelegate()->metaObject()->className() : "null") << "\n";
    if (!m) return;
    ts << "  topLevel rows=" << m->rowCount() << " cols=" << m->columnCount() << "\n";
    for (int i = 0; i < m->rowCount() && i < 10; ++i) {
        dumpItem(ts, m->index(i, 0), 1, 3);
    }
}

void dumpWidgets() {
    openLog();
    QTextStream ts(g_log);
    ts << "\n================ " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ================\n";
    if (!qApp) {
        ts << "QApplication not available\n";
        ts.flush();
        return;
    }
    ts << "topLevel widgets:\n";
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        if (!w || !w->isWindow()) continue;
        ts << "  " << widgetPath(w) << "  visible=" << w->isVisible()
           << " geom=" << w->geometry().x() << "," << w->geometry().y()
           << " " << w->width() << "x" << w->height() << "\n";
    }

    ts << "\ntree views:\n";
    const auto all = QApplication::allWidgets();
    for (QWidget* w : all) {
        if (auto* tv = qobject_cast<QTreeView*>(w)) dumpTree(ts, tv);
    }

    ts << "\nsplitters:\n";
    for (QWidget* w : all) {
        if (auto* sp = qobject_cast<QSplitter*>(w)) {
            ts << "  " << widgetPath(sp) << " orientation=" << (sp->orientation() == Qt::Horizontal ? "H" : "V")
               << " geom=" << sp->geometry().x() << "," << sp->geometry().y()
               << " " << sp->width() << "x" << sp->height() << "\n";
            for (int i = 0; i < sp->count(); ++i) {
                QWidget* c = sp->widget(i);
                ts << "     [" << i << "] " << (c ? QString::fromLatin1(c->metaObject()->className()) : "null")
                   << (c && !c->objectName().isEmpty() ? "#" + c->objectName() : QString())
                   << " size=" << (c ? c->width() : 0) << "x" << (c ? c->height() : 0) << "\n";
            }
        }
    }

    ts << "\ntab widgets / dock / mdi:\n";
    for (QWidget* w : all) {
        const QByteArray cls = w->metaObject()->className();
        if (cls.contains("Tab") || cls.contains("Dock") || cls.contains("Stacked")) {
            ts << "  " << widgetPath(w) << " geom=" << w->width() << "x" << w->height() << "\n";
        }
    }

    ts << "\nfirst 120 widgets:\n";
    int n = 0;
    for (QWidget* w : all) {
        if (n++ > 120) break;
        ts << "  " << widgetPath(w) << "  visible=" << w->isVisible()
           << " " << w->width() << "x" << w->height() << "\n";
    }
    ts.flush();
}

// ---------------------------------------------------------------- layout

struct SplitterSizing {
    QList<int> requestedSizes;
    QList<bool> visibleChildren;
};

constexpr int kComposerHeight = 42;
constexpr int kComposerInset = 12;
constexpr int kComposerBottomGap = foundation::gapTight;
constexpr int kSendWidth = 64;
constexpr int kSendHeight = 30;
constexpr int kSendInset = 6;

struct SavedLayout {
    QPointer<QSplitter> outer;
    QPointer<QSplitter> inner;
    Qt::Orientation outerOrientation = Qt::Vertical;
    Qt::Orientation innerOrientation = Qt::Horizontal;
    QList<int> outerSizes;
    QList<int> innerSizes;
    SplitterSizing outerSizing;
    SplitterSizing innerSizing;
    QPointer<QLayout> chatLayout;
    QMargins chatMargins;
    int chatSpacing = -1;
    QPointer<QTextEdit> composer;
    int composerMinimumHeight = 0;
    int composerMaximumHeight = QWIDGETSIZE_MAX;
};

SavedLayout g_saved;
bool g_layoutApplied = false;

QWidget* findWidget(const char* objectName) {
    const auto all = QApplication::allWidgets();
    for (QWidget* w : all) {
        if (w && w->objectName() == QLatin1String(objectName)) return w;
    }
    return nullptr;
}

QTreeView* findServerTree() {
    const auto all = QApplication::allWidgets();
    for (QWidget* w : all) {
        auto* tv = qobject_cast<QTreeView*>(w);
        if (tv && tv->objectName() == "ServerTreeView") return tv;
    }
    return nullptr;
}

// The chat pane is global, so the arrangement can only live in the splitter of
// the *visible* server tab. Hidden tabs keep their own tree.
QTreeView* findActiveTree() {
    const auto all = QApplication::allWidgets();
    for (QWidget* w : all) {
        auto* tv = qobject_cast<QTreeView*>(w);
        if (tv && tv->objectName() == "ServerTreeView" && tv->parentWidget() &&
            tv->parentWidget()->isVisible()) return tv;
    }
    return nullptr;
}

QSplitter* splitterOfTree(QTreeView* tree) {
    if (!tree) return nullptr;
    QWidget* pane = tree->parentWidget();
    return qobject_cast<QSplitter*>(pane ? pane->parentWidget() : nullptr);
}

void restoreLayout();

class RoundedSurface : public QWidget {
public:
    explicit RoundedSurface(QWidget* parent) : QWidget(parent) {}

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (rect().isEmpty()) return;
        QPainterPath outline;
        outline.addRoundedRect(QRectF(rect()), foundation::radiusOverlay, foundation::radiusOverlay);
        setMask(QRegion(outline.toFillPolygon().toPolygon()));
    }
};

void installContentHost(QWidget* content) {
    if (g_contentHost || !content) return;
    auto* window = qobject_cast<QMainWindow*>(content->parentWidget());
    if (!window || window->centralWidget() != content) return;

    const bool visible = !content->isHidden();
    auto* host = new QWidget(window);
    host->setObjectName(QStringLiteral("FlatClientContent"));
    host->setStyleSheet(QStringLiteral(
        "#FlatClientContent QPushButton, #FlatClientContent QLineEdit,"
        " #FlatClientContent QToolButton, #FlatClientContent QComboBox,"
        " #FlatClientContent QSpinBox, #FlatClientContent QDoubleSpinBox { border-radius:%1px; }"
        " #FlatClientContent QTabBar::tab { border-radius:%1px; }"
        " #FlatClientContent QTextEdit#ChatLineEdit { border-radius:%2px; }")
        .arg(foundation::radiusControl).arg(foundation::radiusOverlay));
    auto* layout = new QVBoxLayout(host);
    constexpr int inset = foundation::gapTight;
    layout->setContentsMargins(inset, inset, inset, inset);
    layout->setSpacing(0);
    auto* surface = new RoundedSurface(host);
    auto* contentLayout = new QVBoxLayout(surface);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(surface);
    g_contentHost = host;
    g_nativeContent = window->takeCentralWidget();
    contentLayout->addWidget(content);
    window->setCentralWidget(host);
    content->setVisible(visible);
}

void restoreContentHost() {
    if (!g_contentHost) return;
    QWidget* host = g_contentHost;
    auto* window = qobject_cast<QMainWindow*>(host->parentWidget());
    if (window && window->centralWidget() == host && g_nativeContent) {
        const bool visible = !g_nativeContent->isHidden();
        if (auto* parent = g_nativeContent->parentWidget()) {
            if (auto* layout = parent->layout()) layout->removeWidget(g_nativeContent);
        }
        window->takeCentralWidget();
        window->setCentralWidget(g_nativeContent);
        g_nativeContent->setVisible(visible);
        delete host;
    }
    g_contentHost = nullptr;
    g_nativeContent = nullptr;
}

// Content-first arrangement:
//
//   +----------------------+---------------------------------------+
//   | channels             |                                       |
//   |                      |                                       |
//   |----------------------|              chat                     |
//   | profile / info       |                                       |
//   +----------------------+---------------------------------------+
//
// TS3 ships tree|info on top and chat below. Flipping the outer splitter to
// horizontal puts chat in the main column, and flipping the inner one to
// vertical stacks tree over info in a single left rail. Only the two
// orientations and their sizes change - no widgets are reparented, so tab
// switching and shutdown stay safe.
//
// The window resizes freely. These ratio constants are the design at
// 940x901; every resize scales both panes with the window, so the split
// never drifts from the arrangement it was tuned for.
constexpr int kDesignWidth = 940;
constexpr int kDesignContentHeight = 804;  // 901 minus menubar/toolbar/statusbar
constexpr int kSidebarWidth = 420;
constexpr int kTreeHeight = 439;
constexpr double kSidebarRatio = double(kSidebarWidth) / double(kDesignWidth);
constexpr double kMembersRatio = 220.0 / double(kDesignWidth);
constexpr double kTreeRatio = double(kTreeHeight) / double(kDesignContentHeight);
constexpr int kMinWindowWidth = 720;
constexpr int kMinWindowHeight = 560;

QList<int> sidebarSizes(int total, int count) {
    const int sidebar = qMax(1, qRound(total * kSidebarRatio));
    if (count >= 3) {
        const int members = qMax(1, qRound(total * kMembersRatio));
        return {sidebar, qMax(1, total - sidebar - members), members};
    }
    return {sidebar, qMax(1, total - sidebar)};
}

QList<int> treeSizes(int height) {
    if (g_infoPage) return {1, 0};
    const int treeH = qBound(120, qRound(height * kTreeRatio), qMax(120, height - 160));
    return {treeH, height - treeH};
}

// The divider belongs to the skin, not the user: disable the handles so a
// stray drag cannot change the proportions. The widgets stay laid out, and
// QSplitter::setSizes still works programmatically.
void lockSplitter(QSplitter* splitter) {
    if (!splitter) return;
    for (int i = 1; i < splitter->count(); ++i) {
        QSplitterHandle* handle = splitter->handle(i);
        if (!handle) continue;
        handle->setEnabled(false);
        handle->setFocusPolicy(Qt::NoFocus);
        handle->setCursor(Qt::ArrowCursor);
    }
}

void unlockSplitter(QSplitter* splitter) {
    if (!splitter) return;
    for (int i = 1; i < splitter->count(); ++i) {
        QSplitterHandle* handle = splitter->handle(i);
        if (!handle) continue;
        handle->setEnabled(true);
        handle->setCursor(splitter->orientation() == Qt::Horizontal ? Qt::SplitHCursor
                                                                   : Qt::SplitVCursor);
    }
}

// Keep the native widgets and their connections. Reserve room below the
// chat stack for the search row and composer.
class ChatComposer : public QObject {
public:
    ChatComposer(QTextEdit* editor, QTabWidget* tabs, QObject* parent)
        : QObject(parent), m_editor(editor), m_tabs(tabs), m_tabStyle(tabs->styleSheet()),
          m_editorStyle(editor->styleSheet()), m_tabPosition(tabs->tabPosition()) {}

    bool install() {
        QWidget* pane = m_editor->parentWidget();
        if (!pane || m_tabs->parentWidget() != pane || !takeSlot(pane->layout(), m_editorSlot, m_editor))
            return false;
        m_installed = true;
        m_search = pane->findChild<QWidget*>(QStringLiteral("SearchFrame"), Qt::FindDirectChildrenOnly);
        if (m_search && !takeSlot(pane->layout(), m_searchSlot, m_search)) m_search = nullptr;
        m_tabs->setTabPosition(QTabWidget::North);
        auto* button = new QPushButton(QStringLiteral("发送"), m_editor);
        m_send = button;
        button->setObjectName(QStringLiteral("FlatChatSend"));
        button->setFixedSize(kSendWidth, kSendHeight);
        button->setAutoDefault(false);
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setVisible(m_editor->metaObject()->indexOfSignal("returnPressed()") >= 0);
        if (auto* emoticon = pane->findChild<QWidget*>(QStringLiteral("EmoticonButton"))) {
            m_emoticon = emoticon;
            m_emoticonHidden = emoticon->isHidden();
            emoticon->hide();
        }
        setEditorPadding(10);
        for (QObject* widget : {static_cast<QObject*>(pane), static_cast<QObject*>(m_tabs.data()),
                                static_cast<QObject*>(m_tabs->tabBar()), static_cast<QObject*>(m_editor.data()),
                                static_cast<QObject*>(m_search.data())}) {
            if (widget) widget->installEventFilter(this);
        }
        connect(m_editor, &QTextEdit::textChanged, this, [this] {
            refreshButton();
            schedule();
        });
        connect(button, &QPushButton::clicked, this, [this] {
            if (!canSend()) return;
            m_editor->setFocus();
            QMetaObject::invokeMethod(m_editor, "returnPressed", Qt::DirectConnection);
            refreshButton();
        });
        refreshTheme();
        arrange();
        return true;
    }

    ~ChatComposer() override {
        if (!m_installed) return;
        m_arranging = true;
        if (m_editor) disconnect(m_editor, nullptr, this, nullptr);
        delete m_send.data();
        if (m_editor) m_editor->setStyleSheet(m_editorStyle);
        if (m_tabs) {
            m_tabs->setStyleSheet(m_tabStyle);
            m_tabs->setTabPosition(m_tabPosition);
        }
        restoreSlot(m_editorSlot, m_editor);
        restoreSlot(m_searchSlot, m_search);
        if (m_emoticon) m_emoticon->setVisible(!m_emoticonHidden);
    }

    void refreshTheme() {
        if (!m_send) return;
        m_send->setStyleSheet(QStringLiteral(
            "QPushButton#FlatChatSend { background:%1; color:%2; border:1px solid transparent;"
            " border-radius:6px; padding:0; min-height:0; font-weight:600; font-size:13px; }"
            "QPushButton#FlatChatSend:hover { background:%3; }"
            "QPushButton#FlatChatSend:pressed { background:%4; }"
            "QPushButton#FlatChatSend:focus { border-color:%2; }"
            "QPushButton#FlatChatSend:disabled { background:%5; color:%6; }"
        ).arg(discord::accent().name(), discord::color(Color::textOnAccent).name(),
              discord::color(Color::accentHover).name(), discord::color(Color::accentPressed).name(),
              discord::color(Color::fillSelected).name(), discord::muted().name()));
    }

    void arrange() {
        if (m_arranging || !m_editor || !m_tabs || !m_send || !m_editor->parentWidget()) return;
        QScopedValueRollback<bool> arranging(m_arranging, true);
        if (m_editor->minimumHeight() != kComposerHeight || m_editor->maximumHeight() != kComposerHeight)
            m_editor->setFixedHeight(kComposerHeight);
        const int searchHeight = m_search && !m_search->isHidden() ? m_search->sizeHint().height() : 0;
        const int spacing = foundation::gapNormal;
        const int reserve = kComposerHeight + spacing + kComposerBottomGap +
                            (searchHeight > 0 ? searchHeight + spacing : 0);
        if (reserve != m_reserve) {
            m_reserve = reserve;
            m_tabs->setStyleSheet(m_tabStyle + QStringLiteral(
                "\nQTabWidget#ChatTabWidget::pane { padding-bottom:%1px; }")
                .arg(reserve));
        }
        const QRect tabBounds = m_tabs->geometry();
        const int left = tabBounds.left() + kComposerInset;
        const int width = qMax(1, tabBounds.width() - 2 * kComposerInset);
        const int top = tabBounds.bottom() + 1 - kComposerBottomGap - kComposerHeight;
        if (top < 0) return;
        setGeometry(m_editor, QRect(left, top, width, kComposerHeight));
        setGeometry(m_send, QRect(qMax(0, width - kSendInset - kSendWidth),
                                  (kComposerHeight - kSendHeight) / 2, kSendWidth, kSendHeight));
        centerEditorText();
        m_editor->raise();
        m_send->raise();
        if (searchHeight > 0) {
            setGeometry(m_search, QRect(left, top - spacing - searchHeight, width, searchHeight));
            m_search->raise();
        }
        refreshButton();
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        switch (event->type()) {
        case QEvent::InputMethod:
            if (object == m_editor) {
                m_preediting = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
                refreshButton();
            }
            break;
        case QEvent::FocusOut:
            if (object == m_editor) m_preediting = false;
            schedule();
            break;
        case QEvent::EnabledChange:
        case QEvent::ReadOnlyChange:
        case QEvent::DynamicPropertyChange:
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
            schedule();
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

private:
    struct Slot {
        QPointer<QLayout> layout;
        QPointer<QWidget> placeholder;
    };

    static bool takeSlot(QLayout* layout, Slot& slot, QWidget* widget) {
        if (!layout) return false;
        auto* placeholder = new QWidget(widget->parentWidget());
        placeholder->hide();
        auto* item = layout->replaceWidget(widget, placeholder);
        if (!item) {
            delete placeholder;
            return false;
        }
        slot = {layout, placeholder};
        delete item;
        return true;
    }

    static void restoreSlot(const Slot& slot, QWidget* widget) {
        if (!slot.placeholder) return;
        if (slot.layout && widget) delete slot.layout->replaceWidget(slot.placeholder, widget);
        delete slot.placeholder.data();
    }

    static void setGeometry(QWidget* widget, const QRect& bounds) {
        if (widget->geometry() != bounds) widget->setGeometry(bounds);
    }

    void setEditorPadding(int top) {
        if (top == m_editorPadding) return;
        m_editorPadding = top;
        m_editor->setStyleSheet(m_editorStyle + QStringLiteral(
            "\nQTextEdit#ChatLineEdit { padding:%1px %2px 0 10px; border-radius:8px; }")
            .arg(top).arg(kSendWidth + kSendInset + foundation::gapNormal));
    }

    void centerEditorText() {
        // Measure the native line, including the document margin. The client
        // can change these metrics when it replaces its placeholder or draft.
        if (m_preediting || !m_editor->isVisible()) return;
        const QTextCursor firstLine(m_editor->document());
        const QRect caret = m_editor->cursorRect(firstLine);
        if (caret.height() <= 0) return;
        const int naturalTop = m_editor->viewport()->y() - m_editorPadding +
                               caret.y() + m_editor->verticalScrollBar()->value();
        const int padding = qBound(0, qRound((kComposerHeight - caret.height()) / 2.0) - naturalTop,
                                   qMax(0, kComposerHeight - caret.height() - 2));
        setEditorPadding(padding);
    }

    bool canSend() const {
        if (!m_editor || !m_editor->isEnabled() || m_editor->isReadOnly() || m_preediting ||
            m_editor->property("helpTextVisible").toBool() || m_editor->toPlainText().trimmed().isEmpty())
            return false;
        if (const auto* layout = m_editor->textCursor().block().layout()) {
            if (!layout->preeditAreaText().isEmpty()) return false;
        }
        if (!g_funcs.getCurrentServerConnectionHandlerID || !g_funcs.getConnectionStatus) return false;
        const uint64 server = g_funcs.getCurrentServerConnectionHandlerID();
        int status = STATUS_DISCONNECTED;
        return server && g_funcs.getConnectionStatus(server, &status) == ERROR_ok &&
               status == STATUS_CONNECTION_ESTABLISHED;
    }

    void refreshButton() {
        if (m_send) m_send->setEnabled(canSend());
    }

    void schedule() {
        if (m_arranging || m_pending) return;
        m_pending = true;
        QTimer::singleShot(0, this, [this] {
            m_pending = false;
            arrange();
        });
    }

    QPointer<QTextEdit> m_editor;
    QPointer<QTabWidget> m_tabs;
    QPointer<QPushButton> m_send;
    QPointer<QWidget> m_search;
    QPointer<QWidget> m_emoticon;
    Slot m_editorSlot;
    Slot m_searchSlot;
    QString m_tabStyle;
    QString m_editorStyle;
    QTabWidget::TabPosition m_tabPosition;
    int m_reserve = -1;
    int m_editorPadding = -1;
    bool m_installed = false;
    bool m_emoticonHidden = false;
    bool m_preediting = false;
    bool m_arranging = false;
    bool m_pending = false;
};

QPointer<ChatComposer> g_chatComposer;

// Lock the editor itself so native placeholder and focus changes cannot
// resize the row and move the tab bar. The pane still belongs to its layout.
void stabilizeChatComposer() {
    const int height = g_chatComposer ? kComposerHeight : foundation::controlHeight + 2;
    if (g_saved.composer && (g_saved.composer->minimumHeight() != height ||
                             g_saved.composer->maximumHeight() != height))
        g_saved.composer->setFixedHeight(height);
}

void applyLayout() {
    if (g_layoutApplied) return;
    QWidget* chat = findWidget("MainWindowChatWidget");
    QTreeView* tree = findActiveTree();
    if (!chat || !tree) return;

    auto* inner = splitterOfTree(tree);
    auto* outer = qobject_cast<QSplitter*>(chat->parentWidget());
    if (!inner || !outer || inner == outer) return;
    if (inner->count() != 2 || outer->indexOf(chat) < 0) return;

    g_saved.outer = outer;
    g_saved.inner = inner;
    g_saved.outerOrientation = outer->orientation();
    g_saved.innerOrientation = inner->orientation();
    g_saved.outerSizes = outer->sizes();
    g_saved.innerSizes = inner->sizes();
    g_saved.outerSizing = {};
    g_saved.innerSizing = {};
    g_saved.chatLayout = chat->layout();
    g_saved.composer = chat->findChild<QTextEdit*>(QStringLiteral("ChatLineEdit"), Qt::FindDirectChildrenOnly);
    if (g_saved.composer) {
        g_saved.composerMinimumHeight = g_saved.composer->minimumHeight();
        g_saved.composerMaximumHeight = g_saved.composer->maximumHeight();
        stabilizeChatComposer();
    }
    if (g_saved.chatLayout) {
        g_saved.chatMargins = g_saved.chatLayout->contentsMargins();
        g_saved.chatSpacing = g_saved.chatLayout->spacing();
        // The content host supplies the outer inset. Space only the controls
        // inside the column so its card and composer share the same edges.
        g_saved.chatLayout->setContentsMargins(0, 0, 0, 0);
        g_saved.chatLayout->setSpacing(foundation::gapTight);
    }

    if (g_saved.composer) {
        auto* tabs = chat->findChild<QTabWidget*>(QStringLiteral("ChatTabWidget"), Qt::FindDirectChildrenOnly);
        if (tabs) {
            auto* composer = new ChatComposer(g_saved.composer, tabs, chat);
            if (composer->install()) g_chatComposer = composer;
            else delete composer;
            stabilizeChatComposer();
        }
    }

    outer->setOrientation(Qt::Horizontal);
    inner->setOrientation(Qt::Vertical);

    const int total = outer->width() > 0 ? outer->width() : kDesignWidth;
    const QList<int> columns = sidebarSizes(total, outer->count());
    outer->setSizes(columns);

    const int height = inner->height() > 0 ? inner->height() : kDesignContentHeight;
    const QList<int> rows = treeSizes(height);
    inner->setSizes(rows);

    lockSplitter(outer);
    lockSplitter(inner);

    g_layoutApplied = true;
    installContentHost(outer);
    logLine(QString("layout applied: content-first (%1x%2), sidebar %3, tree/info %4/%5")
                .arg(total).arg(height).arg(columns.value(0)).arg(rows.value(0)).arg(height - rows.value(0)));
}

// Below this width the rail plus a usable chat column do not fit, so the
// client keeps its stock arrangement.
constexpr int kMinWidthForSidebarLayout = 620;

void updateLayout() {
    QTreeView* tree = findActiveTree();
    if (!tree) return;
    auto* inner = splitterOfTree(tree);
    if (!inner) return;

    QWidget* chat = findWidget("MainWindowChatWidget");
    auto* outer = chat ? qobject_cast<QSplitter*>(chat->parentWidget()) : nullptr;
    const int width = outer ? outer->width() : inner->width();

    // Follow tab switches: the saved splitters may belong to another tab.
    if (g_layoutApplied && g_saved.inner != inner) restoreLayout();

    if (!g_layoutApplied && width >= kMinWidthForSidebarLayout) {
        applyLayout();
    } else if (g_layoutApplied && width < kMinWidthForSidebarLayout) {
        restoreLayout();
    }
}

// Re-apply the ratio split, e.g. after the window was resized and Qt
// redistributed the splitter space. The handles are locked as well, because a
// tab switch or reconnect can rebuild the splitters.
void pinSplitterSizes() {
    if (!g_layoutApplied) return;
    const auto applySizes = [](QSplitter* splitter, SplitterSizing& sizing, const QList<int>& sizes) {
        QList<bool> visible;
        for (int i = 0; i < splitter->count(); ++i) visible.append(!splitter->widget(i)->isHidden());
        if (sizing.requestedSizes == sizes && sizing.visibleChildren == visible) return;
        // Qt adjusts the requested sizes for handle widths and minimum sizes.
        // Compare requests so the heartbeat does not keep reapplying them.
        sizing = {sizes, visible};
        splitter->setSizes(sizes);
        lockSplitter(splitter);
    };
    if (g_saved.outer && g_saved.outer->width() > 0)
        applySizes(g_saved.outer, g_saved.outerSizing, sidebarSizes(g_saved.outer->width(), g_saved.outer->count()));
    if (g_saved.inner && g_saved.inner->count() >= 2 && g_saved.inner->height() > 0)
        applySizes(g_saved.inner, g_saved.innerSizing, treeSizes(g_saved.inner->height()));
}

void restoreLayout() {
    if (!g_layoutApplied) return;
    delete g_chatComposer.data();
    if (g_saved.composer) {
        g_saved.composer->setMinimumHeight(g_saved.composerMinimumHeight);
        g_saved.composer->setMaximumHeight(g_saved.composerMaximumHeight);
        g_saved.composer = nullptr;
    }
    if (g_saved.chatLayout) {
        g_saved.chatLayout->setContentsMargins(g_saved.chatMargins);
        g_saved.chatLayout->setSpacing(g_saved.chatSpacing);
    }
    if (g_saved.outer) {
        unlockSplitter(g_saved.outer);
        g_saved.outer->setOrientation(g_saved.outerOrientation);
        if (!g_saved.outerSizes.isEmpty()) g_saved.outer->setSizes(g_saved.outerSizes);
    }
    if (g_saved.inner) {
        unlockSplitter(g_saved.inner);
        g_saved.inner->setOrientation(g_saved.innerOrientation);
        if (!g_saved.innerSizes.isEmpty()) g_saved.inner->setSizes(g_saved.innerSizes);
    }
    g_layoutApplied = false;
    logLine("layout restored");
}


// Temporary: dump model roles so we can tell channels from clients without
// guessing. Writes to the same log file.
void dumpRoles() {
    QTreeView* tree = findActiveTree();
    if (!tree || !tree->model()) return;
    QAbstractItemModel* m = tree->model();
    openLog();
    QTextStream ts(g_log);
    ts << "\n=== ROLE DUMP ===\n";
    std::function<void(const QModelIndex&, int)> walk = [&](const QModelIndex& idx, int depth) {
        if (depth > 2) return;
        QString text = idx.data(Qt::DisplayRole).toString();
        text.replace('\n', ' ');
        ts << QString(depth * 2, ' ') << "[" << idx.row() << "] kids=" << m->rowCount(idx)
           << " text=\"" << text.left(30) << "\"\n";
        for (int role = 1; role < 48; ++role) {
            const QVariant v = idx.data(role);
            if (!v.isValid()) continue;
            QString repr;
            const int t = v.type();
            if (t == QMetaType::QString) repr = v.toString().left(60);
            else if (t == QMetaType::Int || t == QMetaType::UInt || t == QMetaType::LongLong ||
                     t == QMetaType::ULongLong || t == QMetaType::Bool) repr = v.toString();
            else if (t == QMetaType::QPixmap) repr = "QPixmap";
            else if (t == QMetaType::QIcon) repr = "QIcon";
            else if (t == QMetaType::QFont) repr = "QFont";
            else if (t == QMetaType::QBrush || t == QMetaType::QColor) repr = "QColor/Brush";
            else repr = QString::fromLatin1(v.typeName());
            ts << QString(depth * 2 + 2, ' ') << "role " << role << " = " << repr << "\n";
        }
        for (int i = 0; i < m->rowCount(idx) && i < 6; ++i) walk(m->index(i, 0, idx), depth + 1);
    };
    for (int i = 0; i < m->rowCount() && i < 4; ++i) walk(m->index(i, 0), 0);
    ts.flush();
}


void queueTreeRefresh();

class TreeWheelMotion : public QObject {
public:
    explicit TreeWheelMotion(QObject* parent) : QObject(parent) {
        m_motion.setDuration(foundation::motionDuration);
        m_motion.setEasingCurve(foundation::motionEasing());
        m_motion.setStartValue(0.0);
        m_motion.setEndValue(1.0);
        connect(&m_motion, &QVariantAnimation::valueChanged, this, [this](const QVariant& progress) {
            if (!m_tree || !m_tree->isVisible() || !m_bar || !foundation::animationsEnabled()) {
                stop();
                return;
            }
            QScopedValueRollback<bool> ownWrite(m_writing, true);
            m_bar->setValue(m_start + qRound((m_target - m_start) * progress.toReal()));
        });
    }

    ~TreeWheelMotion() override { bind(nullptr); }

    void bind(QTreeView* tree) {
        const bool animate = tree && foundation::animationsEnabled();
        if (!animate) tree = nullptr;
        if (tree == m_tree && (!tree || (m_viewport == tree->viewport() &&
                                        m_bar == tree->verticalScrollBar()))) return;
        stop();
        if (m_viewport) m_viewport->removeEventFilter(this);
        if (m_bar) {
            m_bar->removeEventFilter(this);
            disconnect(m_bar, nullptr, this, nullptr);
        }
        if (m_tree) {
            m_tree->removeEventFilter(this);
            setScrollMode(m_tree, m_originalMode);
        }
        m_tree = tree;
        m_viewport = tree ? tree->viewport() : nullptr;
        m_bar = tree ? tree->verticalScrollBar() : nullptr;
        if (!tree) return;
        m_originalMode = tree->verticalScrollMode();
        setScrollMode(tree, QAbstractItemView::ScrollPerPixel);
        tree->installEventFilter(this);
        m_viewport->installEventFilter(this);
        m_bar->installEventFilter(this);
        connect(m_bar, &QScrollBar::valueChanged, this, [this] { if (!m_writing) stop(); });
        connect(m_bar, &QScrollBar::rangeChanged, this, [this] { stop(); });
        connect(m_bar, &QScrollBar::sliderPressed, this, [this] { stop(); });
        connect(m_bar, &QScrollBar::actionTriggered, this, [this] { stop(); });
    }

    void stop() { m_motion.stop(); }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        switch (event->type()) {
        case QEvent::Wheel:
            if (object == m_viewport && scroll(static_cast<QWheelEvent*>(event))) return true;
            stop();
            break;
        case QEvent::Hide:
        case QEvent::FocusOut:
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::DragEnter:
            stop();
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

private:
    static void setScrollMode(QTreeView* tree, QAbstractItemView::ScrollMode mode) {
        if (tree->verticalScrollMode() == mode) return;
        const QModelIndex top = tree->indexAt(QPoint(tree->indentation() + 1, 0));
        tree->setVerticalScrollMode(mode);
        if (top.isValid()) tree->scrollTo(top, QAbstractItemView::PositionAtTop);
    }

    bool scroll(QWheelEvent* event) {
        if (!m_tree || !m_tree->isVisible() || !m_bar || m_bar->isSliderDown() ||
            !foundation::isMouseWheelNotch(*event) || !foundation::animationsEnabled()) return false;
        const int current = m_bar->value();
        const bool running = m_motion.state() == QAbstractAnimation::Running;
        const int base = running ? m_target : current;
        const int target = foundation::wheelTarget(*m_bar, base, event->angleDelta().y());
        if (target == base && !running) return false;
        if (target != base) {
            stop();
            m_start = current;
            m_target = target;
            if (current != target) m_motion.start();
        }
        event->accept();
        return true;
    }

    QPointer<QTreeView> m_tree;
    QPointer<QWidget> m_viewport;
    QPointer<QScrollBar> m_bar;
    QAbstractItemView::ScrollMode m_originalMode = QAbstractItemView::ScrollPerItem;
    QVariantAnimation m_motion;
    bool m_writing = false;
    int m_start = 0;
    int m_target = 0;
};

QPointer<TreeWheelMotion> g_treeWheelMotion;

void syncTreeWheel(QTreeView* tree) {
    if (!g_treeWheelMotion && tree) g_treeWheelMotion = new TreeWheelMotion(g_guard);
    if (g_treeWheelMotion) g_treeWheelMotion->bind(tree);
}

// Keep the stock ServerTreeView (native model, ordering and event handling),
// paint its rows in the Discord-light style, and pin our user panel below it.
void styleTree(QTreeView* tree) {
    syncTreeWheel(tree && tree->isVisible() ? tree : nullptr);
    if (!tree) return;
    // Same widget and still carrying our delegate: nothing to do. If the client
    // rebuilt the tree (reconnect, tab switch) the pointer matches but the
    // stock delegate is back, which is the flash the theme has to cover.
    if (g_styledTree == tree && tree->itemDelegate() == g_treeRows) return;

    if (g_styledTree) {
        const auto it = g_oldDelegates.constFind(g_styledTree);
        if (it != g_oldDelegates.constEnd() && it.value())
            g_styledTree->setItemDelegate(it.value());
    }

    if (!g_oldDelegates.contains(tree)) {
        g_oldDelegates.insert(tree, tree->itemDelegate());
        QObject::connect(tree, &QObject::destroyed, [tree] { g_oldDelegates.remove(tree); });
    }

    if (!g_treeRows) {
        auto* rows = new NativeTreeRows(tree);
        rows->setFunctions(g_funcs);
        g_treeRows = rows;
    }
    tree->setItemDelegate(g_treeRows);
    tree->setIndentation(16);
    tree->setUniformRowHeights(false);
    tree->setMouseTracking(true);
    tree->show();
    g_styledTree = tree;
    g_treeRows->refresh();

    // The native model already repaints on membership changes, but the
    // delegate's name/id cache only caught up on the next 1.5s heartbeat, so
    // a client that just joined kept a stale (missing) row for a moment.
    if (QAbstractItemModel* m = tree->model()) {
        QObject::connect(m, &QAbstractItemModel::rowsInserted, tree, [] { queueTreeRefresh(); });
        QObject::connect(m, &QAbstractItemModel::rowsRemoved, tree, [] { queueTreeRefresh(); });
        QObject::connect(m, &QAbstractItemModel::modelReset, tree, [] { queueTreeRefresh(); });
    }
}

// Put the stock InfoFrame into the chat page area on the right, leaving the
// chat tab bar visible. Selecting a client raises the info page; "聊天" or a
// click on the tab bar returns to chat. We deliberately do not register a real
// tab: the client's ChatTabWidget only knows how to drive ChatTab pages.
QWidget* findBanner() {
    static bool logged = false;
    const auto all = QApplication::allWidgets();
    QWidget* visible = nullptr;
    for (QWidget* w : all) {
        if (!w) continue;
        if (QString::fromLatin1(w->metaObject()->className()) != QLatin1String("Banner")) continue;
        if (w->isVisible()) visible = w;
        if (!logged) {
            logged = true;
            logLine(QString("banner candidate: %1 visible=%2").arg(widgetPath(w)).arg(w->isVisible()));
        }
    }
    return visible;
}

void updateChatOverlay();

QWidget* ensureCardHost(QTabWidget* tabs) {
    if (g_cardHost) return g_cardHost->parentWidget() == tabs ? g_cardHost.data() : nullptr;
    auto* stack = tabs->findChild<QStackedWidget*>(QStringLiteral("qt_tabwidget_stackedwidget"),
                                                   Qt::FindDirectChildrenOnly);
    if (!stack) return nullptr;
    auto* host = new QWidget(tabs);
    host->setObjectName(QStringLiteral("FlatCardHost"));
    host->hide();
    g_cardHost = host;
    g_chatStack = stack;
    return host;
}

void layoutCardHost() {
    if (!g_cardHost || !g_chatStack) return;
    stabilizeChatComposer();
    if (g_chatComposer) g_chatComposer->arrange();
    const QRect target = g_chatStack->geometry();
    if (target.isEmpty() || g_chatStack->parentWidget() != g_cardHost->parentWidget()) return;
    if (g_cardHost->geometry() != target) g_cardHost->setGeometry(target);
    const QRect bounds = g_cardHost->rect();
    if (g_infoPage && g_infoPage->geometry() != bounds) g_infoPage->setGeometry(bounds);
    if (g_modernChat && g_modernChat->geometry() != bounds) g_modernChat->setGeometry(bounds);
}

// Fade a tint over the live page. The page and its contents stay in place.
class PageTransition : public QWidget {
public:
    explicit PageTransition(QWidget* page) : QWidget(page) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        m_fade.setDuration(foundation::motionDuration);
        m_fade.setEasingCurve(foundation::motionEasing());
        m_fade.setStartValue(0.0);
        m_fade.setEndValue(1.0);
        connect(&m_fade, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&m_fade, &QVariantAnimation::finished, this, [this] { finish(); });
        page->installEventFilter(this);
        hide();
    }

    static void play(QWidget* page) {
        if (!page || !page->isVisible() || page->rect().isEmpty()) return;
        auto* transition = page->findChild<QWidget*>(QStringLiteral("FlatPageTransition"),
                                                    Qt::FindDirectChildrenOnly);
        auto* fade = dynamic_cast<PageTransition*>(transition);
        if (fade) fade->finish();
        if (!foundation::animationsEnabled()) return;
        if (!fade) {
            fade = new PageTransition(page);
            fade->setObjectName(QStringLiteral("FlatPageTransition"));
        }
        fade->setGeometry(page->rect());
        fade->m_fade.start();
        fade->show();
        fade->raise();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QColor tint = discord::color(Color::bgCanvas);
        tint.setAlphaF(0.22 * (1.0 - m_fade.currentValue().toReal()));
        painter.fillRect(rect(), tint);
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == parent() && (event->type() == QEvent::Resize || event->type() == QEvent::Hide))
            finish();
        return QWidget::eventFilter(object, event);
    }

private:
    void finish() {
        m_fade.stop();
        hide();
    }

    QVariantAnimation m_fade;
};

void showInfoPage() {
    if (!g_cardHost || !g_chatStack || !g_infoPage) return;
    if (g_modernChat) g_modernChat->finishWheelScroll();
    g_infoShown = true;
    layoutCardHost();
    g_cardHost->show();
    g_cardHost->raise();
    g_infoPage->show();
    g_infoPage->raise();
    updateChatOverlay();
    PageTransition::play(g_infoPage);
}

void hideInfoPage() {
    if (!g_infoPage || !g_infoShown) return;
    g_infoShown = false;
    updateChatOverlay();
    if (g_infoPage) g_infoPage->hide();
}

// ------------------------------------------------------------- theme mode

// The client keeps exactly one style sheet active. The dark package names its
// asset folder flat_dark, and the style sheet text carries that path; the
// palette is the second source of truth (the client parses QPalette::Window
// from the style sheet). Both survive a runtime style switch.
bool detectDarkTheme() {
    if (!qApp) return false;
    const QString ss = qApp->styleSheet();
    if (ss.contains(QLatin1String("flat_dark"), Qt::CaseInsensitive)) return true;
    if (ss.contains(QLatin1String("flat_white"), Qt::CaseInsensitive)) return false;
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

void applyLinkPalette() {
    if (!qApp) return;
    const QColor accent = discord::accent();
    QPalette pal = qApp->palette();
    if (pal.color(QPalette::Link) != accent) {
        pal.setColor(QPalette::Link, accent);
        pal.setColor(QPalette::LinkVisited, accent);
        qApp->setPalette(pal);
    }
    // Chat links are drawn from the widget palette, not the app one.
    for (QWidget* w : QApplication::allWidgets()) {
        auto* te = qobject_cast<QTextEdit*>(w);
        if (te && te->objectName() == QLatin1String("ChatTab")) {
            QPalette wp = te->palette();
            if (wp.color(QPalette::Link) != accent) {
                wp.setColor(QPalette::Link, accent);
                wp.setColor(QPalette::LinkVisited, accent);
                te->setPalette(wp);
            }
        }
    }
}

using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

DwmSetWindowAttributeFn dwmSetWindowAttribute() {
    static DwmSetWindowAttributeFn fn = [] {
        HMODULE lib = LoadLibraryW(L"dwmapi.dll");
        if (!lib) return static_cast<DwmSetWindowAttributeFn>(nullptr);
        return reinterpret_cast<DwmSetWindowAttributeFn>(
            GetProcAddress(lib, "DwmSetWindowAttribute"));
    }();
    return fn;
}

enum class WindowFrameMode { Native, Dark, FlatLight, FlatDark };

struct WindowFrame {
    QPointer<QWidget> widget;
    WindowFrameMode mode;
};

QHash<quintptr, WindowFrame> g_windowFrames;

void setWindowFrame(HWND window, WindowFrameMode mode) {
    const auto setAttribute = dwmSetWindowAttribute();
    if (!setAttribute) return;
    const bool dark = mode == WindowFrameMode::Dark || mode == WindowFrameMode::FlatDark;
    const BOOL immersive = dark ? TRUE : FALSE;
    if (FAILED(setAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive)))) {
        // Older Windows 10 builds used 19 for immersive dark mode.
        setAttribute(window, 19, &immersive, sizeof(immersive));
    }

    COLORREF caption = DWMWA_COLOR_DEFAULT;
    COLORREF text = DWMWA_COLOR_DEFAULT;
    COLORREF border = DWMWA_COLOR_DEFAULT;
    if (mode == WindowFrameMode::FlatLight || mode == WindowFrameMode::FlatDark) {
        const auto frameColor = [dark](Color role) {
            const QColor color = foundation::color(role, dark);
            return RGB(color.red(), color.green(), color.blue());
        };
        caption = frameColor(Color::bgCanvas);  // Same background as QMenuBar.
        text = frameColor(Color::textPrimary);
        border = frameColor(Color::strokeDivider);
    }
    const DWM_WINDOW_CORNER_PREFERENCE corners = caption == DWMWA_COLOR_DEFAULT
        ? DWMWCP_DEFAULT : DWMWCP_ROUND;
    setAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    // Explicit colors are supported on Windows 11. Older builds keep the
    // system caption with the immersive dark preference above.
    const HRESULT captionResult = setAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    const HRESULT textResult = setAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
    const HRESULT borderResult = setAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
    if (caption != DWMWA_COLOR_DEFAULT) {
        if (SUCCEEDED(captionResult) && SUCCEEDED(textResult) && SUCCEEDED(borderResult)) {
            const QColor applied(GetRValue(caption), GetGValue(caption), GetBValue(caption));
            logLine(QStringLiteral("window frame palette applied: %1").arg(applied.name()));
        } else {
            logLine(QStringLiteral("window frame palette unavailable (caption=0x%1, text=0x%2, border=0x%3)")
                        .arg(static_cast<quint32>(captionResult), 8, 16, QLatin1Char('0'))
                        .arg(static_cast<quint32>(textResult), 8, 16, QLatin1Char('0'))
                        .arg(static_cast<quint32>(borderResult), 8, 16, QLatin1Char('0')));
        }
    }
}

void resetWindowFrames() {
    for (auto it = g_windowFrames.cbegin(); it != g_windowFrames.cend(); ++it) {
        if (it->widget && it->widget->internalWinId() == it.key())
            setWindowFrame(reinterpret_cast<HWND>(it.key()), WindowFrameMode::Native);
    }
    g_windowFrames.clear();
}

void applyWindowFrame(bool dark) {
    if (!dwmSetWindowAttribute()) return;
    const QString stylesheet = qApp->styleSheet();
    const bool flat = stylesheet.contains(QLatin1String("flat_dark"), Qt::CaseInsensitive) ||
                      stylesheet.contains(QLatin1String("flat_white"), Qt::CaseInsensitive);
    const WindowFrameMode mode = flat ? (dark ? WindowFrameMode::FlatDark : WindowFrameMode::FlatLight)
                                      : (dark ? WindowFrameMode::Dark : WindowFrameMode::Native);
    // Windows can reuse an HWND after a dialog closes or recreates its surface.
    for (auto it = g_windowFrames.begin(); it != g_windowFrames.end();) {
        if (!it->widget || it->widget->internalWinId() != it.key()) it = g_windowFrames.erase(it);
        else ++it;
    }
    if (mode == WindowFrameMode::Native) {
        resetWindowFrames();
        return;
    }
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || !w->isWindow() || !w->isVisible()) continue;
        const Qt::WindowType type = w->windowType();
        if (type == Qt::Popup || type == Qt::ToolTip || w->windowFlags().testFlag(Qt::FramelessWindowHint))
            continue;
        const quintptr handle = static_cast<quintptr>(w->winId());
        if (!handle) continue;
        const auto existing = g_windowFrames.constFind(handle);
        if (existing != g_windowFrames.cend() && existing->widget == w && existing->mode == mode) continue;
        setWindowFrame(reinterpret_cast<HWND>(handle), mode);
        g_windowFrames.insert(handle, {w, mode});
    }
}

// The native volume dialog uses SetFixedSize. Its unwrapped hint changes at
// 0/6 dB, resizing the dialog and moving the slider under the pointer. Reserve
// a stable, wrapped hint area before the dialog is shown; keep all native
// volume signals and buttons intact.
void stabilizeVolumeDialog(QWidget* w) {
    if (!w || QString::fromLatin1(w->metaObject()->className()) !=
                  QLatin1String("UserVolumeWidget")) return;
    auto* hint = w->findChild<QLabel*>(QStringLiteral("hintLabel"), Qt::FindDirectChildrenOnly);
    if (!hint || hint->property("flatWhiteStableHint").toBool()) return;
    const QMargins margins = hint->contentsMargins();
    const int width = hint->width();
    const int height = qMax(hint->height(), hint->fontMetrics().lineSpacing() * 3 +
                                               margins.top() + margins.bottom());
    hint->setWordWrap(true);
    hint->setFixedSize(width, height);
    hint->setProperty("flatWhiteStableHint", true);
    if (w->layout()) w->layout()->activate();
}

// Dialogs (options, channel editor, ...) are built after startup. Catching
// their Show event avoids a light caption until the next heartbeat.
void syncMainWindowLayout();

class FrameFilter : public QObject {
public:
    explicit FrameFilter(QObject* parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if ((ev->type() == QEvent::Show || ev->type() == QEvent::Hide) &&
            obj->objectName() == QLatin1String("ServerTreeView")) {
            if (ev->type() == QEvent::Hide && g_treeWheelMotion) g_treeWheelMotion->stop();
            queueTreeRefresh();
        }
        if (ev->type() == QEvent::Resize) {
            auto* w = qobject_cast<QWidget*>(obj);
            if (w && w->isWindow() &&
                QString::fromLatin1(w->metaObject()->className()) == QLatin1String("MainWindow"))
                syncMainWindowLayout();
        } else if (ev->type() == QEvent::Show) {
            auto* w = qobject_cast<QWidget*>(obj);
            if (w && w->isWindow()) {
                stabilizeVolumeDialog(w);
                if (g_darkMode >= 0) applyWindowFrame(detectDarkTheme());
            }
        }
        return QObject::eventFilter(obj, ev);
    }
};

QPointer<QObject> g_frameFilter;

QString infoBackButtonStyle() {
    return QStringLiteral(
               "QPushButton { background:transparent; border:1px solid transparent;"
               " color:%1; border-radius:%2px; padding:0 %3px; }"
               "QPushButton:hover { background:%4; color:%5; }"
               "QPushButton:pressed { background:%6; }"
               "QPushButton:focus { border-color:%7; }")
        .arg(discord::muted().name())
        .arg(foundation::radiusControl)
        .arg(foundation::gapNormal)
        .arg(discord::hover().name(), discord::ink().name(),
             discord::color(Color::fillPressed).name(), discord::color(Color::focus).name());
}

QIcon infoBackIcon(const QWidget* button) {
    const qreal scale = button->devicePixelRatioF();
    constexpr int size = 16;
    QPixmap image(qRound(size * scale), qRound(size * scale));
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(discord::muted(), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(3.5, 8), QPointF(12.5, 8));
    painter.drawPolyline(QPolygonF{QVector<QPointF>{{7.5, 4}, {3.5, 8}, {7.5, 12}}});
    painter.end();
    return QIcon(image);
}

void refreshUnreadDotIcons();

// Repaints every widget the plugin owns. The client's own widgets are handled
// by the theme's stylesheet, so this only covers our sidebar, user panel, chat
// overlay, chat header and the info page chrome.
void applyPluginTheme(bool dark) {
    discord::setDark(dark);

    if (g_infoBar) {
        g_infoBar->setStyleSheet(QStringLiteral("background:%1; border-bottom:1px solid %2;")
                                     .arg(discord::bg().name(), discord::divider().name()));
    }
    if (g_infoBack) {
        g_infoBack->setStyleSheet(infoBackButtonStyle());
        g_infoBack->setIcon(infoBackIcon(g_infoBack));
    }
    if (g_infoTitle) {
        g_infoTitle->setStyleSheet(
            QStringLiteral("QLabel { border:none; background:transparent; color:%1;"
                           " font-family:'%2'; font-size:%3px; font-weight:600; }")
                .arg(discord::ink().name(), QString::fromLatin1(foundation::fontFamily))
                .arg(foundation::bodyLargeSize));
    }
    if (g_infoStatus) {
        g_infoStatus->setStyleSheet(
            QStringLiteral("QLabel { background:%1; color:%2; padding:%3px %4px %5px;"
                           " font-family:'%6'; font-size:%7px; }")
                .arg(discord::bg().name(), discord::muted().name())
                .arg(foundation::gapTight)
                .arg(foundation::gapLoose)
                .arg(foundation::gapNormal)
                .arg(QString::fromLatin1(foundation::fontFamily))
                .arg(foundation::bodySize));
    }
    if (g_chatComposer) g_chatComposer->refreshTheme();
    if (g_modernChat) g_modernChat->applyTheme();
    if (g_notifyToast) g_notifyToast->update();
    refreshUnreadDotIcons();
    if (g_chatHeader) g_chatHeader->update();
    if (g_userPanel) g_userPanel->update();
    if (g_styledTree) g_styledTree->viewport()->update();
    applyLinkPalette();
}

void mountBanner(QWidget* banner, QVBoxLayout* target, int index) {
    g_banner = banner;
    g_bannerHome = banner->parentWidget();
    g_bannerMaximumHeight = banner->maximumHeight();
    g_bannerWasVisible = !banner->isHidden();
    g_bannerIndex = -1;
    g_bannerStretch = 0;
    if (g_bannerHome) {
        if (auto* box = qobject_cast<QBoxLayout*>(g_bannerHome->layout())) {
            g_bannerIndex = box->indexOf(banner);
            if (g_bannerIndex >= 0) g_bannerStretch = box->stretch(g_bannerIndex);
        }
    }
    banner->setMaximumHeight(qMin(g_bannerMaximumHeight, 240));
    banner->hide();
    target->insertWidget(index, banner);
}

void installInfoTab() {
    if (g_infoPage) return;
    auto* tw = qobject_cast<QTabWidget*>(findWidget("ChatTabWidget"));
    if (!tw) return;
    QWidget* host = ensureCardHost(tw);
    if (!host) return;
    QWidget* info = findWidget("InfoFrame");
    if (!info) return;

    g_infoFrame = info;
    g_infoHome = info->parentWidget();
    g_infoSplitter = qobject_cast<QSplitter*>(g_infoHome.data());
    g_infoIndex = g_infoSplitter ? g_infoSplitter->indexOf(info) : -1;
    g_infoStretch = 0;
    g_infoAlignment = {};
    if (g_infoHome && !g_infoSplitter) {
        if (auto* box = qobject_cast<QBoxLayout*>(g_infoHome->layout())) {
            g_infoIndex = box->indexOf(info);
            if (g_infoIndex >= 0) {
                g_infoStretch = box->stretch(g_infoIndex);
                g_infoAlignment = box->itemAt(g_infoIndex)->alignment();
            }
        }
    }

    auto* page = new QWidget(host);
    // The card follows the native stack's geometry without joining its layout.
    page->setMinimumSize(0, 0);
    page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* bar = new QWidget(page);
    bar->setFixedHeight(foundation::controlHeight + 2 * foundation::gapNormal);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(foundation::gapLoose, 0, foundation::gapLoose, 0);
    h->setSpacing(foundation::gapNormal);
    auto* back = new QPushButton(QStringLiteral("返回聊天"), bar);
    back->setObjectName(QStringLiteral("FlatInfoBackButton"));
    back->setIconSize(QSize(16, 16));
    back->setFont(discord::font(foundation::bodySize));
    back->setCursor(Qt::PointingHandCursor);
    back->setFixedHeight(foundation::controlHeight);
    auto* title = new QLabel(QStringLiteral("信息"), bar);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(title, 1);
    h->addWidget(back);
    v->addWidget(bar);
    g_infoBar = bar;
    g_infoBack = back;

    auto* status = new QLabel(page);
    status->hide();
    v->addWidget(status);
    g_infoStatus = status;

    if (QWidget* banner = findBanner()) mountBanner(banner, v, v->count());
    v->addWidget(info, 1);

    QObject::connect(back, &QPushButton::clicked, page, [] { hideInfoPage(); });

    // The info view floats above the chat stack instead of joining it: the
    // client's ChatTabWidget assumes every stack page is a chat tab, and adding
    // one desynchronises the selected tab from the visible page.
    page->hide();
    g_infoPage = page;
    g_infoTitle = title;
    info->show();
    const bool dark = detectDarkTheme();
    g_darkMode = dark ? 1 : 0;
    applyPluginTheme(dark);
    logLine("info pane moved into the chat page area");
}

// Plugin-drawn message list for channel chat tabs. The client renders channel
// chat as plain rich text; the overlay replaces just the log area, leaving the
// native input box below it untouched. Server and private tabs keep the native
// view because their markup (inline colours, links) is richer than what we can
// reproduce from onTextMessageEvent.
QString stripChatMarkup(QString text) {
    static const QRegularExpression tag(
        QStringLiteral("\\[/?(?:B|I|U|S|URL|IMG|COLOR|SIZE|QUOTE|HR|LIST|\\*)(?:=[^\\]]*)?\\]"),
        QRegularExpression::CaseInsensitiveOption);
    text.remove(tag);
    text.replace(QStringLiteral("<br>"), QStringLiteral("\n"), Qt::CaseInsensitive);
    return text.trimmed();
}

bool currentChannelInfo(uint64& schid, uint64& channel, QString& name) {
    schid = 0;
    channel = 0;
    name.clear();
    if (!g_funcs.getCurrentServerConnectionHandlerID || !g_funcs.getClientID ||
        !g_funcs.getChannelOfClient || !g_funcs.getChannelVariableAsString)
        return false;
    schid = g_funcs.getCurrentServerConnectionHandlerID();
    anyID self = 0;
    if (g_funcs.getClientID(schid, &self) != ERROR_ok || !self) return false;
    if (g_funcs.getChannelOfClient(schid, self, &channel) != ERROR_ok || !channel) return false;
    char* namePtr = nullptr;
    if (g_funcs.getChannelVariableAsString(schid, channel, CHANNEL_NAME, &namePtr) == ERROR_ok && namePtr) {
        name = QString::fromUtf8(namePtr);
        g_funcs.freeMemory(namePtr);
    }
    return true;
}

// The client drops a leaving member from its list before the move event is
// delivered, so getClientDisplayName returns nothing for "left the server".
// Names are cached while the member is still present.
QHash<anyID, QString> g_clientNames;

QString clientNameById(uint64 schid, anyID id) {
    if (!id) return QString();
    if (g_funcs.getClientDisplayName) {
        char name[512] = {};
        if (g_funcs.getClientDisplayName(schid, id, name, sizeof(name)) == ERROR_ok) {
            const QString result = QString::fromUtf8(name);
            if (!result.isEmpty()) {
                g_clientNames.insert(id, result);
                return result;
            }
        }
    }
    return g_clientNames.value(id);
}

QString activePrivateTabText(const QString& channelName) {
    if (!g_chatTab) return QString();
    const int index = g_chatTab->currentIndex();
    if (index <= 0) return QString();
    const QString text = g_chatTab->tabText(index).trimmed();
    if (text.isEmpty()) return QString();
    if (!channelName.isEmpty() && text == channelName) return QString();
    return text;
}

QString serverKey(uint64 schid) {
    return QStringLiteral("s:") + QString::number(schid);
}

QString serverNameById(uint64 schid) {
    if (!g_funcs.getServerVariableAsString || !schid) return QString();
    char* name = nullptr;
    QString result;
    if (g_funcs.getServerVariableAsString(schid, VIRTUALSERVER_NAME, &name) == ERROR_ok && name) {
        result = QString::fromUtf8(name);
        g_funcs.freeMemory(name);
    }
    return result;
}

QString channelNameById(uint64 schid, uint64 channelId) {
    if (!g_funcs.getChannelVariableAsString || !channelId) return QString();
    char* name = nullptr;
    QString result;
    if (g_funcs.getChannelVariableAsString(schid, channelId, CHANNEL_NAME, &name) == ERROR_ok && name) {
        result = QString::fromUtf8(name);
        g_funcs.freeMemory(name);
    }
    return result;
}

void appendSystemMessage(uint64 schid, const QString& text, int severity = 0, bool poke = false) {
    ChatMessage msg;
    msg.text = text;
    msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    msg.system = true;
    msg.severity = severity;
    msg.poke = poke;
    const QString key = serverKey(schid);
    g_chatHistory[key].append(msg);
    if (g_overlayKey == key && g_modernChat) g_modernChat->addMessage(msg);
}

void queueSystemMessage(uint64 schid, const QString& text, int severity = 0) {
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [schid, text, severity] { appendSystemMessage(schid, text, severity); },
                              Qt::QueuedConnection);
}

QString connectionErrorText(unsigned int error) {
    switch (error) {
    case ERROR_client_is_flooding: return QStringLiteral("操作过于频繁，触发服务器防洪保护，请稍后再试");
    case ERROR_server_invalid_password: return QStringLiteral("服务器密码错误");
    case ERROR_server_maxclients_reached: return QStringLiteral("服务器人数已满");
    case ERROR_server_is_shutting_down: return QStringLiteral("服务器正在关闭");
    case ERROR_failed_connection_initialisation:
        return QStringLiteral("无法连接到服务器，请检查服务器地址、端口和网络连接");
    case ERROR_could_not_resolve_hostname: return QStringLiteral("无法解析服务器域名");
    case ERROR_connection_lost: return QStringLiteral("与服务器的连接丢失");
    case ERROR_handshake_failed: return QStringLiteral("与服务器握手失败");
    default: break;
    }
    QString text;
    if (g_funcs.getErrorMessage) {
        char* message = nullptr;
        if (g_funcs.getErrorMessage(error, &message) == ERROR_ok && message) {
            text = QString::fromUtf8(message).trimmed();
            g_funcs.freeMemory(message);
        }
    }
    return text.isEmpty() ? QStringLiteral("未知错误") : text;
}

QString operationErrorText(unsigned int error, const QString& message = QString(),
                            const QString& extra = QString()) {
    QString text = error == ERROR_client_is_flooding ? connectionErrorText(error) : message.trimmed();
    if (text.isEmpty()) text = connectionErrorText(error);
    const QString detail = extra.trimmed();
    if (!detail.isEmpty() && !text.contains(detail)) text += QStringLiteral("：") + detail;
    return QStringLiteral("%1（错误码 %2）").arg(text).arg(error);
}

void appendOperationError(uint64 schid, const QString& text) {
    appendSystemMessage(schid, text, 2);
    if (!g_modernChat || !g_modernChat->isVisible() || g_overlayKey.isEmpty() ||
        g_overlayKey == serverKey(schid) || !g_funcs.getCurrentServerConnectionHandlerID ||
        g_funcs.getCurrentServerConnectionHandlerID() != schid) return;
    // Private keys are name-based; check the active connection as well as the
    // overlay key so another server's error never enters this conversation.
    const bool channel = g_overlayKey.startsWith(QString::number(schid) + QLatin1Char(':'));
    if (!channel && !g_overlayKey.startsWith(QStringLiteral("p:"))) return;
    ChatMessage msg;
    msg.text = text;
    msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    msg.system = true;
    msg.severity = 2;
    g_chatHistory[g_overlayKey].append(msg);
    g_modernChat->addMessage(msg);
}

void queueOperationError(uint64 schid, const QString& text) {
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [schid, text] { appendOperationError(schid, text); },
                              Qt::QueuedConnection);
}

void queueChannelChange(uint64 schid, uint64 channelId, anyID invokerId,
                        const char* invokerName, bool created) {
    if (!qApp || !g_guard) return;
    // Copy callback data and the current channel name before a later rename
    // or deletion can change them while the UI event is still queued.
    QString actor = invokerName ? QString::fromUtf8(invokerName).trimmed() : QString();
    if (actor.isEmpty()) actor = clientNameById(schid, invokerId);
    if (actor.isEmpty())
        actor = invokerId ? QStringLiteral("用户 #%1").arg(invokerId) : QStringLiteral("服务器");
    QString channel = channelNameById(schid, channelId);
    if (channel.isEmpty()) channel = QStringLiteral("#%1").arg(channelId);
    queueSystemMessage(schid, (created ? QStringLiteral("%1 创建了频道 %2")
                                       : QStringLiteral("%1 编辑了频道 %2")).arg(actor, channel));
}

// MainWindow hosts all floating plugin UI. Re-found on every use: the client
// can rebuild its main window, which invalidates the old pointer.
QWidget* mainWindowHost() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w && w->isWindow() &&
            QString::fromLatin1(w->metaObject()->className()) == QLatin1String("MainWindow"))
            return w;
    }
    return nullptr;
}

// Keep notifications above the content on every chat tab.
void showToast(const QString& title, const QString& body,
               std::function<void()> onActivate = {}) {
    if (!qApp) return;
    QWidget* host = g_contentHost ? g_contentHost.data() : mainWindowHost();
    if (!host) return;
    if (!g_notifyToast || g_notifyToast->parentWidget() != host) {
        if (g_notifyToast) delete g_notifyToast.data();
        g_notifyToast = new NotifyToast(host);
    }
    g_notifyToast->onActivate = onActivate;
    g_notifyToast->showNotice(title, body);
}

void clearNotifyTarget() {
    g_notifyKey.clear();
    g_notifyTabName.clear();
    g_notifyMode = 0;
}

void showPokeToast(uint64 schid, const QString& poker, const QString& message) {
    // A poke is not a conversation event; it stays until clicked.
    clearNotifyTarget();
    const QString title = poker.isEmpty() ? QStringLiteral("有人戳了戳你")
                                          : QStringLiteral("%1 戳了戳你").arg(poker);
    showToast(title, message.isEmpty() ? QStringLiteral("没有留言") : message,
              [schid] {
                  openNotificationTarget(schid, TextMessageTarget_SERVER, 0, QString());
              });
}

// The client ships its sound packs under <install>/sound. Prefer a Flat White
// or Flat Dark pack when one is installed, otherwise the stock pack, so the
// cue follows the theme without depending on the Notifications option page.
QString notificationSoundPath() {
    if (!g_funcs.getAppPath) return QString();
    char buf[1024] = {};
    g_funcs.getAppPath(buf, sizeof(buf) - 1);
    const QDir base(QString::fromUtf8(buf));
    const char* candidates[] = {
        "sound/flatwhite/chat_message_inbound.wav",
        "sound/Flat White Sounds/chat_message_inbound.wav",
        "sound/Flat Dark Sounds/chat_message_inbound.wav",
        "sound/default/chat_message_inbound.wav",
    };
    for (const char* rel : candidates) {
        const QString path = base.filePath(QString::fromLatin1(rel));
        if (QFile::exists(path)) return path;
    }
    return QString();
}

void playNotificationSound(uint64 schid) {
    // A bot posting a burst of lines should not machine-gun the same chime.
    static qint64 lastPlayed = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastPlayed < 1200) return;
    lastPlayed = now;
    const QString path = notificationSoundPath();
    if (!path.isEmpty() && g_funcs.playWaveFile && schid) {
        const unsigned int error = g_funcs.playWaveFile(schid, path.toUtf8().constData());
        logLine(QStringLiteral("notify sound: %1 (error=%2)").arg(path).arg(error));
        if (error == ERROR_ok) return;
    }
    // Missing pack or refused playback: still give an audible cue.
    MessageBeep(MB_ICONASTERISK);
}

// Red dot on the chat tab whose conversation received something the user has
// not looked at yet. The stylesheet's tab colours veto the client's own unread
// colour, so the plugin draws the marker itself.
int chatTabIndexForName(const QString& name) {
    if (!g_chatTab || name.isEmpty()) return -1;
    for (int i = 1; i < g_chatTab->count(); ++i) {
        const QString tab = g_chatTab->tabText(i).trimmed();
        if (tab.compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    for (int i = 1; i < g_chatTab->count(); ++i) {
        const QString tab = g_chatTab->tabText(i).trimmed();
        if (tab.isEmpty()) continue;
        if (tab.contains(name, Qt::CaseInsensitive) || name.contains(tab, Qt::CaseInsensitive))
            return i;
    }
    return -1;
}

const QIcon& emptyUnreadDotIcon() {
    static const QIcon icon = [] {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        return QIcon(pm);
    }();
    return icon;
}

const QIcon& unreadDotIcon(bool dark) {
    static const auto icons = [] {
        std::array<QIcon, 2> result;
        for (int mode : {0, 1}) {
            for (int scale : {1, 2}) {
                QPixmap pm(16 * scale, 16 * scale);
                pm.setDevicePixelRatio(scale);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(foundation::color(Color::accent, mode == 1));
                p.drawEllipse(QRectF(4, 4, 8, 8));
                p.end();
                result[mode].addPixmap(pm);
            }
        }
        return result;
    }();
    return icons[dark ? 1 : 0];
}

void refreshUnreadDotIcons() {
    if (!g_chatTab) return;
    const auto& previous = unreadDotIcon(!discord::isDark());
    const auto& current = unreadDotIcon(discord::isDark());
    for (int i = 1; i < g_chatTab->count(); ++i) {
        if (g_chatTab->tabIcon(i).cacheKey() == previous.cacheKey())
            g_chatTab->setTabIcon(i, current);
    }
}

void markUnread(const QString& name) {
    if (!g_chatTab) return;
    const int index = chatTabIndexForName(name);
    if (index < 0 || index == g_chatTab->currentIndex()) return;
    g_chatTab->setTabIcon(index, unreadDotIcon(discord::isDark()));
}

void focusMainWindow() {
    QWidget* host = mainWindowHost();
    if (!host) return;
    if (host->isMinimized()) host->showNormal();
    host->raise();
    host->activateWindow();
}

// Clicking a card opens the conversation behind it: select the tab that owns
// the conversation, create a private tab through the client's own double-click
// handler when none exists, and bring the client forward.
void openNotificationTarget(uint64 schid, anyID targetMode, anyID clientId,
                            const QString& tabName) {
    const uint64 current = g_funcs.getCurrentServerConnectionHandlerID
                               ? g_funcs.getCurrentServerConnectionHandlerID()
                               : 0;
    if (schid && current && schid != current) {
        logLine(QStringLiteral("notify click: conversation on another server (%1)").arg(schid));
        focusMainWindow();
        return;
    }
    if (!g_chatTab) {
        if (auto* tw = qobject_cast<QTabWidget*>(findWidget("ChatTabWidget"))) g_chatTab = tw;
    }
    int index = -1;
    if (targetMode == TextMessageTarget_SERVER) {
        index = (g_chatTab && g_chatTab->count() > 0) ? 0 : -1;
    } else if (!tabName.isEmpty()) {
        index = chatTabIndexForName(tabName);
    }
    if (index >= 0) {
        g_chatTab->setCurrentIndex(index);
    } else if (targetMode == TextMessageTarget_CLIENT && clientId && !tabName.isEmpty()) {
        // No tab yet: replay the client's double-click on that member row so
        // the client creates and selects the private chat itself.
        openPrivateChat(static_cast<unsigned short>(clientId), tabName);
    }
    logLine(QStringLiteral("notify click: mode=%1 tab=%2 index=%3")
                .arg(targetMode)
                .arg(tabName.isEmpty() ? QStringLiteral("-") : tabName)
                .arg(index));
    focusMainWindow();
}

// Sound and taskbar flash for every incoming message. The card and the unread
// dot are only added when the conversation is not the one already on screen.
void notifyIncoming(uint64 schid, anyID targetMode, anyID clientId,
                    const QString& title, const QString& body,
                    const QString& tabName, const QString& chatKey, bool showCard) {
    if (showCard) {
        g_notifyKey = chatKey;
        g_notifyTabName = tabName;
        g_notifyMode = targetMode;
        showToast(title, body, [schid, targetMode, clientId, tabName] {
            clearNotifyTarget();
            openNotificationTarget(schid, targetMode, clientId, tabName);
        });
        markUnread(tabName);
    }
    playNotificationSound(schid);
    QWidget* host = mainWindowHost();
    const bool alert = host && (!host->isActiveWindow() || host->isMinimized());
    if (alert) QApplication::alert(host, 0);
    logLine(QStringLiteral("notify: %1 | tab=%2 | card=%3 | taskbar=%4")
                .arg(title, tabName.isEmpty() ? QStringLiteral("-") : tabName,
                     showCard ? QStringLiteral("on") : QStringLiteral("off"),
                     alert ? QStringLiteral("flash") : QStringLiteral("skip")));
}

// Incoming private messages carry the sender's display name, but the chat tab
// may decorate it (server groups, spacing). Prefer whichever open tab matches.
QString privateKeyForIncoming(uint64 schid, anyID peerId, const QString& fromName) {
    QStringList names;
    names << fromName;
    const QString display = clientNameById(schid, peerId);
    if (!display.isEmpty() && display != fromName) names << display;
    if (g_chatTab) {
        for (int i = 1; i < g_chatTab->count(); ++i) {
            const QString tab = g_chatTab->tabText(i).trimmed();
            if (tab.isEmpty()) continue;
            for (const QString& candidate : names) {
                if (candidate.isEmpty()) continue;
                if (tab.compare(candidate, Qt::CaseInsensitive) == 0 ||
                    tab.contains(candidate, Qt::CaseInsensitive) ||
                    candidate.contains(tab, Qt::CaseInsensitive)) {
                    return QStringLiteral("p:") + tab;
                }
            }
        }
    }
    if (!display.isEmpty()) return QStringLiteral("p:") + display;
    return QStringLiteral("p:") + fromName;
}

void updateChatOverlay() {
    if (!g_chatTab) {
        auto* tw = qobject_cast<QTabWidget*>(findWidget("ChatTabWidget"));
        if (!tw) return;
        g_chatTab = tw;
        g_chatTabConn = QObject::connect(tw, &QTabWidget::currentChanged, tw, [](int index) {
            // Opening a conversation is what clears its unread marker.
            if (g_chatTab && index > 0 &&
                g_chatTab->tabIcon(index).cacheKey() != emptyUnreadDotIcon().cacheKey())
                g_chatTab->setTabIcon(index, emptyUnreadDotIcon());
            const QString before = g_overlayKey;
            const bool wasInfoShown = g_infoShown;
            if (wasInfoShown) hideInfoPage();
            else updateChatOverlay();
            // Manually opening the conversation behind a card dismisses it. A
            // card that arrived while the user was already on that tab stays;
            // there was no switch. Private tabs may decorate the peer name, so
            // the tab text is compared as well as the history key.
            const QString tab = (g_chatTab && index >= 0)
                                    ? g_chatTab->tabText(index).trimmed() : QString();
            uint64 chSchid = 0, chId = 0;
            QString chName;
            currentChannelInfo(chSchid, chId, chName);
            const bool nonPrivateTab = index == 0 || (!chName.isEmpty() && tab == chName);
            const bool samePrivate =
                !nonPrivateTab && g_notifyMode == TextMessageTarget_CLIENT &&
                !g_notifyTabName.isEmpty() && !tab.isEmpty() &&
                (tab.compare(g_notifyTabName, Qt::CaseInsensitive) == 0 ||
                 tab.contains(g_notifyTabName, Qt::CaseInsensitive) ||
                 g_notifyTabName.contains(tab, Qt::CaseInsensitive));
            const bool targetOpen =
                (!g_notifyKey.isEmpty() && g_overlayKey == g_notifyKey) || samePrivate;
            if ((g_overlayKey != before || wasInfoShown) && targetOpen) {
                clearNotifyTarget();
                if (g_notifyToast) g_notifyToast->dismiss();
            }
        });
    }
    for (int i = 1; i < g_chatTab->count(); ++i) {
        if (g_chatTab->tabIcon(i).isNull()) g_chatTab->setTabIcon(i, emptyUnreadDotIcon());
    }
    QWidget* host = ensureCardHost(g_chatTab);
    if (!host) return;
    if (!g_modernChat) {
        auto* mc = new ModernChat(host);
        mc->hide();
        g_modernChat = mc;
        g_overlayKey.clear();
        // currentChanged does not fire when the selected server tab is clicked.
        QObject::connect(g_chatTab->tabBar(), &QTabBar::tabBarClicked, mc, [mc](int index) {
            // A different tab is committed by currentChanged. Only an already
            // selected tab needs this path to reveal chat or jump to the end.
            if (!g_chatTab || index < 0 || index != g_chatTab->currentIndex()) return;
            if (g_infoShown) hideInfoPage();
            else if (index == 0 && !mc->isHidden()) mc->scrollToLatest();
        });
        auto* filter = new OverlayFilter(g_chatTab);
        filter->onLayout = [] { layoutCardHost(); };
        g_chatTab->installEventFilter(filter);
        g_chatStack->installEventFilter(filter);
        g_chatFilter = filter;
    }

    layoutCardHost();
    if (g_infoShown && g_infoPage) {
        host->show();
        host->raise();
        g_infoPage->raise();
    }

    QWidget* log = g_chatTab->currentWidget();
    const bool infoActive = g_infoShown;

    QString channelName;
    uint64 schid = 0;
    uint64 channel = 0;
    currentChannelInfo(schid, channel, channelName);

    const int index = g_chatTab->currentIndex();
    const QString tabText = index >= 0 ? g_chatTab->tabText(index).trimmed() : QString();
    const bool serverTab = (index == 0);
    const bool channelTab = !channelName.isEmpty() && tabText == channelName;
    const bool privateTab = !serverTab && !channelTab && !tabText.isEmpty();
    const bool showOverlay = !infoActive && log && (serverTab || channelTab || privateTab);

    QString key;
    if (serverTab)
        key = serverKey(schid);
    else if (channelTab)
        key = QString::number(schid) + QLatin1Char(':') + QString::number(channel);
    else if (privateTab)
        key = QStringLiteral("p:") + tabText;

    static QString lastDecision;
    const QString decision = QStringLiteral("tab=[%1] channel=[%2] kind=%3 overlay=%4")
                                 .arg(tabText, channelName,
                                      serverTab ? "server" : (channelTab ? "channel" : (privateTab ? "private" : "none")),
                                      showOverlay ? "on" : "off");
    if (decision != lastDecision) {
        lastDecision = decision;
        logLine(QStringLiteral("chat overlay: ") + decision);
    }

    if (g_chatHeader) {
        if (serverTab)
            g_chatHeader->setChatContext(serverNameById(schid), QStringLiteral("服务器"));
        else if (privateTab)
            g_chatHeader->setChatContext(tabText, QStringLiteral("私聊"));
        else
            g_chatHeader->setChatContext(QString(), QString());
    }

    if (!showOverlay) {
        g_modernChat->hide();
        if (!infoActive) host->hide();
        return;
    }

    const bool revealingLog = g_modernChat->isHidden();
    if (g_overlayKey != key) {
        g_modernChat->finishWheelScroll();
        // Remember where the channel or private conversation being left was scrolled to.
        if (!g_overlayKey.isEmpty() && !g_overlayKey.startsWith(QLatin1String("s:")))
            g_chatScroll[g_overlayKey] =
                g_modernChat->isFollowingBottom() ? -1 : g_modernChat->scrollOffset();
        g_overlayKey = key;
        QVector<ChatMessage> messages = g_chatHistory.value(key);
        if (privateTab && messages.isEmpty()) {
            for (auto it = g_chatHistory.constBegin(); it != g_chatHistory.constEnd(); ++it) {
                if (!it.key().startsWith(QStringLiteral("p:"))) continue;
                const QString peer = it.key().mid(2);
                if (peer.contains(tabText, Qt::CaseInsensitive) ||
                    tabText.contains(peer, Qt::CaseInsensitive)) {
                    messages = it.value();
                    if (!messages.isEmpty()) break;
                }
            }
        }
        g_modernChat->setEmptyText(serverTab ? QStringLiteral("暂无服务器事件")
                                             : QStringLiteral("这里还没有消息，来打个招呼吧"));
        g_modernChat->setMessages(messages, serverTab ? -1 : g_chatScroll.value(key, -1));
    } else if (revealingLog && serverTab) {
        g_modernChat->scrollToLatest();
    }
    host->show();
    host->raise();
    if (revealingLog) g_modernChat->show();
}

void attachBannerIfAny() {
    if (!g_infoPage || g_banner) return;
    QWidget* b = findBanner();
    if (!b) return;
    if (auto* v = qobject_cast<QVBoxLayout*>(g_infoPage->layout())) {
        mountBanner(b, v, v->count() - 1);
        logLine("banner attached to info page");
    }
}

void updateInfoStatus();

void refreshClientNames() {
    if (!g_funcs.getClientList || !g_funcs.getClientDisplayName) return;
    const quint64 h = g_funcs.getCurrentServerConnectionHandlerID
                          ? g_funcs.getCurrentServerConnectionHandlerID() : 0;
    if (!h) return;
    anyID* clients = nullptr;
    if (g_funcs.getClientList(h, &clients) != 0 || !clients) return;
    for (anyID* c = clients; *c; ++c) {
        char name[512] = {};
        if (g_funcs.getClientDisplayName(h, *c, name, sizeof(name)) == ERROR_ok) {
            const QString n = QString::fromUtf8(name);
            if (!n.isEmpty()) g_clientNames.insert(*c, n);
        }
    }
    g_funcs.freeMemory(clients);
}

void applySelfState(uint64 server, anyID self, int flag, bool value, bool local) {
    if (!g_funcs.getCurrentServerConnectionHandlerID ||
        g_funcs.getCurrentServerConnectionHandlerID() != server) return;
    if (g_userPanel) g_userPanel->setSelfState(server, self, flag, value, local);
    if (g_treeRows) g_treeRows->setSelfState(server, self, flag, value, local);
    if (g_styledTree) g_styledTree->viewport()->update();
    updateInfoStatus();
}

struct SelfTalkState {
    using Clock = std::chrono::steady_clock;
    uint64 server = 0;
    anyID client = 0;
    bool talking = false;
    Clock::time_point protectUntil{};
};

SelfTalkState g_selfTalk;
SelfTalkState::Clock::time_point g_talkTraceStart;
SelfTalkState::Clock::time_point g_talkTraceUntil;

bool talkTraceActive() {
    return SelfTalkState::Clock::now() < g_talkTraceUntil;
}

void logTalkTrace(const QString& detail) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        SelfTalkState::Clock::now() - g_talkTraceStart).count();
    logLine(QStringLiteral("self-talk +%1ms %2 published=%3 panel=%4 tree=%5")
        .arg(qlonglong(elapsed)).arg(detail).arg(g_selfTalk.talking)
        .arg(!g_userPanel.isNull()).arg(!g_treeRows.isNull()));
}

void publishSelfTalk() {
    if (g_userPanel) g_userPanel->setTalking(g_selfTalk.talking);
    if (g_treeRows) g_treeRows->setSelfTalking(g_selfTalk.server, g_selfTalk.client, g_selfTalk.talking);
}

void bindSelfTalk(uint64 server, anyID client) {
    if (server == g_selfTalk.server && client == g_selfTalk.client) return;
    g_selfTalk = {server, client};
}

uint64 currentTalkServer() {
    return g_funcs.getCurrentServerConnectionHandlerID ? g_funcs.getCurrentServerConnectionHandlerID() : 0;
}

// A temporary failure to resolve self should not clear a live indicator.
// A different connection must establish its own identity from scratch.
anyID resolveSelfTalkClient(uint64 server) {
    int connectionStatus = STATUS_DISCONNECTED;
    if (!server || (g_funcs.getConnectionStatus &&
        g_funcs.getConnectionStatus(server, &connectionStatus) == ERROR_ok &&
        connectionStatus < STATUS_CONNECTED)) {
        bindSelfTalk(0, 0);
        return 0;
    }
    if (server != g_selfTalk.server) bindSelfTalk(server, 0);
    anyID client = 0;
    if (!g_funcs.getClientID || g_funcs.getClientID(server, &client) != ERROR_ok || !client)
        return 0;
    bindSelfTalk(server, client);
    return client;
}

bool isTalkingStatus(int status) {
    return status == STATUS_TALKING || status == STATUS_TALKING_WHILE_DISABLED;
}

void refreshSelfTalk() {
    const uint64 server = currentTalkServer();
    const anyID client = resolveSelfTalkClient(server);
    const bool tracing = talkTraceActive();
    const bool protectedByCallback = SelfTalkState::Clock::now() < g_selfTalk.protectUntil;
    int status = -1;
    int result = -1;
    int selfStatus = -1;
    int selfResult = -1;
    const bool hasSelfGetter = g_funcs.getClientSelfVariableAsInt != nullptr;
    if (client && (!protectedByCallback || tracing)) {
        // The generic client view can stay quiet even while our local capture
        // is transmitting. Reconcile self with the local view whenever available.
        if (hasSelfGetter) {
            selfResult = int(g_funcs.getClientSelfVariableAsInt(server, CLIENT_FLAG_TALKING, &selfStatus));
        }
        if ((!hasSelfGetter || tracing) && g_funcs.getClientVariableAsInt) {
            result = int(g_funcs.getClientVariableAsInt(server, client, CLIENT_FLAG_TALKING, &status));
        }
        const int talkResult = hasSelfGetter ? selfResult : result;
        const int talkStatus = hasSelfGetter ? selfStatus : status;
        if (!protectedByCallback && talkResult == ERROR_ok) g_selfTalk.talking = isTalkingStatus(talkStatus);
    }
    publishSelfTalk();
    if (tracing) {
        logTalkTrace(QStringLiteral("poll server=%1 client=%2 protected=%3 clientRc=%4 clientStatus=%5 selfRc=%6 selfStatus=%7 source=%8")
            .arg(server).arg(client).arg(protectedByCallback).arg(result).arg(status).arg(selfResult).arg(selfStatus)
            .arg(hasSelfGetter ? QLatin1String("self") : QLatin1String("client")));
    }
}

void refreshClientStatesNow() {
    refreshSelfTalk();
    if (!g_treeRows) return;
    g_treeRows->refreshStates();
    if (g_styledTree) g_styledTree->viewport()->update();
    updateInfoStatus();
}

void updateInfoStatus() {
    if (!g_infoStatus) return;
    const QModelIndex cur = (g_infoTree && g_infoTree->selectionModel())
        ? g_infoTree->selectionModel()->currentIndex() : QModelIndex();
    if (!cur.isValid() || !g_treeRows || !g_treeRows->isClientRow(cur)) {
        g_infoStatus->hide();
        return;
    }
    const QString st = g_treeRows->clientStatus(cur);
    if (g_infoStatus->text() != st) g_infoStatus->setText(st);
    if (g_infoStatus->isHidden() == !st.isEmpty()) g_infoStatus->setVisible(!st.isEmpty());
}

void wireInfoSelection(QTreeView* tree) {
    if (!tree || g_infoTree == tree) return;
    if (g_infoConn) QObject::disconnect(g_infoConn);
    g_infoTree = tree;
    if (!tree->selectionModel()) return;
    g_infoConn = QObject::connect(tree->selectionModel(), &QItemSelectionModel::currentChanged,
                         qApp, [](const QModelIndex& cur, const QModelIndex&) {
        if (!g_infoPage || !g_treeRows) return;
        QString name = cur.data(Qt::DisplayRole).toString();
        if (name.isEmpty()) return;
        const bool serverRow = !cur.parent().isValid();
        if (g_banner) g_banner->setVisible(serverRow);
        if (g_infoTitle) g_infoTitle->setText(name);
        updateInfoStatus();
        showInfoPage();
    });
}

void layoutPane() {
    if (!g_userPanel || !g_userPanelHost || !g_paneTree) return;
    const int h = g_userPanel->sizeHint().height();
    QWidget* pane = g_userPanelHost;
    // The user card floats over the bottom of the list. Bottom padding on the
    // viewport keeps the last rows scrollable clear of it while the card sits
    // over the empty band. Re-applied here because a theme switch reloads the
    // stylesheet and would drop an append done at install time.
    if (qApp && qApp->styleSheet().indexOf(QLatin1String("ServerTreeView { padding-bottom")) < 0) {
        qApp->setStyleSheet(qApp->styleSheet() +
                            QStringLiteral("\nQTreeView#ServerTreeView { padding-bottom: %1px; }\n").arg(h));
    }
    // The tree keeps the full pane height so the card floats over its rows
    // instead of sitting in a reserved strip.
    const QRect treeRect = pane->rect();
    // Keep the native scrollbar outside the card and clickable.
    int scrollbar = 0;
    if (QScrollBar* sb = g_paneTree->verticalScrollBar(); sb && sb->isVisible())
        scrollbar = sb->width();
    const QRect panelRect(0, qMax(0, pane->height() - h),
                          qMax(0, pane->width() - scrollbar), h);
    // Unconditional setGeometry on every heartbeat forced a relayout pass and
    // flickered the widgets above it, including the chat input.
    if (g_paneTree->geometry() != treeRect) g_paneTree->setGeometry(treeRect);
    if (g_userPanel->geometry() != panelRect) g_userPanel->setGeometry(panelRect);
    if (!g_userPanel->isVisible()) g_userPanel->show();
    g_userPanel->raise();
}

void installChannelPanel() {
    if (g_installed) return;
    QTreeView* tree = findActiveTree();
    if (!tree) return;
    QWidget* pane = tree->parentWidget();
    if (!pane) return;

    styleTree(tree);

    // Keep the info pane: clicking a client or channel must show its details.
    if (auto* inner = qobject_cast<QSplitter*>(pane->parentWidget())) {
        if (inner->count() > 1) {
            QWidget* info = inner->widget(inner->count() - 1);
            if (info) info->show();
        }
    }

    if (!g_userPanel) {
        auto* user = new UserPanel(pane);
        user->setFocusPolicy(Qt::NoFocus);
        user->setFunctions(g_funcs);
        user->setPluginId(g_pluginId);
        user->setStateChangeHandler([](quint64 server, unsigned short self, int flag, bool value) {
            applySelfState(server, self, flag, value, true);
        });
        user->setErrorHandler([](quint64 server, int flag, unsigned int error) {
            const QString action = flag == CLIENT_AWAY ? QStringLiteral("切换离开状态失败")
                : (flag == CLIENT_INPUT_MUTED ? QStringLiteral("切换麦克风静音失败")
                                              : QStringLiteral("切换耳机静音失败"));
            queueOperationError(server, action + QStringLiteral("：") + operationErrorText(error));
        });
        g_userPanelHost = pane;
        g_paneTree = tree;
        g_userPanel = user;
        if (auto* l = pane->layout()) l->removeWidget(user);
        user->setParent(pane);
        layoutPane();
        auto* pf = new PaneFilter(pane);
        pf->onResize = [] { layoutPane(); };
        pane->installEventFilter(pf);
        g_paneFilter = pf;
    }

    // Channel header above the chat, Discord-style.
    if (!g_chatHeader) {
        QWidget* chat = findWidget("MainWindowChatWidget");
        if (chat && chat->layout()) {
            auto* header = new ChatHeader(chat);
            header->setFunctions(g_funcs);
            chat->layout()->addWidget(header);
            // move to the top of the chat column
            if (auto* box = qobject_cast<QVBoxLayout*>(chat->layout())) {
                box->removeWidget(header);
                box->insertWidget(0, header);
            }
            g_chatHeader = header;
        }
    }

    installInfoTab();
    wireInfoSelection(tree);

    // The tree was painted by the client before the delegate existed. Hiding it
    // until the first styled frame removes the flash of the stock rows.
    if (!g_firstPaintDone) {
        tree->setUpdatesEnabled(false);
        QTimer::singleShot(0, tree, [tree] {
            tree->setUpdatesEnabled(true);
            tree->viewport()->update();
            g_firstPaintDone = true;
        });
    }

    g_installed = true;
    logLine(QString("native tree styled in %1").arg(widgetPath(pane)));

    applyLinkPalette();
    updateInfoStatus();
    layoutPane();
    if (g_userPanel) g_userPanel->refresh();
}

// TS3 opens a private chat when its tree emits doubleClicked. Rather than
// looking for an API call that does not exist, replay that signal on the
// (hidden) stock tree for the matching client row.
void openPrivateChat(unsigned short clientId, const QString& name) {
    Q_UNUSED(clientId);
    QTreeView* tree = findServerTree();
    if (!tree || !tree->model()) return;
    QAbstractItemModel* m = tree->model();

    std::function<QModelIndex(const QModelIndex&, int)> find =
        [&](const QModelIndex& parent, int depth) -> QModelIndex {
        for (int i = 0, n = m->rowCount(parent); i < n; ++i) {
            const QModelIndex idx = m->index(i, 0, parent);
            const QString text = idx.data(Qt::DisplayRole).toString();
            if (depth >= 2 && m->rowCount(idx) == 0 &&
                (text == name || text.endsWith(QStringLiteral(" ") + name))) {
                return idx;
            }
            const QModelIndex found = find(idx, depth + 1);
            if (found.isValid()) return found;
        }
        return QModelIndex();
    };

    const QModelIndex idx = find(QModelIndex(), 0);
    if (!idx.isValid()) {
        logLine(QString("chat: no client row found for %1").arg(name));
        return;
    }
    // Expand ancestors and scroll the row into view so it has a real rect.
    for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent()) tree->expand(p);
    tree->setCurrentIndex(idx);
    tree->scrollTo(idx);

    const QRect vr = tree->visualRect(idx);
    if (!vr.isValid()) {
        logLine(QString("chat: row rect unavailable for %1").arg(name));
        return;
    }
    QWidget* vp = tree->viewport();
    const QPoint pos = vr.center();
    const QPoint global = vp->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent dbl(QEvent::MouseButtonDblClick, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rel(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &press);
    QApplication::sendEvent(vp, &dbl);
    QApplication::sendEvent(vp, &rel);
    logLine(QString("chat: synthetic double-click for %1 at %2,%3").arg(name).arg(pos.x()).arg(pos.y()));
}

void applyLayout();
void pinSplitterSizes();

// The window now resizes freely, so only a floor is enforced: below it the
// sidebar arrangement no longer has a usable chat column. The on-screen check
// stays because the client restores its last geometry, which may point at a
// monitor that has since been unplugged.
void applyWindowBounds() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || !w->isWindow() || !w->isVisible()) continue;
        if (QString::fromLatin1(w->metaObject()->className()) != "MainWindow") continue;
        {
            if (w->minimumWidth() != kMinWindowWidth || w->minimumHeight() != kMinWindowHeight) {
                w->setMinimumSize(kMinWindowWidth, kMinWindowHeight);
                logLine(QString("window minimum size applied: %1x%2")
                            .arg(kMinWindowWidth).arg(kMinWindowHeight));
            }
            const QRect frame = w->frameGeometry();
            bool onScreen = false;
            for (QScreen* screen : QGuiApplication::screens()) {
                if (!screen) continue;
                if (screen->availableGeometry().intersected(frame).width() >= 200 &&
                    screen->availableGeometry().intersected(frame).height() >= 150) {
                    onScreen = true;
                    break;
                }
            }
            if (!onScreen) {
                QScreen* target = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
                if (target) {
                    const QRect avail = target->availableGeometry();
                    w->move(avail.center() - QPoint(w->width() / 2, w->height() / 2));
                    logLine(QString("window recentred into %1x%2").arg(avail.width()).arg(avail.height()));
                }
            }
        }
        return;
    }
}

// The stock toolbar duplicates what the custom sidebar now owns (away, mic,
// headset, master volume, avatar) and breaks the flat look, so hide it. The
// widgets stay alive, which keeps the volume slider usable behind our own.
void hideStockToolbar() {
    if (g_stockToolbar || !qApp) return;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || !w->isWindow()) continue;
        if (QString::fromLatin1(w->metaObject()->className()) != "MainWindow") continue;
        const auto bars = w->findChildren<QToolBar*>();
        for (QToolBar* tb : bars) {
            if (QString::fromLatin1(tb->metaObject()->className()) == "ImprovedToolBar") {
                tb->hide();
                g_stockToolbar = tb;
                logLine("stock toolbar hidden");
                return;
            }
        }
    }
}

// The splitter and the native chat layout own the pane and composer bounds.
// Only synchronize the floating surfaces when the main window resizes.
void syncMainWindowLayout() {
    pinSplitterSizes();
    layoutPane();
    layoutCardHost();
}

// --------------------------------------------------------------- fast burst

// Right after startup or a reconnect the client builds a fresh widget tree and
// paints it in its stock form; a 1.5s tick would leave that visible. A short
// 50ms burst catches the new widgets almost immediately, then stops so the
// steady-state cost stays at the slow heartbeat.
QPointer<QTimer> g_burstTimer;
int g_burstTicks = 0;

void heartbeat();
void heartbeatNow();
void startBurst();

void heartbeatNow() {
    heartbeat();
}

void startBurst() {
    if (!qApp || !g_guard) return;
    if (!g_burstTimer) {
        g_burstTimer = new QTimer(qApp);
        g_burstTimer->setInterval(50);
        QObject::connect(g_burstTimer, &QTimer::timeout, g_guard, [] {
            heartbeat();
            if (++g_burstTicks >= 60) g_burstTimer->stop();
        });
    }
    g_burstTicks = 0;
    if (!g_burstTimer->isActive()) g_burstTimer->start();
}

void heartbeat() {
    if (!qApp || !g_guard) return;
    installChannelPanel();
    hideStockToolbar();
    applyWindowBounds();
    QTreeView* tree = findActiveTree();
    styleTree(tree);
    if (tree) {
        installInfoTab();
        attachBannerIfAny();
        wireInfoSelection(tree);
        if (g_treeRows) g_treeRows->refresh();
        tree->viewport()->update();
    }
    // A style switch needs a restart, but the first heartbeat after that
    // restart is where the plugin's own widgets pick up the new palette.
    const bool darkTheme = detectDarkTheme();
    const int mode = darkTheme ? 1 : 0;
    if (mode != g_darkMode) {
        g_darkMode = mode;
        applyPluginTheme(darkTheme);
        logLine(QStringLiteral("theme mode: %1 (stylesheet %2 chars, window %3)")
                    .arg(darkTheme ? "dark" : "light")
                    .arg(qApp->styleSheet().size())
                    .arg(qApp->palette().color(QPalette::Window).name()));
    }
    applyWindowFrame(darkTheme);
    applyLinkPalette();
    updateInfoStatus();
    layoutPane();
    refreshClientNames();
    if (g_userPanel) g_userPanel->refresh();
    if (g_chatHeader) g_chatHeader->refresh();
    updateChatOverlay();
    updateLayout();
    pinSplitterSizes();
}

// The native model updates itself; we only need to refresh our name/id caches
// and repaint. Coalesce the burst of move events.
void queueTreeRefresh() {
    if (!qApp) return;
    static bool queued = false;
    if (queued || !g_guard) return;
    queued = true;
    QMetaObject::invokeMethod(g_guard, [] {
        queued = false;
        QTreeView* tree = findActiveTree();
        styleTree(tree);
        if (g_treeRows) g_treeRows->refresh();
        if (g_styledTree) g_styledTree->viewport()->update();
    }, Qt::QueuedConnection);
}

void onUiReady() {
    logLine("onUiReady");
    dumpWidgets();
    if (qApp) {
        const bool dark = detectDarkTheme();
        g_darkMode = dark ? 1 : 0;
        discord::setDark(dark);
        applyLinkPalette();
        logLine(QStringLiteral("theme detect: %1 (stylesheet %2 chars, window %3)")
                    .arg(dark ? "dark" : "light")
                    .arg(qApp->styleSheet().size())
                    .arg(qApp->palette().color(QPalette::Window).name()));
    }
    if (!g_guard) g_guard = new QObject(qApp);
    if (qgetenv("FLAT_WHITE_SELF_TALK_TRACE") == "1") {
        g_talkTraceStart = SelfTalkState::Clock::now();
        g_talkTraceUntil = g_talkTraceStart + std::chrono::minutes(10);
        logLine(QStringLiteral("self-talk trace enabled for 10 minutes"));
    }
    if (!g_frameFilter) {
        g_frameFilter = new FrameFilter(qApp);
        qApp->installEventFilter(g_frameFilter);
    }
    if (!g_timer) {
        g_timer = new QTimer(qApp);
        g_timer->setInterval(1500);
        QObject::connect(g_timer, &QTimer::timeout, g_guard, [] { heartbeat(); });
        g_timer->start();
        g_stateTimer = new QTimer(qApp);
        g_stateTimer->setInterval(600);
        QObject::connect(g_stateTimer, &QTimer::timeout, g_guard, [] { refreshClientStatesNow(); });
        g_stateTimer->start();
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { cleanupUi(); });
    }
    installChannelPanel();
    updateLayout();
    // The client paints its own widgets first and the theme only takes over on
    // the next tick, which is visible as a flash of the stock UI. Run a short
    // fast burst so the takeover lands within a frame or two.
    heartbeatNow();
    startBurst();
}

}  // namespace

// ---------------------------------------------------------------- TS3 API

extern "C" __declspec(dllexport) const char* ts3plugin_name() {
    return "Flat White UI";
}
extern "C" __declspec(dllexport) const char* ts3plugin_version() {
    // Injected by tools/build.bat so a release only has to bump release_meta.py.
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.0.0"
#endif
    return PLUGIN_VERSION;
}
extern "C" __declspec(dllexport) int ts3plugin_apiVersion() {
    return 26;
}
extern "C" __declspec(dllexport) const char* ts3plugin_author() {
    return "3ku-nanodesu";
}
extern "C" __declspec(dllexport) const char* ts3plugin_description() {
    return "Content-first layout, Discord-style channel tree, modern chat bubbles, a user panel, poke notifications, free window resizing and light/dark theme support for the Flat White and Flat Dark themes.";
}
extern "C" __declspec(dllexport) void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    g_funcs = funcs;
    logLine("setFunctionPointers");
}
extern "C" __declspec(dllexport) void ts3plugin_registerPluginID(const char* id) {
    g_pluginId = id ? QByteArray(id) : QByteArray();
    if (!qApp || !g_guard) return;
    const QByteArray pluginId = g_pluginId;
    QMetaObject::invokeMethod(g_guard, [pluginId] {
        if (g_userPanel) g_userPanel->setPluginId(pluginId);
    }, Qt::QueuedConnection);
}
extern "C" __declspec(dllexport) int ts3plugin_init() {
    logLine("init");
    // Keep this DLL resident for the process lifetime. TS3 unloads plugins
    // while it is still tearing down its widget tree; pinned code means no
    // dangling vtables/callbacks during that teardown.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCSTR>(&ts3plugin_init), &self);
    std::thread([] {
        for (int i = 0; i < 120; ++i) {
            if (QCoreApplication::instance()) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [] { onUiReady(); },
                                          Qt::QueuedConnection);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }).detach();
    return 0;
}
void cleanupUi() {
    if (g_stockToolbar) g_stockToolbar->show();

    if (g_stateTimer) { g_stateTimer->stop(); delete g_stateTimer.data(); g_stateTimer = nullptr; }
    if (g_timer) { g_timer->stop(); delete g_timer.data(); g_timer = nullptr; }
    if (g_frameFilter) {
        if (qApp) qApp->removeEventFilter(g_frameFilter);
        delete g_frameFilter.data();
        g_frameFilter = nullptr;
    }
    resetWindowFrames();
    delete g_treeWheelMotion.data();
    delete g_guard.data();
    g_guard = nullptr;
    g_connectionStatus.clear();
    g_selfTalk = {};
    g_talkTraceUntil = {};

    if (g_infoConn) { QObject::disconnect(g_infoConn); g_infoConn = QMetaObject::Connection(); }
    if (g_chatTabConn) { QObject::disconnect(g_chatTabConn); g_chatTabConn = QMetaObject::Connection(); }
    if (g_chatFilter) {
        delete g_chatFilter.data();
        g_chatFilter = nullptr;
    }
    if (g_modernChat) { delete g_modernChat.data(); g_modernChat = nullptr; }
    if (g_notifyToast) { delete g_notifyToast.data(); g_notifyToast = nullptr; }
    g_chatTab = nullptr;
    g_infoShown = false;

    if (g_banner && g_bannerHome) {
        g_banner->setParent(g_bannerHome);
        g_banner->setMaximumHeight(g_bannerMaximumHeight);
        if (auto* box = qobject_cast<QBoxLayout*>(g_bannerHome->layout()); box && g_bannerIndex >= 0)
            box->insertWidget(g_bannerIndex, g_banner, g_bannerStretch);
        g_banner->setVisible(g_bannerWasVisible);
    }
    if (g_infoFrame && g_infoHome) {
        g_infoFrame->setParent(g_infoHome);
        if (g_infoSplitter) {
            g_infoSplitter->insertWidget(g_infoIndex, g_infoFrame);
        } else if (auto* box = qobject_cast<QBoxLayout*>(g_infoHome->layout()); box && g_infoIndex >= 0) {
            box->insertWidget(g_infoIndex, g_infoFrame, g_infoStretch, g_infoAlignment);
        }
        g_infoFrame->show();
    }
    delete g_cardHost.data();
    g_cardHost = nullptr;
    if (qApp) {
        restoreLayout();
        restoreContentHost();
    }

    for (auto it = g_oldDelegates.begin(); it != g_oldDelegates.end(); ++it) {
        if (it.key() && it.value()) it.key()->setItemDelegate(it.value());
    }
    g_oldDelegates.clear();
    delete g_treeRows.data();
    g_treeRows = nullptr;

    if (g_userPanel) { delete g_userPanel.data(); g_userPanel = nullptr; }
    if (g_chatHeader) { delete g_chatHeader.data(); g_chatHeader = nullptr; }
    if (g_paneFilter) { delete g_paneFilter.data(); g_paneFilter = nullptr; }
    if (g_log) { g_log->close(); delete g_log; g_log = nullptr; }
}

extern "C" __declspec(dllexport) void ts3plugin_shutdown() {
    logLine("shutdown");
    cleanupUi();
}

// Speech light must not wait for the polling heartbeat.
extern "C" __declspec(dllexport) void ts3plugin_onTalkStatusChangeEvent(
    uint64 serverConnectionHandlerID, int status, int isReceivedWhisper, anyID clientID) {
    Q_UNUSED(isReceivedWhisper);
    if (!qApp || !g_guard || !clientID) return;
    const auto arrived = SelfTalkState::Clock::now();
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, clientID, status, arrived] {
        const uint64 activeServer = currentTalkServer();
        const bool tracing = talkTraceActive();
        const auto traceEvent = [&](const char* decision, anyID self) {
            const auto queued = std::chrono::duration_cast<std::chrono::milliseconds>(
                SelfTalkState::Clock::now() - arrived).count();
            logTalkTrace(QStringLiteral("event server=%1 active=%2 client=%3 self=%4 status=%5 queuedMs=%6 decision=%7")
                .arg(serverConnectionHandlerID).arg(activeServer).arg(clientID).arg(self)
                .arg(status).arg(qlonglong(queued)).arg(QLatin1String(decision)));
        };
        if (activeServer != serverConnectionHandlerID) {
            if (tracing && g_selfTalk.server == serverConnectionHandlerID && g_selfTalk.client == clientID)
                traceEvent("inactive-server", g_selfTalk.client);
            return;
        }
        const anyID self = resolveSelfTalkClient(serverConnectionHandlerID);
        if (self && clientID == self) {
            g_selfTalk.talking = isTalkingStatus(status);
            g_selfTalk.protectUntil = SelfTalkState::Clock::now() + std::chrono::seconds(1);
        } else if (self && g_treeRows) {
            g_treeRows->setTalking(clientID, QString(), isTalkingStatus(status));
        }
        publishSelfTalk();
        if (tracing && (!self || clientID == self)) traceEvent(self ? "applied" : "no-self", self);
        if (g_styledTree) g_styledTree->viewport()->update();
    }, Qt::QueuedConnection);
}

extern "C" __declspec(dllexport) void ts3plugin_onConnectStatusChangeEvent(
    uint64 serverConnectionHandlerID, int status, unsigned int errorNumber) {
    if (!qApp || !g_guard) return;
    const QString reason = errorNumber != ERROR_ok ? connectionErrorText(errorNumber) : QString();
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, status, errorNumber, reason] {
        const int previous = g_connectionStatus.value(serverConnectionHandlerID, STATUS_DISCONNECTED);
        g_connectionStatus[serverConnectionHandlerID] = status;
        QString text;
        int severity = 0;
        switch (status) {
        case STATUS_CONNECTING: text = QStringLiteral("正在连接服务器…"); break;
        case STATUS_CONNECTED: text = QStringLiteral("服务器已接受连接"); break;
        case STATUS_CONNECTION_ESTABLISHING: text = QStringLiteral("正在进入服务器…"); break;
        case STATUS_CONNECTION_ESTABLISHED: text = QStringLiteral("已连接到服务器"); break;
        case STATUS_DISCONNECTED:
            if (g_selfTalk.server == serverConnectionHandlerID) {
                bindSelfTalk(0, 0);
                publishSelfTalk();
                if (g_styledTree) g_styledTree->viewport()->update();
            }
            if (errorNumber != ERROR_ok) {
                text = (previous == STATUS_CONNECTION_ESTABLISHED
                            ? QStringLiteral("与服务器的连接已断开：%1（错误码 %2）")
                            : QStringLiteral("连接服务器失败：%1（错误码 %2）"))
                           .arg(reason).arg(errorNumber);
                severity = 2;
            } else {
                text = QStringLiteral("与服务器的连接已断开");
            }
            g_connectionStatus.remove(serverConnectionHandlerID);
            break;
        default: break;
        }
        if (!text.isEmpty()) appendSystemMessage(serverConnectionHandlerID, text, severity);
        if (g_userPanel) {
            if (status == STATUS_CONNECTION_ESTABLISHED) g_userPanel->applyAwayPreset(serverConnectionHandlerID);
            g_userPanel->refresh();
        }
    }, Qt::QueuedConnection);
    // A reconnect rebuilds the panes from scratch, so the stock widgets are
    // painted again; catch the new tree within a couple of frames.
    if (status == STATUS_CONNECTED || status == STATUS_CONNECTION_ESTABLISHED) startBurst();
    // Entering a server lands on whatever page was open last time, often the
    // client's own info page. Open the server event log instead.
    if (status == STATUS_CONNECTION_ESTABLISHED) {
        if (!g_guard) return;
        QMetaObject::invokeMethod(g_guard, [] {
            if (g_infoShown) hideInfoPage();
            if (g_chatTab && g_chatTab->currentIndex() != 0) g_chatTab->setCurrentIndex(0);
            updateChatOverlay();
        }, Qt::QueuedConnection);
    }
}

// Initial channel synchronization uses onNewChannelEvent; only this callback
// describes a channel that was actually created by an actor.
extern "C" __declspec(dllexport) void ts3plugin_onNewChannelCreatedEvent(
    uint64 serverConnectionHandlerID, uint64 channelID, uint64 channelParentID,
    anyID invokerID, const char* invokerName, const char* invokerUniqueIdentifier) {
    Q_UNUSED(channelParentID);
    Q_UNUSED(invokerUniqueIdentifier);
    queueChannelChange(serverConnectionHandlerID, channelID, invokerID, invokerName, true);
    queueTreeRefresh();
}

extern "C" __declspec(dllexport) void ts3plugin_onUpdateChannelEditedEvent(
    uint64 serverConnectionHandlerID, uint64 channelID, anyID invokerID,
    const char* invokerName, const char* invokerUniqueIdentifier) {
    Q_UNUSED(invokerUniqueIdentifier);
    queueChannelChange(serverConnectionHandlerID, channelID, invokerID, invokerName, false);
    queueTreeRefresh();
}

extern "C" __declspec(dllexport) void ts3plugin_onClientMoveEvent(
    uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
    int visibility, const char* moveMessage) {
    Q_UNUSED(visibility);
    Q_UNUSED(moveMessage);
    // Cache the name while the member is still listed: the client drops a
    // leaving member before this callback runs, so the lookup would fail.
    if (newChannelID != 0) refreshClientNames();
    queueTreeRefresh();
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, clientID, oldChannelID, newChannelID] {
        anyID self = 0;
        if (g_funcs.getClientID) g_funcs.getClientID(serverConnectionHandlerID, &self);
        if (clientID == self) {
            appendSystemMessage(serverConnectionHandlerID,
                                newChannelID ? QStringLiteral("您已切换到频道 %1")
                                                   .arg(channelNameById(serverConnectionHandlerID, newChannelID))
                                             : QStringLiteral("您已离开服务器"));
            return;
        }
        QString name = clientNameById(serverConnectionHandlerID, clientID);
        if (name.isEmpty()) name = QStringLiteral("用户");
        if (oldChannelID == 0)
            appendSystemMessage(serverConnectionHandlerID,
                                QStringLiteral("%1 加入了服务器").arg(name));
        else if (newChannelID == 0)
            appendSystemMessage(serverConnectionHandlerID,
                                QStringLiteral("%1 离开了服务器").arg(name));
        else
            appendSystemMessage(serverConnectionHandlerID,
                                QStringLiteral("%1 从 %2 切换到 %3")
                                    .arg(name,
                                         channelNameById(serverConnectionHandlerID, oldChannelID),
                                         channelNameById(serverConnectionHandlerID, newChannelID)));
    }, Qt::QueuedConnection);
}

extern "C" __declspec(dllexport) void ts3plugin_onClientMoveTimeoutEvent(
    uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
    int visibility, const char* moveMessage) {
    Q_UNUSED(oldChannelID);
    Q_UNUSED(newChannelID);
    Q_UNUSED(visibility);
    Q_UNUSED(moveMessage);
    queueTreeRefresh();
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, clientID] {
        anyID self = 0;
        if (g_funcs.getClientID) g_funcs.getClientID(serverConnectionHandlerID, &self);
        QString name = clientNameById(serverConnectionHandlerID, clientID);
        if (name.isEmpty()) name = QStringLiteral("用户");
        appendSystemMessage(serverConnectionHandlerID,
                            clientID == self ? QStringLiteral("连接超时，已断开")
                                             : QStringLiteral("%1 连接超时").arg(name));
    }, Qt::QueuedConnection);
}

extern "C" __declspec(dllexport) void ts3plugin_onClientMoveMovedEvent(
    uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
    int visibility, anyID moverID, const char* moverName, const char* moverUniqueIdentifier,
    const char* moveMessage) {
    Q_UNUSED(oldChannelID);
    Q_UNUSED(visibility);
    Q_UNUSED(moverID);
    Q_UNUSED(moverUniqueIdentifier);
    Q_UNUSED(moveMessage);
    queueTreeRefresh();
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, clientID, newChannelID, moverName] {
        QString name = clientNameById(serverConnectionHandlerID, clientID);
        if (name.isEmpty()) name = QStringLiteral("用户");
        const QString mover = (moverName && *moverName) ? QString::fromUtf8(moverName) : QStringLiteral("管理员");
        appendSystemMessage(serverConnectionHandlerID,
                            QStringLiteral("%1 被 %2 移动到 %3")
                                .arg(name, mover,
                                     newChannelID ? channelNameById(serverConnectionHandlerID, newChannelID)
                                                  : QStringLiteral("服务器")));
    }, Qt::QueuedConnection);
}

extern "C" __declspec(dllexport) void ts3plugin_onClientSelfVariableUpdateEvent(
    uint64 serverConnectionHandlerID, int flag, const char* oldValue, const char* newValue) {
    Q_UNUSED(oldValue);
    if (!qApp || !g_guard) return;
    const bool stateFlag = flag == CLIENT_AWAY || flag == CLIENT_INPUT_MUTED || flag == CLIENT_OUTPUT_MUTED;
    bool valid = false;
    const int state = newValue ? QString::fromUtf8(newValue).toInt(&valid) : 0;
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, flag, stateFlag, valid, state] {
        if (!g_funcs.getCurrentServerConnectionHandlerID ||
            g_funcs.getCurrentServerConnectionHandlerID() != serverConnectionHandlerID) return;
        anyID self = 0;
        if (stateFlag && valid && g_funcs.getClientID &&
            g_funcs.getClientID(serverConnectionHandlerID, &self) == ERROR_ok && self) {
            applySelfState(serverConnectionHandlerID, self, flag, state != 0, false);
        } else {
            refreshClientStatesNow();
            if (g_userPanel) g_userPanel->refresh();
        }
    }, Qt::QueuedConnection);
}

// Server console lines (kicks, bans, "you were poked", etc.) never pass through
// onTextMessageEvent, and the plugin's own list hides the native log, so they
// have to be mirrored here or they disappear entirely.
extern "C" __declspec(dllexport) void ts3plugin_onServerLogEvent(
    uint64 serverConnectionHandlerID, const char* logMsg) {
    if (!qApp || !g_guard || !logMsg) return;
    QString line = QString::fromUtf8(logMsg).trimmed();
    if (line.isEmpty()) return;
    // Format: "targetmode|channel or server name|message". The level tag is
    // not part of this payload on every client version, so both a 3-field and
    // a 4-field line have to parse correctly.
    QString severityText;
    const QStringList parts = line.split(QLatin1Char('|'));
    if (parts.size() >= 4) {
        severityText = parts.at(2).trimmed();
        line = parts.mid(3).join(QLatin1Char('|')).trimmed();
    } else if (parts.size() == 3) {
        line = parts.at(2).trimmed();
    }
    // Drop the leading INFO/ERROR/WARNING tag from the visible text.
    int severity = 0;
    const QString upper = severityText.toUpper();
    if (upper.contains(QStringLiteral("ERROR")) || upper.contains(QStringLiteral("CRITICAL"))) {
        severity = 2;
    } else if (upper.contains(QStringLiteral("WARNING"))) {
        severity = 1;
    } else if (upper.contains(QStringLiteral("INFO"))) {
        // Connection / join / leave notices duplicate the synthetic lines the
        // plugin already appends; showing both would double every event.
        return;
    }
    if (line.isEmpty()) return;
    const QString schid = QString::number(serverConnectionHandlerID);
    logLine(QStringLiteral("server log [%1|%2]: %3").arg(schid, severityText, line));
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, line, severity] {
        appendSystemMessage(serverConnectionHandlerID, line, severity);
    }, Qt::QueuedConnection);
}

// Server-rejected actions are what the client paints as red console lines
// ("insufficient permissions", "you are not allowed to ..."). They arrive on
// this callback, not on onServerLogEvent.
extern "C" __declspec(dllexport) int ts3plugin_onServerErrorEvent(
    uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error,
    const char* returnCode, const char* extraMessage) {
    Q_UNUSED(returnCode);
    if (error == ERROR_ok || !qApp || !g_guard) return 0;
    const QString text = operationErrorText(error,
        errorMessage ? QString::fromUtf8(errorMessage) : QString(),
        extraMessage ? QString::fromUtf8(extraMessage) : QString());
    logLine(QStringLiteral("server error [%1]: %2").arg(error).arg(text));
    queueOperationError(serverConnectionHandlerID, text);
    return 0;
}

// "Insufficient permission" red lines from the server arrive here, not via
// onServerLogEvent; the failed permission name is resolved through the API.
extern "C" __declspec(dllexport) int ts3plugin_onServerPermissionErrorEvent(
    uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error,
    const char* returnCode, unsigned int failedPermissionID) {
    Q_UNUSED(returnCode);
    if (error == ERROR_ok || !qApp || !g_guard) return 0;
    QString text = (errorMessage && *errorMessage) ? QString::fromUtf8(errorMessage).trimmed()
                                                   : QString();
    QString perm;
    if (g_funcs.getPermissionNameByID && failedPermissionID != 0) {
        char buf[256] = {0};
        if (g_funcs.getPermissionNameByID(serverConnectionHandlerID, failedPermissionID, buf,
                                          sizeof(buf) - 1) == ERROR_ok)
            perm = QString::fromUtf8(buf);
    }
    if (!perm.isEmpty()) text = text.isEmpty() ? perm : text + QStringLiteral("（") + perm + QStringLiteral("）");
    if (text.isEmpty()) text = QStringLiteral("权限不足");
    text = operationErrorText(error, text);
    logLine(QStringLiteral("permission error [%1]: %2").arg(failedPermissionID).arg(text));
    queueOperationError(serverConnectionHandlerID, text);
    return 0;
}
// A poke is delivered only to its target and is not a text message, so the
// overlay would otherwise stay silent when someone pokes you.
extern "C" __declspec(dllexport) void ts3plugin_onClientPokeEvent(
    uint64 serverConnectionHandlerID, anyID fromClientID, const char* pokerName,
    const char* pokerUniqueIdentity, const char* message, int ffIgnored) {
    Q_UNUSED(pokerUniqueIdentity);
    Q_UNUSED(ffIgnored);
    if (!qApp || !g_guard) return;
    const QString rawName =
        (pokerName && *pokerName) ? QString::fromUtf8(pokerName).trimmed() : QString();
    const QString text = (message && *message) ? QString::fromUtf8(message).trimmed() : QString();
    logLine(QStringLiteral("poke raw from=%1 name=%2: %3")
                .arg(fromClientID)
                .arg(rawName.isEmpty() ? QStringLiteral("?") : rawName, text));
    // Pokes belong to the server tab; when that tab is not the one on screen,
    // surface the poke in whichever chat overlay is visible so it cannot be
    // missed. The toast covers the case where no chat surface is on screen.
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, fromClientID, rawName, text] {
        QString name = rawName;
        if (name.isEmpty()) name = clientNameById(serverConnectionHandlerID, fromClientID);
        const QString shown = name.isEmpty() ? QStringLiteral("有人") : name;
        const QString line = text.isEmpty()
                                 ? QStringLiteral("%1 戳了戳你").arg(shown)
                                 : QStringLiteral("%1 戳了戳你：%2").arg(shown, text);
        appendSystemMessage(serverConnectionHandlerID, line, 0, true);
        showPokeToast(serverConnectionHandlerID, name, text);
        if (g_modernChat && g_modernChat->isVisible() && !g_overlayKey.isEmpty() &&
            g_overlayKey != serverKey(serverConnectionHandlerID)) {
            ChatMessage msg;
            msg.text = line;
            msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
            msg.system = true;
            msg.poke = true;
            g_chatHistory[g_overlayKey].append(msg);
            g_modernChat->addMessage(msg);
        }
    }, Qt::QueuedConnection);
}

// Channel chat mirror: store per-channel history and append live messages to
// the overlay when that channel's chat tab is the visible page.
extern "C" __declspec(dllexport) void ts3plugin_onTextMessageEvent(
    uint64 serverConnectionHandlerID, anyID targetMode, anyID toID, anyID fromID,
    const char* fromName, const char* fromUniqueIdentifier, const char* message, int ffIgnored) {
    Q_UNUSED(fromUniqueIdentifier);
    Q_UNUSED(ffIgnored);
    if (!qApp || !g_guard || !message) return;
    // Temporary: proves which callback the server's own notices arrive on.
    logLine(QStringLiteral("text msg raw mode=%1 from=%2 to=%3: %4")
                .arg(targetMode).arg(fromID).arg(toID).arg(QString::fromUtf8(message).left(120)));
    const QString name = (fromName && *fromName) ? QString::fromUtf8(fromName) : QStringLiteral("未知用户");
    const QString text = stripChatMarkup(QString::fromUtf8(message));
    if (text.isEmpty()) return;
    if (targetMode != TextMessageTarget_CHANNEL && targetMode != TextMessageTarget_CLIENT &&
        targetMode != TextMessageTarget_SERVER)
        return;
    QMetaObject::invokeMethod(g_guard, [targetMode, name, text, fromID, toID, serverConnectionHandlerID] {
        anyID self = 0;
        if (g_funcs.getClientID)
            g_funcs.getClientID(serverConnectionHandlerID, &self);
        const bool isSelf = (self && self == fromID);
        QString key;
        uint64 msgChannelId = 0;
        if (targetMode == TextMessageTarget_CHANNEL) {
            // Channel events arrive with toID == 0, so derive the channel from
            // the sender; that is also the channel the open tab is keyed on.
            uint64 chId = 0;
            if (g_funcs.getChannelOfClient &&
                g_funcs.getChannelOfClient(serverConnectionHandlerID, fromID, &chId) != ERROR_ok)
                chId = 0;
            if (!chId) {
                uint64 schid = 0, cur = 0;
                QString chName;
                if (currentChannelInfo(schid, cur, chName) && schid == serverConnectionHandlerID)
                    chId = cur;
            }
            msgChannelId = chId;
            key = QString::number(serverConnectionHandlerID) + QLatin1Char(':') + QString::number(chId);
        } else if (targetMode == TextMessageTarget_SERVER) {
            key = serverKey(serverConnectionHandlerID);
        } else if (!isSelf) {
            key = privateKeyForIncoming(serverConnectionHandlerID, fromID, name);
        } else {
            uint64 chId = 0;
            uint64 chSchid = 0;
            QString chName;
            currentChannelInfo(chSchid, chId, chName);
            QString peer = activePrivateTabText(chName);
            if (peer.isEmpty()) peer = clientNameById(serverConnectionHandlerID, toID);
            if (peer.isEmpty()) return;
            key = QStringLiteral("p:") + peer;
        }
        logLine(QStringLiteral("text msg mode=%1 key=%2").arg(targetMode).arg(key));
        ChatMessage msg;
        msg.sender = name;
        msg.text = text;
        msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        msg.self = isSelf;
        g_chatHistory[key].append(msg);
        if (g_overlayKey == key && g_modernChat) g_modernChat->addMessage(msg);
        if (isSelf) return;

        // The overlay only paints the conversation currently on screen. If the
        // message belongs to that visible conversation, skip the card entirely:
        // the sound and (when the window is in the background) the taskbar
        // flash are enough. Other conversations still get a card.
        const bool visible = (g_overlayKey == key) && g_modernChat && g_modernChat->isVisible();

        QString title;
        QString tabName;
        if (targetMode == TextMessageTarget_CHANNEL) {
            const QString channelName = channelNameById(serverConnectionHandlerID, msgChannelId);
            tabName = channelName;
            title = channelName.isEmpty() ? QStringLiteral("%1 发来频道消息").arg(name)
                                          : QStringLiteral("%1 · #%2").arg(name, channelName);
        } else if (targetMode == TextMessageTarget_CLIENT) {
            tabName = key.startsWith(QStringLiteral("p:")) ? key.mid(2) : name;
            title = QStringLiteral("%1 · 私聊").arg(name);
        } else {
            title = QStringLiteral("%1 · 服务器消息").arg(name);
        }
        notifyIncoming(serverConnectionHandlerID, targetMode, fromID, title, text, tabName, key,
                       !visible);
    }, Qt::QueuedConnection);
}
