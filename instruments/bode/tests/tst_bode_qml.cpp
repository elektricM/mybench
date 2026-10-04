// Bode panel QML in an offscreen window ("less text, more instrument"): every value shows once,
// in its control, so the display carries no sentence-style title or info line.
#include "bodecontroller.h"

#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::BodeController;

class TestBodeQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void lessText()
    {
        BodeController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Bode\nBodePanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&ctl));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();
        offscreen.renderFrame();

        int panels = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            if (o->metaObject()->indexOfProperty("displayTitle") < 0)
                continue;
            ++panels;
            QCOMPARE(o->property("displayTitle").toString(), u"AI 0 – AO 0  ·  AI 1 – Signal"_s);
            if (o->metaObject()->indexOfProperty("displayInfo") >= 0)
                QCOMPARE(o->property("displayInfo").toString(), QString());
        }
        QVERIFY(panels > 0);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestBodeQml)
#include "tst_bode_qml.moc"
