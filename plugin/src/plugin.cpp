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
#include <QMouseEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QTime>
#include <QLabel>
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
#include <QTextEdit>

#include <windows.h>

#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "channelpanel.h"

#include <chrono>
#include <functional>
#include <thread>

void cleanupUi();

namespace {

TS3Functions g_funcs{};
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
QPointer<QTreeView> g_paneTree;
QPointer<QWidget> g_infoFrame;
QPointer<QWidget> g_infoPage;
QPointer<QStackedWidget> g_chatStack;
QPointer<QLabel> g_infoTitle;
QPointer<QLabel> g_infoStatus;
QPointer<QWidget> g_infoHome;
QPointer<QSplitter> g_infoSplitter;
QPointer<QTreeView> g_infoTree;
QMetaObject::Connection g_infoConn;
QPointer<ModernChat> g_modernChat;
QPointer<QTabWidget> g_chatTab;
QPointer<QObject> g_chatFilter;
QPointer<QWidget> g_chatLog;
QHash<QString, QVector<ChatMessage>> g_chatHistory;
QString g_overlayKey;
bool g_infoShown = false;
QMetaObject::Connection g_chatTabConn;
QPointer<QTreeView> g_styledTree;
QPointer<NativeTreeRows> g_treeRows;
QPointer<QWidget> g_userPanelHost;
QHash<QTreeView*, QPointer<QAbstractItemDelegate>> g_oldDelegates;
bool g_installed = false;
bool g_firstPaintDone = false;

void openPrivateChat(unsigned short clientId, const QString& name);

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
        if (ev->type() == QEvent::Resize || ev->type() == QEvent::Show) {
            if (onLayout) onLayout();
        }
        return QObject::eventFilter(obj, ev);
    }
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

struct SavedLayout {
    QPointer<QSplitter> outer;
    QPointer<QSplitter> inner;
    Qt::Orientation outerOrientation = Qt::Vertical;
    Qt::Orientation innerOrientation = Qt::Horizontal;
    QList<int> outerSizes;
    QList<int> innerSizes;
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
// Fixed window geometry: the splitter ratios are tuned for this size, so the
// window is pinned rather than remembered.
constexpr int kWindowWidth = 940;
constexpr int kWindowHeight = 901;
constexpr int kSidebarWidth = 420;
constexpr int kTreeHeight = 439;

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

    outer->setOrientation(Qt::Horizontal);
    inner->setOrientation(Qt::Vertical);

    const int total = outer->width() > 0 ? outer->width() : kWindowWidth;
    const int sidebar = qMin(kSidebarWidth, qMax(1, total - 460));
    if (outer->count() >= 3) {
        const int members = 220;
        outer->setSizes({sidebar, qMax(420, total - sidebar - members), members});
    } else {
        outer->setSizes({sidebar, qMax(440, total - sidebar)});
    }

    const int height = inner->height() > 0 ? inner->height() : kWindowHeight;
    const int treeH = qMin(kTreeHeight, qMax(120, height - 160));
    if (g_infoPage && inner->count() >= 2) inner->setSizes({1, 0});
    else inner->setSizes({treeH, height - treeH});

