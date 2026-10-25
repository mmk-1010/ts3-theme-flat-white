#include "channelpanel.h"

#include <QEvent>
#include <QApplication>
#include <QAbstractItemModel>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QResizeEvent>
#include <QSlider>
#include <QScrollBar>
#include <QStyle>
#include <QTextLayout>
#include <QTextOption>
#include <QToolTip>
#include <QtMath>

#include <functional>
#include <memory>
#include <vector>

namespace {

bool g_darkPalette = false;

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

// One diagonal slash shared by the muted mic and speaker, so both states use
// the same angle and cap style.
void drawSlash(QPainter& p, const QRectF& r, const QColor& color) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(r.left() + 2.5, r.bottom() - 2.5),
               QPointF(r.right() - 2.5, r.top() + 2.5));
}

// Outline speaker for the user-panel toggle: same 1.5px weight as the mic and
// the moon so the three controls read as one set. Muted reuses the shared slash.
void drawSpeaker(QPainter& p, const QRectF& r, const QColor& color, bool muted) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const qreal cy = r.center().y();
    QPainterPath body;
    body.moveTo(r.left() + 2, cy - 2.2);
    body.lineTo(r.left() + 4.6, cy - 2.2);
    body.lineTo(r.left() + 7.6, r.top() + 3.2);
    body.lineTo(r.left() + 7.6, r.bottom() - 3.2);
    body.lineTo(r.left() + 4.6, cy + 2.2);
    body.lineTo(r.left() + 2, cy + 2.2);
    body.closeSubpath();
    p.drawPath(body);
    p.drawArc(QRectF(r.left() + 8.2, r.top() + 4.4, 5.6, 7.2), -55 * 16, 110 * 16);
    if (muted) drawSlash(p, r, color);
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
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal cx = r.center().x();
    p.drawRoundedRect(QRectF(cx - 2.2, r.top() + 2, 4.4, 7), 2.2, 2.2);
    p.drawArc(QRectF(cx - 4.8, r.top() + 4, 9.6, 7.2), 0, -180 * 16);
    p.drawLine(QPointF(cx, r.top() + 11.2), QPointF(cx, r.bottom() - 2));
    if (muted) drawSlash(p, r, color);
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

