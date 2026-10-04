// Launch options of mybench_offscreen: --style / --grab parsing and the controls style resolution
// ("native" -> the platform style, Fusion when rendering offscreen; "mybench" -> MyBench.Style).
#include <mybench/offscreen.h>

#include <QTest>

using namespace mybench;

class TestLaunchOptions : public QObject
{
    Q_OBJECT

private slots:
    void parsesStyleAndGrab()
    {
        LaunchOptions options;
        QString error;
        QVERIFY(parseLaunchOptions({QStringLiteral("--style"), QStringLiteral("native"), QStringLiteral("--grab=out.png"),
                                    QStringLiteral("--delay"), QStringLiteral("2500"), QStringLiteral("--simulated")},
                                   &options, &error));
        QCOMPARE(options.style, QStringLiteral("native"));
        QCOMPARE(options.grabPath, QStringLiteral("out.png"));
        QVERIFY(options.grab());
        QVERIFY(!options.screenshot());
        QVERIFY(options.capture());
        QCOMPARE(options.delayMs, 2500);
        QCOMPARE(options.arguments, QStringList{QStringLiteral("--simulated")});   // not ours: passed on
    }

    void screenshotIsACaptureButNotAGrab()
    {
        LaunchOptions options;
        QVERIFY(parseLaunchOptions({QStringLiteral("--screenshot"), QStringLiteral("a.png"), QStringLiteral("--style=mybench")},
                                   &options));
        QVERIFY(options.capture());
        QVERIFY(!options.grab());
        QCOMPARE(options.style, QStringLiteral("mybench"));
    }

    void rejectsMissingValues()
    {
        LaunchOptions options;
        QString error;
        QVERIFY(!parseLaunchOptions({QStringLiteral("--style")}, &options, &error));
        QVERIFY(error.contains(QStringLiteral("--style")));
        QVERIFY(!parseLaunchOptions({QStringLiteral("--grab")}, &options, &error));
        QVERIFY(error.contains(QStringLiteral("--grab")));
        QVERIFY(!parseLaunchOptions({QStringLiteral("--style=")}, &options, &error));
    }

    void nativeIsThePlatformStyle()
    {
#if defined(Q_OS_MACOS)
        QCOMPARE(nativeQuickStyle(), QStringLiteral("macOS"));
#elif defined(Q_OS_WIN)
        QCOMPARE(nativeQuickStyle(), QStringLiteral("FluentWinUI3"));
#else
        QVERIFY(nativeQuickStyle() == QStringLiteral("org.kde.desktop") || nativeQuickStyle() == QStringLiteral("Fusion"));
#endif
        QCOMPARE(resolveQuickStyle(QStringLiteral("native"), false), nativeQuickStyle());
        QCOMPARE(resolveQuickStyle(QStringLiteral("Native"), false), nativeQuickStyle());
    }

    void nativeIsFusionOffscreen()
    {
        QCOMPARE(resolveQuickStyle(QStringLiteral("native"), true), QStringLiteral("Fusion"));
    }

    void mybenchAndExplicitNames()
    {
        QCOMPARE(resolveQuickStyle(QStringLiteral("mybench"), false), QStringLiteral("MyBench.Style"));
        QCOMPARE(resolveQuickStyle(QStringLiteral("mybench"), true), QStringLiteral("MyBench.Style"));
        QCOMPARE(resolveQuickStyle(QStringLiteral("Fusion"), false), QStringLiteral("Fusion"));
        QCOMPARE(resolveQuickStyle(QStringLiteral("macOS"), true), QStringLiteral("macOS"));   // explicit name wins
        QCOMPARE(resolveQuickStyle(QString(), false), QString());   // keep Qt's default
    }
};

QTEST_GUILESS_MAIN(TestLaunchOptions)
#include "tst_launchoptions.moc"