    g_layoutApplied = true;
    logLine(QString("layout applied: content-first (%1x%2), sidebar %3, tree/info %4/%5")
                .arg(total).arg(height).arg(sidebar).arg(treeH).arg(height - treeH));
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

// Re-apply the pinned sidebar width / tree height, e.g. after the window was
// resized to the fixed geometry and Qt redistributed the splitter space.
void pinSplitterSizes() {
    if (!g_layoutApplied) return;
    if (g_saved.outer) {
        const int total = g_saved.outer->width();
        if (total > 0) {
            const int sidebar = qMin(kSidebarWidth, qMax(1, total - 460));
            if (g_saved.outer->count() >= 3) {
                const int members = 220;
                g_saved.outer->setSizes({sidebar, qMax(420, total - sidebar - members), members});
            } else {
                g_saved.outer->setSizes({sidebar, qMax(440, total - sidebar)});
            }
        }
    }
    if (g_saved.inner && g_saved.inner->count() >= 2) {
        const int height = g_saved.inner->height();
        if (height > 0) {
            if (g_infoPage) g_saved.inner->setSizes({1, 0});
            else {
                const int treeH = qMin(kTreeHeight, qMax(120, height - 160));
                g_saved.inner->setSizes({treeH, height - treeH});
            }
        }
    }
}

void restoreLayout() {
    if (!g_layoutApplied) return;
    if (g_saved.outer) {
        g_saved.outer->setOrientation(g_saved.outerOrientation);
        if (!g_saved.outerSizes.isEmpty()) g_saved.outer->setSizes(g_saved.outerSizes);
    }
    if (g_saved.inner) {
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

// Keep the stock ServerTreeView (native model, ordering and event handling),
// paint its rows in the Discord-light style, and pin our user panel below it.
void styleTree(QTreeView* tree) {
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

    if (!g_oldDelegates.contains(tree))
        g_oldDelegates.insert(tree, tree->itemDelegate());

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

void showInfoPage() {
    if (!g_chatStack || !g_infoPage) return;
    g_infoPage->setGeometry(g_chatStack->geometry());
    g_infoPage->show();
    g_infoPage->raise();
    g_infoShown = true;
    updateChatOverlay();
}

void hideInfoPage() {
    if (!g_infoPage) return;
    g_infoPage->hide();
    g_infoShown = false;
    updateChatOverlay();
}

void installInfoTab() {
    if (g_infoPage) return;
    auto* tw = qobject_cast<QTabWidget*>(findWidget("ChatTabWidget"));
    if (!tw) return;
    auto* stack = tw->findChild<QStackedWidget*>(QStringLiteral("qt_tabwidget_stackedwidget"));
    if (!stack) return;
    QWidget* info = findWidget("InfoFrame");
    if (!info) return;

    g_infoFrame = info;
    g_infoHome = info->parentWidget();
    g_infoSplitter = qobject_cast<QSplitter*>(info->parentWidget());

    auto* page = new QWidget(tw);
    // The info content (client details, server banner) has a tall natural size
    // hint. As a child of the chat stack it propagates upward and the main
    // window grows when a user is selected, so the page is explicitly
    // non-constraining: it follows the stack, never sizes it.
    page->setMinimumSize(0, 0);
    page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* bar = new QWidget(page);
    bar->setFixedHeight(48);
    bar->setStyleSheet(QStringLiteral("background:#FFFFFF; border-bottom:1px solid #E3E5E8;"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 0, 12, 0);
    h->setSpacing(6);
    auto* back = new QPushButton(QStringLiteral("‹ 聊天"), bar);
    back->setCursor(Qt::PointingHandCursor);
    back->setFixedHeight(26);
    back->setStyleSheet(QStringLiteral(
        "QPushButton { background:#F2F3F5; border:1px solid #E3E5E8; border-radius:7px;"
        " color:#313338; font-family:'Microsoft YaHei UI'; font-size:10pt; padding:2px 10px; }"
        "QPushButton:hover { background:#E7E9EC; border-color:#D5D8DD; }"
        "QPushButton:pressed { background:#DBDEE1; }"));
    auto* title = new QLabel(QStringLiteral("信息"), bar);
    title->setStyleSheet(QStringLiteral(
        "QLabel { border:none; background:transparent; color:#313338;"
        " font-family:'Microsoft YaHei UI'; font-size:11pt; font-weight:600; }"));
    h->addWidget(back);
    h->addWidget(title, 1);
    v->addWidget(bar);

    auto* status = new QLabel(page);
    status->setStyleSheet(QStringLiteral(
        "QLabel { background:#FFFFFF; color:#5C5E66; padding:2px 16px 8px 16px;"
        " font-family:'Microsoft YaHei UI'; font-size:10pt; }"));
    status->hide();
    v->addWidget(status);
    g_infoStatus = status;

    if (QWidget* banner = findBanner()) {
        g_banner = banner;
        g_bannerHome = banner->parentWidget();
        banner->setMaximumHeight(240);
        banner->hide();
        v->addWidget(banner);
    }
    v->addWidget(info, 1);

    QObject::connect(back, &QPushButton::clicked, page, [] { hideInfoPage(); });
    if (QTabBar* tb = tw->tabBar()) {
        QObject::connect(tb, &QTabBar::tabBarClicked, page, [](int) { hideInfoPage(); });
    }
    QObject::connect(tw, &QTabWidget::currentChanged, page, [](int) {
        if (g_infoShown) hideInfoPage();
    });

    // The info view floats above the chat stack instead of joining it: the
    // client's ChatTabWidget assumes every stack page is a chat tab, and adding
    // one desynchronises the selected tab from the visible page.
    page->hide();
    g_infoPage = page;
    g_chatStack = stack;
    g_infoTitle = title;
    info->show();
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

void appendSystemMessage(uint64 schid, const QString& text, int severity = 0) {
    ChatMessage msg;
    msg.text = text;
    msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    msg.system = true;
    msg.severity = severity;
    const QString key = serverKey(schid);
    g_chatHistory[key].append(msg);
    if (g_overlayKey == key && g_modernChat) g_modernChat->addMessage(msg);
}

void queueSystemMessage(uint64 schid, const QString& text) {
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [schid, text] { appendSystemMessage(schid, text); },
                              Qt::QueuedConnection);
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
        g_chatTabConn = QObject::connect(tw, &QTabWidget::currentChanged, tw, [](int) {
            updateChatOverlay();
        });
    }
    if (!g_modernChat) {
        auto* mc = new ModernChat(g_chatTab);
        mc->hide();
        g_modernChat = mc;
        auto* filter = new OverlayFilter(g_chatTab);
        filter->onLayout = [] { updateChatOverlay(); };
        g_chatTab->installEventFilter(filter);
        g_chatFilter = filter;
    }

    if (g_infoShown && g_infoPage && g_chatStack) {
        if (g_infoPage->geometry() != g_chatStack->geometry())
            g_infoPage->setGeometry(g_chatStack->geometry());
        g_infoPage->raise();
    }

    QWidget* page = g_chatTab->currentWidget();
    // ChatTab is the scrollable log surface itself (custom-painted, not a
    // QTextEdit), so its rectangle is exactly the area to replace.
    QWidget* log = page;
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
        return;
    }

    if (g_chatLog != log) {
        if (g_chatFilter && g_chatLog) g_chatLog->removeEventFilter(g_chatFilter);
        g_chatLog = log;
        if (g_chatFilter) log->installEventFilter(g_chatFilter);
    }
    // Re-geometry every heartbeat made the chat input repaint mid-typing.
    // Only touch the overlay when its target rectangle actually changed.
    const QRect target(log->mapTo(g_chatTab, QPoint(0, 0)), log->size());
    if (g_modernChat->geometry() != target) g_modernChat->setGeometry(target);
    if (!g_modernChat->isVisible()) g_modernChat->show();

    if (g_overlayKey != key) {
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
        g_modernChat->setMessages(messages);
    }
}

void attachBannerIfAny() {
    if (!g_infoPage || g_banner) return;
    QWidget* b = findBanner();
    if (!b) return;
    g_banner = b;
    g_bannerHome = b->parentWidget();
    b->setMaximumHeight(240);
    b->hide();
    if (auto* v = qobject_cast<QVBoxLayout*>(g_infoPage->layout()))
        v->insertWidget(v->count() - 1, b);
    logLine("banner attached to info page");
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

void refreshClientStatesNow() {
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
    g_infoStatus->setText(st);
    g_infoStatus->setVisible(!st.isEmpty());
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
    const QRect treeRect(0, 0, pane->width(), qMax(0, pane->height() - h));
    const QRect panelRect(0, qMax(0, pane->height() - h), pane->width(), h);
    // Unconditional setGeometry on every heartbeat forced a relayout pass and
    // flickered the widgets above it, including the chat input.
    if (g_paneTree->geometry() != treeRect) g_paneTree->setGeometry(treeRect);
    if (g_userPanel->geometry() != panelRect) g_userPanel->setGeometry(panelRect);
    if (!g_userPanel->isVisible()) g_userPanel->show();
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

    if (qApp) {
        QPalette pal = qApp->palette();
        const QColor link(0x0F, 0x76, 0x6E);
        if (pal.color(QPalette::Link) != link) {
            pal.setColor(QPalette::Link, link);
            pal.setColor(QPalette::LinkVisited, link);
            qApp->setPalette(pal);
        }
        // Chat links are drawn from the widget palette, not the app one.
        for (QWidget* w : QApplication::allWidgets()) {
            auto* te = qobject_cast<QTextEdit*>(w);
            if (te && te->objectName() == QLatin1String("ChatTab")) {
                QPalette wp = te->palette();
                if (wp.color(QPalette::Link) != link) {
                    wp.setColor(QPalette::Link, link);
                    wp.setColor(QPalette::LinkVisited, link);
                    te->setPalette(wp);
                }
            }
        }
    }
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

// The window is pinned to a fixed size; the splitter ratios in applyLayout
// are tuned for it. Re-checked on every heartbeat: the client restores its
// saved geometry after startup and can undo a one-shot resize.
void applyDefaultWindowSize() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || !w->isWindow() || !w->isVisible()) continue;
        if (QString::fromLatin1(w->metaObject()->className()) != "MainWindow") continue;
        {
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
            if (w->width() != kWindowWidth || w->height() != kWindowHeight) {
                // setFixedSize, not resize: a one-shot resize is undone by the
                // client's own layout on the next reflow.
                w->setFixedSize(kWindowWidth, kWindowHeight);
                logLine(QString("fixed window size applied: %1x%2").arg(w->width()).arg(w->height()));
                pinSplitterSizes();
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

// The stock chat input row ends in an emoticon toggle, which the theme does
// not replace; hiding it leaves the field as one clean box.
void hideChatExtras() {
    for (QWidget* w : QApplication::allWidgets()) {
        if (!w) continue;
        if (w->objectName() == QLatin1String("EmoticonButton") &&
            QString::fromLatin1(w->metaObject()->className()) == "QToolButton") {
            if (w->isVisible()) w->hide();
        }
    }
}

// The chat pane's layout pins the input to the bottom of its row, so equal QSS
// margins still leave a bigger gap above the field than below it. Centre it
// explicitly instead, between the tab bar and the status bar.
void centerChatInput() {
    if (!qApp) return;
    static QPointer<QWidget> host;
    if (!host) {
        for (QWidget* w : QApplication::allWidgets()) {
            if (!w || w->objectName() != QLatin1String("MainWindowChatWidget")) continue;
            if (!w->findChild<QTextEdit*>(QStringLiteral("ChatLineEdit"), Qt::FindDirectChildrenOnly))
                continue;
            host = w;
            break;
        }
        if (!host) return;
    }
    auto* edit = host->findChild<QTextEdit*>(QStringLiteral("ChatLineEdit"), Qt::FindDirectChildrenOnly);
    if (!edit || !edit->isVisible()) return;
    // The field is the scroll area's child; that container is what the pane's
    // layout positions, and it forces the field's height.
    QWidget* box = edit->parentWidget();
    if (!box) return;
    QWidget* row = box->parentWidget();
    if (!row) return;
    const int h = 34;
    const int x = 8;
    const int w = qMax(120, row->width() - 16);
    // Row includes the tab bar at its top; the usable band runs from the
    // tab bar's baseline down to the status separator.
    const int top = 30;
    const int bottom = row->height() - 22;
    const int y = qMax(0, top + (qMax(0, bottom - top) - h) / 2);
    if (box->height() < h) {
        box->setMinimumHeight(h);
        box->setMaximumHeight(h);
    }
    if (box->geometry() != QRect(x, y, w, h)) box->setGeometry(x, y, w, h);
    if (edit->height() < h) edit->setMinimumHeight(h);
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
    hideChatExtras();
    centerChatInput();
    applyDefaultWindowSize();
    QTreeView* tree = findActiveTree();
    if (tree) {
        styleTree(tree);
        installInfoTab();
        attachBannerIfAny();
        wireInfoSelection(tree);
        if (g_treeRows) g_treeRows->refresh();
        tree->viewport()->update();
    }
    if (qApp) {
        QPalette pal = qApp->palette();
        const QColor link(0x0F, 0x76, 0x6E);
        if (pal.color(QPalette::Link) != link) {
            pal.setColor(QPalette::Link, link);
            pal.setColor(QPalette::LinkVisited, link);
            qApp->setPalette(pal);
        }
        // Chat links are drawn from the widget palette, not the app one.
        for (QWidget* w : QApplication::allWidgets()) {
            auto* te = qobject_cast<QTextEdit*>(w);
            if (te && te->objectName() == QLatin1String("ChatTab")) {
                QPalette wp = te->palette();
                if (wp.color(QPalette::Link) != link) {
                    wp.setColor(QPalette::Link, link);
                    wp.setColor(QPalette::LinkVisited, link);
                    te->setPalette(wp);
                }
            }
        }
    }
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
        if (tree) styleTree(tree);
        if (g_treeRows) g_treeRows->refresh();
        if (g_styledTree) g_styledTree->viewport()->update();
    }, Qt::QueuedConnection);
}

void onUiReady() {
    logLine("onUiReady");
    dumpWidgets();
    if (qApp) {
        QPalette pal = qApp->palette();
        pal.setColor(QPalette::Link, QColor(0x0F, 0x76, 0x6E));
        pal.setColor(QPalette::LinkVisited, QColor(0x0F, 0x76, 0x6E));
        qApp->setPalette(pal);
    }
    if (!g_guard) g_guard = new QObject(qApp);
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
    return "1.0.0";
}
extern "C" __declspec(dllexport) int ts3plugin_apiVersion() {
    return 26;
}
extern "C" __declspec(dllexport) const char* ts3plugin_author() {
    return "3ku-nanodesu";
}
extern "C" __declspec(dllexport) const char* ts3plugin_description() {
    return "Content-first layout, Discord-style channel tree, modern chat bubbles and a user panel for the Flat White theme.";
}
extern "C" __declspec(dllexport) void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    g_funcs = funcs;
    logLine("setFunctionPointers");
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

    if (qApp) restoreLayout();
    if (g_stockToolbar) g_stockToolbar->show();

    if (g_stateTimer) { g_stateTimer->stop(); delete g_stateTimer.data(); g_stateTimer = nullptr; }
    if (g_timer) { g_timer->stop(); delete g_timer.data(); g_timer = nullptr; }
    delete g_guard.data();
    g_guard = nullptr;

    if (g_infoConn) { QObject::disconnect(g_infoConn); g_infoConn = QMetaObject::Connection(); }
    if (g_chatTabConn) { QObject::disconnect(g_chatTabConn); g_chatTabConn = QMetaObject::Connection(); }
    if (g_chatFilter) {
        if (g_chatLog) g_chatLog->removeEventFilter(g_chatFilter);
        delete g_chatFilter.data();
        g_chatFilter = nullptr;
    }
    if (g_modernChat) { delete g_modernChat.data(); g_modernChat = nullptr; }
    g_chatLog = nullptr;
    g_chatTab = nullptr;
    g_infoShown = false;

    if (g_banner && g_bannerHome) {
        g_banner->setParent(g_bannerHome);
        g_banner->hide();
    }
    if (g_infoFrame && g_infoHome) {
        g_infoFrame->setParent(g_infoHome);
        if (g_infoSplitter && g_infoSplitter->indexOf(g_infoFrame) < 0)
            g_infoSplitter->addWidget(g_infoFrame);
        g_infoFrame->show();
    }
    if (g_infoPage) { delete g_infoPage.data(); g_infoPage = nullptr; }

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
    Q_UNUSED(serverConnectionHandlerID);
    Q_UNUSED(isReceivedWhisper);
    if (!qApp || !g_guard) return;
    const bool talking = (status == STATUS_TALKING || status == STATUS_TALKING_WHILE_DISABLED);
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, clientID, talking] {
        anyID self = 0;
        if (g_funcs.getClientID) g_funcs.getClientID(serverConnectionHandlerID, &self);
        if (clientID == self && g_userPanel) g_userPanel->setTalking(talking);
        if (!g_treeRows) return;
        g_treeRows->setTalking(static_cast<unsigned short>(clientID), QString(), talking);
        if (g_styledTree) g_styledTree->viewport()->update();
    }, Qt::QueuedConnection);
}

extern "C" __declspec(dllexport) void ts3plugin_onConnectStatusChangeEvent(
    uint64 serverConnectionHandlerID, int status, unsigned int errorNumber) {
    Q_UNUSED(errorNumber);
    if (!qApp || !g_guard) return;
    QString text;
    switch (status) {
    case STATUS_CONNECTING: text = QStringLiteral("正在连接服务器…"); break;
    case STATUS_CONNECTED: text = QStringLiteral("服务器已接受连接"); break;
    case STATUS_CONNECTION_ESTABLISHING: text = QStringLiteral("正在进入服务器…"); break;
    case STATUS_CONNECTION_ESTABLISHED: text = QStringLiteral("已连接到服务器"); break;
    case STATUS_DISCONNECTED: text = QStringLiteral("与服务器的连接已断开"); break;
    default: break;
    }
    if (!text.isEmpty()) queueSystemMessage(serverConnectionHandlerID, text);
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
    uint64 serverConnectionHandlerID, int flag, const char* value) {
    Q_UNUSED(serverConnectionHandlerID);
    Q_UNUSED(flag);
    Q_UNUSED(value);
    if (!qApp || !g_guard) return;
    QMetaObject::invokeMethod(g_guard, [] {
        refreshClientStatesNow();
        // The user panel's mic/speaker/away badges are ours to redraw; the
        // 1.5s heartbeat was the only thing touching them before.
        if (g_userPanel) g_userPanel->refresh();
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
extern "C" __declspec(dllexport) void ts3plugin_onServerErrorEvent(
    uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error,
    const char* returnCode, const char* extraMessage) {
    Q_UNUSED(returnCode);
    if (!qApp || !g_guard) return;
    QString text;
    if (errorMessage && *errorMessage)
        text = QString::fromUtf8(errorMessage).trimmed();
    else if (extraMessage && *extraMessage)
        text = QString::fromUtf8(extraMessage).trimmed();
    else if (g_funcs.getErrorMessage && error != 0) {
        char* msg = nullptr;
        if (g_funcs.getErrorMessage(error, &msg) == ERROR_ok && msg) {
            text = QString::fromUtf8(msg).trimmed();
            g_funcs.freeMemory(msg);
        }
    }
    if (text.isEmpty()) text = QStringLiteral("服务器错误");
    logLine(QStringLiteral("server error [%1]: %2").arg(error).arg(text));
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, text] {
        appendSystemMessage(serverConnectionHandlerID, text, 2);
    }, Qt::QueuedConnection);
}

// "Insufficient permission" red lines from the server arrive here, not via
// onServerLogEvent; the failed permission name is resolved through the API.
extern "C" __declspec(dllexport) void ts3plugin_onServerPermissionErrorEvent(
    uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error,
    const char* returnCode, unsigned int failedPermissionID) {
    Q_UNUSED(returnCode);
    if (!qApp || !g_guard) return;
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
    logLine(QStringLiteral("permission error [%1]: %2").arg(failedPermissionID).arg(text));
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, text] {
        appendSystemMessage(serverConnectionHandlerID, text, 2);
    }, Qt::QueuedConnection);
}
// A poke is delivered only to its target and is not a text message, so the
// overlay would otherwise stay silent when someone pokes you.
extern "C" __declspec(dllexport) void ts3plugin_onClientPokeEvent(
    uint64 serverConnectionHandlerID, anyID fromClientID, const char* pokerName,
    const char* pokerUniqueIdentity, const char* message, int ffIgnored) {
    Q_UNUSED(fromClientID);
    Q_UNUSED(pokerUniqueIdentity);
    Q_UNUSED(ffIgnored);
    if (!qApp || !g_guard) return;
    logLine(QStringLiteral("poke raw from=%1 name=%2: %3")
                .arg(fromClientID)
                .arg(pokerName ? QString::fromUtf8(pokerName) : QStringLiteral("?"),
                     message ? QString::fromUtf8(message) : QString()));
    QString name = (pokerName && *pokerName) ? QString::fromUtf8(pokerName) : QStringLiteral("有人");
    const QString text = (message && *message) ? QString::fromUtf8(message) : QString();
    const QString line = text.isEmpty() ? QStringLiteral("%1 戳了戳你").arg(name)
                                        : QStringLiteral("%1 戳了戳你：%2").arg(name, text);
    // Pokes belong to the server tab; when that tab is not the one on screen,
    // surface the poke in whichever chat overlay is visible so it cannot be
    // missed.
    QMetaObject::invokeMethod(g_guard, [serverConnectionHandlerID, line] {
        appendSystemMessage(serverConnectionHandlerID, line, 2);
        if (g_modernChat && g_modernChat->isVisible() && !g_overlayKey.isEmpty() &&
            g_overlayKey != serverKey(serverConnectionHandlerID)) {
            ChatMessage msg;
            msg.text = line;
            msg.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
            msg.system = true;
            msg.severity = 2;
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
    }, Qt::QueuedConnection);
}
