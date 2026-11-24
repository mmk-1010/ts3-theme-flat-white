// Custom Fluent Foundation widgets for the TS3 client.
//
// ChannelSidebar : server header, categories, voice channels and their clients.
// UserPanel      : self avatar, name and mute indicators, pinned at the bottom.
//
// All data comes from the official plugin API (see plugin/sdk).

#pragma once

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QPoint>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVariantAnimation>
#include <QVector>
#include <array>
#include <functional>
#include <QStyledItemDelegate>
#include <QListWidget>
#include <QWidget>

#include "foundation_tokens.h"
#include "ts3_functions.h"
#include "teamspeak/public_rare_definitions.h"

// Compatibility helpers for the plugin's hand-painted widgets. The client
// keeps one active stylesheet, so dark mode is shared by all plugin surfaces.
namespace discord {
void setDark(bool dark);
bool isDark();

inline QColor color(foundation::Color role) { return foundation::color(role, isDark()); }
inline QFont font(int pixelSize, QFont::Weight weight = QFont::Normal) {
    QFont f(QString::fromLatin1(foundation::fontFamily));
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setStyleStrategy(QFont::NoSubpixelAntialias);
    f.setHintingPreference(QFont::PreferVerticalHinting);
    return f;
}

inline QColor bg() { return color(foundation::Color::bgSurface); }
inline QColor sidebar() { return color(foundation::Color::bgSidebar); }
inline QColor ink() { return color(foundation::Color::textPrimary); }
inline QColor muted() { return color(foundation::Color::textSecondary); }
inline QColor faint() { return color(foundation::Color::textTertiary); }
inline QColor selected() { return color(foundation::Color::fillSelected); }
inline QColor hover() { return color(foundation::Color::fillHover); }
inline QColor green() { return color(foundation::Color::statusSuccess); }
inline QColor divider() { return color(foundation::Color::strokeDivider); }
inline QColor accent() { return color(foundation::Color::accent); }
inline QColor danger() { return color(foundation::Color::statusDanger); }
inline QColor awayColor() { return color(foundation::Color::statusAway); }
inline QColor card() { return color(foundation::Color::bgOverlay); }
inline QColor cardEdge() { return color(foundation::Color::strokeOverlay); }
inline QColor bubbleOther() { return color(foundation::Color::bgBubbleOther); }
inline QColor severityRed() { return color(foundation::Color::textDanger); }
inline QColor severityAmber() { return color(foundation::Color::textWarning); }
}  // namespace discord

// ---------------------------------------------------------------------------

class ChannelSidebar : public QWidget {
public:
    explicit ChannelSidebar(QWidget* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void setOpenChatHandler(std::function<void(unsigned short, const QString&)> handler) {
        m_openChat = std::move(handler);
    }
    void refresh();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct ClientRow {
        unsigned short id = 0;
        QString name;
        bool talking = false;
        bool away = false;
        bool inputMuted = false;
        bool outputMuted = false;
    };
    struct ChannelNode {
        quint64 id = 0;
        quint64 parent = 0;
        QString name;
        bool hasPassword = false;
        quint64 order = 0;
        QVector<int> children;
        QVector<ClientRow> clients;
    };
    struct Row {
        enum Kind { Category, Channel, Client, Spacer } kind = Channel;
        int depth = 0;
        QString text;
        quint64 channelId = 0;
        unsigned short clientId = 0;
        bool talking = false;
        bool away = false;
        bool inputMuted = false;
        bool outputMuted = false;
        bool current = false;
        bool collapsed = false;
        int y = 0;
    };

    void rebuild();
    QVector<int> orderedSiblings(quint64 parentId) const;
    int rowAt(int y) const;

    TS3Functions m_funcs{};
    QVector<ChannelNode> m_channels;
    QHash<quint64, int> m_index;
    QVector<quint64> m_roots;
    QVector<Row> m_rows;
    QHash<quint64, bool> m_collapsed;

    QString m_serverName;
    int m_channelCount = 0;
    int m_clientCount = 0;
    quint64 m_myChannel = 0;
    int m_hover = -1;
    std::function<void(unsigned short, const QString&)> m_openChat;

    static constexpr int kHeaderHeight = 54;
    static constexpr int kChannelRowHeight = 34;
    static constexpr int kOtherRowHeight = 28;
};

// ---------------------------------------------------------------------------

// Short-lived local values bridge the SDK cache lag after a state change.
// Scoped to a connection and client so switching servers cannot reuse them.
class SelfStateChanges {
public:
    void bind(quint64 server, unsigned short client);
    bool set(int flag, bool value, bool local);
    bool resolve(int flag, bool polled);

private:
    struct Change {
        bool value = false;
        bool local = false;
        qint64 stamp = 0;
    };
    quint64 m_server = 0;
    unsigned short m_client = 0;
    QHash<int, Change> m_changes;
};

class UserPanel : public QWidget {
public:
    explicit UserPanel(QWidget* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void refresh();
    void setTalking(bool talking);
    void setSelfState(quint64 server, unsigned short client, int flag, bool value, bool local);
    void applyAwayPreset(quint64 server);
    void setStateChangeHandler(std::function<void(quint64, unsigned short, int, bool)> handler) {
        m_stateChange = std::move(handler);
    }
    void setErrorHandler(std::function<void(quint64, int, unsigned int)> handler) {
        m_error = std::move(handler);
    }
    void setPluginId(const QByteArray& id) { m_pluginId = id; }

    QSize sizeHint() const override {
        return QSize(280, foundation::controlHeight + 2 * foundation::gapLoose);
    }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent*) override;

private:
    static constexpr int kButtonCount = 3;
    struct ButtonVisual {
        qreal hover = 0;
        qreal press = 0;
    };
    using ButtonVisuals = std::array<ButtonVisual, kButtonCount>;

