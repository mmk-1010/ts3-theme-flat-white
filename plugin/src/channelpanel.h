// Discord-style light UI for the TS3 client, drawn with our own widgets.
//
// ChannelSidebar : server header, categories, voice channels and their clients.
// UserPanel      : self avatar, name and mute indicators, pinned at the bottom.
//
// All data comes from the official plugin API (see plugin/sdk).

#pragma once

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

namespace discord {
inline QColor bg() { return QColor(0xFF, 0xFF, 0xFF); }
inline QColor sidebar() { return QColor(0xF2, 0xF3, 0xF5); }
inline QColor ink() { return QColor(0x31, 0x33, 0x38); }
inline QColor muted() { return QColor(0x5C, 0x5E, 0x66); }
inline QColor faint() { return QColor(0x94, 0x9B, 0xA3); }
inline QColor selected() { return QColor(0xDB, 0xDE, 0xE1); }
inline QColor hover() { return QColor(0xE3, 0xE5, 0xE8); }
inline QColor blurple() { return QColor(0x58, 0x65, 0xF2); }
inline QColor green() { return QColor(0x23, 0xA5, 0x5A); }
inline QColor divider() { return QColor(0xE3, 0xE5, 0xE8); }
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
    void setMessages(const QVector<ChatMessage>& ms);
    void setEmptyText(const QString& text) { m_emptyText = text; }

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
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