// Outline crescent for the user-panel toggle, same stroke weight as the mic and
// speaker. The visible mass sits left of the circle centre, so it is nudged
// right by about a pixel to look centred next to the other two glyphs.
void drawAwayOutline(QPainter& p, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath outer;
    outer.addEllipse(QRectF(r.left() + 3.1, r.top() + 2, 12, 12));
    QPainterPath inner;
    inner.addEllipse(QRectF(r.left() + 8.1, r.top() + 0.8, 12, 12));
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(outer.subtracted(inner));
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

namespace discord {
void setDark(bool dark) { g_darkPalette = dark; }
bool isDark() { return g_darkPalette; }
}  // namespace discord

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
            drawSpeaker(p, QRectF(indent, r.center().y() - 8, 17, 16), discord::muted(), false);
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
        const QColor off = discord::danger();
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
            drawAwayBadge(p, QRectF(bx, r.center().y() - 7, 18, 15), discord::awayColor());
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

namespace {
// The user strip is a floating card, not a full-width bar: 6px on every
// side, matching the 6px inset of the channel-row highlight cards and the
// bottom gap. 10px of padding inside for the avatar and the button group.
// Painting and hit testing share these numbers.
constexpr int kUserCardInsetX = 6;
constexpr int kUserCardInsetY = 6;
constexpr int kUserCardRadius = 8;
constexpr int kUserPad = 10;
constexpr int kUserBtnSize = 24;
constexpr int kUserBtnGap = 2;
}  // namespace

UserPanel::UserPanel(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }

// 24px buttons, 2px gaps, and 10px of padding inside the rounded card.
// Returns integer-aligned rects so the 16px glyphs land on the pixel grid.
QRectF UserPanel::buttonRect(int index) const {
    const int group = 3 * kUserBtnSize + 2 * kUserBtnGap;
    const int left = width() - kUserCardInsetX - kUserPad - group +
                     index * (kUserBtnSize + kUserBtnGap);
    const int top = (height() - kUserBtnSize) / 2;
    return QRectF(left, top, kUserBtnSize, kUserBtnSize);
}

int UserPanel::buttonAt(const QPoint& pos) const {
    for (int i = 0; i < 3; ++i) {
        if (buttonRect(i).contains(QPointF(pos))) return i;
    }
    return -1;
}

bool UserPanel::buttonActive(int index) const {
    if (index == 0) return m_away;
    if (index == 1) return m_inputMuted;
    return m_outputMuted;
}

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
    // Floating rounded card; the widget itself stays transparent around it so
    // the sidebar bottom shows through instead of a hard full-width strip.
    // The half-pixel offset keeps the hairline border crisp.
    const QRectF card = QRectF(rect()).adjusted(kUserCardInsetX + 0.5, kUserCardInsetY + 0.5,
                                                -kUserCardInsetX - 0.5, -kUserCardInsetY - 0.5);
    const QColor fill = discord::card();
    p.setPen(QPen(discord::cardEdge(), 1));
    p.setBrush(fill);
    p.drawRoundedRect(card, kUserCardRadius, kUserCardRadius);

    const int rowCenter = height() / 2;

    const QRectF av(kUserCardInsetX + kUserPad, rowCenter - 15, 30, 30);
    drawAvatar(p, av, m_name.isEmpty() ? QStringLiteral("?") : m_name, m_talking, discord::green());

    p.setPen(QPen(fill, 2));
    p.setBrush((m_inputMuted || m_away) ? discord::faint() : discord::green());
    p.drawEllipse(QRectF(av.right() - 9, av.bottom() - 9, 9, 9));

    QFont nameFont(QStringLiteral("Microsoft YaHei UI"), 10);
    nameFont.setWeight(QFont::DemiBold);
    p.setFont(nameFont);
    p.setPen(m_away ? discord::muted() : discord::ink());
    const QFontMetrics fm(nameFont);
    const QString displayName = m_name.isEmpty() ? QStringLiteral("未连接") : m_name;
    const int nameLeft = kUserCardInsetX + kUserPad + 30 + 8;
    const int nameRight = static_cast<int>(buttonRect(0).left()) - 8;
    p.drawText(QRect(nameLeft, rowCenter - 16, nameRight - nameLeft, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(displayName, Qt::ElideRight, nameRight - nameLeft));
    p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    p.setPen(discord::faint());
    QString state = QStringLiteral("在线");
    if (m_name.isEmpty()) state = QStringLiteral("离线");
    else if (m_away) state = QStringLiteral("离开");
    else if (m_outputMuted) state = QStringLiteral("耳机静音");
    else if (m_inputMuted) state = QStringLiteral("麦克风静音");
    p.drawText(QRect(nameLeft, rowCenter + 2, nameRight - nameLeft, 13),
               Qt::AlignLeft | Qt::AlignVCenter, state);

    // Toggle buttons: away / microphone / speaker on one 24px grid. Idle
    // glyphs are all the same 1.5px outline; a tinted 24px chip marks the
    // active state and a neutral chip marks hover.
    for (int i = 0; i < 3; ++i) {
        const QRectF btn = buttonRect(i);
        const bool active = buttonActive(i);
        const QColor tint = i == 0 ? discord::awayColor() : discord::danger();
        if (active) {
            QColor bg = tint;
            bg.setAlpha(36);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(btn, 6, 6);
        } else if (m_hover == i) {
            p.setPen(Qt::NoPen);
            p.setBrush(discord::selected());
            p.drawRoundedRect(btn, 6, 6);
        }
        const QRectF icon(btn.left() + 4, btn.top() + 4, 16, 16);
        const QColor color = active ? tint : discord::muted();
        if (i == 0)
            drawAwayOutline(p, icon, color);
        else if (i == 1)
            drawMic(p, icon, color, m_inputMuted);
        else
            drawSpeaker(p, icon, color, m_outputMuted);
    }
}

void UserPanel::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int button = buttonAt(e->pos());
    if (button < 0) return;
    if (!m_funcs.getCurrentServerConnectionHandlerID || !m_funcs.setClientSelfVariableAsInt) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    if (!h) return;
    if (button == 0) {
        m_away = !m_away;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_AWAY, m_away ? AWAY_ZZZ : AWAY_NONE);
    } else if (button == 1) {
        m_inputMuted = !m_inputMuted;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_INPUT_MUTED,
                                          m_inputMuted ? MUTEINPUT_MUTED : MUTEINPUT_NONE);
    } else {
        m_outputMuted = !m_outputMuted;
        m_funcs.setClientSelfVariableAsInt(h, CLIENT_OUTPUT_MUTED,
                                          m_outputMuted ? MUTEOUTPUT_MUTED : MUTEOUTPUT_NONE);
    }
    if (m_funcs.flushClientSelfUpdates) m_funcs.flushClientSelfUpdates(h, nullptr);
    // Our own toggle is authoritative for a moment: the 1.5s poll would
    // otherwise paint the pre-toggle state back.
    m_selfStamp = QDateTime::currentMSecsSinceEpoch();
    update();
}

void UserPanel::mouseMoveEvent(QMouseEvent* e) {
    const int hover = buttonAt(e->pos());
    if (hover == m_hover) return;
    m_hover = hover;
    if (hover >= 0) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    update();
}

void UserPanel::leaveEvent(QEvent*) {
    if (m_hover < 0) return;
    m_hover = -1;
    unsetCursor();
    update();
}