    // Paint and pointer hit testing share buttonRect.
    QRectF buttonRect(int index) const;
    int buttonAt(const QPoint& pos) const;
    bool buttonActive(int index) const;
    void setPointerState(int hover, int pressed, bool snap = false);
    void updatePointerVisuals(qreal progress);

    int connectionStatus(quint64 server) const;
    unsigned short selfId(quint64 server) const;
    bool writeSelfState(quint64 server, int flag, int value, int previous);
    bool awayPresetVisible() const;
    void publishSelfState(quint64 server, unsigned short self, int flag, bool value);

    TS3Functions m_funcs{};
    QString m_name;
    quint64 m_server = 0;
    unsigned short m_self = 0;
    bool m_connected = false;
    bool m_inputMuted = false;
    bool m_outputMuted = false;
    bool m_away = false;
    // Zero is the initial page before TeamSpeak assigns a connection handler.
    QSet<quint64> m_awayPresets;
    bool m_talking = false;
    SelfStateChanges m_selfChanges;
    std::function<void(quint64, unsigned short, int, bool)> m_stateChange;
    std::function<void(quint64, int, unsigned int)> m_error;
    QByteArray m_pluginId;
    int m_hover = -1;
    int m_pressed = -1;
    ButtonVisuals m_visuals{};
    ButtonVisuals m_visualStart{};
    QVariantAnimation m_pointerMotion;
};

// ---------------------------------------------------------------------------

// Paints the *stock* ServerTreeView (so the client keeps its own model, ordering,
// event handling and zero-latency updates) with custom client dots, speaking
// lights, and state badges.
class NativeTreeRows : public QStyledItemDelegate {
public:
    explicit NativeTreeRows(QObject* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void refresh();
    void refreshStates();
    void setSelfState(quint64 server, unsigned short client, int flag, bool value, bool local);
    void setTalking(unsigned short clientId, const QString& displayName, bool talking);
    void setSelfTalking(quint64 server, unsigned short client, bool talking);
    bool isClientRow(const QModelIndex& index) const { return kindOf(index) == ClientRow; }
    QString clientStatus(const QModelIndex& index) const;
    // Public so the send path can resolve a private tab's peer from its name.
    QString stripTag(const QString& raw) const { return stripClientTag(raw); }
    unsigned short idForName(const QString& name) const { return clientIdFor(name); }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    enum Kind { Server, Spacer, Category, Channel, ClientRow };
    Kind kindOf(const QModelIndex& index) const;
    QString stripClientTag(const QString& raw) const;
    unsigned short clientIdFor(const QString& name) const;
    void pollTalking(const QString& name, bool talking);
    void pollClientState(unsigned short client, int flag, bool value);

    SelfStateChanges m_selfChanges;
    unsigned short m_self = 0;
    TS3Functions m_funcs{};
    QString m_serverName;
    QString m_myChannel;
    QSet<QString> m_channels;
    QSet<QString> m_locked;
    QHash<QString, unsigned short> m_clientIds;
    // Talk state is keyed by row name, not by id: paint resolves rows by
    // display text, and a name lookup cannot miss/false-match the way an id
    // built from a stale name map can.
    QHash<QString, bool> m_talking;
    QHash<QString, qint64> m_talkStamp;
    quint64 m_clientServer = 0;
    quint64 m_selfTalkServer = 0;
    unsigned short m_selfTalkClient = 0;
    bool m_selfTalking = false;
    QHash<unsigned short, bool> m_away;
    QHash<unsigned short, bool> m_inMuted;
    QHash<unsigned short, bool> m_outMuted;
};

// ---------------------------------------------------------------------------

struct ChatMessage {
    QString sender;
    QString text;
    QString time;
    bool self = false;
    bool system = false;
    // 0 = info, 1 = warning, 2 = error.
    int severity = 0;
    bool poke = false;
};

// Modern, plugin-drawn chat log for the channel chat tab. Uses a custom
// delegate: coloured names, compact timestamps and rounded bubbles.
class ModernChat : public QListWidget {
public:
    explicit ModernChat(QWidget* parent = nullptr);
    void addMessage(const ChatMessage& msg);
    // Rebuilds the list. keepOffset < 0 puts the newest message in view;
    // otherwise the view is restored to that pixel offset.
    void setMessages(const QVector<ChatMessage>& msgs, int keepOffset = -1);
    void scrollToLatest();
    // Commit pending wheel travel before hiding or saving the current conversation.
    void finishWheelScroll();
    int scrollOffset() const;
    bool isFollowingBottom() const { return m_followBottom; }
    void setEmptyText(const QString& text) { m_emptyText = text; }
    // Re-reads the palette; called when the client switches between the light
    // and dark stylesheets at runtime.
    void applyTheme();

