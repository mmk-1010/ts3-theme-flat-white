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
#include <QScopedValueRollback>
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

#include "foundation_motion.h"
#include "teamspeak/public_errors.h"

namespace {

using foundation::Color;

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
    p.setFont(discord::font(qRound(rect.height() * 0.56), QFont::DemiBold));
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

enum class MutedAudio { Microphone, Speaker };
constexpr int kAudioBadgeSize = 16;
constexpr int kAudioBadgeGap = 1;

QPainterPath mutedAudioShape(MutedAudio audio) {
    QPainterPath glyph;
    if (audio == MutedAudio::Microphone) {
        glyph.addRoundedRect(QRectF(6.1, 3.0, 3.8, 6.0), 1.9, 1.9);
        glyph.moveTo(4.5, 7.5);
        glyph.cubicTo(4.5, 9.5, 5.9, 10.9, 8.0, 10.9);
        glyph.cubicTo(10.1, 10.9, 11.5, 9.5, 11.5, 7.5);
        glyph.moveTo(8.0, 10.9);
        glyph.lineTo(8.0, 13.0);
    } else {
        glyph.moveTo(3.1, 6.2);
        glyph.lineTo(5.6, 6.2);
        glyph.lineTo(8.9, 3.7);
        glyph.lineTo(8.9, 12.3);
        glyph.lineTo(5.6, 9.8);
        glyph.lineTo(3.1, 9.8);
        glyph.closeSubpath();
        glyph.moveTo(11.0, 5.7);
        glyph.quadTo(13.5, 8.0, 11.0, 10.3);
    }

    QPainterPath slash(QPointF(3.0, 3.0));
    slash.lineTo(13.0, 13.0);
    QPainterPathStroker stroke;
    stroke.setCapStyle(Qt::RoundCap);
    stroke.setJoinStyle(Qt::RoundJoin);
    stroke.setWidth(1.4);
    const QPainterPath outline = stroke.createStroke(glyph);
    const QPainterPath muteMark = stroke.createStroke(slash);
    // Cut the gap out of the icon so the row shows through in every state.
    stroke.setWidth(2.9);
    QPainterPath silhouette = outline.subtracted(stroke.createStroke(slash));
    silhouette.addPath(muteMark);
    return silhouette;
}

void drawMutedAudioBadge(QPainter& p, const QPointF& topLeft, MutedAudio audio) {
    static const QPainterPath micShape = mutedAudioShape(MutedAudio::Microphone);
    static const QPainterPath speakerShape = mutedAudioShape(MutedAudio::Speaker);
    const QColor ink = discord::color(Color::badgeMute);

    p.save();
    p.translate(topLeft);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillPath(audio == MutedAudio::Microphone ? micShape : speakerShape, ink);
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

// Pack the member badges from the right edge and return the text boundary.
int drawMemberBadges(QPainter& p, const QRect& row, bool inputMuted, bool outputMuted, bool away) {
    const qreal centerY = row.y() + row.height() / 2.0;
    const qreal top = centerY - kAudioBadgeSize / 2.0;
    int x = row.x() + row.width() - 12;
    if (outputMuted) {
        x -= kAudioBadgeSize;
        drawMutedAudioBadge(p, QPointF(x, top), MutedAudio::Speaker);
        x -= kAudioBadgeGap;
    }
    if (inputMuted) {
        x -= kAudioBadgeSize;
        drawMutedAudioBadge(p, QPointF(x, top), MutedAudio::Microphone);
        x -= kAudioBadgeGap;
    }
    if (away) {
        x -= 18;
        drawAwayBadge(p, QRectF(x, centerY - 7.5, 18, 15), discord::awayColor());
    }
    return x - 4;
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

    const QFont serverFont = discord::font(foundation::bodyLargeSize, QFont::DemiBold);
    const QFont rowFont = discord::font(foundation::bodySize);
    const QFont sectionFont = discord::font(foundation::captionSize, QFont::DemiBold);

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

        const int textRight = drawMemberBadges(p, r, row.inputMuted, row.outputMuted, row.away);
        if (textRight > left) {
            p.drawText(QRect(left, row.y, textRight - left, h), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(row.text, Qt::ElideRight, textRight - left));
        }
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
// Geometry shared by the floating user card and its toggles.
constexpr int kUserCardInset = 6;
constexpr int kUserPad = 10;
// Keep the three inline glyph buttons compact in the narrow sidebar.
constexpr int kUserBtnSize = foundation::gapSection;
constexpr int kUserBtnGap = 2;
constexpr int kUserAvatarSize = 30;
constexpr int kUserGlyphSize = 16;

QColor mixUserColor(const QColor& from, const QColor& to, qreal amount) {
    const qreal rest = 1 - amount;
    return QColor::fromRgbF(from.redF() * rest + to.redF() * amount,
                            from.greenF() * rest + to.greenF() * amount,
                            from.blueF() * rest + to.blueF() * amount);
}
}  // namespace

UserPanel::UserPanel(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    m_pointerMotion.setDuration(foundation::motionDuration);
    m_pointerMotion.setEasingCurve(foundation::motionEasing());
    m_pointerMotion.setStartValue(0.0);
    m_pointerMotion.setEndValue(1.0);
    connect(&m_pointerMotion, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { updatePointerVisuals(value.toReal()); });
}

// Painting and hit testing use the same integer-aligned button rectangles.
QRectF UserPanel::buttonRect(int index) const {
    const int group = kButtonCount * kUserBtnSize + (kButtonCount - 1) * kUserBtnGap;
    const int left = width() - kUserCardInset - kUserPad - group +
                     index * (kUserBtnSize + kUserBtnGap);
    const int top = (height() - kUserBtnSize) / 2;
    return QRectF(left, top, kUserBtnSize, kUserBtnSize);
}

int UserPanel::buttonAt(const QPoint& pos) const {
    for (int i = 0; i < kButtonCount; ++i) {
        if (buttonRect(i).contains(QPointF(pos))) return i;
    }
    return -1;
}

void UserPanel::setPointerState(int hover, int pressed, bool snap) {
    const bool hoverChanged = hover != m_hover;
    const int oldPressTarget = m_pressed == m_hover ? m_pressed : -1;
    const int pressTarget = pressed == hover ? pressed : -1;
    m_pressed = pressed;
    if (hoverChanged) {
        m_hover = hover;
        if (hover >= 0) setCursor(Qt::PointingHandCursor);
        else unsetCursor();
    }
    if (!hoverChanged && pressTarget == oldPressTarget && !snap) return;

    // Retarget all buttons from their current values, including ones fading out.
    m_pointerMotion.stop();
    m_visualStart = m_visuals;
    if (snap || !foundation::animationsEnabled()) {
        updatePointerVisuals(1);
        return;
    }
    m_pointerMotion.start();
}

void UserPanel::updatePointerVisuals(qreal progress) {
    QRect dirty;
    const qreal remaining = 1 - progress;
    for (int i = 0; i < kButtonCount; ++i) {
        const qreal hover = m_visualStart[i].hover * remaining + (m_hover == i ? progress : 0);
        const qreal press = m_visualStart[i].press * remaining +
                            (m_pressed == i && m_hover == i ? progress : 0);
        auto& visual = m_visuals[i];
        if (visual.hover == hover && visual.press == press) continue;
        visual = {hover, press};
        dirty = dirty.united(buttonRect(i).toAlignedRect());
    }
    if (!dirty.isEmpty()) update(dirty);
}

bool UserPanel::awayPresetVisible() const {
    return m_awayPresets.contains(m_server) || (!m_connected && m_awayPresets.contains(0));
}

bool UserPanel::buttonActive(int index) const {
    if (index == 0) return m_away || awayPresetVisible();
    if (index == 1) return m_inputMuted;
    return m_outputMuted;
}

void SelfStateChanges::bind(quint64 server, unsigned short client) {
    if (m_server == server && m_client == client) return;
    m_server = server;
    m_client = client;
    m_changes.clear();
}

bool SelfStateChanges::set(int flag, bool value, bool local) {
    const auto it = m_changes.constFind(flag);
    if (!local && it != m_changes.constEnd() && it->local &&
        QDateTime::currentMSecsSinceEpoch() - it->stamp < 1200)
        return it->value == value;
    m_changes[flag] = {value, local, QDateTime::currentMSecsSinceEpoch()};
    return true;
}

bool SelfStateChanges::resolve(int flag, bool polled) {
    const auto it = m_changes.find(flag);
    if (it == m_changes.end()) return polled;
    if (QDateTime::currentMSecsSinceEpoch() - it->stamp < 1200) return it->value;
    m_changes.erase(it);
    return polled;
}

void UserPanel::setSelfState(quint64 server, unsigned short client, int flag, bool value, bool local) {
    m_selfChanges.bind(server, client);
    if (!m_selfChanges.set(flag, value, local)) return;
    if (flag == CLIENT_AWAY) m_away = value;
    else if (flag == CLIENT_INPUT_MUTED) m_inputMuted = value;
    else if (flag == CLIENT_OUTPUT_MUTED) m_outputMuted = value;
    update();
}

int UserPanel::connectionStatus(quint64 server) const {
    int status = STATUS_DISCONNECTED;
    if (server && m_funcs.getConnectionStatus &&
        m_funcs.getConnectionStatus(server, &status) == ERROR_ok) return status;
    return STATUS_DISCONNECTED;
}

unsigned short UserPanel::selfId(quint64 server) const {
    unsigned short self = 0;
    if (server && m_funcs.getClientID && m_funcs.getClientID(server, &self) == ERROR_ok) return self;
    return 0;
}

bool UserPanel::writeSelfState(quint64 server, int flag, int value, int previous) {
    if (!m_funcs.setClientSelfVariableAsInt || !m_funcs.flushClientSelfUpdates) return false;
    unsigned int error = m_funcs.setClientSelfVariableAsInt(server, flag, value);
    if (error == ERROR_ok) {
        char returnCode[128] = {};
        if (!m_pluginId.isEmpty() && m_funcs.createReturnCode)
            m_funcs.createReturnCode(m_pluginId.constData(), returnCode, sizeof(returnCode));
        error = m_funcs.flushClientSelfUpdates(server, returnCode[0] ? returnCode : nullptr);
        // The setter stages a value locally; discard it if sending failed.
        if (error != ERROR_ok) m_funcs.setClientSelfVariableAsInt(server, flag, previous);
    }
    if (error == ERROR_ok) return true;
    if (m_error) m_error(server, flag, error);
    return false;
}

void UserPanel::publishSelfState(quint64 server, unsigned short self, int flag, bool value) {
    if (m_server == server && m_self == self) setSelfState(server, self, flag, value, true);
    if (m_stateChange) m_stateChange(server, self, flag, value);
}

void UserPanel::applyAwayPreset(quint64 server) {
    if (!server || (!m_awayPresets.contains(server) && !m_awayPresets.contains(0))) return;
    if (connectionStatus(server) != STATUS_CONNECTION_ESTABLISHED) return;
    // Once the next connection is known, keep a failed preset on that tab.
    m_awayPresets.remove(0);
    m_awayPresets.insert(server);
    refresh();
    const unsigned short self = selfId(server);
    if (!self) return;

    int away = AWAY_NONE;
    const bool alreadyAway = m_funcs.getClientVariableAsInt &&
        m_funcs.getClientVariableAsInt(server, self, CLIENT_AWAY, &away) == ERROR_ok && away != AWAY_NONE;
    if (!alreadyAway && !writeSelfState(server, CLIENT_AWAY, AWAY_ZZZ, AWAY_NONE)) return;
    m_awayPresets.remove(server);
    publishSelfState(server, self, CLIENT_AWAY, true);
    update();
}

void UserPanel::refresh() {
    if (!m_funcs.getCurrentServerConnectionHandlerID) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    const bool connected = connectionStatus(h) == STATUS_CONNECTION_ESTABLISHED;
    const unsigned short self = connected ? selfId(h) : 0;
    if (h != m_server || self != m_self) {
        m_name.clear();
        m_away = m_inputMuted = m_outputMuted = false;
        m_selfChanges.bind(h, self);
    }
    m_server = h;
    m_self = self;
    m_connected = connected;
    if (self) {
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
        m_selfChanges.bind(h, self);
        int v = 0;
        if (m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_INPUT_MUTED, &v) == 0)
            m_inputMuted = m_selfChanges.resolve(CLIENT_INPUT_MUTED, v != MUTEINPUT_NONE);
        if (m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_OUTPUT_MUTED, &v) == 0)
            m_outputMuted = m_selfChanges.resolve(CLIENT_OUTPUT_MUTED, v != MUTEOUTPUT_NONE);
        if (m_funcs.getClientVariableAsInt &&
            m_funcs.getClientVariableAsInt(h, self, CLIENT_AWAY, &v) == 0)
            m_away = m_selfChanges.resolve(CLIENT_AWAY, v != AWAY_NONE);
    }
    update();
}

