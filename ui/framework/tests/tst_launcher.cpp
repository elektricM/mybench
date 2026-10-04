// Launcher (Home favorites, user tools, pinned files, recent exports; app.md 18.1) and the About
// fields of AppInfo. Settings live in a temporary INI folder.
#include <mybench/appinfo.h>
#include <mybench/dataexport.h>
#include <mybench/launcher.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace Qt::StringLiterals;
using mybench::Launcher;

class TestLauncher : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_files;

    QString makeFile(const QString& name, bool executable = false)
    {
        const QString path = m_files.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write("#!/bin/sh\nexit 0\n");
        file.close();
        if (executable)
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QVERIFY(m_files.isValid());
        QCoreApplication::setOrganizationName(u"MYBENCH-tests"_s);
        QCoreApplication::setApplicationName(u"tst_launcher"_s);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void init()
    {
        QSettings().clear();
    }

    void favoritesToggleAndPersist()
    {
        Launcher launcher(nullptr, false);
        QVERIFY(launcher.favorites().isEmpty());
        QSignalSpy changed(&launcher, &Launcher::favoritesChanged);

        launcher.toggleFavorite(u"scope"_s);
        launcher.setFavorite(u"dmm"_s, true);
        launcher.setFavorite(u"dmm"_s, true);      // no duplicate, no signal
        launcher.setFavorite(QString(), true);     // ignored
        QCOMPARE(launcher.favorites(), QStringList({u"scope"_s, u"dmm"_s}));
        QCOMPARE(changed.count(), 2);
        QVERIFY(launcher.isFavorite(u"scope"_s));

        launcher.moveFavorite(1, 0);
        launcher.moveFavorite(0, 5);               // out of range: ignored
        QCOMPARE(launcher.favorites(), QStringList({u"dmm"_s, u"scope"_s}));

        Launcher reloaded(nullptr, false);
        QCOMPARE(reloaded.favorites(), QStringList({u"dmm"_s, u"scope"_s}));

        reloaded.toggleFavorite(u"dmm"_s);
        QVERIFY(!reloaded.isFavorite(u"dmm"_s));
        QCOMPARE(Launcher(nullptr, false).favorites(), QStringList({u"scope"_s}));
    }

    void toolKinds()
    {
        QCOMPARE(Launcher::targetKind(u"https://example.org/tools"_s), u"url"_s);
        QCOMPARE(Launcher::targetKind(u"mailto:someone@example.org"_s), u"url"_s);
        QCOMPARE(Launcher::targetKind(QString()), u"missing"_s);
        QCOMPARE(Launcher::targetKind(m_files.filePath(u"nothing-here"_s)), u"missing"_s);
        QCOMPARE(Launcher::targetKind(m_files.path()), u"folder"_s);
        QCOMPARE(Launcher::targetKind(makeFile(u"notes.txt"_s)), u"file"_s);
        const QString file = makeFile(u"data.csv"_s);
        QCOMPARE(Launcher::targetKind(QUrl::fromLocalFile(file).toString()), u"file"_s);
        QCOMPARE(Launcher::normalizedTarget(QUrl::fromLocalFile(file).toString()), QDir::toNativeSeparators(file));
#if !defined(Q_OS_WIN)
        QCOMPARE(Launcher::targetKind(makeFile(u"tool.sh"_s, true)), u"program"_s);
#endif
    }

    void toolsAddRemoveMovePersist()
    {
        Launcher launcher(nullptr, false);
        QVERIFY(launcher.tools().isEmpty());
        QSignalSpy changed(&launcher, &Launcher::toolsChanged);

        QCOMPARE(launcher.addTool(u"  "_s, u"https://example.org/apps"_s), 0);   // name from host
        QCOMPARE(launcher.addTool(QString(), makeFile(u"report.txt"_s)), 1);    // name from file
        QCOMPARE(launcher.addTool(u"Folder"_s, m_files.path(), u"-x"_s), 2);
        QCOMPARE(launcher.addTool(u"Nothing"_s, u"   "_s), -1);
        QCOMPARE(changed.count(), 3);

        QVariantList tools = launcher.tools();
        QCOMPARE(tools.size(), 3);
        QCOMPARE(tools[0].toMap()[u"name"_s].toString(), u"example.org"_s);
        QCOMPARE(tools[0].toMap()[u"kind"_s].toString(), u"url"_s);
        QCOMPARE(tools[1].toMap()[u"name"_s].toString(), u"report"_s);
        QCOMPARE(tools[1].toMap()[u"kind"_s].toString(), u"file"_s);
        QCOMPARE(tools[2].toMap()[u"arguments"_s].toString(), u"-x"_s);
        QVERIFY(tools[2].toMap()[u"exists"_s].toBool());

        launcher.moveTool(2, 0);
        launcher.removeTool(2);                   // "report"
        launcher.removeTool(7);                   // ignored
        launcher.updateTool(1, u"Apps"_s, u"https://example.org/more"_s, QString());

        Launcher reloaded(nullptr, false);
        tools = reloaded.tools();
        QCOMPARE(tools.size(), 2);
        QCOMPARE(tools[0].toMap()[u"name"_s].toString(), u"Folder"_s);
        QCOMPARE(tools[1].toMap()[u"name"_s].toString(), u"Apps"_s);
        QCOMPARE(tools[1].toMap()[u"target"_s].toString(), u"https://example.org/more"_s);
    }

    void launchMissingToolFails()
    {
        Launcher launcher(nullptr, false);
        const QString path = m_files.filePath(u"gone.sh"_s);
        QCOMPARE(launcher.addTool(u"Gone"_s, makeFile(u"gone.sh"_s, true)), 0);
        QVERIFY(QFile::remove(path));
        QSignalSpy failed(&launcher, &Launcher::launchFailed);
        QVERIFY(!launcher.tools()[0].toMap()[u"exists"_s].toBool());
        QVERIFY(!launcher.launchTool(0));
        QCOMPARE(failed.count(), 1);
        QVERIFY(launcher.lastError().contains(u"Gone"_s));
        QVERIFY(!launcher.launchTool(3));
        QVERIFY(!launcher.openFile(path));
        QCOMPARE(failed.count(), 2);
    }

    void pinnedFiles()
    {
        Launcher launcher(nullptr, false);
        const QString a = makeFile(u"a.csv"_s);
        const QString b = makeFile(u"b.png"_s);
        QVERIFY(launcher.pinFile(QUrl::fromLocalFile(a)));
        QVERIFY(launcher.pinFile(b));
        QVERIFY(launcher.pinFile(QUrl::fromLocalFile(a).toString()));   // already pinned
        QVERIFY(!launcher.pinFile(QUrl(u"https://example.org/x.csv"_s)));
        QVERIFY(!launcher.pinFile(QString()));
        QCOMPARE(launcher.pinnedFiles().size(), 2);
        QVERIFY(launcher.isPinned(b));

        const QVariantMap first = launcher.pinnedFiles().first().toMap();
        QCOMPARE(first[u"name"_s].toString(), u"a.csv"_s);
        QCOMPARE(first[u"suffix"_s].toString(), u"csv"_s);
        QCOMPARE(first[u"url"_s].toUrl(), QUrl::fromLocalFile(a));
        QVERIFY(first[u"exists"_s].toBool());

#if !defined(Q_OS_WIN)
        // Folders under the home directory are shown with "~"; a missing file stays listed.
        const QString inHome = QDir::homePath() + u"/Lab notes/not-there.pdf"_s;
        QVERIFY(launcher.pinFile(inHome));
        const QVariantMap homeEntry = launcher.pinnedFiles().last().toMap();
        QCOMPARE(homeEntry[u"folder"_s].toString(), u"~/Lab notes"_s);
        QVERIFY(!homeEntry[u"exists"_s].toBool());
        launcher.unpinFile(inHome);
#endif
        launcher.unpinFile(a);
        QCOMPARE(Launcher(nullptr, false).pinnedFiles().size(), 1);
        QCOMPARE(Launcher(nullptr, false).pinnedFiles().first().toMap()[u"name"_s].toString(), u"b.png"_s);
    }

    void recentExportsFollowDataExport()
    {
        Launcher launcher;   // follows DataExport::exported
        QSignalSpy changed(&launcher, &Launcher::recentExportsChanged);
        for (int i = 0; i < Launcher::kMaxRecentExports + 2; ++i)
            emit mybench::DataExport::instance()->exported(m_files.filePath(u"export_%1.csv"_s.arg(i)));
        QCOMPARE(changed.count(), Launcher::kMaxRecentExports + 2);
        QVariantList list = launcher.recentExports();
        QCOMPARE(list.size(), Launcher::kMaxRecentExports);
        QCOMPARE(list.first().toMap()[u"name"_s].toString(), u"export_11.csv"_s);   // newest first
        QCOMPARE(list.last().toMap()[u"name"_s].toString(), u"export_2.csv"_s);

        launcher.addRecentExport(m_files.filePath(u"export_5.csv"_s));            // back to the top
        list = launcher.recentExports();
        QCOMPARE(list.size(), Launcher::kMaxRecentExports);
        QCOMPARE(list.first().toMap()[u"name"_s].toString(), u"export_5.csv"_s);

        launcher.removeRecentExport(m_files.filePath(u"export_5.csv"_s));
        QCOMPARE(Launcher(nullptr, false).recentExports().size(), Launcher::kMaxRecentExports - 1);
        launcher.clearRecentExports();
        QVERIFY(Launcher(nullptr, false).recentExports().isEmpty());
    }

    void resourcesAndAbout()
    {
        Launcher launcher(nullptr, false);
        const QVariantList resources = launcher.resources();
        QVERIFY(resources.size() >= 4);
        for (const QVariant& entry : resources) {
            const QVariantMap map = entry.toMap();
            QVERIFY(!map[u"title"_s].toString().isEmpty());
            QVERIFY2(map[u"url"_s].toString().startsWith(u"https://"_s), qPrintable(map[u"url"_s].toString()));
        }
        mybench::AppInfo* info = mybench::AppInfo::instance();
        QVERIFY(info->homepage().startsWith(u"https://"_s));
        QCOMPARE(launcher.moreToolsUrl(), info->homepage());
        QCOMPARE(info->qtVersion(), QString::fromLatin1(qVersion()));
        QVERIFY(!info->licenseText().trimmed().isEmpty());
        Q_UNUSED(info->usbBackend());
    }

    void aboutRequest()
    {
        Launcher launcher(nullptr, false);
        QSignalSpy about(&launcher, &Launcher::aboutRequested);
        launcher.showAbout();
        QCOMPARE(about.count(), 1);
    }
};

QTEST_MAIN(TestLauncher)
#include "tst_launcher.moc"
