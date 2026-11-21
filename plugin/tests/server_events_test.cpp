// Exercise the exported callbacks and real ModernChat delegate without starting
// the plugin, its UI installation, heartbeat, or a TeamSpeak connection.
#include "../src/plugin.cpp"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QTextLayout>
#include <QTemporaryFile>
#include <QtMath>
#include <QtTest>
#include <cstring>

namespace server_events_mock {
constexpr uint64 server = 101;
constexpr uint64 otherServer = 202;
constexpr anyID self = 7;
QHash<QString, QString> channels;
QHash<anyID, QString> clients;
QSet<void*> allocations;
int allocated = 0;
int freed = 0;
int invalidFrees = 0;
int errorCalls = 0;
unsigned int lastError = 0;
unsigned int errorResult = ERROR_ok;
QByteArray errorText;
bool nullError = false;
uint64 current = server;
int permissionCalls = 0;
uint64 permissionServer = 0;
unsigned int permissionId = 0;

uint64 currentServer() { return current; }

unsigned int permissionName(uint64 schid, unsigned int id, char* result, size_t capacity) {
    ++permissionCalls;
    permissionServer = schid;
    permissionId = id;
    const char name[] = "i_channel_modify_power";
    if (capacity < sizeof(name)) return ERROR_parameter_invalid;
    std::memcpy(result, name, sizeof(name));
    return ERROR_ok;
}

QString channelKey(uint64 schid, uint64 channel) {
    return QStringLiteral("%1:%2").arg(schid).arg(channel);
}

char* copy(const QByteArray& text) {
    auto* result = new char[size_t(text.size()) + 1];
    std::memcpy(result, text.constData(), size_t(text.size()) + 1);
    allocations.insert(result);
    ++allocated;
    return result;
}

unsigned int freeMemory(void* pointer) {
    if (!allocations.remove(pointer)) {
        ++invalidFrees;
        return ERROR_parameter_invalid;
    }
    delete[] static_cast<char*>(pointer);
    ++freed;
    return ERROR_ok;
}

unsigned int channelName(uint64 schid, uint64 channel, size_t flag, char** result) {
    *result = nullptr;
    const QString key = channelKey(schid, channel);
    if (flag != CHANNEL_NAME || !channels.contains(key)) return ERROR_channel_invalid_id;
    *result = copy(channels.value(key).toUtf8());
    return ERROR_ok;
}

unsigned int clientName(uint64, anyID id, char* result, size_t capacity) {
    if (!clients.contains(id)) return ERROR_client_invalid_id;
    const QByteArray name = clients.value(id).toUtf8();
    if (size_t(name.size()) + 1 > capacity) return ERROR_parameter_invalid;
    std::memcpy(result, name.constData(), size_t(name.size()) + 1);
    return ERROR_ok;
}

unsigned int clientId(uint64, anyID* result) {
    *result = self;
    return ERROR_ok;
}

unsigned int errorMessage(unsigned int error, char** result) {
    ++errorCalls;
    lastError = error;
    *result = nullptr;
    if (errorResult == ERROR_ok && !nullError) *result = copy(errorText);
    return errorResult;
}
} // namespace server_events_mock

class ServerEventsTest : public QObject {
    Q_OBJECT
    QTemporaryFile m_log;
    ModernChat* m_chat = nullptr;