void UserPanel::setTalking(bool talking) {
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
    const qreal inset = kUserCardInset + 0.5;
    const QRectF card = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    const QColor fill = discord::card();
    p.setPen(QPen(discord::cardEdge(), 1));
    p.setBrush(fill);
    p.drawRoundedRect(card, foundation::radiusOverlay, foundation::radiusOverlay);

    const qreal rowCenter = buttonRect(0).center().y();
    const QRectF av(kUserCardInset + kUserPad, rowCenter - kUserAvatarSize / 2.0,
                    kUserAvatarSize, kUserAvatarSize);
    drawAvatar(p, av, m_name.isEmpty() ? QStringLiteral("?") : m_name, m_talking, discord::green());

    p.setPen(QPen(fill, 2));
    p.setBrush((m_inputMuted || m_away) ? discord::faint() : discord::green());
    p.drawEllipse(QRectF(av.right() - 9, av.bottom() - 9, 9, 9));

    const QFont nameFont = discord::font(foundation::bodySize, QFont::DemiBold);
    const QFont stateFont = discord::font(foundation::captionSize);
    const QFontMetrics nameMetrics(nameFont, this);
    const QFontMetrics stateMetrics(stateFont, this);
    const int textLeft = qRound(av.right()) + foundation::gapNormal;
    const int textWidth = qMax(0, qRound(buttonRect(0).left()) - foundation::gapNormal - textLeft);
    const int textHeight = nameMetrics.height() + foundation::gapTight + stateMetrics.height();
    const qreal top = rowCenter - textHeight / 2.0;
    if (textWidth > 0) {
        const QString displayName = m_name.isEmpty() ? QStringLiteral("未连接") : m_name;
        p.setFont(nameFont);
        p.setPen(m_away ? discord::muted() : discord::ink());
        p.drawText(QRectF(textLeft, top, textWidth, nameMetrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   nameMetrics.elidedText(displayName, Qt::ElideRight, textWidth));

        QString state = QStringLiteral("在线");
        if (m_away) state = QStringLiteral("离开");
        else if (awayPresetVisible()) state = QStringLiteral("预设离开");
        else if (!m_connected) state = QStringLiteral("离线");
        else if (m_outputMuted) state = QStringLiteral("耳机静音");
        else if (m_inputMuted) state = QStringLiteral("麦克风静音");
        p.setFont(stateFont);
        p.setPen(discord::muted());
        p.drawText(QRectF(textLeft, top + nameMetrics.height() + foundation::gapTight,
                          textWidth, stateMetrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   stateMetrics.elidedText(state, Qt::ElideRight, textWidth));
    }

    const QColor hoverFill = discord::hover();
    const QColor pressedFill = discord::color(Color::fillPressed);
    for (int i = 0; i < kButtonCount; ++i) {
        const QRectF btn = buttonRect(i);
        const bool active = buttonActive(i);
        const QColor tint = i == 0 ? discord::awayColor() : discord::danger();
        const auto& visual = m_visuals[i];
        if (active || visual.hover > 0 || visual.press > 0) {
            QColor bg = mixUserColor(fill, hoverFill, visual.hover);
            bg = mixUserColor(bg, pressedFill, visual.press);
            if (active) {
                const QColor activeFill = i == 0 ? tint : discord::color(Color::fillDanger);
                bg = mixUserColor(bg, activeFill, (36 + 24 * visual.press) / 255.0);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(btn, foundation::radiusControl, foundation::radiusControl);
        }
        // The glyph alone moves down a pixel on press; the button rect stays fixed.
        const QRectF icon(btn.center().x() - kUserGlyphSize / 2.0,
                          btn.center().y() - kUserGlyphSize / 2.0 + visual.press,
                          kUserGlyphSize, kUserGlyphSize);
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
    setPointerState(button, button);
    if (button < 0) return;
    // Refresh first in case this click follows a server switch.
    refresh();
    if (button == 0) {
        if (awayPresetVisible() && !m_away) {
            m_awayPresets.remove(m_server);
            if (!m_connected) m_awayPresets.remove(0);
            update();
            return;
        }
        if (!m_connected || !m_self) {
            m_awayPresets.insert(m_server);
            update();
            return;
        }
    }
    if (!m_self) return;
    const int flag = button == 0 ? CLIENT_AWAY
                                 : (button == 1 ? CLIENT_INPUT_MUTED : CLIENT_OUTPUT_MUTED);
    const bool active = button == 0 ? m_away : buttonActive(button);
    const int enabled = button == 0 ? AWAY_ZZZ
                                     : (button == 1 ? MUTEINPUT_MUTED : MUTEOUTPUT_MUTED);
    if (writeSelfState(m_server, flag, active ? 0 : enabled, active ? enabled : 0)) {
        if (button == 0) m_awayPresets.remove(m_server);
        publishSelfState(m_server, m_self, flag, !active);
    }
}

void UserPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) setPointerState(buttonAt(e->pos()), -1);
    QWidget::mouseReleaseEvent(e);
}

void UserPanel::mouseMoveEvent(QMouseEvent* e) {
    // The implicit mouse grab can deliver moves outside the panel before Leave.
    const QPoint pos = e->pos();
    const bool keepPress = rect().contains(pos) && e->buttons().testFlag(Qt::LeftButton);
    setPointerState(buttonAt(pos), keepPress ? m_pressed : -1);
}

void UserPanel::leaveEvent(QEvent*) {
    setPointerState(-1, -1);
}

bool UserPanel::event(QEvent* e) {
    switch (e->type()) {
    case QEvent::UngrabMouse:
    case QEvent::WindowDeactivate:
        setPointerState(-1, -1);
        break;
    case QEvent::Hide:
    case QEvent::EnabledChange:
        if (e->type() == QEvent::Hide || !isEnabled()) setPointerState(-1, -1, true);
        break;
    default:
        break;
    }
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int button = buttonAt(help->pos());
        if (button < 0) {
            QToolTip::hideText();
            return true;
        }
        static const char* const onText[] = {"设为离开", "静音麦克风", "静音耳机"};
        static const char* const offText[] = {"取消离开", "取消麦克风静音", "取消耳机静音"};
        const char* text = buttonActive(button) ? offText[button] : onText[button];
        if (button == 0) {
            if (awayPresetVisible() && !m_away) text = "取消预设离开";
            else if (!m_connected || !m_self) text = "连接后设为离开";
        }
        QToolTip::showText(help->globalPos(), QString::fromUtf8(text), this);
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
    m_self = 0;

    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID
                          ? m_funcs.getCurrentServerConnectionHandlerID() : 0;
    m_clientServer = h;
    if (h && m_funcs.getClientID) m_funcs.getClientID(h, &m_self);
    m_selfChanges.bind(h, m_self);
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
                if (id != m_self && !(h == m_selfTalkServer && id == m_selfTalkClient) &&
                    m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_FLAG_TALKING, &v) == 0)
                    pollTalking(stripClientTag(name), v != 0);
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_AWAY, &v) == 0)
                    pollClientState(id, CLIENT_AWAY, v != AWAY_NONE);
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_INPUT_MUTED, &v) == 0)
                    pollClientState(id, CLIENT_INPUT_MUTED, v != MUTEINPUT_NONE);
                if (m_funcs.getClientVariableAsInt &&
                    m_funcs.getClientVariableAsInt(h, id, CLIENT_OUTPUT_MUTED, &v) == 0)
                    pollClientState(id, CLIENT_OUTPUT_MUTED, v != MUTEOUTPUT_NONE);
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

