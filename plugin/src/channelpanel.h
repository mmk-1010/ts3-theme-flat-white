// Discord-style light UI for the TS3 client, drawn with our own widgets.
//
// ChannelSidebar : server header, categories, voice channels and their clients.
// UserPanel      : self avatar, name and mute indicators, pinned at the bottom.
//
// All data comes from the official plugin API (see plugin/sdk).

#pragma once

#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>
#include <QStyledItemDelegate>
#include <QListWidget>
#include <QWidget>

#include "ts3_functions.h"
#include "teamspeak/public_rare_definitions.h"

// Palette for the plugin's own widgets. The dark values mirror the Flat Dark
// stylesheet; the client keeps one theme active per process, so a single
// process-wide flag is enough.
namespace discord {
void setDark(bool dark);
bool isDark();

inline QColor bg() { return isDark() ? QColor(0x1B, 0x1D, 0x22) : QColor(0xFF, 0xFF, 0xFF); }
inline QColor sidebar() { return isDark() ? QColor(0x0F, 0x10, 0x13) : QColor(0xF2, 0xF3, 0xF5); }
inline QColor panel() { return isDark() ? QColor(0x1B, 0x1D, 0x22) : QColor(0xEB, 0xED, 0xEF); }
inline QColor ink() { return isDark() ? QColor(0xE7, 0xE9, 0xEC) : QColor(0x31, 0x33, 0x38); }
inline QColor muted() { return isDark() ? QColor(0xA4, 0xAA, 0xB2) : QColor(0x5C, 0x5E, 0x66); }
inline QColor faint() { return isDark() ? QColor(0x7C, 0x84, 0x8D) : QColor(0x94, 0x9B, 0xA3); }
inline QColor selected() { return isDark() ? QColor(0x2C, 0x30, 0x37) : QColor(0xDB, 0xDE, 0xE1); }
inline QColor hover() { return isDark() ? QColor(0x24, 0x27, 0x2D) : QColor(0xE3, 0xE5, 0xE8); }
inline QColor blurple() { return QColor(0x58, 0x65, 0xF2); }
inline QColor green() { return isDark() ? QColor(0x2E, 0xCC, 0x71) : QColor(0x23, 0xA5, 0x5A); }
inline QColor divider() { return isDark() ? QColor(0x2A, 0x2D, 0x33) : QColor(0xE3, 0xE5, 0xE8); }
inline QColor accent() { return isDark() ? QColor(0x2D, 0xD4, 0xBF) : QColor(0x0F, 0x76, 0x6E); }
inline QColor danger() { return isDark() ? QColor(0xE0, 0x6C, 0x64) : QColor(0xC2, 0x5A, 0x52); }
inline QColor awayColor() { return isDark() ? QColor(0xE3, 0xB3, 0x41) : QColor(0xD9, 0x9A, 0x3D); }
inline QColor input() { return isDark() ? QColor(0x23, 0x26, 0x2C) : QColor(0xF2, 0xF3, 0xF5); }
inline QColor inputFocus() { return isDark() ? QColor(0x2B, 0x2F, 0x36) : QColor(0xEB, 0xED, 0xEF); }
inline QColor bubbleSelf() { return isDark() ? QColor(0x10, 0x32, 0x2D) : QColor(0xE3, 0xF1, 0xEE); }
inline QColor bubbleOther() { return isDark() ? QColor(0x23, 0x26, 0x2C) : QColor(0xF2, 0xF3, 0xF5); }
inline QColor severityRed() { return isDark() ? QColor(0xE8, 0x80, 0x78) : QColor(0xC2, 0x3B, 0x2E); }
inline QColor severityAmber() { return isDark() ? QColor(0xE0, 0xA8, 0x4A) : QColor(0xB0, 0x74, 0x1E); }
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

class UserPanel : public QWidget {
public:
    explicit UserPanel(QWidget* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void refresh();
    void setTalking(bool talking);

    QSize sizeHint() const override { return QSize(280, 64); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:

    TS3Functions m_funcs{};
    QString m_name;
    bool m_inputMuted = false;
    bool m_outputMuted = false;
    bool m_away = false;
    bool m_talking = false;
    qint64 m_talkStamp = 0;
    qint64 m_selfStamp = 0;
};

// ---------------------------------------------------------------------------

// Paints the *stock* ServerTreeView (so the client keeps its own model, ordering,
// event handling and zero-latency updates) in the Discord-light look: circles
// instead of avatars for clients, speaking light, and state badges.
class NativeTreeRows : public QStyledItemDelegate {
public:
    explicit NativeTreeRows(QObject* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void refresh();
    void refreshStates();
    void setTalking(unsigned short clientId, const QString& displayName, bool talking);
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
    // 0 = info (grey), 1 = warning (amber), 2 = error / poke (red).
    int severity = 0;
};

// Modern, plugin-drawn chat log for the channel chat tab. Uses a custom
// delegate: coloured names, compact timestamps and rounded bubbles.
class ModernChat : public QListWidget {
public:
    explicit ModernChat(QWidget* parent = nullptr);
    void addMessage(const ChatMessage& msg);
    void setMessages(const QVector<ChatMessage>& msgs);
    void setEmptyText(const QString& text) { m_emptyText = text; }
    // Re-reads the palette; called when the client switches between the light
    // and dark stylesheets at runtime.
    void applyTheme();

    // Character range of the active text selection inside one row's message
    // text; used by the bubble delegate to paint the highlight.
    bool selectionRangeForRow(int row, int& start, int& end) const;
    QString rowPlainText(int row) const;

protected:
    void paintEvent(QPaintEvent* ev) override;
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

    int m_selAnchorRow = -1;
    int m_selAnchorPos = 0;
    int m_selCursorRow = -1;
    int m_selCursorPos = 0;
    bool m_selecting = false;
    QString m_emptyText = QStringLiteral("这里还没有消息，来打个招呼吧");
};

// ---------------------------------------------------------------------------

class ChatHeader : public QWidget {
public:
    explicit ChatHeader(QWidget* parent = nullptr);

    void setFunctions(const TS3Functions& funcs) { m_funcs = funcs; }
    void setChatContext(const QString& title, const QString& subtitle);
    void refresh();

    QSize sizeHint() const override { return QSize(400, 46); }

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

// Transient card shown inside the client window when someone pokes you. Lives
// as a direct child of MainWindow, so it floats above the chat pane on every
// tab without joining any layout and without taking keyboard focus. It stays
// up until the user clicks it.
class PokeToast : public QWidget {
public:
    explicit PokeToast(QWidget* parent = nullptr);

    void showPoke(const QString& poker, const QString& message);
    void reposition();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QString m_poker;
    QString m_message;
    int m_count = 0;
};


