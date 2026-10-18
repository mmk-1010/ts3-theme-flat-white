#include "channelpanel.h"

#include <QEvent>
#include <QApplication>
#include <QAbstractItemModel>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QInputDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QResizeEvent>
#include <QSlider>
#include <QScrollBar>
#include <QStyle>

#include <functional>

namespace {

const QColor kAvatarPalette[] = {
    QColor(0xE0, 0x5B, 0x5B), QColor(0xE0, 0x8B, 0x5B), QColor(0xE0, 0xB3, 0x5B),
    QColor(0x7A, 0xB8, 0x5B), QColor(0x5B, 0xB8, 0xA8), QColor(0x5B, 0x8F, 0xE0),
    QColor(0x7C, 0x6B, 0xE0), QColor(0xB8, 0x5B, 0xD6), QColor(0xD6, 0x5B, 0x9B),
};

QColor avatarColor(const QString& name) {
    const int idx = qHash(name) % (sizeof(kAvatarPalette) / sizeof(kAvatarPalette[0]));
    return kAvatarPalette[idx];
}

QString initialOf(const QString& name) {
    for (const QChar& ch : name) {
        if (!ch.isSpace()) return QString(ch).toUpper();
    }
    return QStringLiteral("?");
}

void drawAvatar(QPainter& p, const QRectF& rect, const QString& name, bool ring, const QColor& ringColor) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (ring) {
        p.setPen(QPen(ringColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(rect.adjusted(-1, -1, 1, 1));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(avatarColor(name));
    p.drawEllipse(rect);
    QFont f(QStringLiteral("Microsoft YaHei UI"), int(rect.height() * 0.42));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect, Qt::AlignCenter, initialOf(name));
    p.restore();
}

// Discord-style speaker icon for voice channels.
void drawSpeaker(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    QPainterPath body;
    body.moveTo(r.left() + 1, r.center().y() - 2.5);
    body.lineTo(r.left() + 3.5, r.center().y() - 2.5);
    body.lineTo(r.left() + 7, r.top() + 2);
    body.lineTo(r.left() + 7, r.bottom() - 2);
    body.lineTo(r.left() + 3.5, r.center().y() + 2.5);
    body.lineTo(r.left() + 1, r.center().y() + 2.5);
    body.closeSubpath();
    p.drawPath(body);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawArc(QRectF(r.left() + 5, r.top() + 3, 7, r.height() - 6), -60 * 16, 120 * 16);
    p.restore();
}

void drawLock(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.2));
    p.setBrush(Qt::NoBrush);
    const QRectF body(r.left() + 1, r.center().y(), r.width() - 2, r.height() / 2 - 1);
    p.drawRoundedRect(body, 1.5, 1.5);
    p.drawArc(QRectF(r.left() + 3, r.top() + 1, r.width() - 6, r.height() - 5), 0, 180 * 16);
    p.restore();
}

void drawMic(QPainter& p, const QRectF& r, const QColor& color, bool muted) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal cx = r.center().x();
    p.drawRoundedRect(QRectF(cx - 2.2, r.top() + 2, 4.4, 7), 2.2, 2.2);
    p.drawArc(QRectF(cx - 4.8, r.top() + 4, 9.6, 7.2), 0, -180 * 16);
    p.drawLine(QPointF(cx, r.top() + 11.2), QPointF(cx, r.bottom() - 2));
    if (muted) {
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.left() + 2, r.bottom() - 2),
                   QPointF(r.right() - 2, r.top() + 2));
    }
    p.restore();
}

// Flat muted speaker: just the cone plus one diagonal slash through it.
void drawSpeakerMutedBadge(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath body;
    const qreal cy = r.center().y();
    body.moveTo(r.left() + 2.2, cy - 2.4);
    body.lineTo(r.left() + 4.8, cy - 2.4);
    body.lineTo(r.left() + 8.4, r.top() + 2.6);
    body.lineTo(r.left() + 8.4, r.bottom() - 2.6);
    body.lineTo(r.left() + 4.8, cy + 2.4);
    body.lineTo(r.left() + 2.2, cy + 2.4);
    body.closeSubpath();
    p.drawPath(body);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(r.left() + 1.5, r.bottom() - 2),
               QPointF(r.right() - 1.5, r.top() + 2));
    p.restore();
}

// Small per-member state badges. Kept compact (12-14px) so they read as
// secondary information next to the name.
void drawMicBadge(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal cx = r.center().x();
    QPen pen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(cx - 1.9, r.top() + 2.2, 3.8, 6.2), 1.9, 1.9);
    p.drawArc(QRectF(cx - 4.2, r.top() + 4.4, 8.4, 6.6), 0, -180 * 16);
    p.drawLine(QPointF(cx, r.top() + 11), QPointF(cx, r.bottom() - 1.4));
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(r.left() + 1.5, r.bottom() - 2),
               QPointF(r.right() - 1.5, r.top() + 2));
    p.restore();
}

// Modern idle marker: a small crescent moon.
void drawAwayBadge(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF outer(r.left() + r.width() * 0.14, r.top() + r.height() * 0.1,
                       r.width() * 0.72, r.height() * 0.8);
    QPainterPath o;
    o.addEllipse(outer);
    QPainterPath i;
    i.addEllipse(outer.translated(outer.width() * 0.42, -outer.height() * 0.2));
    p.fillPath(o.subtracted(i), color);
    p.restore();
}

void drawChevron(QPainter& p, const QRectF& r, const QColor& color, bool down) {    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.5));
    QPainterPath path;
    if (down) {
        path.moveTo(r.left(), r.center().y() - 2);
        path.lineTo(r.center().x(), r.center().y() + 2);
        path.lineTo(r.right(), r.center().y() - 2);
    } else {
        path.moveTo(r.left() + 1, r.center().y() - 3);
        path.lineTo(r.center().x() + 2, r.center().y());
        path.lineTo(r.left() + 1, r.center().y() + 3);
    }
    p.drawPath(path);
    p.restore();
}

}  // namespace

// ============================================================ ChannelSidebar

ChannelSidebar::ChannelSidebar(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
}