void NativeTreeRows::setSelfTalking(quint64 server, unsigned short client, bool talking) {
    m_selfTalkServer = server;
    m_selfTalkClient = client;
    m_selfTalking = talking;
}

// The heartbeat can read stale talk state immediately after a remote event.
void NativeTreeRows::pollTalking(const QString& name, bool talking) {
    const qint64 last = m_talkStamp.value(name, 0);
    if (QDateTime::currentMSecsSinceEpoch() - last < 1000 &&
        m_talking.value(name, false) != talking)
        return;
    m_talking[name] = talking;
}

void NativeTreeRows::setSelfState(quint64 server, unsigned short client, int flag, bool value, bool local) {
    m_self = client;
    m_selfChanges.bind(server, client);
    if (!m_selfChanges.set(flag, value, local)) return;
    pollClientState(client, flag, value);
}

void NativeTreeRows::pollClientState(unsigned short client, int flag, bool value) {
    if (client == m_self) value = m_selfChanges.resolve(flag, value);
    if (flag == CLIENT_AWAY) m_away[client] = value;
    else if (flag == CLIENT_INPUT_MUTED) m_inMuted[client] = value;
    else if (flag == CLIENT_OUTPUT_MUTED) m_outMuted[client] = value;
}

void NativeTreeRows::refreshStates() {
    if (!m_funcs.getCurrentServerConnectionHandlerID) return;
    const quint64 h = m_funcs.getCurrentServerConnectionHandlerID();
    m_self = 0;
    if (h && m_funcs.getClientID) m_funcs.getClientID(h, &m_self);
    m_selfChanges.bind(h, m_self);
    if (!h || !m_funcs.getClientList) return;
    unsigned short* clients = nullptr;
    if (m_funcs.getClientList(h, &clients) != 0 || !clients) return;
    for (unsigned short* c = clients; *c; ++c) {
        const unsigned short id = *c;
        int v = 0;
        if (m_funcs.getClientVariableAsInt) {
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_AWAY, &v) == 0)
                pollClientState(id, CLIENT_AWAY, v != AWAY_NONE);
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_INPUT_MUTED, &v) == 0)
                pollClientState(id, CLIENT_INPUT_MUTED, v != MUTEINPUT_NONE);
            if (m_funcs.getClientVariableAsInt(h, id, CLIENT_OUTPUT_MUTED, &v) == 0)
                pollClientState(id, CLIENT_OUTPUT_MUTED, v != MUTEOUTPUT_NONE);
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
    int h = foundation::controlHeight;
    if (k == Server) h = 54;
    else if (k == Channel) h += foundation::gapTight;
    else if (k == Category) h += foundation::gapTight / 2;
    return QSize(200, h);
}