bool UserPanel::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int button = buttonAt(help->pos());
        if (button < 0) {
            QToolTip::hideText();
            return true;
        }
        static const char* const onText[] = {"设为离开", "静音麦克风", "静音耳机"};
        static const char* const offText[] = {"取消离开", "取消麦克风静音", "取消耳机静音"};
        QToolTip::showText(help->globalPos(),
                           QString::fromUtf8(buttonActive(button) ? offText[button]
                                                                  : onText[button]),
                           this);
        return true;
    }
    return QWidget::event(e);
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
        drawSpeaker(*p, QRectF(r.left() + 4, r.center().y() - 8, 17, 16), discord::muted(), false);
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
        QColor ident = discord::accent();
        if (away) ident.setAlpha(150);
        p->setPen(Qt::NoPen);
        p->setBrush(ident);
    }
    p->drawEllipse(dot);

    p->setPen(away ? discord::muted() : discord::ink());
    const int left = r.left() + 27;

    const QColor off = discord::danger();
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
        drawAwayBadge(*p, QRectF(bx, r.center().y() - 7, 18, 15), discord::awayColor());
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

constexpr int kChatPad = 14;        // row side margin
constexpr int kChatBubblePad = 10;  // bubble inner padding, horizontal
constexpr int kChatBubblePadY = 8;  // bubble inner padding, vertical