void ChannelSidebar::refresh() {
    m_channels.clear();
    m_index.clear();
    m_roots.clear();
    m_serverName.clear();
    m_channelCount = 0;
    m_clientCount = 0;
    m_myChannel = 0;

    if (!m_funcs.getCurrentServerConnectionHandlerID) {
        rebuild();
        return;
    }
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) {
        rebuild();
        return;
    }

    if (m_funcs.getServerVariableAsString) {
        char* name = nullptr;
        if (m_funcs.getServerVariableAsString(h, VIRTUALSERVER_NAME, &name) == 0 && name) {
            m_serverName = QString::fromUtf8(name);
            m_funcs.freeMemory(name);
        }
    }

    if (m_funcs.getClientID && m_funcs.getChannelOfClient) {
        unsigned short self = 0;
        if (m_funcs.getClientID(h, &self) == 0) m_funcs.getChannelOfClient(h, self, &m_myChannel);
    }

    quint64* ids = nullptr;
    if (!m_funcs.getChannelList || m_funcs.getChannelList(h, &ids) != 0 || !ids) {
        rebuild();
        return;
    }

    for (quint64* p = ids; *p; ++p) {
        ChannelNode node;
        node.id = *p;

        char* name = nullptr;
        if (m_funcs.getChannelVariableAsString(h, node.id, CHANNEL_NAME, &name) == 0 && name) {
            node.name = QString::fromUtf8(name);
            m_funcs.freeMemory(name);
        }
        if (node.name.isEmpty()) continue;

        uint64 parent = 0;
        if (m_funcs.getParentChannelOfChannel) m_funcs.getParentChannelOfChannel(h, node.id, &parent);
        node.parent = parent;

        int pw = 0;
        if (m_funcs.getChannelVariableAsInt) m_funcs.getChannelVariableAsInt(h, node.id, CHANNEL_FLAG_PASSWORD, &pw);
        node.hasPassword = pw != 0;

        uint64 order = 0;
        if (m_funcs.getChannelVariableAsUInt64) m_funcs.getChannelVariableAsUInt64(h, node.id, CHANNEL_ORDER, &order);
        node.order = order;

        if (m_funcs.getChannelClientList) {
            unsigned short* clients = nullptr;
            if (m_funcs.getChannelClientList(h, node.id, &clients) == 0 && clients) {
                for (unsigned short* c = clients; *c; ++c) {
                    ClientRow row;
                    row.id = *c;
                    if (m_funcs.getClientDisplayName) {
                        char buf[128] = {0};
                        if (m_funcs.getClientDisplayName(h, row.id, buf, sizeof(buf) - 1) == 0) {
                            row.name = QString::fromUtf8(buf);
                        }
                    }
                    if (row.name.isEmpty()) row.name = QString::number(row.id);

                    int type = 0;
                    if (m_funcs.getClientVariableAsInt &&
                        m_funcs.getClientVariableAsInt(h, row.id, CLIENT_TYPE, &type) == 0 &&
                        type == ClientType_SERVERQUERY) {
                        continue;  // hide server query connections
                    }
                    int talking = 0;
                    if (m_funcs.getClientVariableAsInt &&
                        m_funcs.getClientVariableAsInt(h, row.id, CLIENT_FLAG_TALKING, &talking) == 0) {
                        row.talking = talking != 0;
                    }
                    int v = 0;
                    if (m_funcs.getClientVariableAsInt &&
                        m_funcs.getClientVariableAsInt(h, row.id, CLIENT_AWAY, &v) == 0) {
                        row.away = v != AWAY_NONE;
                    }
                    if (m_funcs.getClientVariableAsInt &&
                        m_funcs.getClientVariableAsInt(h, row.id, CLIENT_INPUT_MUTED, &v) == 0) {
                        row.inputMuted = v != MUTEINPUT_NONE;
                    }
                    if (m_funcs.getClientVariableAsInt &&
                        m_funcs.getClientVariableAsInt(h, row.id, CLIENT_OUTPUT_MUTED, &v) == 0) {
                        row.outputMuted = v != MUTEOUTPUT_NONE;
                    }
                    node.clients.push_back(row);
                    ++m_clientCount;
                }
                m_funcs.freeMemory(clients);
            }
        }

        m_index.insert(node.id, m_channels.size());
        m_channels.push_back(node);
        ++m_channelCount;
    }
    m_funcs.freeMemory(ids);

    for (int i = 0; i < m_channels.size(); ++i) {
        const auto it = m_index.constFind(m_channels[i].parent);
        if (m_channels[i].parent != 0 && it != m_index.constEnd()) {
            m_channels[*it].children.push_back(i);
        } else {
            m_roots.push_back(m_channels[i].id);
        }
    }

    rebuild();
    update();
}

QVector<int> ChannelSidebar::orderedSiblings(quint64 parentId) const {
    QVector<int> sibs;
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].parent == parentId) sibs.push_back(i);
    }

    // channel_order holds the id of the channel this one is displayed below;
    // 0 means "top of the level". Rebuild that linked order.
    QSet<quint64> ids;
    for (int i : sibs) ids.insert(m_channels[i].id);

    QHash<quint64, QVector<int>> below;  // anchor id -> channels placed under it
    QVector<int> roots;
    for (int i : sibs) {
        const quint64 anchor = m_channels[i].order;
        if (anchor != 0 && ids.contains(anchor)) below[anchor].push_back(i);
        else roots.push_back(i);
    }

    QVector<int> out;
    QSet<int> seen;
    std::function<void(int)> push = [&](int idx) {
        if (seen.contains(idx)) return;
        seen.insert(idx);
        out.push_back(idx);
        for (int child : below.value(m_channels[idx].id)) push(child);
    };
    for (int r : roots) push(r);
    for (int i : sibs) push(i);  // cycles / orphans
    return out;
}

