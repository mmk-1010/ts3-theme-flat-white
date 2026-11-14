// Compile the production helper in this translation unit, including its Show filter.
// Do not copy stabilizeVolumeDialog(): these assertions must exercise src/plugin.cpp.
#include "../src/plugin.cpp"

#include <QDialog>
#include <QFontDatabase>
#include <QLayout>
#include <QSignalSpy>
#include <QSlider>
#include <QtTest>

static QString volumeHint(int db) {
    if (db <= 0)
        return QStringLiteral("零和負值的音量比較不有雜訊和破音，為了比較好的音頻質量，比較建議使用。");
    if (db <= 6)
        return QStringLiteral("正值會放大音量，但也可能放大雜訊。");
    return QStringLiteral("音量高於 6 dB 可能導致雜訊和破音，請降低音量以獲得比較好的音頻質量，並注意保護聽力。");
}

// moc gives this fixture the exact native metaObject()->className().
class UserVolumeWidget : public QDialog {
    Q_OBJECT
public:
    explicit UserVolumeWidget(int initialDb = 0, const QFont& font = QFont(),
                              int verticalMargin = 0, int initialHintHeight = 0) {
        setFont(font);
        auto* box = new QVBoxLayout(this);
        box->setContentsMargins(12, 12, 12, 12);
        box->setSizeConstraint(QLayout::SetFixedSize);
        auto* title = new QLabel(QStringLiteral("Native client volume"), this);
        box->addWidget(title);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-40, 20);
        slider->setMinimumWidth(220);
        slider->setValue(initialDb);
        box->addWidget(slider);
        valueLabel = new QLabel(this);
        box->addWidget(valueLabel);
        hint = new QLabel(this);
        hint->setObjectName(QStringLiteral("hintLabel"));
        hint->setContentsMargins(0, verticalMargin, 0, verticalMargin);
        if (initialHintHeight) hint->setMinimumHeight(initialHintHeight);
        box->addWidget(hint);
        updateVolume(initialDb);
        connect(slider, &QSlider::valueChanged, this, [this](int db) { updateVolume(db); });
        layout()->activate();
        adjustSize();
    }

    void updateVolume(int db) {
        valueLabel->setText(QStringLiteral("%1 dB").arg(db));
        hint->setText(volumeHint(db));
    }

    QSlider* slider;
    QLabel* valueLabel;
    QLabel* hint;
};

class OtherVolumeWidget : public UserVolumeWidget {
    Q_OBJECT
};

static void settle(QWidget& window) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents();
    if (window.layout()) window.layout()->activate();
    QCoreApplication::processEvents();
}

class VolumeDialogTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        const QStringList families = QFontDatabase().families();
        QVERIFY2(families.contains(QStringLiteral("Segoe UI")), "Real system fonts must load in offscreen mode");
        QVERIFY(families.contains(QStringLiteral("Microsoft YaHei UI")));
        QVERIFY(QFontMetrics(QFont(QStringLiteral("Segoe UI"), 16)).lineSpacing() >
                QFontMetrics(QFont(QStringLiteral("Segoe UI"), 9)).lineSpacing());
    }

    void fixtureReproducesNativeResize() {
        UserVolumeWidget window;
        QCOMPARE(QString::fromLatin1(window.metaObject()->className()), QStringLiteral("UserVolumeWidget"));
        QCOMPARE(window.layout()->sizeConstraint(), QLayout::SetFixedSize);
        QVERIFY(!window.hint->wordWrap());
        window.show();
        settle(window);
        const QSize low = window.size();
        window.slider->setValue(10);
        settle(window);
        const QSize high = window.size();
        QVERIFY2(low != high, "Control fixture must reproduce the unpatched threshold resize");
        window.slider->setValue(0);
        settle(window);
        QCOMPARE(window.size(), low);
    }

    void thresholdsAndContinuousUpdates_data() {
        QTest::addColumn<QString>("family");
        QTest::addColumn<int>("pointSize");
        QTest::addColumn<int>("initialDb");
        QTest::addColumn<int>("margin");
        QTest::addColumn<int>("initialHeight");
        QTest::newRow("default-low") << QStringLiteral("Microsoft YaHei UI") << 9 << -10 << 0 << 0;
        QTest::newRow("default-medium") << QStringLiteral("Microsoft YaHei UI") << 9 << 3 << 0 << 0;
        QTest::newRow("initial-high") << QStringLiteral("Microsoft YaHei UI") << 9 << 12 << 0 << 0;
        QTest::newRow("large-font-margins") << QStringLiteral("Segoe UI") << 16 << 12 << 5 << 0;
        QTest::newRow("existing-tall-hint") << QStringLiteral("Microsoft YaHei UI") << 12 << 0 << 3 << 120;
    }

    void thresholdsAndContinuousUpdates() {
        QFETCH(QString, family);
        QFETCH(int, pointSize);
        QFETCH(int, initialDb);
        QFETCH(int, margin);
        QFETCH(int, initialHeight);
        UserVolumeWidget window(initialDb, QFont(family, pointSize), margin, initialHeight);
        const int initialWidth = window.hint->width();
        const int minimumHeight = qMax(window.hint->height(),
                                      window.hint->fontMetrics().lineSpacing() * 3 + margin * 2);
        // Exercise the real production Show-event integration, not just a direct call.
        FrameFilter filter;
        window.installEventFilter(&filter);
        window.show();
        settle(window);
        QVERIFY(window.hint->wordWrap());
        QVERIFY(window.hint->property("flatWhiteStableHint").toBool());
        QCOMPARE(window.hint->width(), initialWidth);
        QCOMPARE(window.hint->height(), minimumHeight);
        QCOMPARE(window.hint->minimumSize(), window.hint->maximumSize());
        const QSize stableSize = window.size();
        const QRect sliderGeometry = window.slider->geometry();
        const QSize stableHint = window.hint->size();
        QSignalSpy updates(window.slider, &QSlider::valueChanged);
        int expectedSignals = 0;
        QSet<QString> seenHints;
        // Both directions cross 0/1 and 6/7; each integer must still update the native UI.
        for (int direction : {1, -1}) {
            for (int step = 0; step <= 60; ++step) {
                const int db = direction == 1 ? -40 + step : 20 - step;
                if (window.slider->value() != db) ++expectedSignals;
                window.slider->setValue(db);
                settle(window);
                QCOMPARE(window.slider->value(), db);
                QCOMPARE(window.valueLabel->text(), QStringLiteral("%1 dB").arg(db));
                QCOMPARE(window.hint->text(), volumeHint(db));
                seenHints.insert(window.hint->text());
                QCOMPARE(window.size(), stableSize);
                QCOMPARE(window.slider->geometry(), sliderGeometry);
                QCOMPARE(window.hint->size(), stableHint);
                // A heartbeat or a second event must not accumulate size changes.
                stabilizeVolumeDialog(&window);
                settle(window);
                QCOMPARE(window.size(), stableSize);
                QCOMPARE(window.hint->size(), stableHint);
            }
        }
        QCOMPARE(seenHints.size(), 3);
        QCOMPARE(updates.count(), expectedSignals);
        QVERIFY(updates.count() >= 120);
        window.hide();
        window.show();
        settle(window);
        QCOMPARE(window.size(), stableSize);
        QCOMPARE(window.slider->geometry(), sliderGeometry);
        QCOMPARE(window.hint->size(), stableHint);
    }

    void repeatedCallsPreserveFirstReservation() {
        UserVolumeWidget window;
        stabilizeVolumeDialog(&window);
        const QSize reserved = window.hint->size();
        const QSize dialogSize = window.size();
        // Once initialized, the helper must not reserve another three lines when
        // a later caller changes font/margins or when native hint text changes.
        window.hint->setFont(QFont(QStringLiteral("Segoe UI"), 20));
        window.hint->setContentsMargins(0, 10, 0, 10);
        window.slider->setValue(20);
        for (int i = 0; i < 10; ++i) stabilizeVolumeDialog(&window);
        settle(window);
        QCOMPARE(window.hint->minimumSize(), reserved);
        QCOMPARE(window.hint->maximumSize(), reserved);
        QCOMPARE(window.size(), dialogSize);
    }

    void unrelatedWindowsAndMissingHintsAreUntouched() {
        stabilizeVolumeDialog(nullptr);
        QDialog unrelated;
        unrelated.setObjectName(QStringLiteral("UserVolumeWidget")); // Object name is insufficient.
        auto* box = new QVBoxLayout(&unrelated);
        auto* label = new QLabel(QStringLiteral("unrelated dialog"), &unrelated);
        label->setObjectName(QStringLiteral("hintLabel"));
        box->addWidget(label);
        unrelated.adjustSize();
        const QSize before = unrelated.size();
        const QSize minBefore = label->minimumSize();
        const QSize maxBefore = label->maximumSize();
        FrameFilter filter;
        unrelated.installEventFilter(&filter);
        unrelated.show();
        settle(unrelated);
        stabilizeVolumeDialog(&unrelated);
        QVERIFY(!label->wordWrap());
        QVERIFY(!label->property("flatWhiteStableHint").isValid());
        QCOMPARE(label->minimumSize(), minBefore);
        QCOMPARE(label->maximumSize(), maxBefore);
        QCOMPARE(unrelated.size(), before);

        OtherVolumeWidget subclass;
        stabilizeVolumeDialog(&subclass);
        QVERIFY(!subclass.hint->wordWrap());
        QVERIFY(!subclass.hint->property("flatWhiteStableHint").isValid());

        UserVolumeWidget missing;
        missing.hint->setObjectName(QStringLiteral("anotherLabel"));
        stabilizeVolumeDialog(&missing);
        QVERIFY(!missing.hint->wordWrap());
        QVERIFY(!missing.hint->property("flatWhiteStableHint").isValid());

        UserVolumeWidget nested;
        auto* container = new QWidget(&nested);
        nested.hint->setParent(container); // Correct name but not a direct child.
        const QSize nestedMin = nested.hint->minimumSize();
        stabilizeVolumeDialog(&nested);
        QVERIFY(!nested.hint->wordWrap());
        QCOMPARE(nested.hint->minimumSize(), nestedMin);
        QVERIFY(!nested.hint->property("flatWhiteStableHint").isValid());
    }
};

QTEST_MAIN(VolumeDialogTest)
#include "volume_dialog_test.moc"
