// Dsa panel QML in an offscreen window ("less text, more instrument"): every value shows once,
// in its control, so the display carries no sentence-style title or info line.
#include "dsacontroller.h"

#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::DsaController;

class TestDsaQml : public QObject
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
        DsaController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Dsa\nDsaPanel { }\n", QUrl());
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
            QCOMPARE(o->property("displayTitle").toString(), QString());
            if (o->metaObject()->indexOfProperty("displayInfo") >= 0)
                QCOMPARE(o->property("displayInfo").toString(), QString());
        }
        QVERIFY(panels > 0);
        // The Input card names the source once (its checked button), not again as a subtitle.
        auto* input = root->findChild<QObject*>(u"inputCard"_s);
        QVERIFY(input);
        QCOMPARE(input->property("subtitle").toString(), QString());
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestDsaQml)
#include "tst_dsa_qml.moc"