void ChannelSidebar::rebuild() {
    m_rows.clear();

    std::function<void(quint64, int)> addChannel = [&](quint64 cid, int depth) {
        const auto it = m_index.constFind(cid);
        if (it == m_index.constEnd()) return;
        const ChannelNode& node = m_channels[*it];

        const bool isSpacer = node.name.startsWith("[cspacer");
        const bool isCategory = !isSpacer && !node.children.isEmpty();

        Row row;
        row.depth = depth;
        row.channelId = node.id;
        if (isSpacer) {
            row.kind = Row::Spacer;
            const int close = node.name.indexOf(']');
            row.text = close >= 0 ? node.name.mid(close + 1) : node.name;
        } else if (isCategory) {
            row.kind = Row::Category;
            row.text = node.name;
            row.collapsed = m_collapsed.value(node.id, false);
        } else {
            row.kind = Row::Channel;
            row.text = node.name;
            row.current = (node.id == m_myChannel);
        }
        m_rows.push_back(row);

        if (isCategory && m_collapsed.value(node.id, false)) return;

        for (int c : orderedSiblings(node.id)) addChannel(m_channels[c].id, depth + 1);

        if (!isCategory) {
            for (const ClientRow& c : node.clients) {
                Row r;
                r.kind = Row::Client;
                r.depth = depth + 1;
                r.text = c.name;
                r.talking = c.talking;
                r.away = c.away;
                r.inputMuted = c.inputMuted;
                r.outputMuted = c.outputMuted;
                r.channelId = node.id;
                r.clientId = c.id;
                m_rows.push_back(r);
            }
        }
    };

    for (int rootIndex : orderedSiblings(0)) addChannel(m_channels[rootIndex].id, 0);

    int y = kHeaderHeight;
    for (Row& row : m_rows) {
        row.y = y;
        y += (row.kind == Row::Channel) ? kChannelRowHeight : kOtherRowHeight;
    }
    setMinimumHeight(y + 12);
}

QSize ChannelSidebar::sizeHint() const {
    return QSize(300, kHeaderHeight + m_rows.size() * kOtherRowHeight + 12);
}

int ChannelSidebar::rowAt(int y) const {
    for (int i = 0; i < m_rows.size(); ++i) {
        const int h = m_rows[i].kind == Row::Channel ? kChannelRowHeight : kOtherRowHeight;
        if (y >= m_rows[i].y && y < m_rows[i].y + h) return i;
    }
    return -1;
}

void ChannelSidebar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), discord::sidebar());

    QFont serverFont(QStringLiteral("Microsoft YaHei UI"), 12);
    serverFont.setWeight(QFont::DemiBold);
    QFont rowFont(QStringLiteral("Microsoft YaHei UI"), 10);
    QFont sectionFont(QStringLiteral("Microsoft YaHei UI"), 9);
    sectionFont.setWeight(QFont::DemiBold);

    p.setFont(serverFont);
    p.setPen(discord::ink());
    const QFontMetrics sfm(serverFont);
    p.drawText(QRect(14, 0, width() - 40, kHeaderHeight), Qt::AlignLeft | Qt::AlignVCenter,
               sfm.elidedText(m_serverName.isEmpty() ? QStringLiteral("TeamSpeak") : m_serverName,
                              Qt::ElideRight, width() - 44));
    drawChevron(p, QRectF(width() - 26, kHeaderHeight / 2.0 - 5, 10, 10), discord::muted(), true);
    p.setPen(QPen(discord::divider(), 1));
    p.drawLine(0, kHeaderHeight - 1, width(), kHeaderHeight - 1);

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        const int h = row.kind == Row::Channel ? kChannelRowHeight : kOtherRowHeight;
        const QRect r(0, row.y, width(), h);
        const int indent = 8 + row.depth * 16;

        if (i == m_hover && row.kind != Row::Spacer) {
            p.setPen(Qt::NoPen);
            p.setBrush(discord::hover());
            p.drawRoundedRect(r.adjusted(6, 1, -6, -1), 4, 4);
        }

        if (row.kind == Row::Spacer) {
            p.setFont(sectionFont);
            p.setPen(discord::faint());
            p.drawText(QRect(14, row.y, width() - 28, h), Qt::AlignLeft | Qt::AlignVCenter,
                       row.text.toUpper());
            continue;
        }

        if (row.kind == Row::Category) {
            drawChevron(p, QRectF(10, r.center().y() - 4, 8, 8), discord::faint(), !row.collapsed);
            p.setFont(sectionFont);
            p.setPen(discord::muted());
            p.drawText(QRect(24, row.y, width() - 36, h), Qt::AlignLeft | Qt::AlignVCenter,
                       row.text.toUpper());
            continue;
        }

        if (row.kind == Row::Channel) {
            if (row.current) {
                p.setPen(Qt::NoPen);
                p.setBrush(discord::selected());
                p.drawRoundedRect(r.adjusted(6, 1, -6, -1), 4, 4);
            }
            drawSpeaker(p, QRectF(indent, r.center().y() - 8, 17, 16), discord::muted());
            p.setFont(rowFont);
            p.setPen(discord::ink());
            const QFontMetrics fm(rowFont);
            const int left = indent + 25;
            const int right = width() - 38;
            p.drawText(QRect(left, row.y, right - left, h), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(row.text, Qt::ElideRight, right - left));
            const auto ci = m_index.constFind(row.channelId);
            if (ci != m_index.constEnd() && m_channels[*ci].hasPassword) {
                drawLock(p, QRectF(width() - 26, r.center().y() - 6, 11, 13), discord::faint());
            }
            continue;
        }

        const QRectF av(indent + 6, r.center().y() - 9, 18, 18);
        drawAvatar(p, av, row.text, row.talking, discord::green());
        p.setFont(rowFont);
        p.setPen(row.talking ? discord::green() : (row.away ? discord::faint() : discord::muted()));
        const QFontMetrics fm(rowFont);
        const int left = indent + 30;

        // State cluster, right aligned: headset mute, mic mute, away (Zz).
        const QColor off(0xC2, 0x5A, 0x52);
        int bx = width() - 12;
        if (row.outputMuted) {
            bx -= 15;
            drawSpeakerMutedBadge(p, QRectF(bx, r.center().y() - 7, 14, 14), off);
        }
        if (row.inputMuted) {
            bx -= 15;
            drawMicBadge(p, QRectF(bx, r.center().y() - 7, 14, 14), off);
        }
        if (row.away) {
            bx -= 19;
            drawAwayBadge(p, QRectF(bx, r.center().y() - 7, 18, 15), QColor(0xD9, 0x9A, 0x3D));
        }
        const int textRight = qMax(left + 24, bx - 4);
        p.drawText(QRect(left, row.y, textRight - left, h), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(row.text, Qt::ElideRight, textRight - left));
    }
}