namespace {

void drawTreeLabel(QPainter& painter, const QRect& rect, const QString& text) {
    constexpr int alignment = Qt::AlignLeft | Qt::AlignVCenter;
    const auto diamond = QStringLiteral("\U0001F538");
    int i = text.indexOf(diamond);
    if (i < 0 || painter.layoutDirection() == Qt::RightToLeft ||
        text.contains(QLatin1Char('\n')) || text.contains(QChar::LineSeparator)) {
        painter.drawText(rect, alignment, text);
        return;
    }

    // Keep the glyph's advance and the surrounding native text, but suppress
    // its color bitmap: a transparent outline makes Qt use the outline path.
    QTextCharFormat hiddenDiamond;
    hiddenDiamond.setForeground(Qt::transparent);
    hiddenDiamond.setTextOutline(QPen(QColor(Qt::transparent)));
    QVector<QTextLayout::FormatRange> formats;
    for (; i >= 0; i = text.indexOf(diamond, i + diamond.size())) {
        const int end = i + diamond.size();
        const ushort variant = end < text.size() ? text.at(end).unicode() : 0;
        if (variant != 0xFE0E)
            formats.append({i, diamond.size() + (variant == 0xFE0F ? 1 : 0), hiddenDiamond});
    }
    if (formats.isEmpty()) {
        painter.drawText(rect, alignment, text);
        return;
    }

    QString label = text;
    label.replace(QLatin1Char('\t'), QLatin1Char(' '));
    label.replace(QLatin1Char('\r'), QLatin1Char(' '));
    QTextLayout layout(label, painter.font(), painter.device());
    QTextOption option;
    option.setTextDirection(painter.layoutDirection());
    option.setWrapMode(QTextOption::NoWrap);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    layout.setTextOption(option);
    layout.setFormats(formats);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(rect.width());
    layout.endLayout();
    if (!line.isValid()) return;

    painter.save();
    if (line.naturalTextWidth() > rect.width() || line.height() > rect.height())
        painter.setClipRect(rect, Qt::IntersectClip);
    const QPointF origin(rect.x(), rect.y() + (rect.height() - line.ascent() - line.descent()) / 2);
    line.draw(&painter, origin);

    // Draw a single solid shape with coverage antialiasing, not RGB text masks.
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& format : formats) {
        const qreal start = line.cursorToX(format.start, QTextLine::Leading);
        const qreal end = line.cursorToX(format.start, QTextLine::Trailing);
        const qreal advance = qAbs(end - start);
        const qreal radius = advance * 0.19;
        const qreal x = origin.x() + (start + end) / 2;
        const qreal y = origin.y() + line.ascent() - advance * 0.26;
        QPainterPath path;
        path.moveTo(x, y - radius);
        path.lineTo(x + radius, y);
        path.lineTo(x, y + radius);
        path.lineTo(x - radius, y);
        path.closeSubpath();
        painter.fillPath(path, QColor(0xFF, 0x67, 0x23));
    }
    painter.restore();
}

} // namespace

