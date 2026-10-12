// Waveform Editor documents: library, rendering rules and .mbw files (docs/instruments/gen.md 4.3).
#include "genwaveform.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace mybench::instruments::gen;
using namespace Qt::StringLiterals;

namespace {

Document oneSegment(double duration, double rate, std::vector<Component> components)
{
    Document d;
    d.sampleRate = rate;
    d.segments = {Segment{QString(), duration, 1.0, std::move(components)}};
    return d;
}

double maxAbs(const std::vector<double>& v)
{
    double m = 0;
    for (double x : v)
        m = std::max(m, std::abs(x));
    return m;
}

}  // namespace

class TestGenWaveform : public QObject
{
    Q_OBJECT

private slots:
    void libraryTable()
    {
        // 21 functions in the ELVISmx order with the recovered parameter lists.
        const QStringList names{"DC Level", "Sine", "Square", "Triangle", "Sawtooth", "Uniform Noise",
                                "Triangular Noise", "Gaussian Noise", "Rising Exponential", "Falling Exponential",
                                "Hanning", "Ramp", "Linear Sweep", "Log Sweep", "Sin x/x", "Multitone", "Trapezoid",
                                "Stairstep", "Circle", "Haversine", "Pulse"};
        QCOMPARE(names.size(), kLibraryFunctionCount);
        for (int i = 0; i < kLibraryFunctionCount; ++i) {
            const FunctionInfo& info = functionInfo(LibraryFunction(i));
            QCOMPARE(QString::fromLatin1(info.name), names[i]);
            QVERIFY(info.params.size() >= 1 && info.params.size() <= size_t(kMaxLibraryParams));
            QVERIFY(functionFromId(QString::fromLatin1(info.id)) == LibraryFunction(i));
        }
        auto keys = [](LibraryFunction f) {
            QStringList k;
            for (const ParamInfo& p : functionInfo(f).params)
                k.append(QString::fromLatin1(p.key));
            return k;
        };
        QCOMPARE(keys(LibraryFunction::DcLevel), QStringList({"offset"}));
        QCOMPARE(keys(LibraryFunction::Square), QStringList({"amplitude", "offset", "frequency", "phase", "dutyCycle"}));
        QCOMPARE(keys(LibraryFunction::GaussianNoise), QStringList({"level", "offset"}));
        QCOMPARE(keys(LibraryFunction::RisingExponential), QStringList({"amplitude", "offset", "frequency", "tau"}));
        QCOMPARE(keys(LibraryFunction::Ramp), QStringList({"start", "end"}));
        QCOMPARE(keys(LibraryFunction::LinearSweep), QStringList({"amplitude", "offset", "startFrequency", "stopFrequency"}));
        QCOMPARE(keys(LibraryFunction::Sinc), QStringList({"amplitude", "offset", "frequency"}));
        QCOMPARE(keys(LibraryFunction::Trapezoid), QStringList({"amplitude", "delay", "rise", "hold", "fall"}));
        QCOMPARE(keys(LibraryFunction::Stairstep), QStringList({"start", "end", "steps"}));
        QCOMPARE(keys(LibraryFunction::Haversine), QStringList({"start", "end"}));
        QCOMPARE(keys(LibraryFunction::Pulse), QStringList({"amplitude", "delay", "rise", "hold", "fall"}));
        // New component: Sine {1, 0, 1, 0}.
        const Component c;
        QCOMPARE(c.function, LibraryFunction::Sine);
        QCOMPARE(c.param("amplitude"), 1.0);
        QCOMPARE(c.param("frequency"), 1.0);
    }

    void defaultsAndSampleCount()
    {
        const Document d;
        QCOMPARE(d.sampleRate, 10000.0);
        QCOMPARE(d.segments.size(), size_t(1));
        QCOMPARE(d.segments[0].duration, 10.0);
        const RenderResult r = render(d);
        QCOMPARE(r.samples.size(), size_t(100000));
        QCOMPARE(maxAbs(r.samples), 0.0);   // empty segment = 0 V
        QCOMPARE(segmentSamples(0.00001, 10000), size_t(1));
    }

