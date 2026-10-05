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

// Items by objectName through the visual tree (Repeater delegates have no QObject parent).
static QQuickItem* findItem(QQuickItem* item, const QString& name)
{
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (QQuickItem* child : item->childItems())
        if (QQuickItem* found = findItem(child, name))
            return found;
    return nullptr;
}

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

        // Result cards: SNR next to THD / SINAD; every window button shows its shape.
        auto* snr = root->findChild<QObject*>(u"cardSnr"_s);
        QVERIFY(snr);
        QCOMPARE(snr->property("title").toString(), u"SNR"_s);
        QCOMPARE(snr->property("value").toString(), u"—"_s);   // no result yet
        root->setProperty("tool", u"window"_s);   // the Window panel open
        offscreen.renderFrame();
        for (int i = 0; i < ctl.windowLabels().size(); ++i) {
            QQuickItem* button = findItem(item, u"window"_s + QString::number(i));
            QVERIFY2(button, qPrintable(QString::number(i)));
            QVERIFY(!button->property("glyph").toString().isEmpty());
        }

        // A single spectrum on the simulator: the SNR card shows the controller's SNR, with
        // statistics once there are two results.
        ctl.runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.state() == mybench::InstrumentController::Idle && ctl.hasResult(), 10000);
        QCOMPARE(snr->property("value").toString(), QString::number(ctl.snr(), 'f', 2) + u" dB"_s);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestDsaQml)
#include "tst_dsa_qml.moc"