void NativeTreeRows::paint(QPainter* p, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    const Kind kind = kindOf(index);
    const QRect r = option.rect;
    const QString raw = index.data(Qt::DisplayRole).toString();

    if (r.isEmpty()) return;
    p->save();
    p->setClipRect(r, Qt::IntersectClip);
    p->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool current = (kind == Channel || kind == Category) && !m_myChannel.isEmpty() &&
                         raw == m_myChannel;
    const QRectF rowSurface = QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5);
    if (selected || current) {
        p->setPen(Qt::NoPen);
        p->setBrush(selected ? discord::selected() : discord::color(Color::fillAccentSubtle));
        p->drawRoundedRect(rowSurface, foundation::radiusControl, foundation::radiusControl);
    }
    const bool focused = option.state.testFlag(QStyle::State_HasFocus);
    const bool keyboardFocus = focused && option.state.testFlag(QStyle::State_KeyboardFocusChange);
    if (focused && !selected && kind != ClientRow) {
        p->setPen(QPen(discord::color(Color::focus), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(rowSurface, foundation::radiusControl, foundation::radiusControl);
    }
    if (kind == ClientRow && (selected || keyboardFocus) && r.width() >= foundation::gapLoose) {
        // The keyboard focus marker extends farther down the same gutter.
        const int markerHeight = keyboardFocus ? foundation::gapSection : foundation::gapLoose;
        p->setPen(Qt::NoPen);
        p->setBrush(discord::accent());
        p->drawRoundedRect(QRectF(r.left() + 2, r.y() + (r.height() - markerHeight) / 2.0,
                                  3, markerHeight), 1.5, 1.5);
    }

    if (kind == Server) {
        const QFont f = discord::font(foundation::subtitleSize, QFont::DemiBold);
        p->setFont(f);
        p->setPen(discord::ink());
        const QFontMetrics fm = p->fontMetrics();
        const QString title = m_serverName.isEmpty() ? raw : m_serverName;
        const QRect label = r.adjusted(foundation::gapLoose, 0, -foundation::gapLoose,
                                       -foundation::gapTight);
        if (!label.isEmpty())
            drawTreeLabel(*p, label, fm.elidedText(title, Qt::ElideRight, label.width()));
        p->setPen(QPen(discord::divider(), 1));
        p->drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        p->restore();
        return;
    }

    if (kind == Spacer || kind == Category) {
        QString t = raw;
        if (kind == Spacer) {
            const int close = t.indexOf(']');
            if (close >= 0) t = t.mid(close + 1);
            t = t.trimmed();
        }
        p->setFont(discord::font(foundation::captionSize, QFont::DemiBold));
        p->setPen(kind == Spacer ? discord::faint() : discord::muted());
        const QRect label = r.adjusted(kind == Spacer ? foundation::gapLoose : foundation::gapNormal,
                                       0, -foundation::gapLoose, 0);
        if (!label.isEmpty())
            drawTreeLabel(*p, label, p->fontMetrics().elidedText(t.toUpper(), Qt::ElideRight, label.width()));
        p->restore();
        return;
    }

    const QFont rowFont = discord::font(foundation::bodySize);
    p->setFont(rowFont);
    const QFontMetrics fm = p->fontMetrics();

    if (kind == Channel) {
        drawSpeaker(*p, QRectF(r.left() + foundation::gapTight, r.center().y() - 8, 17, 16),
                    discord::muted(), false);
        p->setPen(discord::ink());
        const QRect label = r.adjusted(26, 0, -26, 0);
        if (!label.isEmpty())
            drawTreeLabel(*p, label, fm.elidedText(raw, Qt::ElideRight, label.width()));
        if (m_locked.contains(raw))
            drawLock(*p, QRectF(r.right() - 20, r.center().y() - 6, 11, 13), discord::faint());
        p->restore();
        return;
    }

    // Client row: circle instead of an avatar; lights up while talking.
    const QString name = stripClientTag(raw);
    const unsigned short id = clientIdFor(name);
    const bool matchesSelfTalk = id && m_clientServer == m_selfTalkServer && id == m_selfTalkClient;
    const bool isSelf = matchesSelfTalk || (id && id == m_self);
    const bool talking = isSelf ? matchesSelfTalk && m_selfTalking : m_talking.value(name, false);
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
        // The member dot keeps its teal identity independently of the UI palette.
        QColor ident = discord::isDark() ? QColor(0x2D, 0xD4, 0xBF) : QColor(0x0F, 0x76, 0x6E);
        if (away) ident.setAlpha(150);
        p->setPen(Qt::NoPen);
        p->setBrush(ident);
    }
    p->drawEllipse(dot);

    p->setPen(away ? discord::muted() : discord::ink());
    const int left = r.left() + 27;

    const int textRight = drawMemberBadges(*p, r, muted, outMuted, away);
    if (textRight <= left) {
        p->restore();
        return;
    }

    // Draw leading [group] tags bold, then the nickname in the normal weight.
    int x = left;
    QString rest = raw;
    QFont groupFont = rowFont;
    groupFont.setWeight(QFont::Bold);
    const QFontMetrics gfm(groupFont, p->device());
    while (rest.startsWith('[')) {
        const int close = rest.indexOf(']');
        if (close < 0) break;
        const QString tag = rest.left(close + 1);
        rest = rest.mid(close + 1);
        p->setFont(groupFont);
        p->setPen(discord::ink());
        const int w = gfm.horizontalAdvance(tag);
        drawTreeLabel(*p, QRect(x, r.top(), qMax(0, textRight - x), r.height()), tag);
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
        drawTreeLabel(*p, QRect(x, r.top(), textRight - x, r.height()), rest);
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
    ChatSeverity,
    ChatPoke
};

constexpr int kChatPad = foundation::gapLoose;
constexpr int kChatBubblePad = foundation::gapNormal;
constexpr int kChatPadY = foundation::gapNormal;
constexpr int kChatScrollGutter = 10;  // Matches the Foundation scrollbar extent.

QFont chatNameFont() { return discord::font(foundation::bodySize, QFont::DemiBold); }
QFont chatTextFont() { return discord::font(foundation::bodySize); }
QFont chatTimeFont() { return discord::font(foundation::captionSize); }

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
    std::vector<ChatPara> paras;
    qreal maxLineWidth = 0;
    int height = 0;
};