    void sineNormalisedFrequency()
    {
        // Frequency f becomes f / SampleRate cycles per sample.
        const RenderResult r = render(oneSegment(0.01, 100000, {Component::library(LibraryFunction::Sine)}));
        QCOMPARE(r.samples.size(), size_t(1000));
        Document d = oneSegment(0.01, 100000, {});
        Component sine = Component::library(LibraryFunction::Sine);
        sine.setParam("frequency", 1000);
        d.segments[0].components = {sine};
        const RenderResult s = render(d);
        QVERIFY(std::abs(s.samples[25] - 1.0) < 1e-12);    // quarter period at 1 kHz = 25 samples
        QVERIFY(std::abs(s.samples[100]) < 1e-9);          // one period
        QVERIFY(!s.alias);
        sine.setParam("frequency", 60000);
        d.segments[0].components = {sine};
        QVERIFY(render(d).alias);
    }

    void zeroFrequencyIsOneCyclePerSegment()
    {
        Component sine = Component::library(LibraryFunction::Sine);
        sine.setParam("frequency", 0);
        const RenderResult r = render(oneSegment(2.0, 100, {sine}));
        QVERIFY(std::abs(r.samples[50] - 1.0) < 1e-12);
    }

    void shapes()
    {
        const double rate = 1000;
        auto one = [&](Component c) { return render(oneSegment(1.0, rate, {c})).samples; };
        Component square = Component::library(LibraryFunction::Square);
        square.setParam("dutyCycle", 25);
        auto sq = one(square);
        QCOMPARE(sq[0], 1.0);
        QCOMPARE(sq[249], 1.0);
        QCOMPARE(sq[250], -1.0);
        auto tri = one(Component::library(LibraryFunction::Triangle));
        QVERIFY(std::abs(tri[250] - 1.0) < 1e-12 && std::abs(tri[750] + 1.0) < 1e-12);
        auto saw = one(Component::library(LibraryFunction::Sawtooth));
        QVERIFY(std::abs(saw[250] - 0.5) < 1e-12 && std::abs(saw[500] + 1.0) < 1e-12);

        Component dc = Component::library(LibraryFunction::DcLevel);
        dc.setParam("offset", 2.5);
        QCOMPARE(one(dc)[123], 2.5);

        // Whole-segment functions
        auto ramp = one(Component::library(LibraryFunction::Ramp));   // Start + (End - Start) t / D
        QCOMPARE(ramp.front(), -1.0);
        QVERIFY(std::abs(ramp[500]) < 1e-12);
        QVERIFY(std::abs(ramp.back() - 0.998) < 1e-12);
        auto hanning = one(Component::library(LibraryFunction::Hanning));
        QVERIFY(std::abs(hanning[0]) < 1e-12 && std::abs(hanning[500] - 1.0) < 1e-12);
        auto haversine = one(Component::library(LibraryFunction::Haversine));   // 0 -> 1
        QVERIFY(std::abs(haversine[0]) < 1e-12);           // (Start-End)/2 cos(0) + (Start+End)/2 = Start
        QVERIFY(std::abs(haversine[500] - 0.5) < 1e-12);
        auto stairs = one(Component::library(LibraryFunction::Stairstep));   // 10 levels -1 .. 1
        QCOMPARE(stairs[0], -1.0);
        QVERIFY(std::abs(stairs[200] - (-1.0 + 2.0 * 2 / 9)) < 1e-12);
        QCOMPARE(stairs[999], 1.0);
        auto trap = one(Component::library(LibraryFunction::Trapezoid));   // 20 % delay, rise, hold, fall
        QCOMPARE(trap[100], 0.0);
        QVERIFY(std::abs(trap[300] - 0.5) < 1e-9);
        QCOMPARE(trap[500], 1.0);
        QVERIFY(std::abs(trap[700] - 0.5) < 1e-9);
        QCOMPARE(trap[900], 0.0);
        auto pulse = one(Component::library(LibraryFunction::Pulse));
        QCOMPARE(pulse[500], 1.0);
        QVERIFY(std::abs(pulse[300] - 0.5) < 1e-9);   // raised-cosine edge crosses half at mid-rise

        Component multitone = Component::library(LibraryFunction::Multitone);
        multitone.setParam("amplitude", 2.0);
        auto mt = one(multitone);
        QVERIFY(std::abs(maxAbs(mt) - 2.0) < 1e-9);   // peak normalised to Amplitude
    }

