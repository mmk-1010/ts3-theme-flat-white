// Exercise the production self-state bridge and callbacks without initializing
// the plugin, heartbeat, or a TeamSpeak connection.
#include "../src/plugin.cpp"

#include <QHelpEvent>
#include <QSet>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QToolTip>
#include <QtTest>
#include <cstdlib>
#include <cstring>

namespace self_state_mock {
constexpr uint64 server = 101;
constexpr uint64 otherServer = 202;
constexpr anyID self = 7; // Deliberately the same client ID on both servers.
constexpr uint64 channel = 31;
const int flags[] = {CLIENT_AWAY, CLIENT_INPUT_MUTED, CLIENT_OUTPUT_MUTED};
const int enabled[] = {AWAY_ZZZ, MUTEINPUT_MUTED, MUTEOUTPUT_MUTED};

uint64 current = server;
// Successful sends leave the SDK getter stale until a test explicitly updates it.
QHash<uint64, QHash<int, int>> polled;
QHash<uint64, QHash<int, int>> staged;
struct SetCall { uint64 server; int flag; int value; };
QVector<SetCall> setters;
int flushes = 0;
int reads = 0;
unsigned int setterResult = ERROR_ok;
unsigned int flushResult = ERROR_ok;
int returnCodeCalls = 0;
QByteArray returnCodePlugin;
QByteArray flushedReturnCode;
bool flushedNull = true;
size_t returnCodeCapacity = 0;
QSet<void*> allocations;
int invalidFrees = 0;

void* allocate(size_t size) {
    void* pointer = std::malloc(size);
    allocations.insert(pointer);
    return pointer;
}

char* copy(const char* text) {
    const size_t size = std::strlen(text) + 1;
    auto* result = static_cast<char*>(allocate(size));
    std::memcpy(result, text, size);
    return result;
}

unsigned int freeMemory(void* pointer) {
    if (!allocations.remove(pointer)) {
        ++invalidFrees;
        return ERROR_parameter_invalid;
    }
    std::free(pointer);
    return ERROR_ok;
}

uint64 currentServer() { return current; }

unsigned int clientId(uint64, anyID* result) {
    *result = self;
    return ERROR_ok;
}

unsigned int clientName(uint64, anyID id, char* result, size_t capacity) {
    const char name[] = "Self";
    if (id != self || capacity < sizeof(name)) return ERROR_parameter_invalid;
    std::memcpy(result, name, sizeof(name));
    return ERROR_ok;
}

unsigned int serverName(uint64, size_t flag, char** result) {
    *result = nullptr;
    if (flag != VIRTUALSERVER_NAME) return ERROR_parameter_invalid;
    *result = copy("Server");
    return ERROR_ok;
}

unsigned int channelName(uint64, uint64 id, size_t flag, char** result) {
    *result = nullptr;
    if (id != channel || flag != CHANNEL_NAME) return ERROR_parameter_invalid;
    *result = copy("Channel");
    return ERROR_ok;
}

unsigned int channelOfClient(uint64, anyID, uint64* result) {
    *result = channel;
    return ERROR_ok;
}

unsigned int channelList(uint64, uint64** result) {
    auto* list = static_cast<uint64*>(allocate(2 * sizeof(uint64)));
    list[0] = channel;
    list[1] = 0;
    *result = list;
    return ERROR_ok;
}

unsigned int clientList(uint64, anyID** result) {
    auto* list = static_cast<anyID*>(allocate(2 * sizeof(anyID)));
    list[0] = self;
    list[1] = 0;
    *result = list;
    return ERROR_ok;
}

unsigned int clientInt(uint64 schid, anyID id, size_t flag, int* result) {
    ++reads;
    if (id != self) return ERROR_client_invalid_id;
    *result = polled.value(schid).value(int(flag), 0);
    return ERROR_ok;
}

unsigned int setSelfInt(uint64 schid, size_t flag, int value) {
    setters.append({schid, int(flag), value});
    if (setterResult != ERROR_ok) return setterResult;
    staged[schid][int(flag)] = value;
    return ERROR_ok;
}

void createReturnCode(const char* pluginId, char* result, size_t capacity) {
    ++returnCodeCalls;
    returnCodePlugin = pluginId;
    returnCodeCapacity = capacity;
    const char code[] = "mock-plugin-receipt";
    if (capacity >= sizeof(code)) std::memcpy(result, code, sizeof(code));
}

unsigned int flush(uint64, const char* returnCode) {
    ++flushes;
    flushedNull = returnCode == nullptr;
    flushedReturnCode = returnCode ? QByteArray(returnCode) : QByteArray();
    return flushResult;
}
} // namespace self_state_mock