ChatTextGeom buildChatText(const QString& text, const QFont& font, int wrapWidth,
                           const QPaintDevice* device,
                           QTextOption::WrapMode wrapMode = QTextOption::WordWrap,
                           Qt::Alignment alignment = Qt::AlignLeft) {
    ChatTextGeom g;
    g.text = text;
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
        para.layout.reset(new QTextLayout(text.mid(start, len), font, device));
        QTextOption opt;
        opt.setWrapMode(wrapMode);
        opt.setAlignment(alignment);
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
        if (para.layout->lineCount() == 0) y = QFontMetricsF(font, device).height();
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

// Use the layout's own selection painting so wrapped and bidirectional text
// share the same bounds and keep the Foundation selection foreground.
void drawChatText(QPainter* p, const ChatTextGeom& g, const QPointF& origin,
                  const QColor& ink, int selStart, int selEnd, bool onAccent = false) {
    p->setPen(ink);
    QTextCharFormat selected;
    if (selEnd > selStart) {
        selected.setBackground(discord::color(onAccent ? Color::selectionText : Color::selectionBg));
        selected.setForeground(discord::color(onAccent ? Color::selectionBg : Color::selectionText));
    }
    for (const ChatPara& para : g.paras) {
        QVector<QTextLayout::FormatRange> ranges;
        const int start = qMax(selStart, para.start);
        const int end = qMin(selEnd, para.start + para.length);
        if (end > start) ranges.append({start - para.start, end - start, selected});
        para.layout->draw(p, origin + QPointF(0, para.top), ranges);
    }
}

// Everything the delegate and the selection code need to agree on: where the
// bubble is, where the text sits and how it wraps.
struct ChatRowGeom {
    bool valid = false;
    bool system = false;
    int height = foundation::controlHeight;
    int metadataHeight = 0;
    QRect bubbleRect;
    QRect textRect;
    ChatTextGeom textGeom;
};

ChatRowGeom buildChatRow(int width, const QModelIndex& index, const QPaintDevice* device) {
    ChatRowGeom g;
    if (!index.isValid()) return g;
    g.valid = true;
    g.system = index.data(ChatSystem).toBool();
    if (g.system) {
        const QString sender = index.data(ChatSender).toString();
        const QString text = index.data(ChatText).toString();
        // Server logs keep the compact line geometry used for text selection.
        constexpr int sideInset = 14;
        constexpr int minimumHeight = 30;
        constexpr int bottomInset = 8;
        constexpr int verticalInsets = 12;
        const int innerWidth = qMax(1, width - 2 * sideInset);
        g.textGeom = buildChatText(sender.isEmpty() ? text : sender + QStringLiteral("  ") + text,
                                   chatTimeFont(), innerWidth, device,
                                   QTextOption::WrapAtWordBoundaryOrAnywhere, Qt::AlignHCenter);
        g.height = qMax(minimumHeight, g.textGeom.height + verticalInsets);
        g.textRect = QRect(sideInset, g.height - bottomInset - g.textGeom.height,
                           innerWidth, g.textGeom.height);
        return g;
    }

    const bool self = index.data(ChatSelf).toBool();
    const QString text = index.data(ChatText).toString();
    const int available = qMax(1, width - 2 * kChatPad);
    const int maxBubble = qMin(available, qMax(120, int(width * 0.76)));
    const int pad = qMin(kChatBubblePad, (maxBubble - 1) / 2);
    const int innerMax = maxBubble - 2 * pad;
    g.textGeom = buildChatText(text, chatTextFont(), innerMax, device);

    const int innerW = qMin(innerMax, qCeil(g.textGeom.maxLineWidth) + 1);
    const int bubbleW = innerW + 2 * pad;
    const int bubbleH = g.textGeom.height + 2 * kChatPadY;
    g.metadataHeight = qMax(QFontMetrics(chatNameFont(), device).height(),
                            QFontMetrics(chatTimeFont(), device).height());
    const int bubbleY = kChatPadY + g.metadataHeight + foundation::gapTight;
    const int bubbleX = self ? (width - kChatPad - bubbleW) : kChatPad;

    g.bubbleRect = QRect(bubbleX, bubbleY, bubbleW, bubbleH);
    g.textRect = QRect(bubbleX + pad, bubbleY + kChatPadY, innerW, g.textGeom.height);
    g.height = bubbleY + bubbleH + kChatPadY;
    return g;
}

bool chatWordChar(QChar ch) {
    return ch.isLetterOrNumber() || QStringLiteral("_-./:@#?&=%+").contains(ch);
}

// How close to the end of the history still counts as "reading the newest".
// Small enough that one wheel notch up stops the auto-follow, so the reader
// keeps the position they scrolled to.
constexpr int kFollowSlack = 24;

class ChatScrollBar : public QScrollBar {
public:
    explicit ChatScrollBar(QWidget* parent) : QScrollBar(Qt::Vertical, parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        // Keep the gutter in the layout even while there is no thumb to draw.
        if (minimum() < maximum()) QScrollBar::paintEvent(event);
    }
};

class ChatBubbleDelegate : public QStyledItemDelegate {
public:
    explicit ChatBubbleDelegate(ModernChat* view) : m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QWidget* viewport = m_view->viewport();
        const int width = option.rect.width() > 0 ? option.rect.width() : viewport->width();
        const ChatRowGeom row = buildChatRow(width, index, viewport);
        return QSize(width, row.height);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QRect r = option.rect;
        p->save();
        p->setClipRect(r, Qt::IntersectClip);
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillRect(r, discord::bg());

        const ChatRowGeom row = buildChatRow(r.width(), index, m_view->viewport());
        if (!row.valid) {
            p->restore();
            return;
        }

        int selA = 0;
        int selB = 0;
        m_view->selectionRangeForRow(index.row(), selA, selB);
        const QPoint textOrigin = row.textRect.topLeft() + r.topLeft();
        if (row.system) {
            const int severity = index.data(ChatSeverity).toInt();
            const bool poke = index.data(ChatPoke).toBool();
            QColor ink = discord::muted();
            if (poke) {
                ink = discord::accent();
                p->setPen(Qt::NoPen);
                p->setBrush(ink);
                const int height = foundation::gapLoose;
                p->drawRoundedRect(QRectF(r.left() + foundation::gapTight,
                                          r.y() + (r.height() - height) / 2.0, 3, height), 1.5, 1.5);
            } else if (severity >= 2) {
                ink = discord::severityRed();
            } else if (severity == 1) {
                ink = discord::severityAmber();
            }
            drawChatText(p, row.textGeom, textOrigin, ink, selA, selB);
            p->restore();
            return;
        }

        const bool self = index.data(ChatSelf).toBool();
        const QString sender = self ? QStringLiteral("我") : index.data(ChatSender).toString();
        const QString time = index.data(ChatTime).toString();
        const QRect meta = r.adjusted(kChatPad, kChatPadY, -kChatPad, 0);
        const QFont nameFont = chatNameFont();
        const QFont timeFont = chatTimeFont();
        const QFontMetrics nameMetrics(nameFont, p->device());
        const QFontMetrics timeMetrics(timeFont, p->device());
        const int timeWidth = time.isEmpty() ? 0 : timeMetrics.horizontalAdvance(time) + 2;
        const int timeSpace = timeWidth && timeWidth + 2 * foundation::gapNormal < meta.width()
                                  ? timeWidth + foundation::gapNormal : 0;
        const int nameSpace = qMax(0, meta.width() - timeSpace);
        const QString display = nameMetrics.elidedText(sender, Qt::ElideRight, nameSpace);
        const int nameWidth = qMin(nameSpace, nameMetrics.horizontalAdvance(display) + 2);
        const int nameX = self ? meta.x() + meta.width() - nameWidth : meta.x();

        p->setFont(nameFont);
        p->setPen(self ? discord::accent() : discord::ink());
        p->drawText(QRect(nameX, meta.top(), nameWidth, row.metadataHeight),
                    (self ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, display);

        if (timeSpace > 0) {
            const int timeX = self ? nameX - timeSpace : nameX + nameWidth + foundation::gapNormal;
            p->setFont(timeFont);
            p->setPen(discord::faint());
            p->drawText(QRect(timeX, meta.top(), timeWidth, row.metadataHeight),
                        (self ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, time);
        }

        p->setPen(Qt::NoPen);
        p->setBrush(self ? discord::accent() : discord::bubbleOther());
        p->drawRoundedRect(row.bubbleRect.translated(r.topLeft()),
                           foundation::radiusOverlay, foundation::radiusOverlay);
        const QColor bodyInk = self ? discord::color(Color::textOnAccent) : discord::ink();
        drawChatText(p, row.textGeom, textOrigin, bodyInk, selA, selB, self);
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
    auto* sb = new ChatScrollBar(this);
    setVerticalScrollBar(sb);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setResizeMode(QListView::Adjust);  // Wrapped rows are sized against the viewport width.
    setUniformItemSizes(false);
    setSpacing(0);
    viewport()->setCursor(Qt::IBeamCursor);
    applyTheme();

    m_wheelMotion.setDuration(foundation::motionDuration);
    m_wheelMotion.setEasingCurve(foundation::motionEasing());
    m_wheelMotion.setStartValue(0.0);
    m_wheelMotion.setEndValue(1.0);
    connect(&m_wheelMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (m_wheelMotion.state() != QAbstractAnimation::Running) return;
        const int offset = m_wheelStart + qRound((m_wheelTarget - m_wheelStart) * value.toReal());
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        verticalScrollBar()->setValue(offset);
    });
    connect(&m_wheelMotion, &QVariantAnimation::finished, this, [this] {
        auto* bar = verticalScrollBar();
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        bar->setValue(m_wheelTarget);
        rememberScrollPosition(bar->value());
    });

    // Layout can finish after the rows go in. Use the reading intent saved
    // before that layout, and ignore intermediate clamps during our rebuilds.
    connect(sb, &QScrollBar::rangeChanged, this, [this] { restoreScrollPosition(); });
    connect(sb, &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_adjustingScroll && m_wheelMotion.state() != QAbstractAnimation::Running)
            rememberScrollPosition(value);
    });
    // Direct use of the thumb or scrollbar actions takes over at the visible offset.
    connect(sb, &QScrollBar::actionTriggered, this, [this] { stopWheelScroll(); });
    connect(sb, &QScrollBar::sliderPressed, this, [this] { stopWheelScroll(); });
}