QFont chatNameFont() {
    QFont f(QStringLiteral("Microsoft YaHei UI"), 9);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont chatTextFont() { return QFont(QStringLiteral("Microsoft YaHei UI"), 10); }

// QTextLayout does not break at '\n', so a message is split into paragraphs
// first and each paragraph is wrapped on its own.
struct ChatPara {
    std::unique_ptr<QTextLayout> layout;
    int start = 0;   // offset of the paragraph in the full message text
    int length = 0;
    qreal top = 0;
    qreal height = 0;
};

struct ChatTextGeom {
    QString text;
    QFont font;
    std::vector<ChatPara> paras;
    qreal maxLineWidth = 0;
    int height = 0;
};

ChatTextGeom buildChatText(const QString& text, const QFont& font, int wrapWidth) {
    ChatTextGeom g;
    g.text = text;
    g.font = font;
    if (text.isEmpty()) return g;

    int start = 0;
    qreal top = 0;
    for (;;) {
        const int nl = text.indexOf(QLatin1Char('\n'), start);
        const int len = (nl < 0 ? text.size() : nl) - start;

        ChatPara para;
        para.start = start;
        para.length = len;
        para.top = top;
        para.layout.reset(new QTextLayout(text.mid(start, len), font));
        QTextOption opt;
        opt.setWrapMode(QTextOption::WordWrap);
        para.layout->setTextOption(opt);
        para.layout->beginLayout();
        qreal y = 0;
        for (;;) {
            QTextLine line = para.layout->createLine();
            if (!line.isValid()) break;
            line.setLineWidth(qMax(1, wrapWidth));
            line.setPosition(QPointF(0, y));
            y += line.height();
            g.maxLineWidth = qMax(g.maxLineWidth, line.naturalTextWidth());
        }
        para.layout->endLayout();
        // An empty paragraph still occupies one line so blank lines keep
        // their height.
        if (para.layout->lineCount() == 0) y = QFontMetricsF(font).height();
        para.height = y;
        top += y;
        g.paras.push_back(std::move(para));
        if (nl < 0) break;
        start = nl + 1;
    }
    g.height = qCeil(top);
    return g;
}

// Character offset (in the message text) at a point given relative to the
// top-left of the laid-out text.
int chatTextPosAt(const ChatTextGeom& g, const QPointF& p) {
    if (g.text.isEmpty() || g.paras.empty()) return 0;
    if (p.y() <= 0) return 0;

    size_t pi = g.paras.size() - 1;
    for (size_t i = 0; i < g.paras.size(); ++i) {
        const ChatPara& para = g.paras[i];
        if (p.y() < para.top + para.height) {
            pi = i;
            break;
        }
    }
    const ChatPara& para = g.paras[pi];
    if (p.y() >= para.top + para.height) return para.start + para.length;

    const qreal y = p.y() - para.top;
    int li = para.layout->lineCount() - 1;
    if (li < 0) return para.start + para.length;
    for (int i = 0; i < para.layout->lineCount(); ++i) {
        const QTextLine line = para.layout->lineAt(i);
        if (y < line.y() + line.height()) {
            li = i;
            break;
        }
    }
    const QTextLine line = para.layout->lineAt(li);
    const int local = qBound(0, line.xToCursor(p.x()), para.length);
    return para.start + local;
}

QVector<QRectF> chatSelectionRects(const ChatTextGeom& g, int selStart, int selEnd,
                                   const QPointF& origin) {
    QVector<QRectF> rects;
    if (selEnd <= selStart) return rects;
    for (const ChatPara& para : g.paras) {
        const int ps = para.start;
        const int pe = para.start + para.length;
        if (pe <= selStart || ps >= selEnd) continue;
        const int a = qMax(selStart, ps);
        const int b = qMin(selEnd, pe);
        if (b <= a) continue;
        for (int i = 0; i < para.layout->lineCount(); ++i) {
            const QTextLine line = para.layout->lineAt(i);
            const int ls = ps + line.textStart();
            const int le = ls + line.textLength();
            const int la = qMax(a, ls);
            const int lb = qMin(b, le);
            if (lb <= la) continue;
            qreal x0 = line.cursorToX(la - ps);
            qreal x1 = line.cursorToX(lb - ps);
            if (x1 < x0) qSwap(x0, x1);
            rects.append(QRectF(origin.x() + x0, origin.y() + para.top + line.y(),
                                x1 - x0, line.height()));
        }
    }
    return rects;
}

// Everything the delegate and the selection code need to agree on: where the
// bubble is, where the text sits and how it wraps.
struct ChatRowGeom {
    bool valid = false;
    bool system = false;
    int height = 30;
    QRect bubbleRect;
    QRect textRect;
    ChatTextGeom textGeom;
};

ChatRowGeom buildChatRow(int width, const QModelIndex& index) {
    ChatRowGeom g;
    if (!index.isValid()) return g;
    g.valid = true;
    g.system = index.data(ChatSystem).toBool();
    if (g.system) {
        g.height = 30;
        return g;
    }

    const bool self = index.data(ChatSelf).toBool();
    const QString text = index.data(ChatText).toString();
    const int maxBubble = qMax(120, int(width * 0.76));
    const int innerMax = qMax(40, maxBubble - 2 * kChatBubblePad);
    g.textGeom = buildChatText(text, chatTextFont(), innerMax);

    const int innerW = qMin(innerMax, qCeil(g.textGeom.maxLineWidth) + 1);
    const int bubbleW = qMin(maxBubble, innerW + 2 * kChatBubblePad);
    const int bubbleH = g.textGeom.height + 2 * kChatBubblePadY;
    const int nameH = QFontMetrics(chatNameFont()).height();
    const int bubbleY = 6 + nameH + 4;
    const int bubbleX = self ? (width - kChatPad - bubbleW) : kChatPad;

    g.bubbleRect = QRect(bubbleX, bubbleY, bubbleW, bubbleH);
    g.textRect = QRect(bubbleX + kChatBubblePad, bubbleY + kChatBubblePadY,
                       bubbleW - 2 * kChatBubblePad, bubbleH - 2 * kChatBubblePadY);
    g.height = bubbleY + bubbleH + 12;
    return g;
}

bool chatWordChar(QChar ch) {
    return ch.isLetterOrNumber() || QStringLiteral("_-./:@#?&=%+").contains(ch);
}

// How close to the end of the history still counts as "reading the newest".
// Small enough that one wheel notch up stops the auto-follow, so the reader
// keeps the position they scrolled to.
constexpr int kFollowSlack = 24;

class ChatBubbleDelegate : public QStyledItemDelegate {
public:
    explicit ChatBubbleDelegate(ModernChat* view) : m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        int width = option.rect.width();
        if (width <= 0 && m_view) width = m_view->viewport()->width();
        const ChatRowGeom row = buildChatRow(width, index);
        return QSize(qMax(width, 0), row.valid ? row.height : 30);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillRect(option.rect, discord::bg());

        const ChatRowGeom row = buildChatRow(option.rect.width(), index);
        if (!row.valid) {
            p->restore();
            return;
        }

        const QString sender = index.data(ChatSender).toString();
        const QString text = index.data(ChatText).toString();
        const QString time = index.data(ChatTime).toString();
        const bool self = index.data(ChatSelf).toBool();

        if (row.system) {
            p->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
            const int severity = index.data(ChatSeverity).toInt();
            // Server log errors keep the client's red; warnings amber; our
            // own informational lines stay grey.
            p->setPen(severity >= 2 ? discord::severityRed()
                                    : (severity == 1 ? discord::severityAmber()
                                                     : discord::faint()));
            p->drawText(option.rect.adjusted(0, 0, 0, -8), Qt::AlignHCenter | Qt::AlignBottom,
                        sender.isEmpty() ? text : sender + QStringLiteral("  ") + text);
            p->restore();
            return;
        }

        const QColor ink = discord::ink();
        const QColor faint = discord::faint();
        const QColor selfBubble = discord::bubbleSelf();
        const QColor otherBubble = discord::bubbleOther();
        const QColor accent = discord::accent();

        const QRect r = option.rect;
        const int left = r.left() + kChatPad;
        const int right = r.right() - kChatPad;
        const QFont nameFont = chatNameFont();
        const QFontMetrics afm(nameFont);
        const int nameH = afm.height();
        const int top = r.top() + 6;

        p->setFont(nameFont);
        p->setPen(self ? accent : ink);
        const QString display = self ? QStringLiteral("我") : sender;
        const int nameW = afm.horizontalAdvance(display);
        const int nameX = self ? (right - nameW) : left;
        p->drawText(QRect(nameX, top, nameW + 2, nameH), Qt::AlignLeft | Qt::AlignVCenter, display);

        if (!time.isEmpty()) {
            const QFont timeFont(QStringLiteral("Microsoft YaHei UI"), 7);
            p->setFont(timeFont);
            p->setPen(faint);
            const QFontMetrics tfm2(timeFont);
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
        p->drawRoundedRect(QRectF(row.bubbleRect).translated(r.topLeft()), 10, 10);

        int selA = 0;
        int selB = 0;
        if (m_view && m_view->selectionRangeForRow(index.row(), selA, selB)) {
            const QVector<QRectF> rects =
                chatSelectionRects(row.textGeom, selA, selB, row.textRect.topLeft() + r.topLeft());
            QColor highlight = discord::accent();
            highlight.setAlpha(discord::isDark() ? 90 : 64);
            p->setBrush(highlight);
            for (const QRectF& rect : rects) p->drawRect(rect);
        }

        p->setPen(ink);
        p->setFont(row.textGeom.font);
        for (const ChatPara& para : row.textGeom.paras) {
            if (!para.layout) continue;
            para.layout->draw(p, QPointF(row.textRect.left() + r.left(),
                                         row.textRect.top() + para.top + r.top()));
        }
        p->restore();
    }

private:
    ModernChat* m_view = nullptr;
};
}  // namespace