    void libraryFidelity()
    {
        // Defaults verified in the ELVISmx UI (gen.md 4.3.4; audit editor #4).
        auto def = [](LibraryFunction f, const char* key) { return Component::library(f).param(key, -999); };
        QCOMPARE(def(LibraryFunction::GaussianNoise, "level"), 1.0);
        for (LibraryFunction f : {LibraryFunction::LinearSweep, LibraryFunction::LogSweep, LibraryFunction::Multitone}) {
            QCOMPARE(def(f, "startFrequency"), 0.1);
            QCOMPARE(def(f, "stopFrequency"), 1.0);
        }
        for (LibraryFunction f : {LibraryFunction::Trapezoid, LibraryFunction::Pulse}) {
            QCOMPARE(def(f, "delay"), 20.0);
            QCOMPARE(def(f, "rise"), 20.0);
            QCOMPARE(def(f, "hold"), 20.0);
            QCOMPARE(def(f, "fall"), 20.0);
        }
        QCOMPARE(def(LibraryFunction::Stairstep, "steps"), 10.0);
        QCOMPARE(QString::fromLatin1(functionInfo(LibraryFunction::Stairstep).params[1].label), u"Stop"_s);
        QCOMPARE(def(LibraryFunction::Haversine, "start"), 0.0);
        QCOMPARE(def(LibraryFunction::Haversine, "end"), 1.0);
        QCOMPARE(def(LibraryFunction::RisingExponential, "tau"), 5.0);
        QCOMPARE(def(LibraryFunction::FallingExponential, "tau"), 5.0);
        QCOMPARE(functionInfo(LibraryFunction::RisingExponential).params[3].kind, ParamKind::Number);   // no unit

        const double rate = 1000;
        auto one = [&](Component c) { return render(oneSegment(1.0, rate, {c})).samples; };
        // Rising / Falling Exp (audit editor #5): 0 -> A steep at the end / A -> 0 steep at the start.
        const double e5 = std::exp(5.0);
        auto rising = one(Component::library(LibraryFunction::RisingExponential));
        QVERIFY(std::abs(rising[0]) < 1e-12);
        QVERIFY(std::abs(rising[500] - (std::exp(2.5) - 1) / (e5 - 1)) < 1e-12);
        QVERIFY(rising[999] > 0.99 && rising[999] < 1.0);
        auto falling = one(Component::library(LibraryFunction::FallingExponential));
        QVERIFY(std::abs(falling[0] - 1.0) < 1e-12);
        QVERIFY(std::abs(falling[500] - (std::exp(-2.5) - std::exp(-5.0)) / (1 - std::exp(-5.0))) < 1e-12);
        QVERIFY(falling[999] < 0.01);
        // Circle (audit editor #6): one arch per period from -A to +A.
        auto circle = one(Component::library(LibraryFunction::Circle));
        QVERIFY(std::abs(circle[0] + 1.0) < 1e-12);
        QVERIFY(std::abs(circle[500] - 1.0) < 1e-12);
        QVERIFY(std::abs(circle[250] - (2 * std::sqrt(0.75) - 1)) < 1e-12);
        QVERIFY(std::abs(circle[750] - (2 * std::sqrt(0.75) - 1)) < 1e-12);
        // Sin x/x (audit editor #7): main lobe centred, zeros every 1/(2f).
        auto sinc = one(Component::library(LibraryFunction::Sinc));
        QVERIFY(std::abs(sinc[500] - 1.0) < 1e-12);
        QVERIFY(std::abs(sinc[0]) < 1e-12);
        QVERIFY(std::abs(sinc[250] - 2 / std::numbers::pi) < 1e-12);
        // Seed 0: a new random sequence at every render (audit editor #10).
        Component noise = Component::library(LibraryFunction::UniformNoise);
        noise.seed = 0;
        QVERIFY(one(noise) != one(noise));
        // .mbw written with the spec's id `triangleNoise` (audit editor #23).
        QCOMPARE(functionFromId(u"triangleNoise"_s), std::optional<LibraryFunction>(LibraryFunction::TriangularNoise));
        QCOMPARE(functionFromId(u"triangularNoise"_s), std::optional<LibraryFunction>(LibraryFunction::TriangularNoise));
    }

    void pulsePercentagesSumTo100()
    {
        // Delay + Rise + Hold + Fall <= 100 (audit editor #9): the edited value gives way.
        Component c = Component::library(LibraryFunction::Trapezoid);
        QCOMPARE(coercePercentParam(c, "hold", 90), 40.0);
        QCOMPARE(coercePercentParam(c, "hold", 30), 30.0);
        Component sine = Component::library(LibraryFunction::Square);
        QCOMPARE(coercePercentParam(sine, "dutyCycle", 90), 90.0);   // not a breakpoint parameter
    }