void ModernChat::applyTheme() {
    setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:none; }"
        "QListWidget::item { border:none; }"
        "QListWidget::item:selected { background:transparent; }"
        "QScrollBar:vertical { width:%2px; background:transparent; margin:0; }")
        .arg(discord::bg().name()).arg(kChatScrollGutter));
    viewport()->update();
}

void ModernChat::rememberScrollPosition(int value) {
    const QScrollBar* sb = verticalScrollBar();
    // A collapsed or non-scrollable pane cannot tell us where the reader was.
    if (sb->maximum() <= sb->minimum() || viewport()->height() <= 0) return;
    m_followBottom = value >= sb->maximum() - kFollowSlack;
    m_savedOffset = m_followBottom ? -1 : value;
}

void ModernChat::restoreScrollPosition() {
    if (m_adjustingScroll || m_wheelMotion.state() == QAbstractAnimation::Running) return;
    auto* sb = verticalScrollBar();
    if (sb->maximum() <= sb->minimum() || viewport()->height() <= 0) return;
    QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
    if (m_followBottom) sb->setValue(sb->maximum());
    else if (m_savedOffset >= 0) sb->setValue(m_savedOffset);
}

void ModernChat::scrollToLatest() {
    m_wheelMotion.stop();
    m_followBottom = true;
    m_savedOffset = -1;
    {
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        executeDelayedItemsLayout();
    }
    restoreScrollPosition();
}

void ModernChat::resizeEvent(QResizeEvent* ev) {
    finishWheelScroll();
    {
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        QListWidget::resizeEvent(ev);
        if (viewport()->height() > 0) executeDelayedItemsLayout();
    }
    restoreScrollPosition();
}

void ModernChat::showEvent(QShowEvent* ev) {
    {
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        QListWidget::showEvent(ev);
        executeDelayedItemsLayout();
    }
    restoreScrollPosition();
}

void ModernChat::hideEvent(QHideEvent* ev) {
    finishWheelScroll();
    m_selecting = false;
    QListWidget::hideEvent(ev);
}

void ModernChat::insertMessage(const ChatMessage& msg) {
    auto* item = new QListWidgetItem(this);
    item->setData(ChatSender, msg.sender);
    item->setData(ChatText, msg.text);
    item->setData(ChatTime, msg.time);
    item->setData(ChatSelf, msg.self);
    item->setData(ChatSystem, msg.system);
    item->setData(ChatSeverity, msg.severity);
    item->setData(ChatPoke, msg.poke);
    item->setFlags(Qt::NoItemFlags);
}

void ModernChat::addMessage(const ChatMessage& msg) {
    // A wheel headed to the newest messages must keep following when new rows arrive.
    if (m_followBottom) finishWheelScroll();
    {
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        insertMessage(msg);
        executeDelayedItemsLayout();
    }
    restoreScrollPosition();
}

void ModernChat::setMessages(const QVector<ChatMessage>& msgs, int keepOffset) {
    finishWheelScroll();
    {
        QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        clear();
        m_selecting = false;
        clearTextSelection();
        for (const ChatMessage& msg : msgs) insertMessage(msg);
        executeDelayedItemsLayout();
    }
    // A saved reading offset remains valid even if this layout temporarily has
    // no scroll range. A later rangeChanged can still restore it.
    m_followBottom = keepOffset < 0;
    m_savedOffset = keepOffset;
    restoreScrollPosition();
}