ModernChat::ModernChat(QWidget* parent) : QListWidget(parent) {
    setItemDelegate(new ChatBubbleDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::ClickFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setUniformItemSizes(false);
    setSpacing(0);
    viewport()->setCursor(Qt::IBeamCursor);
    applyTheme();
    // The delegate sizes the rows, so the scrollbar range reaches its final
    // value only a layout after the rows go in - long after setMessages() ran.
    // Follow the bottom whenever the range grows while the reader sits there;
    // scrolling away from the bottom drops the flag, so reading back through
    // the history is not fought every time a message arrives.
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int max) {
        // Same guard: a collapsed pane reports 0..0, and re-pinning there only
        // destroys the offset that the next real layout should have kept.
        if (max <= 0 || !m_followBottom) return;
        verticalScrollBar()->setValue(max);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        const QScrollBar* sb = verticalScrollBar();
        if (!sb) return;
        // A zero range means "nothing to scroll", not "the reader is at the
        // bottom". The client shrinks the chat page to 0 height for a frame
        // while it rebuilds the pane, and treating that as bottom re-armed the
        // follow flag, so the next real layout yanked the view back down.
        if (sb->maximum() <= 0) return;
        m_followBottom = value >= sb->maximum() - kFollowSlack;
        // Remember the reading position so a pane rebuild can restore it.
        m_savedOffset = m_followBottom ? -1 : value;
    });
}

void ModernChat::applyTheme() {
    setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:none; }"
        "QListWidget::item { border:none; }"
        "QListWidget::item:selected { background:transparent; }")
        .arg(discord::bg().name()));
    viewport()->update();
}

void ModernChat::scrollToLatest() {
    if (count() == 0) return;
    m_followBottom = true;
    m_savedOffset = -1;
    // Rows are sized by the delegate, so right after a rebuild the scrollbar
    // range is still 0 and scrollToBottom() has nothing to scroll to - the
    // list then sits at the top of the history and addMessage()'s "am I at the
    // bottom" test stays false forever. Lay the rows out first, and let the
    // rangeChanged handler catch the case where even that is too early.
    executeDelayedItemsLayout();
    if (QScrollBar* sb = verticalScrollBar()) sb->setValue(sb->maximum());
}

void ModernChat::resizeEvent(QResizeEvent* ev) {
    QListWidget::resizeEvent(ev);
    if (m_followBottom) {
        // A relayout can drop the offset back to the top; re-pin it.
        scrollToLatest();
        return;
    }
    // The client collapses the page to 0 height for a frame while it rebuilds
    // the pane, which clamps the offset to 0. Put the reader back where they
    // were once the real geometry is in place again.
    if (m_savedOffset < 0 || height() <= 0) return;
    executeDelayedItemsLayout();
    QScrollBar* sb = verticalScrollBar();
    if (!sb || sb->maximum() <= 0) return;
    const int want = qMin(m_savedOffset, sb->maximum());
    if (sb->value() != want) sb->setValue(want);
}