void ChannelSidebar::mouseMoveEvent(QMouseEvent* e) {
    const int row = rowAt(e->pos().y());
    if (row != m_hover) {
        m_hover = row;
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void ChannelSidebar::leaveEvent(QEvent* e) {
    m_hover = -1;
    update();
    QWidget::leaveEvent(e);
}

void ChannelSidebar::mousePressEvent(QMouseEvent* e) {
    // Single click only expands/collapses categories; joining a channel and
    // opening a private chat are both double-click actions.
    const int index = rowAt(e->pos().y());
    if (index < 0) return;
    const Row row = m_rows[index];
    if (row.kind != Row::Category) return;

    m_collapsed[row.channelId] = !m_collapsed.value(row.channelId, false);
    rebuild();
    update();
}

void ChannelSidebar::mouseDoubleClickEvent(QMouseEvent* e) {
    const int index = rowAt(e->pos().y());
    if (index < 0) return;
    const Row row = m_rows[index];

    if (row.kind == Row::Channel) {
        if (row.current) return;
        if (!m_funcs.getCurrentServerConnectionHandlerID || !m_funcs.getClientID || !m_funcs.requestClientMove)
            return;
        const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
        if (!h) return;

        // Password-protected channels need a prompt; the API only takes a
        // plain password string and an empty one fails with "invalid password".
        const auto node = m_index.constFind(row.channelId);
        const bool needsPassword = node != m_index.constEnd() && m_channels[*node].hasPassword;
        QByteArray password;
        if (needsPassword) {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, QStringLiteral("输入频道密码"),
                QStringLiteral("频道「%1」需要密码").arg(row.text),
                QLineEdit::Password, QString(), &ok);
            if (!ok) return;
            // An empty password can never unlock a channel; sending it would
            // reproduce the client's "invalid channel password" error.
            if (text.isEmpty()) return;
            password = text.toUtf8();
        }

        unsigned short self = 0;
        if (m_funcs.getClientID(h, &self) == 0)
            m_funcs.requestClientMove(h, self, row.channelId, password.constData(), nullptr);
        return;
    }
    if (row.kind == Row::Client && m_openChat) {
        m_openChat(row.clientId, row.text);
    }
}

// ================================================================ UserPanel

UserPanel::UserPanel(QWidget* parent) : QWidget(parent) {}

void UserPanel::refresh() {
    if (!m_funcs.getCurrentServerConnectionHandlerID) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) return;

    unsigned short self = 0;
    if (m_funcs.getClientID && m_funcs.getClientID(h, &self) == 0) {
        if (m_funcs.getClientDisplayName) {
            char buf[128] = {0};
            if (m_funcs.getClientDisplayName(h, self, buf, sizeof(buf) - 1) == 0) {
                m_name = QString::fromUtf8(buf);
            }
        }
        if (m_name.isEmpty() && m_funcs.getClientSelfVariableAsString) {
            char* nick = nullptr;
            if (m_funcs.getClientSelfVariableAsString(h, CLIENT_NICKNAME, &nick) == 0 && nick) {
                m_name = QString::fromUtf8(nick);
                m_funcs.freeMemory(nick);
            }
        }
        int v = 0;
        const bool fresh = QDateTime::currentMSecsSinceEpoch() - m_selfStamp >= 1200;
        if (fresh && m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_INPUT_MUTED, &v) == 0)
            m_inputMuted = v != MUTEINPUT_NONE;
        if (fresh && m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_OUTPUT_MUTED, &v) == 0)
            m_outputMuted = v != MUTEOUTPUT_NONE;
        if (fresh && m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_AWAY, &v) == 0)
            m_away = v != AWAY_NONE;
        if (m_funcs.getClientVariableAsInt && m_funcs.getClientVariableAsInt(h, self, CLIENT_FLAG_TALKING, &v) == 0) {
            // The heartbeat poll trails the talk event by up to 1.5s; do not
            // let a stale read undo a fresh one.
            if (QDateTime::currentMSecsSinceEpoch() - m_talkStamp >= 1000)
                m_talking = v != 0;
        }
    }
    update();
}

void UserPanel::setTalking(bool talking) {
    m_talkStamp = QDateTime::currentMSecsSinceEpoch();
    if (m_talking == talking) return;
    m_talking = talking;
    update();
}

void UserPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0xEB, 0xED, 0xEF));
    p.setPen(QPen(discord::divider(), 1));
    p.drawLine(0, 0, width(), 0);

    const int rowCenter = height() / 2;
    const QColor off(0xC2, 0x5A, 0x52);
    const QColor idle = discord::faint();

    const QRectF av(10, rowCenter - 15, 30, 30);
    drawAvatar(p, av, m_name.isEmpty() ? QStringLiteral("?") : m_name, m_talking, discord::green());

    p.setPen(QPen(QColor(0xEB, 0xED, 0xEF), 2));
    p.setBrush((m_inputMuted || m_away) ? QColor(0xB0, 0xB4, 0xBA) : discord::green());
    p.drawEllipse(QRectF(av.right() - 9, av.bottom() - 9, 9, 9));

    QFont nameFont(QStringLiteral("Microsoft YaHei UI"), 10);
    nameFont.setWeight(QFont::DemiBold);
    p.setFont(nameFont);
    p.setPen(m_away ? discord::muted() : discord::ink());
    const QFontMetrics fm(nameFont);
    const QString displayName = m_name.isEmpty() ? QStringLiteral("未连接") : m_name;
    const int nameRight = width() - 84;
    p.drawText(QRect(48, rowCenter - 16, nameRight - 48, 18), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(displayName, Qt::ElideRight, nameRight - 48));
    p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    p.setPen(discord::faint());
    QString state = QStringLiteral("在线");
    if (m_name.isEmpty()) state = QStringLiteral("离线");
    else if (m_away) state = QStringLiteral("离开");
    else if (m_outputMuted) state = QStringLiteral("耳机静音");
    else if (m_inputMuted) state = QStringLiteral("麦克风静音");
    p.drawText(QRect(48, rowCenter + 2, nameRight - 48, 13), Qt::AlignLeft | Qt::AlignVCenter, state);

    // Toggle icons: away / microphone / speaker.
    const int iconY = rowCenter - 9;
    drawAwayBadge(p, QRectF(width() - 74, iconY, 18, 18), m_away ? QColor(0xD9, 0x9A, 0x3D) : idle);
    drawMic(p, QRectF(width() - 52, iconY, 18, 18), m_inputMuted ? off : discord::muted(), m_inputMuted);
    const QRectF hp(width() - 30, iconY, 18, 18);
    if (m_outputMuted) drawSpeakerMutedBadge(p, hp, off);
    else drawSpeaker(p, hp, discord::muted());
}