    void drain() {
        // Flush callback MetaCalls before allowing any GUI layout events.
        QCoreApplication::sendPostedEvents(g_guard, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
    }

    void connectStatus(uint64 schid, int status, unsigned int error = ERROR_ok) {
        ts3plugin_onConnectStatusChangeEvent(schid, status, error);
        // CONNECTED/ESTABLISHED synchronously start the production burst timer.
        // Stop it before processing even one event, so heartbeat never runs.
        if (g_burstTimer) g_burstTimer->stop();
    }

    void checkSystem(const ChatMessage& message, const QString& text, int severity = 0) {
        QCOMPARE(message.text, text);
        QVERIFY(message.system);
        QVERIFY(!message.self);
        QVERIFY(message.sender.isEmpty());
        QCOMPARE(message.severity, severity);
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{2}:\\d{2}$")).match(message.time).hasMatch());
    }

    static ChatMessage systemMessage(const QString& text) {
        ChatMessage message;
        message.text = text;
        message.system = true;
        return message;
    }

    static int hintedHeight(ModernChat& chat, int width) {
        QStyleOptionViewItem option;
        option.initFrom(&chat);
        option.rect = QRect(0, 0, width, 2000);
        return chat.itemDelegate()->sizeHint(option, chat.model()->index(0, 0)).height();
    }

    int actualHeight(ModernChat& chat, int width) {
        chat.resize(width, 2000);
        drain();
        return chat.visualItemRect(chat.item(0)).height();
    }

    // Independent coordinates for a single-paragraph system row: centered,
    // 9pt YaHei, 14px sides, 4px top / 8px bottom, minimum height 30.
    QPoint systemTextPoint(int row, const QString& plain, int offset) const {
        QTextLayout layout(plain, QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(Qt::AlignHCenter);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal height = 0;
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(qMax(1, m_chat->viewport()->width() - 28));
            line.setPosition(QPointF(0, height));
            height += line.height();
        }
        layout.endLayout();
        const QRect rowRect = m_chat->visualItemRect(m_chat->item(row));
        const int textTop = qMax(30, qCeil(height) + 12) - 8 - qCeil(height);
        for (int i = 0; i < layout.lineCount(); ++i) {
            const QTextLine line = layout.lineAt(i);
            if (offset < line.textStart() + line.textLength() || i == layout.lineCount() - 1)
                return QPoint(14 + qRound(line.cursorToX(offset)),
                              rowRect.top() + textTop + qFloor(line.y() + line.height() / 2));
        }
        return QPoint(-1, -1);
    }

    void dragText(const QPoint& from, const QPoint& to) {
        QWidget* viewport = m_chat->viewport();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, from);
        // QTest::mouseMove(QWidget*) can omit held buttons on offscreen Qt.
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         QPointF(viewport->mapToGlobal(to)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &move);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, to);
        drain();
    }

    QImage renderedRow(int row) const {
        QImage image(m_chat->viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        m_chat->viewport()->render(&image);
        return image.copy(m_chat->visualItemRect(m_chat->item(row)));
    }

private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    }

    void init() {
        using namespace server_events_mock;
        QVERIFY(!g_guard);
        QVERIFY(!g_log);
        QVERIFY(!g_timer);
        QVERIFY(!g_stateTimer);
        QVERIFY(!g_burstTimer);
        QVERIFY(m_log.open());
        g_log = &m_log; // openLog() must never touch the user's plugin log.
        g_guard = new QObject;
        g_chatHistory.clear();
        g_connectionStatus.clear();
        g_clientNames.clear();
        g_chatScroll.clear();
        g_overlayKey = serverKey(server);
        g_notifyKey.clear();
        g_notifyTabName.clear();
        g_notifyMode = 0;
        g_infoShown = false;
        channels.clear();
        clients.clear();
        allocated = freed = invalidFrees = errorCalls = 0;
        lastError = 0;
        errorResult = ERROR_ok;
        errorText.clear();
        nullError = false;
        current = server;
        permissionCalls = 0;
        permissionServer = permissionId = 0;
        g_funcs = TS3Functions{};
        g_funcs.getCurrentServerConnectionHandlerID = currentServer;
        g_funcs.getPermissionNameByID = permissionName;
        g_funcs.getChannelVariableAsString = channelName;
        g_funcs.getClientDisplayName = clientName;
        g_funcs.getClientID = clientId;
        g_funcs.getErrorMessage = errorMessage;
        g_funcs.freeMemory = freeMemory;
        m_chat = new ModernChat;
        m_chat->resize(540, 520);
        g_modernChat = m_chat;
        m_chat->show();
        drain();
    }

    void cleanup() {
        using namespace server_events_mock;
        if (g_burstTimer) g_burstTimer->stop();
        drain(); // Also resets queueTreeRefresh()'s coalescing flag.
        const bool burstWasStopped = !g_burstTimer || !g_burstTimer->isActive();
        const bool noHeartbeat = !g_timer && !g_stateTimer && !g_installed && !g_panel;
        delete g_burstTimer.data();
        g_burstTimer = nullptr;
        g_burstTicks = 0;
        delete g_guard.data();
        g_guard = nullptr;
        delete m_chat;
        m_chat = nullptr;
        g_modernChat = nullptr;
        g_chatHistory.clear();
        g_connectionStatus.clear();
        g_clientNames.clear();
        g_chatScroll.clear();
        g_overlayKey.clear();
        g_notifyKey.clear();
        g_notifyTabName.clear();
        g_notifyMode = 0;
        g_funcs = TS3Functions{};
        g_log = nullptr;
        m_log.close();
        const int outstanding = allocations.size();
        for (void* pointer : allocations) delete[] static_cast<char*>(pointer);
        allocations.clear();
        channels.clear();
        clients.clear();
        QVERIFY(burstWasStopped);
        QVERIFY2(noHeartbeat, "The fixture must not initialize the plugin or run heartbeat");
        QCOMPARE(invalidFrees, 0);
        QCOMPARE(outstanding, 0);
        QCOMPARE(allocated, freed);
    }

    void channelPayloadIsCopiedBeforeQueue_data() {
        QTest::addColumn<bool>("created");
        QTest::addColumn<bool>("bySelf");
        QTest::newRow("create-self") << true << true;
        QTest::newRow("create-other") << true << false;
        QTest::newRow("edit-self") << false << true;
        QTest::newRow("edit-other") << false << false;
    }

    void channelPayloadIsCopiedBeforeQueue() {
        using namespace server_events_mock;
        QFETCH(bool, created);
        QFETCH(bool, bySelf);
        const anyID actorId = bySelf ? self : 42;
        const QString actor = bySelf ? QStringLiteral("自己甲") : QStringLiteral("他人乙");
        const QString channel = QStringLiteral("频道原名");
        QByteArray actorBuffer = (QStringLiteral("  ") + actor + QStringLiteral("  ")).toUtf8();
        char uidBuffer[] = "original-uid";
        channels[channelKey(server, 31)] = channel;
        if (created)
            ts3plugin_onNewChannelCreatedEvent(server, 31, 0, actorId, actorBuffer.data(), uidBuffer);
        else
            ts3plugin_onUpdateChannelEditedEvent(server, 31, actorId, actorBuffer.data(), uidBuffer);
        QVERIFY(g_chatHistory.isEmpty());
        QCOMPARE(m_chat->count(), 0);
        QCOMPARE(allocated, 1);
        QCOMPARE(freed, 1);
        actorBuffer.fill('X');
        std::memset(uidBuffer, 'Y', sizeof(uidBuffer) - 1);
        channels[channelKey(server, 31)] = QStringLiteral("排队后的新名字");
        clients[actorId] = QStringLiteral("排队后的操作者");
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 1);
        const QString expected = (created ? QStringLiteral("%1 创建了频道 %2")
                                          : QStringLiteral("%1 编辑了频道 %2")).arg(actor, channel);
        checkSystem(history.first(), expected);
        QCOMPARE(m_chat->count(), 1);
        QVERIFY(m_chat->isVisible());
        QCOMPARE(m_chat->rowPlainText(0), expected);
    }

    void channelActorAndIdFallbacks_data() {
        QTest::addColumn<int>("actorId");
        QTest::addColumn<QString>("callbackName");
        QTest::addColumn<QString>("displayName");
        QTest::addColumn<QString>("cachedName");
        QTest::addColumn<QString>("expectedActor");
        QTest::newRow("display-name") << 42 << QStringLiteral("  ") << QStringLiteral("显示名") << QString() << QStringLiteral("显示名");
        QTest::newRow("cached-name") << 42 << QString() << QString() << QStringLiteral("缓存名") << QStringLiteral("缓存名");
        QTest::newRow("user-id") << 42 << QString() << QString() << QString() << QStringLiteral("用户 #42");
        QTest::newRow("server-actor") << 0 << QString() << QString() << QString() << QStringLiteral("服务器");
        QTest::newRow("callback-wins") << 42 << QStringLiteral("回调名") << QStringLiteral("显示名") << QStringLiteral("缓存名") << QStringLiteral("回调名");
    }

    void channelActorAndIdFallbacks() {
        using namespace server_events_mock;
        QFETCH(int, actorId);
        QFETCH(QString, callbackName);
        QFETCH(QString, displayName);
        QFETCH(QString, cachedName);
        QFETCH(QString, expectedActor);
        if (!displayName.isEmpty()) clients[anyID(actorId)] = displayName;
        if (!cachedName.isEmpty()) g_clientNames[anyID(actorId)] = cachedName;
        const QByteArray buffer = callbackName.toUtf8();
        const char* name = callbackName.isNull() ? nullptr : buffer.constData();
        // Missing channel and successful-but-empty name both fall back to IDs.
        channels[channelKey(server, 901)] = QString();
        ts3plugin_onNewChannelCreatedEvent(server, 900, 0, anyID(actorId), name, nullptr);
        ts3plugin_onUpdateChannelEditedEvent(server, 901, anyID(actorId), name, nullptr);
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 2);
        checkSystem(history.at(0), expectedActor + QStringLiteral(" 创建了频道 #900"));
        checkSystem(history.at(1), expectedActor + QStringLiteral(" 编辑了频道 #901"));
        QCOMPARE(m_chat->count(), 2);
    }

    void serverHistoriesAndVisibleAppendAreIsolated() {
        using namespace server_events_mock;
        channels[channelKey(server, 31)] = QStringLiteral("服务器甲频道");
        channels[channelKey(otherServer, 31)] = QStringLiteral("服务器乙频道");
        ts3plugin_onNewChannelCreatedEvent(server, 31, 0, self, "Self", nullptr);
        ts3plugin_onUpdateChannelEditedEvent(otherServer, 31, 42, "Other", nullptr);
        drain();
        QCOMPARE(g_chatHistory.size(), 2);
        QCOMPARE(g_chatHistory.value(serverKey(server)).size(), 1);
        QCOMPARE(g_chatHistory.value(serverKey(otherServer)).size(), 1);
        QCOMPARE(m_chat->count(), 1);
        QCOMPARE(m_chat->rowPlainText(0), QStringLiteral("Self 创建了频道 服务器甲频道"));
        g_overlayKey = serverKey(otherServer);
        m_chat->setMessages(g_chatHistory.value(g_overlayKey));
        ts3plugin_onNewChannelCreatedEvent(server, 31, 0, self, "Self", nullptr);
        ts3plugin_onUpdateChannelEditedEvent(otherServer, 31, 42, "Other again", nullptr);
        drain();
        QCOMPARE(g_chatHistory.value(serverKey(server)).size(), 2);
        QCOMPARE(g_chatHistory.value(serverKey(otherServer)).size(), 2);
        QCOMPARE(m_chat->count(), 2);
        QCOMPARE(m_chat->rowPlainText(0), QStringLiteral("Other 编辑了频道 服务器乙频道"));
        QCOMPARE(m_chat->rowPlainText(1), QStringLiteral("Other again 编辑了频道 服务器乙频道"));
    }

    void serverTabOpensAtLatestAndPreservesManualReading() {
        using namespace server_events_mock;
        const uint64 channelId = 31;
        g_funcs.getChannelOfClient = [](uint64, anyID, uint64* result) -> unsigned int {
            *result = 31;
            return ERROR_ok;
        };
        channels[channelKey(server, channelId)] = QStringLiteral("Channel");

        const QString serverHistory = serverKey(server);
        const QString channelHistory = channelKey(server, channelId);
        const QString privateHistory = QStringLiteral("p:Peer");
        for (const QString& key : {serverHistory, channelHistory, privateHistory}) {
            for (int i = 0; i < 40; ++i)
                g_chatHistory[key].append(systemMessage(QStringLiteral("%1 message %2").arg(key).arg(i)));
        }

        // Use the production tab and info-page wiring, independent of heartbeat.
        g_modernChat = nullptr;
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* tabs = new QTabWidget(&host);
        tabs->setObjectName(QStringLiteral("ChatTabWidget"));
        tabs->addTab(new QWidget, QStringLiteral("Server"));
        tabs->addTab(new QWidget, QStringLiteral("Channel"));
        tabs->addTab(new QWidget, QStringLiteral("Peer"));
        layout->addWidget(tabs);
        auto* info = new QWidget(&host);
        info->setObjectName(QStringLiteral("InfoFrame"));
        host.resize(520, 330);
        host.show();
        installInfoTab();
        updateChatOverlay();
        drain();
        QVERIFY(g_modernChat && g_modernChat->isVisible());
        auto* scroll = g_modernChat->verticalScrollBar();
        QVERIFY(scroll->maximum() > 200);
        QCOMPARE(scroll->value(), scroll->maximum());
        QCOMPARE(g_modernChat->rowPlainText(39), serverHistory + QStringLiteral(" message 39"));

        scroll->setValue(80);
        appendSystemMessage(server, QStringLiteral("New server event"));
        updateChatOverlay();
        drain();
        QCOMPARE(scroll->value(), 80);
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          tabs->tabBar()->tabRect(0).center());
        drain();
        QCOMPARE(scroll->value(), scroll->maximum());
        QCOMPARE(g_modernChat->rowPlainText(40), QStringLiteral("New server event"));

        scroll->setValue(80);
        tabs->setCurrentIndex(1);
        drain();
        QCOMPARE(g_overlayKey, channelHistory);
        scroll->setValue(90);
        tabs->setCurrentIndex(2);
        drain();
        QCOMPARE(g_overlayKey, privateHistory);
        scroll->setValue(120);
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          tabs->tabBar()->tabRect(0).center());
        drain();
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(scroll->value(), scroll->maximum());

        for (bool useTab : {true, false}) {
            scroll->setValue(80);
            showInfoPage();
            QVERIFY(g_modernChat->isHidden());
            appendSystemMessage(server, QStringLiteral("Event behind info"));
            if (useTab)
                QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                                  tabs->tabBar()->tabRect(0).center());
            else
                QTest::mouseClick(g_infoBack, Qt::LeftButton);
            drain();
            QVERIFY(g_modernChat->isVisible());
            QCOMPARE(scroll->value(), scroll->maximum());
        }

        tabs->setCurrentIndex(1);
        drain();
        QCOMPARE(scroll->value(), 90);
        tabs->setCurrentIndex(2);
        drain();
        QCOMPARE(scroll->value(), 120);
    }

    void serverErrorsCopyPayloadAndReturnZero_data() {
        QTest::addColumn<bool>("tagged");
        QTest::newRow("null-return-code") << false;
        QTest::newRow("tagged-return-code") << true;
    }

    void serverErrorsCopyPayloadAndReturnZero() {
        using namespace server_events_mock;
        QFETCH(bool, tagged);
        QByteArray message("client is flooding");
        QByteArray extra = QStringLiteral("  请等待 12 秒，原因：切换状态过于频繁  ").toUtf8();
        QByteArray tag("plugin-request-1");
        QCOMPARE(unsigned(ERROR_client_is_flooding), 524u);
        QCOMPARE(ts3plugin_onServerErrorEvent(server, message.data(), 524,
                    tagged ? tag.data() : nullptr, extra.data()), 0);
        QVERIFY(g_chatHistory.isEmpty());
        QCOMPARE(m_chat->count(), 0);
        message.fill('X');
        extra.fill('Y');
        tag.fill('Z');
        drain();
        const QString expected = QStringLiteral("操作过于频繁，触发服务器防洪保护，请稍后再试：请等待 12 秒，原因：切换状态过于频繁（错误码 524）");
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 1);
        checkSystem(history.first(), expected, 2);
        QCOMPARE(m_chat->count(), 1);
        QCOMPARE(m_chat->rowPlainText(0), expected);
        QCOMPARE(errorCalls, 0);
        // Exercise the public context menu's Copy All, including system rows.
        QGuiApplication::clipboard()->setText(QStringLiteral("stale clipboard"));
        QTimer::singleShot(0, m_chat, [] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (menu) {
                menu->setActiveAction(menu->actions().last());
                QTest::keyClick(menu, Qt::Key_Return);
            }
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20),
                                m_chat->mapToGlobal(QPoint(20, 20)));
        QCoreApplication::sendEvent(m_chat->viewport(), &event);
        QCOMPARE(QGuiApplication::clipboard()->text(), expected);
    }

    void genericErrorsAndSdkFallbacks() {
        using namespace server_events_mock;
        QByteArray message = QStringLiteral("  修改频道失败  ").toUtf8();
        QByteArray extra = QStringLiteral("  保留原频道设置  ").toUtf8();
        const unsigned int error = 0x7abc;
        QCOMPARE(ts3plugin_onServerErrorEvent(server, message.data(), error, "tag", extra.data()), 0);
        message.fill('X');
        extra.fill('Y');
        errorText = "  SDK custom error  ";
        QCOMPARE(ts3plugin_onServerErrorEvent(server, nullptr, error, nullptr, nullptr), 0);
        errorText = "   ";
        QCOMPARE(ts3plugin_onServerErrorEvent(server, "  ", error, nullptr, nullptr), 0);
        QCOMPARE(errorCalls, 2);
        QCOMPARE(lastError, error);
        QCOMPARE(allocated, 2);
        QCOMPARE(freed, allocated);
        QVERIFY(g_chatHistory.isEmpty());
        errorText = "changed after queue";
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 3);
        checkSystem(history.at(0), QStringLiteral("修改频道失败：保留原频道设置（错误码 31420）"), 2);
        checkSystem(history.at(1), QStringLiteral("SDK custom error（错误码 31420）"), 2);
        checkSystem(history.at(2), QStringLiteral("未知错误（错误码 31420）"), 2);
        QCOMPARE(m_chat->count(), 3);
    }

    void successfulErrorsAreIgnored() {
        using namespace server_events_mock;
        for (const char* tag : {static_cast<const char*>(nullptr), "plugin-success"}) {
            QCOMPARE(ts3plugin_onServerErrorEvent(server, "success", ERROR_ok, tag, "ignore extra"), 0);
            QCOMPARE(ts3plugin_onServerPermissionErrorEvent(server, "success", ERROR_ok, tag, 42), 0);
        }
        drain();
        QVERIFY(g_chatHistory.isEmpty());
        QCOMPARE(m_chat->count(), 0);
        QCOMPARE(errorCalls, 0);
        QCOMPARE(permissionCalls, 0);
    }

    void permissionErrorsIncludeNameAndCode() {
        using namespace server_events_mock;
        QByteArray message = QStringLiteral("  权限不足，无法编辑频道  ").toUtf8();
        QCOMPARE(ts3plugin_onServerPermissionErrorEvent(server, message.data(),
                    ERROR_permissions_client_insufficient, "permission-request", 42), 0);
        QCOMPARE(permissionCalls, 1);
        QCOMPARE(permissionServer, server);
        QCOMPARE(permissionId, 42u);
        message.fill('X');
        QCOMPARE(ts3plugin_onServerPermissionErrorEvent(server, nullptr,
                    ERROR_permissions_client_insufficient, nullptr, 0), 0);
        QVERIFY(g_chatHistory.isEmpty());
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 2);
        checkSystem(history.at(0), QStringLiteral("权限不足，无法编辑频道（i_channel_modify_power）（错误码 2568）"), 2);
        checkSystem(history.at(1), QStringLiteral("权限不足（错误码 2568）"), 2);
        QCOMPARE(m_chat->count(), 2);
        QCOMPARE(permissionCalls, 1);
        QCOMPARE(errorCalls, 0);
    }

    void operationErrorsRespectVisibleConversation_data() {
        QTest::addColumn<QString>("key");
        QTest::addColumn<quint64>("activeServer");
        QTest::addColumn<bool>("visible");
        QTest::addColumn<bool>("mirror");
        QTest::newRow("server-once") << QStringLiteral("s:101") << quint64(server_events_mock::server) << true << false;
        QTest::newRow("same-channel") << QStringLiteral("101:31") << quint64(server_events_mock::server) << true << true;
        QTest::newRow("same-private") << QStringLiteral("p:Peer") << quint64(server_events_mock::server) << true << true;
        QTest::newRow("other-channel") << QStringLiteral("202:31") << quint64(server_events_mock::otherServer) << true << false;
        QTest::newRow("other-private") << QStringLiteral("p:Peer") << quint64(server_events_mock::otherServer) << true << false;
        QTest::newRow("hidden-channel") << QStringLiteral("101:31") << quint64(server_events_mock::server) << false << false;
        QTest::newRow("hidden-private") << QStringLiteral("p:Peer") << quint64(server_events_mock::server) << false << false;
    }

    void operationErrorsRespectVisibleConversation() {
        using namespace server_events_mock;
        QFETCH(QString, key);
        QFETCH(quint64, activeServer);
        QFETCH(bool, visible);
        QFETCH(bool, mirror);
        current = activeServer;
        g_overlayKey = key;
        m_chat->setVisible(visible);
        const QString expected = QStringLiteral("测试错误（错误码 524）");
        queueOperationError(server, expected);
        QVERIFY(g_chatHistory.isEmpty());
        QCOMPARE(m_chat->count(), 0);
        drain();
        const QString sourceKey = serverKey(server);
        const auto history = g_chatHistory.value(sourceKey);
        QCOMPARE(history.size(), 1);
        checkSystem(history.first(), expected, 2);
        QCOMPARE(g_chatHistory.size(), mirror ? 2 : 1);
        QCOMPARE(m_chat->count(), key == sourceKey || mirror ? 1 : 0);
        if (mirror) {
            QCOMPARE(g_chatHistory.value(key).size(), 1);
            checkSystem(g_chatHistory.value(key).first(), expected, 2);
        }
        if (m_chat->count()) QCOMPARE(m_chat->rowPlainText(0), expected);
        QVERIFY(!g_chatHistory.contains(serverKey(otherServer)));
    }

    void connectionErrors_data() {
        QTest::addColumn<unsigned int>("error");
        QTest::addColumn<QString>("reason");
        QTest::newRow("password") << unsigned(ERROR_server_invalid_password) << QStringLiteral("服务器密码错误");
        QTest::newRow("full") << unsigned(ERROR_server_maxclients_reached) << QStringLiteral("服务器人数已满");
        QTest::newRow("hostname") << unsigned(ERROR_could_not_resolve_hostname) << QStringLiteral("无法解析服务器域名");
        QTest::newRow("shutting-down") << unsigned(ERROR_server_is_shutting_down) << QStringLiteral("服务器正在关闭");
        QTest::newRow("initialisation") << unsigned(ERROR_failed_connection_initialisation) << QStringLiteral("无法连接到服务器，请检查服务器地址、端口和网络连接");
        QTest::newRow("lost") << unsigned(ERROR_connection_lost) << QStringLiteral("与服务器的连接丢失");
        QTest::newRow("handshake") << unsigned(ERROR_handshake_failed) << QStringLiteral("与服务器握手失败");
    }

    void connectionErrors() {
        using namespace server_events_mock;
        QFETCH(unsigned int, error);
        QFETCH(QString, reason);
        connectStatus(server, STATUS_CONNECTING);
        connectStatus(server, STATUS_DISCONNECTED, error);
        QVERIFY(g_chatHistory.isEmpty());
        QVERIFY(g_connectionStatus.isEmpty());
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 2);
        checkSystem(history.at(0), QStringLiteral("正在连接服务器…"));
        checkSystem(history.at(1), QStringLiteral("连接服务器失败：%1（错误码 %2）").arg(reason).arg(error), 2);
        QVERIFY(!g_connectionStatus.contains(server));
        QCOMPARE(errorCalls, 0);
        QCOMPARE(m_chat->rowPlainText(1), history.at(1).text);
    }

    void connectionErrorFallback_data() {
        QTest::addColumn<QString>("sdkText");
        QTest::addColumn<int>("mode"); // 0: success, 1: failure, 2: null, 3: absent API
        QTest::addColumn<QString>("reason");
        QTest::newRow("sdk-message") << QStringLiteral("  SDK 自定义错误  ") << 0 << QStringLiteral("SDK 自定义错误");
        QTest::newRow("empty-sdk-message") << QStringLiteral("  ") << 0 << QStringLiteral("未知错误");
        QTest::newRow("sdk-failed") << QString() << 1 << QStringLiteral("未知错误");
        QTest::newRow("sdk-null") << QString() << 2 << QStringLiteral("未知错误");
        QTest::newRow("no-sdk-api") << QString() << 3 << QStringLiteral("未知错误");
    }

    void connectionErrorFallback() {
        using namespace server_events_mock;
        QFETCH(QString, sdkText);
        QFETCH(int, mode);
        QFETCH(QString, reason);
        const unsigned int error = 0x7abc;
        errorText = sdkText.toUtf8();
        errorResult = mode == 1 ? ERROR_parameter_invalid : ERROR_ok;
        nullError = mode == 2;
        if (mode == 3) g_funcs.getErrorMessage = nullptr;
        connectStatus(server, STATUS_DISCONNECTED, error);
        QCOMPARE(errorCalls, mode == 3 ? 0 : 1);
        if (mode != 3) QCOMPARE(lastError, error);
        QCOMPARE(allocated, mode == 0 ? 1 : 0);
        QCOMPARE(freed, allocated);
        errorText = "changed after callback";
        drain();
        const auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 1);
        checkSystem(history.first(), QStringLiteral("连接服务器失败：%1（错误码 %2）").arg(reason).arg(error), 2);
    }

    void connectionLifecycleAndReconnectReset() {
        using namespace server_events_mock;
        connectStatus(server, STATUS_CONNECTING);
        connectStatus(server, STATUS_CONNECTED);
        QVERIFY(g_burstTimer);
        QVERIFY(!g_burstTimer->isActive());
        connectStatus(server, STATUS_CONNECTION_ESTABLISHING);
        connectStatus(server, STATUS_CONNECTION_ESTABLISHED);
        QVERIFY(!g_burstTimer->isActive());
        drain();
        QCOMPARE(g_connectionStatus.value(server), int(STATUS_CONNECTION_ESTABLISHED));
        auto history = g_chatHistory.value(serverKey(server));
        QCOMPARE(history.size(), 4);
        checkSystem(history.at(0), QStringLiteral("正在连接服务器…"));
        checkSystem(history.at(1), QStringLiteral("服务器已接受连接"));
        checkSystem(history.at(2), QStringLiteral("正在进入服务器…"));
        checkSystem(history.at(3), QStringLiteral("已连接到服务器"));
        // Another server's failure must not inherit this established state.
        connectStatus(otherServer, STATUS_DISCONNECTED, ERROR_connection_lost);
        connectStatus(server, STATUS_DISCONNECTED, ERROR_connection_lost);
        drain();
        checkSystem(g_chatHistory.value(serverKey(otherServer)).last(),
                    QStringLiteral("连接服务器失败：与服务器的连接丢失（错误码 %1）").arg(ERROR_connection_lost), 2);
        checkSystem(g_chatHistory.value(serverKey(server)).last(),
                    QStringLiteral("与服务器的连接已断开：与服务器的连接丢失（错误码 %1）").arg(ERROR_connection_lost), 2);
        QVERIFY(g_connectionStatus.isEmpty());
        connectStatus(server, STATUS_CONNECTING);
        connectStatus(server, STATUS_DISCONNECTED, ERROR_server_invalid_password);
        drain();
        checkSystem(g_chatHistory.value(serverKey(server)).last(),
                    QStringLiteral("连接服务器失败：服务器密码错误（错误码 %1）").arg(ERROR_server_invalid_password), 2);
        QVERIFY(g_connectionStatus.isEmpty());
        connectStatus(server, STATUS_CONNECTION_ESTABLISHED);
        drain();
        connectStatus(server, STATUS_DISCONNECTED);
        drain();
        checkSystem(g_chatHistory.value(serverKey(server)).last(), QStringLiteral("与服务器的连接已断开"));
        QVERIFY(g_connectionStatus.isEmpty());
        // A subsequent failure after a normal disconnect is also a fresh attempt.
        connectStatus(server, STATUS_DISCONNECTED, ERROR_could_not_resolve_hostname);
        drain();
        checkSystem(g_chatHistory.value(serverKey(server)).last(),
                    QStringLiteral("连接服务器失败：无法解析服务器域名（错误码 %1）").arg(ERROR_could_not_resolve_hostname), 2);
        QCOMPARE(m_chat->count(), g_chatHistory.value(serverKey(server)).size());
        QCOMPARE(g_burstTicks, 0);
    }

    void firstRealSystemMessageSurvivesChat_data() {
        QTest::addColumn<bool>("rebuild");
        QTest::newRow("addMessage") << false;
        QTest::newRow("setMessages") << true;
    }

    void firstRealSystemMessageSurvivesChat() {
        using namespace server_events_mock;
        QFETCH(bool, rebuild);
        ts3plugin_onNewChannelCreatedEvent(server, 31, 0, self, "Self", nullptr);
        drain();
        QCOMPARE(m_chat->count(), 1);
        const ChatMessage event = g_chatHistory.value(serverKey(server)).first();
        ChatMessage chat;
        chat.sender = QStringLiteral("普通用户");
        chat.text = QStringLiteral("第一条普通聊天");
        if (rebuild) m_chat->setMessages(QVector<ChatMessage>{event, chat});
        else m_chat->addMessage(chat);
        drain();
        QCOMPARE(m_chat->count(), 2);
        QCOMPARE(m_chat->rowPlainText(0), event.text);
        QVERIFY(m_chat->rowPlainText(1).contains(chat.text));
    }

    void systemRowDragCopiesWrappedTextAndPaintsSelection() {
        ChatMessage message = systemMessage(QStringLiteral(
            "created a channel with a long English description that wraps across several centered lines"));
        message.sender = QStringLiteral("ServerAdmin");
        const QString plain = message.sender + QStringLiteral("  ") + message.text;
        m_chat->resize(220, 520);
        m_chat->setMessages(QVector<ChatMessage>{message});
        m_chat->setFocus();
        drain();
        QCOMPARE(m_chat->rowPlainText(0), plain);
        const int from = 2;
        const int to = plain.size() - 3;
        const QPoint first = systemTextPoint(0, plain, from);
        const QPoint last = systemTextPoint(0, plain, to);
        QVERIFY2(last.y() > first.y(), "The drag must cross wrapped system text lines");
        QVERIFY(m_chat->visualItemRect(m_chat->item(0)).contains(first));
        QVERIFY(m_chat->visualItemRect(m_chat->item(0)).contains(last));
        const QImage unselected = renderedRow(0);
        for (bool reverse : {false, true}) {
            QGuiApplication::clipboard()->setText(QStringLiteral("stale clipboard"));
            dragText(reverse ? last : first, reverse ? first : last);
            int start = -1, end = -1;
            QVERIFY(m_chat->selectionRangeForRow(0, start, end));
            QCOMPARE(start, from);
            QCOMPARE(end, to);
            QVERIFY2(renderedRow(0) != unselected, "System selection must paint a visible highlight");
            QTest::keyClick(m_chat, Qt::Key_C, Qt::ControlModifier);
            QCOMPARE(QGuiApplication::clipboard()->text(), plain.mid(from, to - from));
            QTest::keyClick(m_chat, Qt::Key_Escape);
            drain();
            QVERIFY(!m_chat->selectionRangeForRow(0, start, end));
            QVERIFY2(renderedRow(0) == unselected, "Clearing selection must remove the highlight");
        }
    }

    void selectAllCopiesSystemAndOrdinaryRowsInOrder() {
        ChatMessage first = systemMessage(QStringLiteral("connected"));
        first.sender = QStringLiteral("Server");
        ChatMessage chat;
        chat.sender = QStringLiteral("Alice");
        chat.text = QStringLiteral("hello channel");
        ChatMessage middle = systemMessage(QStringLiteral("频道设置已更新"));
        ChatMessage reply;
        reply.sender = QStringLiteral("Me");
        reply.text = QStringLiteral("received");
        reply.self = true;
        ChatMessage last = systemMessage(QStringLiteral("finished"));
        last.sender = QStringLiteral("Administrator");
        const QStringList expected{QStringLiteral("Server  connected"), chat.text, middle.text,
                                   reply.text, QStringLiteral("Administrator  finished")};
        m_chat->setMessages(QVector<ChatMessage>{first, chat, middle, reply, last});
        drain();
        QTest::mouseClick(m_chat->viewport(), Qt::LeftButton, Qt::NoModifier,
                          m_chat->visualItemRect(m_chat->item(2)).center());
        QTest::keyClick(m_chat, Qt::Key_A, Qt::ControlModifier);
        for (int row = 0; row < expected.size(); ++row) {
            QCOMPARE(m_chat->rowPlainText(row), expected.at(row));
            int start = -1, end = -1;
            QVERIFY(m_chat->selectionRangeForRow(row, start, end));
            QCOMPARE(start, 0);
            QCOMPARE(end, expected.at(row).size());
        }
        QGuiApplication::clipboard()->setText(QStringLiteral("stale clipboard"));
        QTest::keyClick(m_chat, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), expected.join(QLatin1Char('\n')));
    }

    void systemRowDoubleClickSelectsEnglishWord() {
        ChatMessage message = systemMessage(QStringLiteral("channel Permission denied"));
        message.sender = QStringLiteral("ServerAdmin");
        message.severity = 2;
        const QString plain = message.sender + QStringLiteral("  ") + message.text;
        m_chat->setMessages(QVector<ChatMessage>{message});
        drain();
        // Test both the sender prefix and a body word, whose offsets include it.
        for (const QString& word : {message.sender, QStringLiteral("Permission")}) {
            const int wordStart = plain.indexOf(word);
            const QPoint point = systemTextPoint(0, plain, wordStart + 3);
            QTest::mouseDClick(m_chat->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseRelease(m_chat->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            int start = -1, end = -1;
            QVERIFY(m_chat->selectionRangeForRow(0, start, end));
            QCOMPARE(start, wordStart);
            QCOMPARE(end, wordStart + word.size());
            QGuiApplication::clipboard()->setText(QStringLiteral("stale clipboard"));
            QTest::keyClick(m_chat, Qt::Key_C, Qt::ControlModifier);
            QCOMPARE(QGuiApplication::clipboard()->text(), word);
            QTest::keyClick(m_chat, Qt::Key_Escape);
        }
    }

    void systemRowContextMenuCopiesWholeMessage() {
        ChatMessage message = systemMessage(QStringLiteral("updated channel permissions"));
        message.sender = QStringLiteral("ServerAdmin");
        message.severity = 1;
        const QString plain = message.sender + QStringLiteral("  ") + message.text;
        m_chat->setMessages(QVector<ChatMessage>{message});
        drain();
        const QPoint point = systemTextPoint(0, plain, plain.size() - 4);
        int start = -1, end = -1;
        QVERIFY(!m_chat->selectionRangeForRow(0, start, end));
        QGuiApplication::clipboard()->setText(QStringLiteral("stale clipboard"));
        bool found = false;
        bool enabled = false;
        QTimer::singleShot(0, m_chat, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) menu = m_chat->findChild<QMenu*>();
            if (!menu) return;
            for (QAction* action : menu->actions()) {
                if (action->text() != QStringLiteral("复制这条消息")) continue;
                found = true;
                enabled = action->isEnabled();
                if (enabled) {
                    menu->setActiveAction(action);
                    QTest::keyClick(menu, Qt::Key_Return);
                } else {
                    menu->close();
                }
                return;
            }
            menu->close();
        });
        QTest::mouseClick(m_chat->viewport(), Qt::RightButton, Qt::NoModifier, point);
        // QTest mouse events do not synthesize the platform context-menu event.
        QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                                m_chat->viewport()->mapToGlobal(point));
        QCoreApplication::sendEvent(m_chat->viewport(), &event);
        QVERIFY2(found, "The row-copy action must be present in the system-row menu");
        QVERIFY2(enabled, "The row-copy action must be enabled without a text selection");
        QCOMPARE(QGuiApplication::clipboard()->text(), plain);
    }

    void systemRowsResizeAndWrap_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("words") << QStringLiteral("A long server event should wrap and remain fully readable. ").repeated(12);
        QTest::newRow("chinese") << QStringLiteral("这是需要完整显示并随聊天窗口宽度变化的服务器系统事件。").repeated(16);
        QTest::newRow("unbroken-token") << QString(600, QLatin1Char('W'));
    }

    void systemRowsResizeAndWrap() {
        QFETCH(QString, text);
        ModernChat chat;
        chat.addMessage(systemMessage(text));
        chat.show();
        const int narrowHint = hintedHeight(chat, 220);
        const int wideHint = hintedHeight(chat, 980);
        QVERIFY(wideHint >= 30);
        QVERIFY2(narrowHint > wideHint, "System rows must wrap, including text without spaces");
        const int narrow = actualHeight(chat, 220);
        QCOMPARE(narrow, hintedHeight(chat, chat.viewport()->width()));
        const int wide = actualHeight(chat, 980);
        QCOMPARE(wide, hintedHeight(chat, chat.viewport()->width()));
        QVERIFY2(narrow > wide, "Visible row height must shrink when the chat gets wider");
        const int narrowAgain = actualHeight(chat, 220);
        QCOMPARE(narrowAgain, hintedHeight(chat, chat.viewport()->width()));
        QCOMPARE(narrowAgain, narrow);
        QVERIFY2(narrowAgain > wide, "Visible row height must grow again when the chat gets narrower");
        QCOMPARE(chat.rowPlainText(0), text);
    }

    void systemRowsHonorExplicitNewlines_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("three-lines") << QStringLiteral("alpha\nbeta\ngamma");
        QTest::newRow("blank-line") << QStringLiteral("alpha\n\nbeta");
        QTest::newRow("trailing-newline") << QStringLiteral("alpha\nbeta\n");
    }

    void systemRowsHonorExplicitNewlines() {
        QFETCH(QString, text);
        ModernChat chat;
        chat.addMessage(systemMessage(QStringLiteral("alpha")));
        chat.show();
        const int single = hintedHeight(chat, 600);
        QVERIFY(single >= 30);
        chat.setMessages(QVector<ChatMessage>{systemMessage(text)});
        const int multiple = hintedHeight(chat, 600);
        QVERIFY2(multiple > single + QFontMetrics(QFont(QStringLiteral("Microsoft YaHei UI"), 9)).height(),
                 "Explicit and empty paragraphs must reserve their own lines");
        QCOMPARE(actualHeight(chat, 600), hintedHeight(chat, chat.viewport()->width()));
        QCOMPARE(chat.rowPlainText(0), text);
    }
};

QTEST_MAIN(ServerEventsTest)
#include "server_events_test.moc"