    // Character range of the active text selection inside one row's message
    // text; used by the bubble delegate to paint the highlight.
    bool selectionRangeForRow(int row, int& start, int& end) const;
    QString rowPlainText(int row) const;
    bool showsKeyboardCursor() const { return m_keyboardNavigation; }

protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void focusInEvent(QFocusEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;

private:
    struct TextPos {
        int row = -1;
        int pos = 0;
    };
    TextPos positionAt(const QPoint& viewportPos) const;
    void selectTo(const TextPos& p);
    void clearTextSelection();
    void selectAllText();
    void copySelection();
    void copyRow(int row);
    void copyAll();
    void insertMessage(const ChatMessage& msg);
    void focusVisibleRow();
    void setReadingRow(int row);
    void restoreScrollPosition();
    void rememberScrollPosition(int value);
    void stopWheelScroll();

    int m_selAnchorRow = -1;
    int m_selAnchorPos = 0;
    int m_selCursorRow = -1;
    int m_selCursorPos = 0;
    bool m_selecting = false;
    bool m_keyboardNavigation = false;
    // True while the reader is at the bottom; the scrollbar's rangeChanged
    // handler uses it to keep the newest row in view after a rebuild.
    bool m_followBottom = false;
    // Reading position to restore after the client collapses and rebuilds the
    // chat page; -1 while following the bottom.
    int m_savedOffset = -1;
    // Keep transient scrollbar signals from replacing the saved position during
    // layout or while writing an intermediate wheel animation frame.
    bool m_adjustingScroll = false;
    QVariantAnimation m_wheelMotion;
    int m_wheelStart = 0;
    int m_wheelTarget = 0;
    QString m_emptyText = QStringLiteral("这里还没有消息，来打个招呼吧");
};

// ---------------------------------------------------------------------------

class ChatHeader : public QWidget {
public:
    explicit ChatHeader(QWidget* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void setChatContext(const QString& title, const QString& subtitle);
    void refresh();

    QSize sizeHint() const override {
        return QSize(400, foundation::controlHeight + 2 * foundation::gapNormal);
    }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    TS3Functions m_funcs{};
    QString m_contextTitle;
    QString m_contextSubtitle;
    QString m_name;
    QString m_topic;
};

// ---------------------------------------------------------------------------

// Card shown above the client content for pokes and incoming messages.
// A repeated title updates the same card. Clicking opens the conversation.
class NotifyToast : public QWidget {
public:
    explicit NotifyToast(QWidget* parent = nullptr);

    void showNotice(const QString& title, const QString& body);
    // Hides without running onActivate; used when the conversation is opened
    // by other means (tab switch).
    void dismiss();

    std::function<void()> onActivate;

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    void reposition();
    QString m_title;
    QString m_body;
    int m_count = 0;
};