void ModernChat::showEvent(QShowEvent* ev) {
    QListWidget::showEvent(ev);
    if (m_followBottom) scrollToLatest();
}

void ModernChat::addMessage(const ChatMessage& msg) {
    // Drop the empty-state hint as soon as the first real message arrives.
    if (!msg.system && count() == 1 && item(0) && item(0)->data(ChatSystem).toBool()) {
        clear();
        clearTextSelection();
    }
    auto* sb = verticalScrollBar();
    const int keepValue = sb ? sb->value() : 0;
    auto* item = new QListWidgetItem(this);
    item->setData(ChatSender, msg.sender);
    item->setData(ChatText, msg.text);
    item->setData(ChatTime, msg.time);
    item->setData(ChatSelf, msg.self);
    item->setData(ChatSystem, msg.system);
    item->setData(ChatSeverity, msg.severity);
    item->setFlags(Qt::NoItemFlags);
    if (m_filling) return;
    // m_followBottom, not a fresh maximum-minus-value test: a collapsed pane
    // reports a zero range, which would read as "at the bottom" and snatch a
    // reader who is actually scrolled up.
    if (m_followBottom) {
        scrollToLatest();
        return;
    }
    // Scroll-per-pixel offsets are recalculated from the cached row heights;
    // inserting a row can clamp the offset back to the top while the reader is
    // further down. Re-assert the position it had before the insert.
    executeDelayedItemsLayout();
    if (sb && sb->maximum() > 0) sb->setValue(qMin(keepValue, sb->maximum()));
}

void ModernChat::setMessages(const QVector<ChatMessage>& msgs, int keepOffset) {
    clear();
    clearTextSelection();
    m_filling = true;
    for (const ChatMessage& m : msgs) addMessage(m);
    m_filling = false;
    // Switching back to a conversation the reader had scrolled up in must not
    // yank the view: -1 means "was at the bottom", anything else is the pixel
    // offset to restore. The rows have to be laid out before the offset means
    // anything, and the stored offset can be past the end of a shorter list.
    if (keepOffset < 0) {
        scrollToLatest();
        return;
    }
    executeDelayedItemsLayout();
    QScrollBar* sb = verticalScrollBar();
    if (!sb) return;
    sb->setValue(qMin(keepOffset, sb->maximum()));
    m_followBottom = sb->value() >= sb->maximum() - kFollowSlack;
    m_savedOffset = keepOffset;
}

int ModernChat::scrollOffset() const {
    const QScrollBar* sb = verticalScrollBar();
    return sb ? sb->value() : 0;
}

void ModernChat::clearTextSelection() {
    if (m_selAnchorRow < 0 && m_selCursorRow < 0) return;
    m_selAnchorRow = m_selCursorRow = -1;
    m_selAnchorPos = m_selCursorPos = 0;
    viewport()->update();
}

QString ModernChat::rowPlainText(int row) const {
    const QListWidgetItem* it = item(row);
    if (!it) return QString();
    const QString text = it->data(ChatText).toString();
    if (it->data(ChatSystem).toBool()) {
        const QString sender = it->data(ChatSender).toString();
        return sender.isEmpty() ? text : sender + QStringLiteral("  ") + text;
    }
    return text;
}

bool ModernChat::selectionRangeForRow(int row, int& start, int& end) const {
    start = end = 0;
    if (m_selAnchorRow < 0 || m_selCursorRow < 0) return false;
    const QListWidgetItem* it = item(row);
    if (!it || it->data(ChatSystem).toBool()) return false;

    int aRow = m_selAnchorRow;
    int aPos = m_selAnchorPos;
    int cRow = m_selCursorRow;
    int cPos = m_selCursorPos;
    if (aRow > cRow || (aRow == cRow && aPos > cPos)) {
        qSwap(aRow, cRow);
        qSwap(aPos, cPos);
    }
    if (row < aRow || row > cRow) return false;

    const int len = it->data(ChatText).toString().size();
    start = row == aRow ? qBound(0, aPos, len) : 0;
    end = row == cRow ? qBound(0, cPos, len) : len;
    return end > start;
}

ModernChat::TextPos ModernChat::positionAt(const QPoint& viewportPos) const {
    TextPos p;
    if (count() == 0) return p;

    QPoint pt = viewportPos;
    pt.setX(qBound(0, pt.x(), qMax(0, viewport()->width() - 1)));
    pt.setY(qBound(0, pt.y(), qMax(0, viewport()->height() - 1)));

    const QModelIndex idx = indexAt(pt);
    int row = -1;
    if (idx.isValid())
        row = idx.row();
    else if (pt.y() < visualItemRect(item(0)).top())
        row = 0;
    else
        row = count() - 1;
    if (row < 0) return p;

    p.row = row;
    const ChatRowGeom g = buildChatRow(viewport()->width(), model()->index(row, 0));
    if (!g.valid || g.system || g.textGeom.text.isEmpty()) return p;

    const QRect rowRect = visualItemRect(item(row));
    const QPointF local(pt.x() - g.textRect.left(), pt.y() - rowRect.top() - g.textRect.top());
    p.pos = chatTextPosAt(g.textGeom, local);
    return p;
}