    void sweeps()
    {
        Component lin = Component::library(LibraryFunction::LinearSweep);
        lin.setParam("startFrequency", 1);
        lin.setParam("stopFrequency", 20);
        const auto y = render(oneSegment(10.0, 1000, {lin})).samples;
        QCOMPARE(y.size(), size_t(10000));
        // Zero crossings get denser: count sign changes in the first and last second.
        auto crossings = [&](size_t from, size_t to) {
            int n = 0;
            for (size_t i = from + 1; i < to; ++i)
                n += (y[i - 1] < 0) != (y[i] < 0);
            return n;
        };
        QVERIFY(crossings(0, 1000) < crossings(9000, 10000));
        Component log = Component::library(LibraryFunction::LogSweep);
        const auto l = render(oneSegment(1.0, 1000, {log})).samples;
        QVERIFY(maxAbs(l) <= 1.0 + 1e-12);
    }

    void noiseIsDeterministic()
    {
        for (LibraryFunction f : {LibraryFunction::UniformNoise, LibraryFunction::TriangularNoise, LibraryFunction::GaussianNoise}) {
            Component c = Component::library(f);
            c.seed = 42;
            const auto a = render(oneSegment(0.1, 10000, {c})).samples;
            const auto b = render(oneSegment(0.1, 10000, {c})).samples;
            QCOMPARE(a, b);
            c.seed = 43;
            QVERIFY(render(oneSegment(0.1, 10000, {c})).samples != a);
        }
        Component u = Component::library(LibraryFunction::UniformNoise);
        QVERIFY(maxAbs(render(oneSegment(1, 10000, {u})).samples) <= 1.0);
    }

