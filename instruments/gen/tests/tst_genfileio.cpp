// Waveform file import/export (docs/instruments/gen.md 4.2.3, 4.3.8, 4.3.9).
#include "genfileio.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace mybench::instruments::gen;

namespace {

QByteArray pcmWav(int bits, int channels, const std::vector<int32_t>& frames, uint32_t rate = 8000, uint16_t format = 1)
{
    QByteArray out;
    auto le32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.append(char((v >> (8 * i)) & 0xFF));
    };
    auto le16 = [&](uint16_t v) {
        out.append(char(v & 0xFF));
        out.append(char(v >> 8));
    };
    const int bytes = bits / 8;
    const uint32_t dataBytes = uint32_t(frames.size() * size_t(bytes));
    out.append("RIFF");
    le32(36 + 12 + dataBytes);
    out.append("WAVE");
    out.append("LIST");   // an unknown chunk to skip
    le32(4);
    out.append("abcd");
    out.append("fmt ");
    le32(16);
    le16(format);
    le16(uint16_t(channels));
    le32(rate);
    le32(rate * uint32_t(bytes * channels));
    le16(uint16_t(bytes * channels));
    le16(uint16_t(bits));
    out.append("data");
    le32(dataBytes);
    for (int32_t v : frames) {
        for (int b = 0; b < bytes; ++b)
            out.append(char((uint32_t(v) >> (8 * b)) & 0xFF));
    }
    return out;
}

}  // namespace

class TestGenFileIo : public QObject
{
    Q_OBJECT

private slots:
    void textAutomatic()
    {
        auto r = parseText("# sampleRate=2000\n1\n2.5\n-3\n", {});
        QVERIFY(r);
        QCOMPARE(r->samples, (std::vector<double>{1, 2.5, -3}));
        QCOMPARE(r->sampleRate, 2000.0);

        // Header with a time column: second column, rate from the time step.
        r = parseText("Time (s),AO 0 (V)\n0,0.5\n0.001,0.25\n0.002,x\n0.003,1\n", {});
        QVERIFY(r);
        QCOMPARE(r->samples, (std::vector<double>{0.5, 0.25, 1}));
        QVERIFY(std::abs(r->sampleRate - 1000.0) < 1e-6);

        // MYBENCH export comment, semicolons with decimal commas.
        r = parseText("# Sample rate: 44100 S/s\nvalue;other\n1,5;9\n2,5;9\n", {});
        QVERIFY(r);
        QCOMPARE(r->samples, (std::vector<double>{1.5, 2.5}));
        QCOMPARE(r->sampleRate, 44100.0);

        // Whitespace separated, NaN replaced.
        r = parseText("1 2\n nan 3\n", {});
        QVERIFY(r);
        QCOMPARE(r->samples, (std::vector<double>{1, 0}));
        QCOMPARE(r->notices.size(), 1);

        QString error;
        QVERIFY(!parseText("a,b\nc,d\n", {}, &error));
        QCOMPARE(error, QStringLiteral("no numeric data"));
    }

    void textWizard()
    {
        TextImportOptions o;
        o.automatic = false;
        o.delimiters = QStringLiteral(";");
        o.column = 2;
        o.startLine = 2;
        o.count = 2;
        auto r = parseText("h1;h2\n1;10\n2;x\n3;30\n4;40\n", o);
        QVERIFY(r);
        QCOMPARE(r->samples, (std::vector<double>{10, 30}));
    }

    void wav()
    {
        auto w = parseWav(pcmWav(16, 1, {0, 16384, -32768}, 22050));
        QVERIFY(w);
        QCOMPARE(w->sampleRate, 22050.0);
        QCOMPARE(w->channels[0], (std::vector<double>{0, 0.5, -1}));
        w = parseWav(pcmWav(8, 2, {128, 255, 0, 128}));
        QVERIFY(w);
        QCOMPARE(w->channels.size(), size_t(2));
        QCOMPARE(w->channels[0], (std::vector<double>{0, -1}));
        QCOMPARE(w->channels[1][0], 127.0 / 128.0);
        w = parseWav(pcmWav(24, 1, {0x400000, -0x800000}));
        QVERIFY(w);
        QCOMPARE(w->channels[0], (std::vector<double>{0.5, -1}));
        QString error;
        QVERIFY(!parseWav(pcmWav(16, 1, {0}, 8000, 2), &error));   // ADPCM
        QVERIFY(error.startsWith(u"unsupported WAV encoding"));

        const QByteArray f = encodeWavFloat32({0.25, -0.5, 2.0}, 48000);
        w = parseWav(f);
        QVERIFY(w);
        QCOMPARE(w->sampleRate, 48000.0);
        QCOMPARE(w->channels[0], (std::vector<double>{0.25, -0.5, 1.0}));   // clipped at full scale
    }

    void binary()
    {
        const std::vector<double> v{0, 2.5, -5};
        const auto back = parseBinaryI16(encodeBinaryI16(v));
        QCOMPARE(back, v);
    }

    void wdt()
    {
        std::vector<WdtWaveform> in(2);
        in[0].dt = 1e-4;
        in[0].samples = {0, 0.5, 1, 0.5, 0, -0.5};
        in[1].dt = 1e-3;
        in[1].samples = {1, 2, 3};
        const auto out = parseWdt(encodeWdt(in));
        QCOMPARE(out.size(), size_t(2));
        QCOMPARE(out[0].dt, 1e-4);
        QCOMPARE(out[0].samples, in[0].samples);
        QCOMPARE(out[1].samples, in[1].samples);
        QVERIFY(parseWdt(QByteArray(100, 'x')).empty());
    }

    void filesRoundTrip()
    {
        QTemporaryDir dir;
        const std::vector<double> samples{0, 0.5, -0.25, 1};
        for (const char* ext : {"csv", "wav", "bin"}) {
            const QString path = dir.filePath(QStringLiteral("w.%1").arg(QLatin1StringView(ext)));
            QString error;
            QVERIFY2(saveWaveformFile(path, samples, 1000, &error), qPrintable(error));
            const auto loaded = loadWaveformFile(path, &error);
            QVERIFY2(loaded, qPrintable(error));
            QCOMPARE(loaded->samples.size(), samples.size());
            for (size_t i = 0; i < samples.size(); ++i)
                QVERIFY(std::abs(loaded->samples[i] - samples[i]) < 1e-3);
            if (QLatin1StringView(ext) != u"bin")
                QCOMPARE(loaded->sampleRate, 1000.0);
            QCOMPARE(loaded->name, QStringLiteral("w.%1").arg(QLatin1StringView(ext)));
        }
        // .mbw is rendered at its own rate.
        Document d;
        d.sampleRate = 100;
        d.segments[0].duration = 0.5;
        d.segments[0].components = {Component::library(LibraryFunction::Sine)};
        QFile f(dir.filePath("doc.mbw"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(serialize(d));
        f.close();
        const auto doc = loadWaveformFile(dir.filePath("doc.mbw"));
        QVERIFY(doc && doc->document);
        QCOMPARE(doc->samples.size(), size_t(50));
        QCOMPARE(doc->sampleRate, 100.0);
        QString error;
        QVERIFY(!loadWaveformFile(dir.filePath("missing.csv"), &error));
    }
};

QTEST_GUILESS_MAIN(TestGenFileIo)
#include "tst_genfileio.moc"