void ModernChat::finishWheelScroll() {
    if (m_wheelMotion.state() != QAbstractAnimation::Running) return;
    m_wheelMotion.stop();
    auto* sb = verticalScrollBar();
    if (sb->maximum() <= sb->minimum() || viewport()->height() <= 0) return;
    sb->setValue(m_wheelTarget);
    rememberScrollPosition(sb->value());
}

void ModernChat::stopWheelScroll() {
    if (m_wheelMotion.state() != QAbstractAnimation::Running) return;
    m_wheelMotion.stop();
    rememberScrollPosition(verticalScrollBar()->value());
}

void ModernChat::wheelEvent(QWheelEvent* ev) {
    auto* sb = verticalScrollBar();
    if (m_selecting || sb->isSliderDown() || !foundation::isMouseWheelNotch(*ev) ||
        !foundation::animationsEnabled()) {
        finishWheelScroll();
        QListWidget::wheelEvent(ev);
        return;
    }

    const int current = sb->value();
    const bool running = m_wheelMotion.state() == QAbstractAnimation::Running;
    const int base = running ? m_wheelTarget : current;
    const int target = foundation::wheelTarget(*sb, base, ev->angleDelta().y());
    if (target == base) {
        if (running) ev->accept();
        else QListWidget::wheelEvent(ev);
        return;
    }

    m_wheelMotion.stop();
    // Follow is based on the destination so an append cannot pull the view
    // back down during the first frames of scrolling up.
    rememberScrollPosition(target);
    m_wheelStart = current;
    m_wheelTarget = target;
    if (target != current) m_wheelMotion.start();
    ev->accept();
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
    if (!it) return false;

    int aRow = m_selAnchorRow;
    int aPos = m_selAnchorPos;
    int cRow = m_selCursorRow;
    int cPos = m_selCursorPos;
    if (aRow > cRow || (aRow == cRow && aPos > cPos)) {
        qSwap(aRow, cRow);
        qSwap(aPos, cPos);
    }
    if (row < aRow || row > cRow) return false;

    const int len = rowPlainText(row).size();
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
    const QRect rowRect = visualItemRect(item(row));
    const ChatRowGeom g = buildChatRow(rowRect.width(), model()->index(row, 0), viewport());
    if (!g.valid || g.textGeom.text.isEmpty()) return p;

    const QPointF local(pt - rowRect.topLeft() - g.textRect.topLeft());
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
    stopWheelScroll();
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
    if (!it) return;

    const QString text = rowPlainText(p.row);
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
    stopWheelScroll();
    QListWidget::keyPressEvent(ev);
}

void ModernChat::contextMenuEvent(QContextMenuEvent* ev) {
    stopWheelScroll();
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
    copyRowAct->setEnabled(row >= 0);
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
        if (!it) continue;
        const QString text = rowPlainText(row);
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
    p.setFont(chatTextFont());
    p.setPen(discord::muted());
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
    p.fillRect(0, height() - 1, width(), 1, discord::divider());

    const int left = foundation::gapLoose;
    const int titleLeft = left + foundation::gapSection;
    const int right = width() - foundation::gapLoose;
    const int titleSpace = qMax(0, right - titleLeft);
    constexpr int alignment = Qt::AlignLeft | Qt::AlignVCenter;
    p.setFont(discord::font(foundation::subtitleSize, QFont::Bold));
    p.setPen(discord::faint());
    p.drawText(QRect(left, 0, foundation::gapSection, height()), alignment, QStringLiteral("#"));

    if (titleSpace == 0) return;
    p.setFont(discord::font(foundation::bodyLargeSize, QFont::DemiBold));
    p.setPen(discord::ink());
    const QFontMetrics titleMetrics = p.fontMetrics();
    const QString title = m_name.isEmpty() ? QStringLiteral("未连接") : m_name;
    const int titleLimit = m_topic.isEmpty() ? titleSpace : qMin(titleSpace, width() / 2);
    const QString display = titleMetrics.elidedText(title, Qt::ElideRight, titleLimit);
    const int titleWidth = qMin(titleLimit, titleMetrics.horizontalAdvance(display) + 2);
    p.drawText(QRect(titleLeft, 0, titleWidth, height()), alignment, display);

    const int separator = titleLeft + titleWidth + foundation::gapNormal;
    const int topicLeft = separator + foundation::gapNormal;
    const int topicWidth = right - topicLeft;
    if (!m_topic.isEmpty() && topicWidth >= foundation::gapLoose) {
        p.setPen(QPen(discord::divider(), 1));
        p.drawLine(separator, height() / 2 - foundation::gapNormal,
                   separator, height() / 2 + foundation::gapNormal);
        p.setFont(discord::font(foundation::bodySize));
        p.setPen(discord::muted());
        p.drawText(QRect(topicLeft, 0, topicWidth, height()), alignment,
                   p.fontMetrics().elidedText(m_topic, Qt::ElideRight, topicWidth));
    }
}

// ---------------------------------------------------------------------------

namespace {
constexpr int kToastWidth = 340;
constexpr int kToastHeight = 72;
constexpr int kToastMargin = foundation::gapLoose;
constexpr int kToastIndicatorWidth = 3;
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
    move(qMax(kToastMargin, host->width() - width() - kToastMargin), kToastMargin);
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
    p.drawRoundedRect(card, foundation::radiusOverlay, foundation::radiusOverlay);

    const int accentHeight = height() - 2 * foundation::cardPadding;
    p.setPen(Qt::NoPen);
    p.setBrush(discord::accent());
    p.drawRoundedRect(QRectF(card.left() + foundation::gapTight, foundation::cardPadding,
                             kToastIndicatorWidth, accentHeight), 1.5, 1.5);

    const int left = foundation::cardPadding;
    const int textWidth = width() - left - foundation::cardPadding;

    QString title = m_title.isEmpty() ? QStringLiteral("新消息") : m_title;
    if (m_count > 1) title += QStringLiteral(" ×%1").arg(m_count);
    const QFont titleFont = discord::font(foundation::bodySize, QFont::DemiBold);
    const QFont bodyFont = discord::font(foundation::bodySize);
    const QFontMetrics titleMetrics(titleFont, this);
    const QFontMetrics bodyMetrics(bodyFont, this);
    const int gap = foundation::gapTight;
    const int top = (height() - titleMetrics.height() - gap - bodyMetrics.height()) / 2;
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