void ModernChat::selectTo(const TextPos& p) {
    if (p.row < 0) return;
    if (m_selCursorRow == p.row && m_selCursorPos == p.pos) return;
    m_selCursorRow = p.row;
    m_selCursorPos = p.pos;
    viewport()->update();
}

void ModernChat::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) {
        QListWidget::mousePressEvent(ev);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_selecting = true;
    const TextPos p = positionAt(ev->pos());
    m_selAnchorRow = m_selCursorRow = p.row;
    m_selAnchorPos = m_selCursorPos = p.pos;
    viewport()->update();
    ev->accept();
}

void ModernChat::mouseMoveEvent(QMouseEvent* ev) {
    if (!m_selecting || !(ev->buttons() & Qt::LeftButton)) {
        QListWidget::mouseMoveEvent(ev);
        return;
    }
    const int margin = 24;
    QScrollBar* sb = verticalScrollBar();
    if (sb) {
        if (ev->pos().y() < margin && sb->value() > sb->minimum())
            sb->setValue(sb->value() - 16);
        else if (ev->pos().y() > viewport()->height() - margin && sb->value() < sb->maximum())
            sb->setValue(sb->value() + 16);
    }
    selectTo(positionAt(ev->pos()));
    ev->accept();
}

void ModernChat::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        ev->accept();
        return;
    }
    QListWidget::mouseReleaseEvent(ev);
}

void ModernChat::mouseDoubleClickEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) {
        QListWidget::mouseDoubleClickEvent(ev);
        return;
    }
    const TextPos p = positionAt(ev->pos());
    if (p.row < 0) return;
    const QListWidgetItem* it = item(p.row);
    if (!it || it->data(ChatSystem).toBool()) return;

    const QString text = it->data(ChatText).toString();
    int a = qBound(0, p.pos, text.size());
    int b = a;
    while (a > 0 && chatWordChar(text.at(a - 1))) --a;
    while (b < text.size() && chatWordChar(text.at(b))) ++b;

    m_selecting = false;
    m_selAnchorRow = m_selCursorRow = p.row;
    m_selAnchorPos = a;
    m_selCursorPos = b;
    viewport()->update();
    ev->accept();
}

void ModernChat::keyPressEvent(QKeyEvent* ev) {
    const bool ctrlInsert =
        (ev->key() == Qt::Key_Insert) && (ev->modifiers() & Qt::ControlModifier);
    if (ev->matches(QKeySequence::Copy) || ctrlInsert) {
        copySelection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::SelectAll)) {
        selectAllText();
        ev->accept();
        return;
    }
    if (ev->key() == Qt::Key_Escape) {
        clearTextSelection();
        ev->accept();
        return;
    }
    QListWidget::keyPressEvent(ev);
}

void ModernChat::contextMenuEvent(QContextMenuEvent* ev) {
    const QPoint vp = viewport()->mapFrom(this, ev->pos());
    const QModelIndex idx = indexAt(vp);
    const int row = idx.isValid() ? idx.row() : -1;

    QMenu menu(this);
    QAction* copyAct = menu.addAction(QStringLiteral("复制"));
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(m_selAnchorRow >= 0 && m_selCursorRow >= 0 &&
                        (m_selAnchorRow != m_selCursorRow ||
                         m_selAnchorPos != m_selCursorPos));
    QAction* copyRowAct = menu.addAction(QStringLiteral("复制这条消息"));
    copyRowAct->setEnabled(row >= 0 && !item(row)->data(ChatSystem).toBool());
    QAction* allAct = menu.addAction(QStringLiteral("复制全部"));
    allAct->setEnabled(count() > 0);

    QAction* chosen = menu.exec(ev->globalPos());
    if (chosen == copyAct)
        copySelection();
    else if (chosen == copyRowAct)
        copyRow(row);
    else if (chosen == allAct)
        copyAll();
    ev->accept();
}

void ModernChat::selectAllText() {
    if (count() == 0) return;
    m_selAnchorRow = 0;
    m_selAnchorPos = 0;
    m_selCursorRow = count() - 1;
    m_selCursorPos = rowPlainText(m_selCursorRow).size();
    viewport()->update();
}