class SelfStateTest : public QObject {
    Q_OBJECT
    QTemporaryFile m_log;
    QStandardItemModel m_model;
    QModelIndex m_selfIndex;
    int m_changes = 0;

    void drain() {
        QCoreApplication::sendPostedEvents(g_guard, QEvent::MetaCall);
        QCoreApplication::processEvents();
    }

    QPoint buttonCenter(int button) const {
        // UserPanel::buttonRect(): 6px card inset, 10px padding, 24px
        // buttons and 2px gaps. Use the public widget geometry for hit testing.
        return QPoint(g_userPanel->width() - 6 - 10 - (3 * 24 + 2 * 2) + button * 26 + 12,
                      (g_userPanel->height() - 24) / 2 + 12);
    }

    void click(int button) {
        QTest::mouseClick(g_userPanel, Qt::LeftButton, Qt::NoModifier, buttonCenter(button), 0);
    }

    QString tooltip(int button) {
        const QPoint point = buttonCenter(button);
        QHelpEvent event(QEvent::ToolTip, point, g_userPanel->mapToGlobal(point));
        QCoreApplication::sendEvent(g_userPanel, &event);
        const QString text = QToolTip::text();
        QToolTip::hideText();
        return text;
    }

    void checkPanel(bool away, bool input, bool output) {
        const bool values[] = {away, input, output};
        const char* on[] = {"取消离开", "取消麦克风静音", "取消耳机静音"};
        const char* off[] = {"设为离开", "静音麦克风", "静音耳机"};
        for (int i = 0; i < 3; ++i)
            QCOMPARE(tooltip(i), QString::fromUtf8(values[i] ? on[i] : off[i]));
    }

    QString status() const { return g_treeRows->clientStatus(m_selfIndex); }

    QString activeStatus(int button) const {
        const char* texts[] = {"状态：离开", "状态：闭麦", "状态：扬声器静音"};
        return QString::fromUtf8(texts[button]);
    }

    void checkButton(int button, bool active) {
        checkPanel(button == 0 && active, button == 1 && active, button == 2 && active);
    }

    void refreshBoth() {
        g_treeRows->refresh();
        g_userPanel->refresh();
    }