void UserPanel::mousePressEvent(QMouseEvent* e) {
    if (!m_funcs.getCurrentServerConnectionHandlerID || !m_funcs.setClientSelfVariableAsInt) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) return;
    const int x = e->pos().x();
    const int right = width();
    if (x >= right - 80 && x < right - 56) {
        m_away = !m_away;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_AWAY, m_away ? AWAY_ZZZ : AWAY_NONE);
    } else if (x >= right - 56 && x < right - 32) {
        m_inputMuted = !m_inputMuted;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_INPUT_MUTED,
                                          m_inputMuted ? MUTEINPUT_MUTED : MUTEINPUT_NONE);
    } else if (x >= right - 32 && x < right - 8) {
        m_outputMuted = !m_outputMuted;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_OUTPUT_MUTED,
                                          m_outputMuted ? MUTEOUTPUT_MUTED : MUTEOUTPUT_NONE);
    } else {
        return;
    }
    if (m_funcs.flushClientSelfUpdates) m_funcs.flushClientSelfUpdates(h, nullptr);
    // Our own toggle is authoritative for a moment: the 1.5s poll would
    // otherwise paint the pre-toggle state back.
    m_selfStamp = QDateTime::currentMSecsSinceEpoch();
    update();
}

// ============================================================ NativeTreeRows
//
// The client's own TreeModel drives this delegate, so membership and ordering
// changes show up immediately; only the painting is ours.

NativeTreeRows::NativeTreeRows(QObject* parent) : QStyledItemDelegate(parent) {}

void NativeTreeRows::refresh() {
    m_serverName.clear();
    m_myChannel.clear();
    m_channels.clear();
    m_locked.clear();
    m_clientIds.clear();
    m_away.clear();
    m_inMuted.clear();
    m_outMuted.clear();

    if (!m_funcs.getCurrentServerConnectionHandlerID) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) return;

    if (m_funcs.getServerVariableAsString) {
        char* name = nullptr;
        if (m_funcs.getServerVariableAsString(h, VIRTUALSERVER_NAME, &name) == 0 && name) {
            m_serverName = QString::fromUtf8(name);
            m_funcs.freeMemory(name);
        }
    }

    if (m_funcs.getClientID && m_funcs.getChannelOfClient && m_funcs.getChannelVariableAsString) {
        unsigned short self = 0;
        if (m_funcs.getClientID(h, &self) == 0) {
            uint64 cid = 0;
            if (m_funcs.getChannelOfClient(h, self, &cid) == 0) {
                char* name = nullptr;
                if (m_funcs.getChannelVariableAsString(h, cid, CHANNEL_NAME, &name) == 0 && name) {
                    m_myChannel = QString::fromUtf8(name);
                    m_funcs.freeMemory(name);
                }
            }
        }
    }

    if (m_funcs.getChannelList) {
        quint64* ids = nullptr;
        if (m_funcs.getChannelList(h, &ids) == 0 && ids) {
            for (quint64* p = ids; *p; ++p) {
                char* name = nullptr;
                if (m_funcs.getChannelVariableAsString &&
                    m_funcs.getChannelVariableAsString(h, *p, CHANNEL_NAME, &name) == 0 && name) {
                    const QString n = QString::fromUtf8(name);
                    m_channels.insert(n);
                    m_funcs.freeMemory(name);
                }
                int pw = 0;
                if (m_funcs.getChannelVariableAsInt &&
                    m_funcs.getChannelVariableAsInt(h, *p, CHANNEL_FLAG_PASSWORD, &pw) == 0 && pw != 0) {
                    char* name2 = nullptr;
                    if (m_funcs.getChannelVariableAsString &&
                        m_funcs.getChannelVariableAsString(h, *p, CHANNEL_NAME, &name2) == 0 && name2) {
                        m_locked.insert(QString::fromUtf8(name2));
                        m_funcs.freeMemory(name2);
                    }
                }
            }
            m_funcs.freeMemory(ids);
        }
    }

    if (m_funcs.getClientList) {
        unsigned short* clients = nullptr;
        if (m_funcs.getClientList(h, &clients) == 0 && clients) {
            for (unsigned short* c = clients; *c; ++c) {
                const unsigned short id = *c;
                int type = 0;
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_TYPE, &type) == 0 &&
                    type == ClientType_SERVERQUERY) {
                    continue;
                }
                QString name;
                if (m_funcs.getClientDisplayName) {
                    char buf[128] = {0};
                    if (m_funcs.getClientDisplayName(h, id, buf, sizeof(buf) - 1) == 0)
                        name = QString::fromUtf8(buf);
                }
                if (name.isEmpty()) {
                    char* nick = nullptr;
                    if (m_funcs.getClientVariableAsString &&
                        m_funcs.getClientVariableAsString(h, id, CLIENT_NICKNAME, &nick) == 0 && nick) {
                        name = QString::fromUtf8(nick);
                        m_funcs.freeMemory(nick);
                    }
                }
                if (name.isEmpty()) continue;
                m_clientIds.insert(stripClientTag(name), id);

                int v = 0;
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_FLAG_TALKING, &v) == 0)
                    pollTalking(stripClientTag(name), v != 0);
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_AWAY, &v) == 0)
                    m_away[id] = v != AWAY_NONE;
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_INPUT_MUTED, &v) == 0)
                    m_inMuted[id] = v != MUTEINPUT_NONE;
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_OUTPUT_MUTED, &v) == 0)
                    m_outMuted[id] = v != MUTEOUTPUT_NONE;
            }
            m_funcs.freeMemory(clients);
        }
    }
}

void NativeTreeRows::setTalking(unsigned short clientId, const QString& displayName, bool talking) {
    // The row is painted from its display text, so remember the state under
    // every key we can derive from the event and resolve the row by name.
    QString name = displayName;
    if (name.isEmpty() && m_funcs.getClientDisplayName && m_funcs.getCurrentServerConnectionHandlerID) {
        const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
        char buf[128] = {0};
        if (h && m_funcs.getClientDisplayName(h, clientId, buf, sizeof(buf) - 1) == 0)
            name = QString::fromUtf8(buf);
    }
    if (name.isEmpty()) return;
    m_clientIds.insert(stripClientTag(name), clientId);
    m_talking[stripClientTag(name)] = talking;
    m_talkStamp[stripClientTag(name)] = QDateTime::currentMSecsSinceEpoch();
}