void ModernChat::copySelection() {
    if (m_selAnchorRow < 0 || m_selCursorRow < 0) return;
    int aRow = m_selAnchorRow;
    int aPos = m_selAnchorPos;
    int cRow = m_selCursorRow;
    int cPos = m_selCursorPos;
    if (aRow > cRow || (aRow == cRow && aPos > cPos)) {
        qSwap(aRow, cRow);
        qSwap(aPos, cPos);
    }

    QStringList parts;
    for (int row = aRow; row <= cRow; ++row) {
        const QListWidgetItem* it = item(row);
        if (!it || it->data(ChatSystem).toBool()) continue;
        const QString text = it->data(ChatText).toString();
        if (row == aRow && row == cRow) {
            const int from = qBound(0, aPos, text.size());
            const int to = qBound(0, cPos, text.size());
            if (to > from) parts << text.mid(from, to - from);
        } else if (row == aRow) {
            parts << text.mid(qBound(0, aPos, text.size()));
        } else if (row == cRow) {
            parts << text.left(qBound(0, cPos, text.size()));
        } else {
            parts << text;
        }
    }
    const QString out = parts.join(QLatin1Char('\n'));
    if (!out.isEmpty()) QGuiApplication::clipboard()->setText(out);
}

void ModernChat::copyRow(int row) {
    const QString text = rowPlainText(row);
    if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text);
}

void ModernChat::copyAll() {
    QStringList parts;
    for (int row = 0; row < count(); ++row) {
        const QString text = rowPlainText(row);
        if (!text.isEmpty()) parts << text;
    }
    if (!parts.isEmpty()) QGuiApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
}

void ModernChat::paintEvent(QPaintEvent* ev) {
    QListWidget::paintEvent(ev);
    if (count() > 0) return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f(QStringLiteral("Microsoft YaHei UI"), 10);
    p.setFont(f);
    p.setPen(discord::faint());
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

// ---------------------------------------------------------------------------

namespace {
constexpr int kToastWidth = 340;
constexpr int kToastHeight = 72;
constexpr int kToastMargin = 18;
// Clear of the menu bar, which stays visible in this layout.
constexpr int kToastTop = 52;
}  // namespace

NotifyToast::NotifyToast(QWidget* parent) : QWidget(parent) {
    // Rounded corners need the parent's pixels to show through the widget.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("点击关闭"));
    setFixedSize(kToastWidth, kToastHeight);
    hide();
}

void NotifyToast::reposition() {
    QWidget* host = parentWidget();
    if (!host) return;
    move(qMax(kToastMargin, host->width() - width() - kToastMargin), kToastTop);
}

void NotifyToast::showNotice(const QString& title, const QString& body) {
    // A second event while the card is up updates it in place and counts up,
    // so a burst of pokes or messages never stacks overlapping cards.
    const QString cleanTitle = title.trimmed();
    m_count = (isVisible() && cleanTitle == m_title) ? m_count + 1 : 1;
    m_title = cleanTitle;
    m_body = body.trimmed();
    reposition();
    raise();
    if (!isVisible()) show();
    update();
}

void NotifyToast::dismiss() {
    m_count = 0;
    hide();
}

void NotifyToast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(discord::cardEdge(), 1));
    p.setBrush(discord::card());
    p.drawRoundedRect(card, 10, 10);

    // Accent strip down the left edge, clipped to the rounded card.
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(card, 10, 10);
    p.setClipPath(clip);
    p.fillRect(QRectF(card.left(), card.top(), 3, card.height()), discord::accent());
    p.restore();

    const int left = 18;
    const int textWidth = width() - left - 14;

    QString title = m_title.isEmpty() ? QStringLiteral("新消息") : m_title;
    if (m_count > 1) title += QStringLiteral(" ×%1").arg(m_count);
    QFont titleFont(QStringLiteral("Microsoft YaHei UI"), 10);
    titleFont.setWeight(QFont::DemiBold);
    QFont bodyFont(QStringLiteral("Microsoft YaHei UI"), 9);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics bodyMetrics(bodyFont);
    // Centre by the fonts' real heights so a scaled display never clips a line.
    const int gap = 4;
    const int top = qMax(10, (height() - (titleMetrics.height() + gap + bodyMetrics.height())) / 2);
    const QString body = m_body.isEmpty() ? QStringLiteral("没有留言") : m_body;

    p.setFont(titleFont);
    p.setPen(discord::ink());
    p.drawText(QRect(left, top, textWidth, titleMetrics.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               titleMetrics.elidedText(title, Qt::ElideRight, textWidth));

    p.setFont(bodyFont);
    p.setPen(discord::muted());
    p.drawText(QRect(left, top + titleMetrics.height() + gap, textWidth, bodyMetrics.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               bodyMetrics.elidedText(body, Qt::ElideRight, textWidth));
}

void NotifyToast::mousePressEvent(QMouseEvent* ev) {
    Q_UNUSED(ev);
    auto action = onActivate;
    dismiss();
    if (action) action();
}