    void checkStalePolls(int button, bool active) {
        const QString expected = active ? activeStatus(button) : QStringLiteral("状态：在线");
        g_treeRows->refreshStates();
        QCOMPARE(status(), expected);
        checkButton(button, active);
        g_treeRows->refresh();
        QCOMPARE(status(), expected);
        g_userPanel->refresh();
        QCOMPARE(status(), expected);
        checkButton(button, active);
    }

private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    }

    void init() {
        using namespace self_state_mock;
        QVERIFY(!g_guard && !g_treeRows && !g_userPanel && !g_log);
        QVERIFY(!g_timer && !g_stateTimer && !g_burstTimer && !g_installed);
        QVERIFY(m_log.open());
        g_log = &m_log; // Prevent openLog() from accessing the real plugin log.
        current = server;
        polled.clear();
        staged.clear();
        setters.clear();
        flushes = reads = invalidFrees = m_changes = 0;
        setterResult = flushResult = ERROR_ok;
        returnCodeCalls = 0;
        returnCodePlugin.clear();
        flushedReturnCode.clear();
        flushedNull = true;
        returnCodeCapacity = 0;
        g_pluginId.clear();
        g_chatHistory.clear();
        g_overlayKey.clear();
        QVERIFY(allocations.isEmpty());
        g_funcs = TS3Functions{};
        g_funcs.getCurrentServerConnectionHandlerID = currentServer;
        g_funcs.getClientID = clientId;
        g_funcs.getClientDisplayName = clientName;
        g_funcs.getServerVariableAsString = serverName;
        g_funcs.getChannelVariableAsString = channelName;
        g_funcs.getChannelOfClient = channelOfClient;
        g_funcs.getChannelList = channelList;
        g_funcs.getClientList = clientList;
        g_funcs.getClientVariableAsInt = clientInt;
        g_funcs.setClientSelfVariableAsInt = setSelfInt;
        g_funcs.flushClientSelfUpdates = flush;
        g_funcs.createReturnCode = createReturnCode;
        g_funcs.freeMemory = freeMemory;
        g_guard = new QObject;
        g_treeRows = new NativeTreeRows(g_guard);
        g_treeRows->setFunctions(g_funcs);
        g_userPanel = new UserPanel;
        g_userPanel->setFunctions(g_funcs);
        g_userPanel->setStateChangeHandler([this](quint64 schid, unsigned short id, int flag, bool value) {
            ++m_changes;
            applySelfState(schid, id, flag, value, true);
        });
        auto* serverItem = new QStandardItem(QStringLiteral("Server"));
        auto* channelItem = new QStandardItem(QStringLiteral("Channel"));
        auto* selfItem = new QStandardItem(QStringLiteral("[Member] Self"));
        m_model.appendRow(serverItem);
        serverItem->appendRow(channelItem);
        channelItem->appendRow(selfItem);
        m_selfIndex = selfItem->index();
        refreshBoth();
        QVERIFY(!g_treeRows->isClientRow(serverItem->index()));
        QVERIFY(!g_treeRows->isClientRow(channelItem->index()));
        QVERIFY(g_treeRows->isClientRow(m_selfIndex));
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        g_userPanel->resize(g_userPanel->sizeHint());
        g_userPanel->show();
        drain();
        checkPanel(false, false, false);
    }

    void cleanup() {
        using namespace self_state_mock;
        drain();
        const bool noHeartbeat = !g_timer && !g_stateTimer && !g_burstTimer && !g_installed && !g_panel;
        QToolTip::hideText();
        delete g_userPanel.data();
        g_userPanel = nullptr;
        delete g_guard.data(); // Owns the delegate and queued callback context.
        g_guard = nullptr;
        g_treeRows = nullptr;
        g_modernChat = nullptr;
        g_chatHistory.clear();
        g_overlayKey.clear();
        g_pluginId.clear();
        m_selfIndex = QModelIndex();
        m_model.clear();
        g_funcs = TS3Functions{};
        g_log = nullptr;
        m_log.close();
        const int outstanding = allocations.size();
        for (void* pointer : allocations) std::free(pointer);
        allocations.clear();
        QVERIFY2(noHeartbeat, "The fixture must not initialize the plugin or start heartbeat");
        QCOMPARE(outstanding, 0);
        QCOMPARE(invalidFrees, 0);
    }

    void clicksSynchronizeBeforeReturning_data() {
        QTest::addColumn<int>("button");
        QTest::newRow("away") << 0;
        QTest::newRow("microphone") << 1;
        QTest::newRow("speaker") << 2;
    }

    void clicksSynchronizeBeforeReturning() {
        using namespace self_state_mock;
        QFETCH(int, button);
        click(button);
        // No event draining or polling between mouseClick() and this assertion.
        QCOMPARE(status(), activeStatus(button));
        QCOMPARE(m_changes, 1);
        QCOMPARE(setters.size(), 1);
        QCOMPARE(setters.first().server, server);
        QCOMPARE(setters.first().flag, flags[button]);
        QCOMPARE(setters.first().value, enabled[button]);
        QCOMPARE(flushes, 1);
        QCOMPARE(polled.value(server).value(flags[button]), 0);
        checkButton(button, true);
        checkStalePolls(button, true);
    }

    void rapidToggleRejectsReverseOldCallbacks_data() {
        clicksSynchronizeBeforeReturning_data();
    }

    void rapidToggleRejectsReverseOldCallbacks() {
        using namespace self_state_mock;
        QFETCH(int, button);
        click(button); // SDK still says off.
        QCOMPARE(status(), activeStatus(button));
        ts3plugin_onClientSelfVariableUpdateEvent(server, flags[button], "0", "1");
        click(button); // Must toggle the protected on value, not the stale getter.
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        // Deliver the earlier on callback after the newer off click.
        drain();
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        checkButton(button, false);
        ts3plugin_onClientSelfVariableUpdateEvent(server, flags[button], "1", "0");
        drain();
        polled[server][flags[button]] = enabled[button]; // Now the getter trails in the other direction.
        checkStalePolls(button, false);
        click(button);
        QCOMPARE(status(), activeStatus(button));
        ts3plugin_onClientSelfVariableUpdateEvent(server, flags[button], "1", "0");
        drain();
        QCOMPARE(status(), activeStatus(button));
        polled[server][flags[button]] = 0;
        checkStalePolls(button, true);
        QCOMPARE(m_changes, 3);
        QCOMPARE(setters.size(), 3);
        QCOMPARE(setters.at(0).value, enabled[button]);
        QCOMPARE(setters.at(1).value, 0);
        QCOMPARE(setters.at(2).value, enabled[button]);
        QCOMPARE(flushes, 3);
    }

    void selfVariableCopiesNewValueBeforeQueue() {
        using namespace self_state_mock;
        QByteArray oldValues[3] = {"0", "0", "0"};
        QByteArray newValues[3] = {"1", "1", "1"};
        for (int i = 0; i < 3; ++i) {
            ts3plugin_onClientSelfVariableUpdateEvent(server, flags[i], oldValues[i].constData(), newValues[i].constData());
            oldValues[i][0] = '1';
            newValues[i][0] = '0';
        }
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        drain();
        const QString expected = QStringLiteral("状态：离开 · 闭麦 · 扬声器静音");
        QCOMPARE(status(), expected);
        checkPanel(true, true, true);
        g_treeRows->refreshStates();
        QCOMPARE(status(), expected);
        refreshBoth();
        QCOMPARE(status(), expected);
        checkPanel(true, true, true);
        QCOMPARE(m_changes, 0);
        QVERIFY(setters.isEmpty());
        QCOMPARE(flushes, 0);
    }

    void otherServerEventsCannotPolluteCurrentState() {
        using namespace self_state_mock;
        click(0);
        QCOMPARE(status(), activeStatus(0));
        const int before = reads;
        ts3plugin_onClientSelfVariableUpdateEvent(otherServer, CLIENT_AWAY, "1", "0");
        ts3plugin_onClientSelfVariableUpdateEvent(otherServer, CLIENT_INPUT_MUTED, "0", "1");
        ts3plugin_onClientSelfVariableUpdateEvent(otherServer, CLIENT_OUTPUT_MUTED, "0", "1");
        drain();
        QCOMPARE(reads, before);
        QCOMPARE(status(), activeStatus(0));
        checkPanel(true, false, false);
        checkStalePolls(0, true);
        QCOMPARE(m_changes, 1);
        QCOMPARE(setters.size(), 1);
    }

    void failedWritesPreserveState_data() {
        QTest::addColumn<bool>("failFlush");
        QTest::newRow("setter-failure") << false;
        QTest::newRow("flush-failure-rolls-back-staged-setter") << true;
    }

    void failedWritesPreserveState() {
        using namespace self_state_mock;
        QFETCH(bool, failFlush);
        struct ErrorCall { quint64 server; int flag; unsigned int code; };
        QVector<ErrorCall> errors;
        ModernChat chat;
        chat.resize(540, 520);
        chat.show();
        g_modernChat = &chat;
        g_overlayKey = serverKey(server);
        g_userPanel->setErrorHandler([&](quint64 schid, int flag, unsigned int error) {
            errors.append({schid, flag, error});
            queueOperationError(schid, operationErrorText(error));
        });
        for (bool initial : {false, true}) {
            polled[server][CLIENT_INPUT_MUTED] = initial ? MUTEINPUT_MUTED : MUTEINPUT_NONE;
            staged[server][CLIENT_INPUT_MUTED] = polled[server][CLIENT_INPUT_MUTED];
            refreshBoth();
            setters.clear();
            flushes = 0;
            setterResult = failFlush ? ERROR_ok : 524u;
            flushResult = failFlush ? 524u : ERROR_ok;
            errors.clear();
            g_chatHistory.clear();
            chat.clear();
            click(1);
            QCOMPARE(errors.size(), 1);
            QCOMPARE(errors.first().server, server);
            QCOMPARE(errors.first().flag, int(CLIENT_INPUT_MUTED));
            QCOMPARE(errors.first().code, 524u);
            QVERIFY(g_chatHistory.isEmpty());
            QCOMPARE(chat.count(), 0);
            QCOMPARE(status(), initial ? activeStatus(1) : QStringLiteral("状态：在线"));
            checkButton(1, initial);
            QCOMPARE(m_changes, 0);
            QCOMPARE(setters.size(), failFlush ? 2 : 1);
            QCOMPARE(flushes, failFlush ? 1 : 0);
            QCOMPARE(setters.first().server, server);
            QCOMPARE(setters.first().flag, int(CLIENT_INPUT_MUTED));
            QCOMPARE(setters.first().value, initial ? int(MUTEINPUT_NONE) : int(MUTEINPUT_MUTED));
            if (failFlush) {
                QCOMPARE(setters.last().server, server);
                QCOMPARE(setters.last().flag, int(CLIENT_INPUT_MUTED));
                QCOMPARE(setters.last().value, polled[server][CLIENT_INPUT_MUTED]);
            }
            QCOMPARE(staged[server][CLIENT_INPUT_MUTED], polled[server][CLIENT_INPUT_MUTED]);
            checkStalePolls(1, initial);
            drain();
            QCOMPARE(errors.size(), 1);
            const auto history = g_chatHistory.value(serverKey(server));
            QCOMPARE(history.size(), 1);
            const QString expected = QStringLiteral("操作过于频繁，触发服务器防洪保护，请稍后再试（错误码 524）");
            QCOMPARE(history.first().text, expected);
            QVERIFY(history.first().system);
            QVERIFY(!history.first().self);
            QCOMPARE(history.first().severity, 2); // The real delegate's red error role.
            QCOMPARE(chat.count(), 1);
            QCOMPARE(chat.rowPlainText(0), expected);
        }
        g_userPanel->setErrorHandler({});
    }

    void pluginIdIsCopiedAndFlushUsesReturnCode() {
        using namespace self_state_mock;
        click(1);
        QCOMPARE(returnCodeCalls, 0);
        QVERIFY(flushedNull);
        QByteArray id("original-plugin-id");
        ts3plugin_registerPluginID(id.data());
        id.fill('X');
        QCOMPARE(g_pluginId, QByteArray("original-plugin-id"));
        drain();
        click(1);
        QCOMPARE(returnCodeCalls, 1);
        QCOMPARE(returnCodePlugin, QByteArray("original-plugin-id"));
        QCOMPARE(returnCodeCapacity, size_t(128));
        QVERIFY(!flushedNull);
        QCOMPARE(flushedReturnCode, QByteArray("mock-plugin-receipt"));
        QCOMPARE(ts3plugin_onServerErrorEvent(server, "success", ERROR_ok,
                    flushedReturnCode.constData(), nullptr), 0);
        drain();
        QVERIFY(g_chatHistory.isEmpty());
        ts3plugin_registerPluginID(nullptr);
        drain();
        click(1);
        QCOMPARE(returnCodeCalls, 1);
        QVERIFY(flushedNull);
        QCOMPARE(flushes, 3);
        QCOMPARE(m_changes, 3);
    }

    void serverSwitchClearsOldProtection() {
        using namespace self_state_mock;
        click(0);
        click(1);
        click(2);
        QCOMPARE(status(), QStringLiteral("状态：离开 · 闭麦 · 扬声器静音"));
        ts3plugin_onClientSelfVariableUpdateEvent(server, CLIENT_AWAY, "0", "1");
        current = otherServer;
        refreshBoth();
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        checkPanel(false, false, false);
        drain(); // The queued event belongs to the previous server.
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        checkPanel(false, false, false);
        click(1);
        QCOMPARE(status(), activeStatus(1));
        QCOMPARE(setters.last().server, otherServer);
        current = server;
        refreshBoth(); // Same client ID and names must not resurrect either server's override.
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        checkPanel(false, false, false);
        g_treeRows->refreshStates();
        QCOMPARE(status(), QStringLiteral("状态：在线"));
    }

    void protectionExpiresAndPollReconciles() {
        using namespace self_state_mock;
        SelfStateChanges changes;
        changes.bind(server, self);
        QVERIFY(changes.set(CLIENT_INPUT_MUTED, true, true));
        QVERIFY(!changes.set(CLIENT_INPUT_MUTED, false, false));
        QVERIFY(changes.resolve(CLIENT_INPUT_MUTED, false));
        click(1);
        QCOMPARE(status(), activeStatus(1));
        checkStalePolls(1, true);
        QTest::qWait(1250); // The only wall-clock expiry test (production protection is 1200ms).
        QVERIFY(!changes.resolve(CLIENT_INPUT_MUTED, false));
        QVERIFY(changes.set(CLIENT_INPUT_MUTED, false, false));
        g_treeRows->refreshStates();
        QCOMPARE(status(), QStringLiteral("状态：在线"));
        g_userPanel->refresh();
        checkPanel(false, false, false);
        g_treeRows->refresh();
        QCOMPARE(status(), QStringLiteral("状态：在线"));
    }
};

QTEST_MAIN(SelfStateTest)
#include "self_state_test.moc"
