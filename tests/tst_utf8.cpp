// The compiler must read the UTF-8 sources as UTF-8 (MSVC needs /utf-8): a literal such as u"Ω"
// or "µ" in a .cpp file has to be the intended code point, not ANSI-code-page mojibake.
#include <QString>
#include <QtTest>

class Utf8Test : public QObject
{
    Q_OBJECT
private slots:
    void sourceLiteralsAreUtf8()
    {
        QCOMPARE(QString(u"Ω").size(), 1);
        QCOMPARE(QString(u"Ω").at(0).unicode(), char16_t(0x03A9));
        QCOMPARE(QString(u"µ").at(0).unicode(), char16_t(0x00B5));
        QCOMPARE(QString(u"\u2014").at(0).unicode(), char16_t(0x2014));
        QCOMPARE(QString(u"—").at(0).unicode(), char16_t(0x2014));
        // Narrow literals are UTF-8 bytes.
        QCOMPARE(QByteArray("Ω"), QByteArray("\xCE\xA9"));
        QCOMPARE(QByteArray("µ"), QByteArray("\xC2\xB5"));
        QCOMPARE(QString::fromUtf8("±").at(0).unicode(), char16_t(0x00B1));
    }
};

QTEST_APPLESS_MAIN(Utf8Test)
#include "tst_utf8.moc"