// The 600ms poll reports stale talk state while a burst is in flight; a value
// that would undo an event from the last second is ignored.
void NativeTreeRows::pollTalking(const QString& name, bool talking) {
    const qint64 last = m_talkStamp.value(name, 0);
    if (QDateTime::currentMSecsSinceEpoch() - last < 1000 &&
        m_talking.value(name, false) != talking)
        return;
    m_talking[name] = talking;
}

void NativeTreeRows::refreshStates() {
    if (!m_funcs.getCurrentServerConnectionHandlerID) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h || !m_funcs.getClientList) return;
    unsigned short* clients = nullptr;
    if (m_funcs.getClientList(h, &clients) != 0 || !clients) return;
    for (unsigned short* c = clients; *c; ++c) {
        const unsigned short id = *c;
        int v = 0;
        if (m_funcs.getClientVariableAsInt) {
            // CLIENT_FLAG_TALKING lags behind onTalkStatusChangeEvent; only
            // write it when no name is cached for this client yet.
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_AWAY, &v) == 0)
                m_away[id] = v != AWAY_NONE;
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_INPUT_MUTED, &v) == 0)
                m_inMuted[id] = v != MUTEINPUT_NONE;
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_OUTPUT_MUTED, &v) == 0)
                m_outMuted[id] = v != MUTEOUTPUT_NONE;
        }
    }
    m_funcs.freeMemory(clients);
}

QString NativeTreeRows::stripClientTag(const QString& raw) const {
    QString s = raw;
    for (;;) {
        if (!s.startsWith('[')) break;
        const int close = s.indexOf(']');
        if (close < 0) break;
        int next = close + 1;
        while (next < s.size() && s[next] == ' ') ++next;
        if (next >= s.size()) break;
        s = s.mid(next);
    }
    return s;
}

QString NativeTreeRows::clientStatus(const QModelIndex& index) const {
    if (!index.isValid() || kindOf(index) != ClientRow) return QString();
    const QString name = stripClientTag(index.data(Qt::DisplayRole).toString());
    const unsigned short id = clientIdFor(name);
    if (!id) return QString();
    QStringList parts;
    if (m_away.value(id, false)) parts << QStringLiteral("离开");
    if (m_inMuted.value(id, false)) parts << QStringLiteral("闭麦");
    if (m_outMuted.value(id, false)) parts << QStringLiteral("扬声器静音");
    if (parts.isEmpty()) parts << QStringLiteral("在线");
    return QStringLiteral("状态：") + parts.join(QStringLiteral(" · "));
}

unsigned short NativeTreeRows::clientIdFor(const QString& name) const {
    const auto it = m_clientIds.constFind(name);
    if (it != m_clientIds.constEnd()) return *it;
    for (auto i = m_clientIds.constBegin(); i != m_clientIds.constEnd(); ++i) {
        if (name.endsWith(QStringLiteral("] ") + i.key()) || name.endsWith(QStringLiteral(" ") + i.key()))
            return i.value();
    }
    return 0;
}

NativeTreeRows::Kind NativeTreeRows::kindOf(const QModelIndex& index) const {
    if (!index.isValid()) return Channel;
    if (!index.parent().isValid()) return Server;

    const QString raw = index.data(Qt::DisplayRole).toString();
    if (raw.startsWith(QStringLiteral("[cspacer"), Qt::CaseInsensitive)) return Spacer;

    if (m_channels.contains(raw)) {
        // A channel that contains child channels is drawn as a section header,
        // mirroring the custom list; occupancy by clients does not count.
        const QAbstractItemModel* m = index.model();
        if (m) {
            const int n = m->rowCount(index);
            for (int i = 0; i < n; ++i) {
                const QString t = m->index(i, 0, index).data(Qt::DisplayRole).toString();
                if (m_channels.contains(t) || t.startsWith(QStringLiteral("[cspacer"), Qt::CaseInsensitive))
                    return Category;
            }
        }
        return Channel;
    }
    return ClientRow;
}

QSize NativeTreeRows::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    const Kind k = kindOf(index);
    int h = 32;
    if (k == Server) h = 54;
    else if (k == Channel) h = 36;
    else if (k == Category) h = 34;
    return QSize(200, h);
}