    void operators()
    {
        Component dc2 = Component::library(LibraryFunction::DcLevel);
        dc2.setParam("offset", 2.0);
        Component dc3 = dc2;
        dc3.setParam("offset", 3.0);
        auto combine = [&](Operator op) {
            Component b = dc3;
            b.op = op;
            return render(oneSegment(0.01, 1000, {dc2, b})).samples.front();
        };
        QCOMPARE(combine(Operator::Add), 5.0);
        QCOMPARE(combine(Operator::Subtract), -1.0);
        QCOMPARE(combine(Operator::Multiply), 6.0);
        QVERIFY(std::abs(combine(Operator::Divide) - 2.0 / 3.0) < 1e-12);
        // The first component's operator is ignored.
        Component first = dc2;
        first.op = Operator::Multiply;
        QCOMPARE(render(oneSegment(0.01, 1000, {first})).samples.front(), 2.0);
        // Division by zero: 0 and a warning.
        Component zero = Component::library(LibraryFunction::DcLevel);
        zero.setParam("offset", 0.0);
        zero.op = Operator::Divide;
        const RenderResult r = render(oneSegment(0.01, 1000, {dc2, zero}));
        QCOMPARE(r.samples.front(), 0.0);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.front().startsWith(u"Division by zero in segment 1"));
    }

    void frequencyModulation()
    {
        // acc = 1 (DC): the carrier runs twice as fast.
        Component dc = Component::library(LibraryFunction::DcLevel);
        dc.setParam("offset", 1.0);
        Component carrier = Component::library(LibraryFunction::Sine);
        carrier.op = Operator::FM;
        const auto y = render(oneSegment(1.0, 1000, {dc, carrier})).samples;
        QVERIFY(std::abs(y[125] - 1.0) < 1e-9);   // quarter period of 2 Hz
    }

    void segmentsConcatenateWithGain()
    {
        Document d;
        d.sampleRate = 1000;
        Component dc = Component::library(LibraryFunction::DcLevel);
        d.segments = {Segment{"a", 0.5, 1.0, {dc}}, Segment{"b", 0.25, 2.0, {dc}}};
        const RenderResult r = render(d);
        QCOMPARE(r.samples.size(), size_t(750));
        QCOMPARE(r.segmentStarts, (std::vector<size_t>{0, 500}));
        QCOMPARE(r.samples[499], 1.0);
        QCOMPARE(r.samples[500], 2.0);
    }

    void expressionComponent()
    {
        Component e = Component::makeExpression(QStringLiteral("sin(2*pi*x)"));
        e.cycles = 5;
        const auto y = render(oneSegment(1.0, 1000, {e})).samples;
        QVERIFY(std::abs(y[50] - 1.0) < 1e-9);   // 5 cycles over 1 s: peak at 50 ms
        Component bad = Component::makeExpression(QStringLiteral("sin(2*pi*x"));
        QCOMPARE(maxAbs(render(oneSegment(1.0, 100, {bad})).samples), 0.0);
        Component invalid = Component::makeExpression(QStringLiteral("sqrt(x - 0.5)"));
        const RenderResult r = render(oneSegment(1.0, 100, {invalid}));
        QVERIFY(!r.warnings.isEmpty());
        QVERIFY(r.warnings.front().contains(u"invalid values in segment 1"));
    }

    void sketchInterpolation()
    {
        // M values span one cycle, linear interpolation.
        Component s = Component::makeSketch("s", {0.0, 1.0, 0.0});
        const auto y = render(oneSegment(1.0, 4, {s})).samples;   // positions 0, .5, 1, 1.5
        QCOMPARE(y, (std::vector<double>{0.0, 0.5, 1.0, 0.5}));
        const Document imported = documentFromSamples({1, 2, 3, 4}, 1000, "x");
        QCOMPARE(render(imported).samples, (std::vector<double>{1, 2, 3, 4}));
    }

    void sampleLimit()
    {
        Document d;
        d.sampleRate = 1e6;
        d.segments[0].duration = 11;
        const RenderResult r = render(d);
        QVERIFY(r.samples.empty());
        QVERIFY(r.warnings.front().contains(u"max 10 000 000"));
    }

    void setFunctionKeepsCommonParameters()
    {
        Component c = Component::library(LibraryFunction::Sine);
        c.setParam("amplitude", 3);
        c.setParam("frequency", 7);
        c.setFunction(LibraryFunction::Square);
        QCOMPARE(c.param("amplitude"), 3.0);
        QCOMPARE(c.param("frequency"), 7.0);
        QCOMPARE(c.param("dutyCycle"), 50.0);
        c.setFunction(LibraryFunction::Ramp);
        QCOMPARE(c.param("start"), -1.0);
    }

    void jsonRoundTrip()
    {
        Document d;
        d.name = "Test burst";
        d.info = "description";
        d.sampleRate = 20000;
        d.units = FrequencyUnit::kHz;
        Component sine = Component::library(LibraryFunction::Sine);
        sine.setParam("phase", 45);
        Component noise = Component::library(LibraryFunction::GaussianNoise);
        noise.seed = 1234;
        noise.op = Operator::Add;
        Component expr = Component::makeExpression("x^2");
        expr.op = Operator::Multiply;
        expr.cycles = 3;
        QList<double> big;
        for (int i = 0; i < 12000; ++i)
            big.append(float(std::sin(i * 0.01)));   // float-exact values survive base64 float32
        Component sketch = Component::makeSketch("Sketch 1", big);
        sketch.op = Operator::FM;
        sketch.vertices = {QPointF(0, 0), QPointF(1, 0.5)};
        d.segments = {Segment{"one", 0.5, 1.5, {sine, noise, expr}}, Segment{"", 0.1, 1.0, {sketch}}};
        const QByteArray bytes = serialize(d);
        QString error;
        const auto back = parse(bytes, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(*back, d);
        QCOMPARE(render(*back).samples, render(d).samples);   // bitwise identical
        QVERIFY(bytes.contains("\"samplesBase64\""));
        QVERIFY(bytes.contains("\"op\": \"fm\""));

        QVERIFY(!parse("{\"format\":\"other\"}", &error));
        QCOMPARE(error, QStringLiteral("not a MYBENCH waveform"));
        QVERIFY(!parse("{\"format\":\"mybench-waveform\",\"version\":2}", &error));
        QVERIFY(error.contains(u"newer MYBENCH"));
        // Top-level parameters (spec example style) are accepted.
        const auto spec = parse(R"({"format":"mybench-waveform","version":1,"sampleRate":1000,"segments":[{"duration":1,
            "components":[{"type":"library","op":"+","function":"sine","amplitude":2,"offset":0,"frequency":5,"phase":0,"params":{}}]}]})");
        QVERIFY(spec);
        QCOMPARE(spec->segments[0].components[0].param("amplitude"), 2.0);
    }
};

QTEST_GUILESS_MAIN(TestGenWaveform)
#include "tst_genwaveform.moc"