void NativeTreeRows::paint(QPainter* p, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    const Kind kind = kindOf(index);
    const QRect r = option.rect;
    const QString raw = index.data(Qt::DisplayRole).toString();

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool current = (kind == Channel || kind == Category) && !m_myChannel.isEmpty() &&
                         raw == m_myChannel;
    if (selected || current) {
        p->setPen(Qt::NoPen);
        p->setBrush(discord::selected());
        p->drawRoundedRect(r.adjusted(5, 1, -5, -1), 4, 4);
    }

    if (kind == Server) {
        QFont f(QStringLiteral("Microsoft YaHei UI"), 13);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(discord::ink());
        const QFontMetrics fm(f);
        const QString title = m_serverName.isEmpty() ? raw : m_serverName;
        p->drawText(QRect(r.left() + 12, r.top(), r.width() - 24, r.height() - 4),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(title, Qt::ElideRight, r.width() - 24));
        p->setPen(QPen(discord::divider(), 1));
        p->drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        p->restore();
        return;
    }

    if (kind == Spacer) {
        QString t = raw;
        const int close = t.indexOf(']');
        if (close >= 0) t = t.mid(close + 1);
        t = t.trimmed().toUpper();
        QFont f(QStringLiteral("Microsoft YaHei UI"), 10);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(discord::faint());
        const QFontMetrics fm(f);
        p->drawText(QRect(r.left() + 14, r.top(), r.width() - 24, r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(t, Qt::ElideRight, r.width() - 24));
        p->restore();
        return;
    }

    if (kind == Category) {
        QFont f(QStringLiteral("Microsoft YaHei UI"), 10);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(discord::muted());
        const QFontMetrics fm(f);
        p->drawText(QRect(r.left() + 4, r.top(), r.width() - 20, r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    raw.toUpper());
        p->restore();
        return;
    }

    QFont rowFont(QStringLiteral("Microsoft YaHei UI"), 11);
    const QFontMetrics fm(rowFont);
    p->setFont(rowFont);

    if (kind == Channel) {
        drawSpeaker(*p, QRectF(r.left() + 4, r.center().y() - 8, 17, 16), discord::muted());
        p->setPen(discord::ink());
        const int left = r.left() + 26;
        const int right = r.right() - 26;
        p->drawText(QRect(left, r.top(), right - left, r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    raw);
        if (m_locked.contains(raw))
            drawLock(*p, QRectF(r.right() - 20, r.center().y() - 6, 11, 13), discord::faint());
        p->restore();
        return;
    }

    // Client row: circle instead of an avatar; lights up while talking.
    const QString name = stripClientTag(raw);
    const unsigned short id = clientIdFor(name);
    const bool talking = m_talking.value(name, false);
    const bool away = m_away.value(id, false);
    const bool muted = m_inMuted.value(id, false);
    const bool outMuted = m_outMuted.value(id, false);

    const QRectF dot(r.left() + 7, r.center().y() - 6, 12, 12);
    if (talking) {
        p->setPen(Qt::NoPen);
        p->setBrush(discord::green());
        p->drawEllipse(dot.adjusted(-1.5, -1.5, 1.5, 1.5));
        p->setBrush(discord::green().lighter(115));
    } else {
        // Online with an open mic: the theme accent, so it reads as "active"
        // long before the green speaking light fires.
        QColor ident(0x0F, 0x76, 0x6E);
        if (away) ident.setAlpha(150);
        p->setPen(Qt::NoPen);
        p->setBrush(ident);
    }
    p->drawEllipse(dot);

    p->setPen(away ? discord::muted() : discord::ink());
    const int left = r.left() + 27;

    const QColor off(0xC2, 0x5A, 0x52);
    int bx = r.right() - 10;
    if (outMuted) {
        bx -= 15;
        drawSpeakerMutedBadge(*p, QRectF(bx, r.center().y() - 7, 14, 14), off);
    }
    if (muted) {
        bx -= 15;
        drawMicBadge(*p, QRectF(bx, r.center().y() - 7, 14, 14), off);
    }
    if (away) {
        bx -= 19;
        drawAwayBadge(*p, QRectF(bx, r.center().y() - 7, 18, 15), QColor(0xD9, 0x9A, 0x3D));
    }
    const int textRight = qMax(left + 24, bx - 4);

    // Draw leading [group] tags bold, then the nickname in the normal weight.
    int x = left;
    QString rest = raw;
    QFont groupFont = rowFont;
    groupFont.setWeight(QFont::Bold);
    const QFontMetrics gfm(groupFont);
    while (rest.startsWith('[')) {
        const int close = rest.indexOf(']');
        if (close < 0) break;
        const QString tag = rest.left(close + 1);
        rest = rest.mid(close + 1);
        p->setFont(groupFont);
        p->setPen(discord::ink());
        const int w = gfm.horizontalAdvance(tag);
        p->drawText(QRect(x, r.top(), qMax(0, textRight - x), r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, tag);
        x += w;
        if (x >= textRight) break;
        if (rest.startsWith(' ')) {
            x += fm.horizontalAdvance(QLatin1Char(' '));
            rest = rest.mid(1);
        }
    }
    p->setFont(rowFont);
    p->setPen(away ? discord::muted() : discord::ink());
    if (!rest.isEmpty() && x < textRight) {
        p->drawText(QRect(x, r.top(), textRight - x, r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, rest);
    }
    p->restore();
}

// ================================================================ ModernChat

namespace {
enum ChatRole {
    ChatSender = Qt::UserRole + 1,
    ChatText,
    ChatTime,
    ChatSelf,
    ChatSystem,
    ChatSeverity
};

class ChatBubbleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QSize(option.rect.width(), heightFor(option.rect.width(), index));
    }

private:
    static int heightFor(int width, const QModelIndex& index) {
        if (index.data(ChatSystem).toBool()) return 30;
        QFont nameFont(QStringLiteral("Microsoft YaHei UI"), 9);
        nameFont.setWeight(QFont::DemiBold);
        QFont textFont(QStringLiteral("Microsoft YaHei UI"), 10);
        const QFontMetrics afm(nameFont);
        const QFontMetrics tfm(textFont);
        const int maxBubble = qMax(120, int(width * 0.76));
        const QRect bound = tfm.boundingRect(QRect(0, 0, maxBubble - 20, 0), Qt::TextWordWrap,
                                             index.data(ChatText).toString());
        return 6 + afm.height() + 4 + bound.height() + 16 + 12;
    }

public:
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillRect(option.rect, Qt::white);

        const QString sender = index.data(ChatSender).toString();
        const QString text = index.data(ChatText).toString();
        const QString time = index.data(ChatTime).toString();
        const bool self = index.data(ChatSelf).toBool();
        const bool system = index.data(ChatSystem).toBool();

        const int width = option.rect.width();
        const int left = option.rect.left() + 14;
        const int right = option.rect.right() - 14;

        QFont nameFont(QStringLiteral("Microsoft YaHei UI"), 9);
        nameFont.setWeight(QFont::DemiBold);
        QFont timeFont(QStringLiteral("Microsoft YaHei UI"), 7);
        QFont textFont(QStringLiteral("Microsoft YaHei UI"), 10);

        if (system) {
            p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
            const int severity = index.data(ChatSeverity).toInt();
            // Server log errors keep the client's red; warnings amber; our
            // own informational lines stay grey.
            p->setPen(severity >= 2 ? QColor(0xC2, 0x3B, 0x2E)
                                    : (severity == 1 ? QColor(0xB0, 0x74, 0x1E)
                                                     : QColor(0x94, 0x9B, 0xA3)));
            p->drawText(option.rect.adjusted(0, 0, 0, -8), Qt::AlignHCenter | Qt::AlignBottom,
                        sender.isEmpty() ? text : sender + QStringLiteral("  ") + text);
            p->restore();
            return;
        }

        const QColor ink(0x31, 0x33, 0x38);
        const QColor faint(0x94, 0x9B, 0xA3);
        const QColor selfBubble(0xE3, 0xF1, 0xEE);
        const QColor otherBubble(0xF2, 0xF3, 0xF5);
        const QColor accent(0x0F, 0x76, 0x6E);

        QFontMetrics afm(nameFont);
        QFontMetrics tfm(textFont);
        const int maxBubble = qMax(120, int(width * 0.76));
        const QRect bound = tfm.boundingRect(QRect(0, 0, maxBubble - 20, 0), Qt::TextWordWrap, text);
        const int bubbleW = qMin(maxBubble, bound.width() + 20);
        const int bubbleH = bound.height() + 16;
        const int nameH = afm.height();
        const int top = option.rect.top() + 6;
        const int bubbleY = top + nameH + 4;
        const int bubbleX = self ? (right - bubbleW) : left;

        p->setFont(nameFont);
        p->setPen(self ? accent : ink);
        const QString display = self ? QStringLiteral("我") : sender;
        const int nameW = afm.horizontalAdvance(display);
        const int nameX = self ? (right - nameW) : left;
        p->drawText(QRect(nameX, top, nameW + 2, nameH), Qt::AlignLeft | Qt::AlignVCenter, display);

        if (!time.isEmpty()) {
            p->setFont(timeFont);
            p->setPen(faint);
            QFontMetrics tfm2(timeFont);
            const int tw = tfm2.horizontalAdvance(time);
            if (self) {
                p->drawText(QRect(nameX - tw - 8, top, tw, nameH),
                            Qt::AlignRight | Qt::AlignVCenter, time);
            } else {
                p->drawText(QRect(nameX + nameW + 8, top, tw + 4, nameH),
                            Qt::AlignLeft | Qt::AlignVCenter, time);
            }
        }

        p->setPen(Qt::NoPen);
        p->setBrush(self ? selfBubble : otherBubble);
        p->drawRoundedRect(QRectF(bubbleX, bubbleY, bubbleW, bubbleH), 10, 10);
        p->setFont(textFont);
        p->setPen(ink);
        p->drawText(QRect(bubbleX + 10, bubbleY + 8, bubbleW - 20, bubbleH - 16),
                    Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
        p->restore();
    }
};
}  // namespace

ModernChat::ModernChat(QWidget* parent) : QListWidget(parent) {
    setItemDelegate(new ChatBubbleDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral(
        "QListWidget { background:#FFFFFF; border:none; }"
        "QListWidget::item { border:none; }"
        "QListWidget::item:selected { background:transparent; }"));
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setUniformItemSizes(false);
    setSpacing(0);
}

void ModernChat::addMessage(const ChatMessage& msg) {
    // Drop the empty-state hint as soon as the first real message arrives.
    if (!msg.system && count() == 1 && item(0) && item(0)->data(ChatSystem).toBool())
        clear();
    auto* sb = verticalScrollBar();
    const bool nearBottom = !sb || (sb->maximum() - sb->value() < 60);
    auto* item = new QListWidgetItem(this);
    item->setData(ChatSender, msg.sender);
    item->setData(ChatText, msg.text);
    item->setData(ChatTime, msg.time);
    item->setData(ChatSelf, msg.self);
    item->setData(ChatSystem, msg.system);
    item->setData(ChatSeverity, msg.severity);
    item->setFlags(Qt::NoItemFlags);
    if (nearBottom) scrollToBottom();
}

void ModernChat::setMessages(const QVector<ChatMessage>& msgs) {
    clear();
    for (const ChatMessage& m : msgs) addMessage(m);
    scrollToBottom();
}

void ModernChat::paintEvent(QPaintEvent* ev) {
    QListWidget::paintEvent(ev);
    if (count() > 0) return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f(QStringLiteral("Microsoft YaHei UI"), 10);
    p.setFont(f);
    p.setPen(QColor(0x94, 0x9B, 0xA3));
    p.drawText(viewport()->rect(), Qt::AlignCenter, m_emptyText);
}

// ================================================================ ChatHeader

ChatHeader::ChatHeader(QWidget* parent) : QWidget(parent) {}

void ChatHeader::setChatContext(const QString& title, const QString& subtitle) {
    if (m_contextTitle == title && m_contextSubtitle == subtitle) return;
    m_contextTitle = title;
    m_contextSubtitle = subtitle;
    refresh();
}

void ChatHeader::refresh() {
    m_name.clear();
    m_topic.clear();
    if (!m_contextTitle.isEmpty()) {
        m_name = m_contextTitle;
        m_topic = m_contextSubtitle;
        update();
        return;
    }
    if (!m_funcs.getCurrentServerConnectionHandlerID || !m_funcs.getClientID ||
        !m_funcs.getChannelOfClient || !m_funcs.getChannelVariableAsString) {
        update();
        return;
    }
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) { update(); return; }
    unsigned short self = 0;
    if (m_funcs.getClientID(h, &self) != 0) { update(); return; }
    uint64 cid = 0;
    if (m_funcs.getChannelOfClient(h, self, &cid) != 0) { update(); return; }

    char* name = nullptr;
    if (m_funcs.getChannelVariableAsString(h, cid, CHANNEL_NAME, &name) == 0 && name) {
        m_name = QString::fromUtf8(name);
        m_funcs.freeMemory(name);
    }
    char* topic = nullptr;
    if (m_funcs.getChannelVariableAsString(h, cid, CHANNEL_TOPIC, &topic) == 0 && topic) {
        m_topic = QString::fromUtf8(topic);
        m_funcs.freeMemory(topic);
    }
    update();
}

void ChatHeader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), discord::bg());
    p.setPen(QPen(discord::divider(), 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    QFont hash(QStringLiteral("Microsoft YaHei UI"), 14);
    hash.setWeight(QFont::Bold);
    p.setFont(hash);
    p.setPen(discord::faint());
    p.drawText(QRect(16, 0, 20, height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("#"));

    QFont nameFont(QStringLiteral("Microsoft YaHei UI"), 11);
    nameFont.setWeight(QFont::DemiBold);
    p.setFont(nameFont);
    p.setPen(discord::ink());
    const QFontMetrics nfm(nameFont);
    const QString title = m_name.isEmpty() ? QStringLiteral("未连接") : m_name;
    const int nameWidth = qMin(nfm.horizontalAdvance(title), width() / 2);
    p.drawText(QRect(36, 0, nameWidth + 4, height()), Qt::AlignLeft | Qt::AlignVCenter, title);

    if (!m_topic.isEmpty()) {
        const int sepX = 36 + nameWidth + 12;
        p.setPen(QPen(discord::divider(), 1));
        p.drawLine(sepX, height() / 2 - 8, sepX, height() / 2 + 8);

        QFont topicFont(QStringLiteral("Microsoft YaHei UI"), 8);
        p.setFont(topicFont);
        p.setPen(discord::muted());
        const QFontMetrics tfm(topicFont);
        p.drawText(QRect(sepX + 12, 0, width() - sepX - 24, height()), Qt::AlignLeft | Qt::AlignVCenter,
                   tfm.elidedText(m_topic, Qt::ElideRight, width() - sepX - 24));
    }
}
