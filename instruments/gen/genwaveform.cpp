#include "genwaveform.h"

#include "genexpression.h"

#include <mydaq/dsp/fft.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <random>
#include <numbers>

using namespace Qt::StringLiterals;

namespace mybench::instruments::gen {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kMaxFrequency = 10e6;

QString tr(const char* text)
{
    return QCoreApplication::translate("Waveform", text);
}

using K = ParamKind;

// The library, in ELVISmx order (docs/instruments/algorithms/gen.md 3.4) with the parameter
// names and defaults verified in the ELVISmx UI (gen.md 4.3.4).
const std::vector<FunctionInfo>& library()
{
    static const std::vector<FunctionInfo> table = [] {
        const ParamInfo amplitude{"amplitude", "Amplitude", K::Voltage, 1.0, -1000, 1000};
        const ParamInfo offset{"offset", "Offset", K::Voltage, 0.0, -1000, 1000};
        const ParamInfo frequency{"frequency", "Freq", K::Frequency, 1.0, 0, kMaxFrequency};
        const ParamInfo phase{"phase", "Phase", K::Degrees, 0.0, -360, 360};
        const ParamInfo startFrequency{"startFrequency", "Start Freq", K::Frequency, 0.1, 0, kMaxFrequency};
        const ParamInfo stopFrequency{"stopFrequency", "Stop Freq", K::Frequency, 1.0, 0, kMaxFrequency};
        const ParamInfo start{"start", "Start", K::Voltage, -1.0, -1000, 1000};
        const ParamInfo end{"end", "End", K::Voltage, 1.0, -1000, 1000};
        const ParamInfo delay{"delay", "Delay", K::Percent, 20.0, 0, 100};
        const ParamInfo rise{"rise", "Rise", K::Percent, 20.0, 0, 100};
        const ParamInfo hold{"hold", "Hold", K::Percent, 20.0, 0, 100};
        const ParamInfo fall{"fall", "Fall", K::Percent, 20.0, 0, 100};
        // Dimensionless steepness of the exponentials (e^(Tau u) over one period).
        const ParamInfo tau{"tau", "Tau", K::Number, 5.0, 1e-4, 700};
        using F = LibraryFunction;
        return std::vector<FunctionInfo>{
            {F::DcLevel, "dcLevel", "DC Level", "DC Level", {{"offset", "Offset", K::Voltage, 1.0, -1000, 1000}}, false, false},
            {F::Sine, "sine", "Sine", "Sine", {amplitude, offset, frequency, phase}, true, false},
            {F::Square, "square", "Square", "Square",
             {amplitude, offset, frequency, phase, {"dutyCycle", "Duty Cycle", K::Percent, 50.0, 0, 100}}, true, false},
            {F::Triangle, "triangle", "Triangle", "Triangle", {amplitude, offset, frequency, phase}, true, false},
            {F::Sawtooth, "sawtooth", "Sawtooth", "Sawtooth", {amplitude, offset, frequency, phase}, true, false},
            {F::UniformNoise, "uniformNoise", "Uniform Noise", "Uniform Noise", {amplitude, offset}, false, true},
            {F::TriangularNoise, "triangularNoise", "Triangular Noise", "Triangular Noise", {amplitude, offset}, false, true},
            {F::GaussianNoise, "gaussianNoise", "Gaussian Noise", "Gaussian Noise",
             {{"level", "Level", K::Voltage, 1.0, 0, 1000}, offset}, false, true},
            {F::RisingExponential, "risingExponential", "Rising Exponential", "Rising Exp", {amplitude, offset, frequency, tau}, true, false},
            {F::FallingExponential, "fallingExponential", "Falling Exponential", "Falling Exp", {amplitude, offset, frequency, tau}, true, false},
            {F::Hanning, "hanning", "Hanning", "Hanning", {amplitude, offset}, false, false},
            {F::Ramp, "ramp", "Ramp", "Ramp", {start, end}, false, false},
            {F::LinearSweep, "linearSweep", "Linear Sweep", "Lin Sweep", {amplitude, offset, startFrequency, stopFrequency}, false, false},
            {F::LogSweep, "logSweep", "Log Sweep", "Log Sweep", {amplitude, offset, startFrequency, stopFrequency}, false, false},
            {F::Sinc, "sinc", "Sin x/x", "Sin x/x", {amplitude, offset, frequency}, false, false},
            {F::Multitone, "multitone", "Multitone", "Multitone", {amplitude, offset, startFrequency, stopFrequency}, false, false},
            {F::Trapezoid, "trapezoid", "Trapezoid", "Trapezoid", {amplitude, delay, rise, hold, fall}, false, false},
            {F::Stairstep, "stairstep", "Stairstep", "Stairstep",
             {start, {"end", "Stop", K::Voltage, 1.0, -1000, 1000}, {"steps", "Steps", K::Count, 10, 1, 100000}}, false, false},
            {F::Circle, "circle", "Circle", "Circle", {amplitude, offset, frequency}, true, false},
            {F::Haversine, "haversine", "Haversine", "Haversine",
             {{"start", "Start", K::Voltage, 0.0, -1000, 1000}, end}, false, false},
            {F::Pulse, "pulse", "Pulse", "Pulse", {amplitude, delay, rise, hold, fall}, false, false},
        };
    }();
    return table;
}

int paramIndex(const FunctionInfo& info, const char* key)
{
    for (size_t i = 0; i < info.params.size(); ++i) {
        if (std::strcmp(info.params[i].key, key) == 0)
            return int(i);
    }
    return -1;
}

double frac(double v)
{
    return v - std::floor(v);
}

// Seed of a noise component: 0 = a new sequence at every render (ELVISmx behaviour).
uint64_t noiseSeed(const Component& component)
{
    if (component.seed != 0)
        return component.seed;
    static std::atomic<uint64_t> counter{0};
    return (uint64_t(std::random_device{}()) << 32) ^ uint64_t(std::random_device{}()) ^ ++counter;
}

// Piecewise-linear breakpoints in percent of the segment (Trapezoid/Pulse): value in [0, 1].
double trapezoidShape(double percent, double delay, double rise, double hold, double fall, bool smooth)
{
    const double a = delay;
    const double b = a + rise;
    const double c = b + hold;
    const double d = c + fall;
    auto edge = [smooth](double x) { return smooth ? 0.5 - 0.5 * std::cos(std::numbers::pi * x) : x; };
    if (percent < a)
        return 0.0;
    if (percent < b)
        return rise > 0 ? edge((percent - a) / rise) : 1.0;
    if (percent < c)
        return 1.0;
    if (percent < d)
        return fall > 0 ? edge(1.0 - (percent - c) / fall) : 0.0;
    return 0.0;
}

// Renders one component on the (possibly warped) local times `t` (s) of a segment of `n`
// samples at `rate`; returns the component's own values (before combination).
std::vector<double> evaluateComponent(const Component& component, const std::vector<double>& t, size_t n,
                                      double rate, int segmentIndex, QStringList& warnings)
{
    std::vector<double> y(t.size(), 0.0);
    const double duration = double(n) / rate;   // rendered duration
    if (component.type == ComponentType::Expression) {
        const Expression expression = Expression::parse(component.expression);
        if (!expression.ok())
            return y;   // invalid: 0 V (the editor shows the error)
        const double cycles = component.cycles > 0 ? component.cycles : 1.0;
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = component.from + (component.to - component.from) * frac(cycles * t[i] / duration);
        expression.evaluate(y, y);
        for (double& v : y)
            v *= component.gain;
        return y;
    }
    if (component.type == ComponentType::Sketch) {
        const qsizetype m = component.samples.size();
        if (m == 0)
            return y;
        if (m == 1) {
            std::fill(y.begin(), y.end(), component.samples[0] * component.gain);
            return y;
        }
        // Sample i reads position (i (M-1) / N * Cycles) mod (M-1), linear interpolation.
        const double span = double(m - 1);
        const double cycles = component.cycles > 0 ? component.cycles : 1.0;
        for (size_t i = 0; i < t.size(); ++i) {
            double position = std::fmod(t[i] / duration * span * cycles, span);
            if (position < 0)
                position += span;
            const qsizetype k = std::min<qsizetype>(qsizetype(position), m - 2);
            const double f = position - double(k);
            y[i] = (component.samples[k] + (component.samples[k + 1] - component.samples[k]) * f) * component.gain;
        }
        return y;
    }

    // Library
    const FunctionInfo& info = functionInfo(component.function);
    auto p = [&](const char* key, double fallback = 0.0) { return component.param(key, fallback); };
    const double amplitude = p("amplitude", 1.0);
    const double offset = p("offset");
    double frequency = std::max(0.0, p("frequency", 1.0));
    if (info.periodic && frequency == 0)
        frequency = 1.0 / duration;   // 0 Hz: one cycle over the segment
    const double phaseCycles = p("phase") / 360.0;
    using F = LibraryFunction;
    switch (component.function) {
    case F::DcLevel:
        std::fill(y.begin(), y.end(), p("offset", 1.0));
        break;
    case F::Sine:
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = offset + amplitude * std::sin(kTwoPi * frac(frequency * t[i] + phaseCycles));
        break;
    case F::Square: {
        const double duty = std::clamp(p("dutyCycle", 50.0), 0.0, 100.0) / 100.0;
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = offset + amplitude * (frac(frequency * t[i] + phaseCycles) < duty ? 1.0 : -1.0);
        break;
    }
    case F::Triangle:
        for (size_t i = 0; i < t.size(); ++i) {
            const double u = frac(frequency * t[i] + phaseCycles);
            const double s = u < 0.25 ? 4 * u : u < 0.75 ? 2 - 4 * u : 4 * u - 4;
            y[i] = offset + amplitude * s;
        }
        break;
    case F::Sawtooth:
        for (size_t i = 0; i < t.size(); ++i) {
            const double u = frac(frequency * t[i] + phaseCycles);
            y[i] = offset + amplitude * (u < 0.5 ? 2 * u : 2 * u - 2);
        }
        break;
    case F::UniformNoise: {
        NoiseGenerator rng(noiseSeed(component));
        for (double& v : y)
            v = offset + amplitude * (2.0 * rng.uniform() - 1.0);
        break;
    }
    case F::TriangularNoise: {
        NoiseGenerator rng(noiseSeed(component));
        for (double& v : y) {
            const double r1 = rng.uniform();
            v = offset + amplitude * (r1 + rng.uniform() - 1.0);
        }
        break;
    }
    case F::GaussianNoise: {
        NoiseGenerator rng(noiseSeed(component));
        const double level = p("level", 1.0);
        for (double& v : y)
            v = offset + level * rng.gaussian();
        break;
    }
    case F::RisingExponential:
    case F::FallingExponential: {
        // Per period, u = frac(f t): rising (e^(Tau u) - 1) / (e^Tau - 1) goes 0 -> 1, steep at
        // the end; falling (e^(-Tau u) - e^-Tau) / (1 - e^-Tau) goes 1 -> 0, steep at the start
        // (the shapes observed in ELVISmx, gen.md 4.3.4). Tau -> 0 tends to a ramp.
        const double tau = std::clamp(p("tau", 5.0), -700.0, 700.0);
        const bool rising = component.function == F::RisingExponential;
        for (size_t i = 0; i < t.size(); ++i) {
            const double u = frac(frequency * t[i]);
            double shape;
            if (std::abs(tau) < 1e-9)
                shape = rising ? u : 1.0 - u;
            else if (rising)
                shape = std::expm1(tau * u) / std::expm1(tau);
            else
                shape = (std::exp(-tau * u) - std::exp(-tau)) / -std::expm1(-tau);
            y[i] = offset + amplitude * shape;
        }
        break;
    }
    case F::Hanning:
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = offset + amplitude / 2 * (1.0 - std::cos(kTwoPi * t[i] / duration));
        break;
    case F::Ramp: {
        const double s = p("start", -1.0);
        const double e = p("end", 1.0);
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = s + (e - s) * std::clamp(t[i] / duration, 0.0, 1.0);   // v = t / D
        break;
    }
    case F::LinearSweep: {
        const double f1 = std::max(0.0, p("startFrequency", 1.0));
        const double f2 = std::max(0.0, p("stopFrequency", 10.0));
        for (size_t i = 0; i < t.size(); ++i) {
            const double cycles = f1 * t[i] + (f2 - f1) * t[i] * t[i] / (2 * duration);
            y[i] = offset + amplitude * std::sin(kTwoPi * frac(cycles));
        }
        break;
    }
    case F::LogSweep: {
        const double f1 = std::max(1e-6, p("startFrequency", 1.0));
        const double f2 = std::max(1e-6, p("stopFrequency", 10.0));
        const double r = f2 / f1;
        for (size_t i = 0; i < t.size(); ++i) {
            const double cycles = std::abs(r - 1.0) < 1e-12
                                      ? f1 * t[i]
                                      : f1 * duration / std::log(r) * (std::pow(r, t[i] / duration) - 1.0);
            y[i] = offset + amplitude * std::sin(kTwoPi * frac(cycles));
        }
        break;
    }
    case F::Sinc: {
        // sinc(2 f (t - D/2)): main lobe centred in the segment, zeros every 1 / (2f) (observed).
        const double f = frequency > 0 ? frequency : 1.0 / duration;
        for (size_t i = 0; i < t.size(); ++i) {
            const double z = 2.0 * f * (t[i] - duration / 2.0);
            const double s = std::abs(z) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * z) / (std::numbers::pi * z);
            y[i] = offset + amplitude * s;
        }
        break;
    }
    case F::Multitone: {
        // Tones on every DFT bin from Start to Stop Freq (bin spacing = 1 / duration), Schroeder
        // phases, inverse FFT, then scaled so the peak equals Amplitude.
        const double df = rate / double(n);
        const double f1 = std::max(0.0, std::min(p("startFrequency", 1.0), p("stopFrequency", 10.0)));
        const double f2 = std::max(p("startFrequency", 1.0), p("stopFrequency", 10.0));
        const size_t half = n / 2;
        size_t k1 = size_t(std::ceil(f1 / df - 1e-9));
        size_t k2 = std::min(half, size_t(std::floor(f2 / df + 1e-9)));
        k1 = std::max<size_t>(k1, 1);
        if (k1 > k2)
            k2 = k1 = std::clamp<size_t>(size_t(std::llround(f1 / df)), 1, std::max<size_t>(1, half));
        std::vector<mydaq::dsp::Complex> spectrum(n);
        const double tones = double(k2 - k1 + 1);
        if (n >= 2 && k1 <= half) {
            for (size_t k = k1; k <= k2; ++k) {
                const double index = double(k - k1 + 1);
                const double phase = -std::numbers::pi * index * (index - 1.0) / tones;
                spectrum[k] = std::polar(1.0, phase);
                if (k != n - k)
                    spectrum[n - k] = std::conj(spectrum[k]);
            }
            mydaq::dsp::ifft(spectrum);
        }
        std::vector<double> base(n);
        double peak = 0;
        for (size_t i = 0; i < n; ++i) {
            base[i] = spectrum[i].real();
            peak = std::max(peak, std::abs(base[i]));
        }
        const double scale = peak > 0 ? amplitude / peak : 0.0;
        for (size_t i = 0; i < t.size(); ++i) {
            // Warped time (FM) reads the tone table cyclically.
            const double position = frac(t[i] / duration) * double(n);
            const size_t k = std::min(n - 1, size_t(position));
            y[i] = offset + base[k] * scale;
        }
        break;
    }
    case F::Trapezoid:
    case F::Pulse: {
        const bool smooth = component.function == F::Pulse;
        const double delay = std::clamp(p("delay", 20), 0.0, 100.0);
        const double rise = std::clamp(p("rise", 20), 0.0, 100.0);
        const double hold = std::clamp(p("hold", 20), 0.0, 100.0);
        const double fall = std::clamp(p("fall", 20), 0.0, 100.0);
        for (size_t i = 0; i < t.size(); ++i) {
            const double percent = 100.0 * t[i] / duration;
            y[i] = amplitude * trapezoidShape(percent, delay, rise, hold, fall, smooth);
        }
        break;
    }
    case F::Stairstep: {
        const double s = p("start", -1.0);
        const double e = p("end", 1.0);
        const int steps = std::max(1, int(std::lround(p("steps", 10))));
        for (size_t i = 0; i < t.size(); ++i) {
            const int level = std::clamp(int(std::floor(t[i] / duration * steps)), 0, steps - 1);
            y[i] = steps == 1 ? s : s + (e - s) * double(level) / double(steps - 1);
        }
        break;
    }
    case F::Circle:
        // One arch per period spanning -A ... +A (observed): 2 sqrt(1 - (2u - 1)^2) - 1.
        for (size_t i = 0; i < t.size(); ++i) {
            const double u = frac(frequency * t[i]);
            y[i] = offset + amplitude * (2.0 * std::sqrt(std::max(0.0, 1.0 - std::pow(2.0 * u - 1.0, 2))) - 1.0);
        }
        break;
    case F::Haversine: {
        const double s = p("start", -1.0);
        const double e = p("end", 1.0);
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = (s - e) / 2 * std::cos(std::numbers::pi * t[i] / duration) + (s + e) / 2;
        break;
    }
    }
    Q_UNUSED(segmentIndex)
    Q_UNUSED(warnings)
    return y;
}

// Renders segment `k` of `document` at `rate`; componentOnly >= 0 renders that component alone.
std::vector<double> renderSegmentSamples(const Segment& segment, int k, double rate, int componentOnly,
                                         QStringList& warnings, bool& alias)
{
    const size_t n = segmentSamples(segment.duration, rate);
    std::vector<double> t(n);
    for (size_t i = 0; i < n; ++i)
        t[i] = double(i) / rate;

    for (const Component& c : segment.components) {
        if (componentAliases(c, rate))
            alias = true;
    }
    if (componentOnly >= 0) {
        if (size_t(componentOnly) >= segment.components.size())
            return std::vector<double>(n, 0.0);
        return evaluateComponent(segment.components[size_t(componentOnly)], t, n, rate, k, warnings);
    }

    std::vector<double> acc(n, 0.0);
    size_t divisionByZero = 0;
    for (size_t ci = 0; ci < segment.components.size(); ++ci) {
        const Component& c = segment.components[ci];
        if (ci > 0 && c.op == Operator::FM) {
            // Time warp: the carrier runs at (1 + acc) times its rate.
            std::vector<double> tau(n);
            double sum = 0;
            for (size_t i = 0; i < n; ++i) {
                tau[i] = sum;
                sum += (1.0 + acc[i]) / rate;
            }
            acc = evaluateComponent(c, tau, n, rate, k, warnings);
            continue;
        }
        const std::vector<double> y = evaluateComponent(c, t, n, rate, k, warnings);
        if (ci == 0) {
            acc = y;
            continue;
        }
        switch (c.op) {
        case Operator::Add:
            for (size_t i = 0; i < n; ++i)
                acc[i] += y[i];
            break;
        case Operator::Subtract:
            for (size_t i = 0; i < n; ++i)
                acc[i] -= y[i];
            break;
        case Operator::Multiply:
            for (size_t i = 0; i < n; ++i)
                acc[i] *= y[i];
            break;
        case Operator::Divide:
            for (size_t i = 0; i < n; ++i) {
                if (std::abs(y[i]) < 1e-12) {
                    acc[i] = 0;
                    ++divisionByZero;
                } else {
                    acc[i] /= y[i];
                }
            }
            break;
        case Operator::FM:
            break;
        }
    }
    if (divisionByZero > 0)
        warnings.append(tr("Division by zero in segment %1: %2 samples set to 0").arg(k + 1).arg(divisionByZero));
    size_t invalid = 0;
    for (double& v : acc) {
        v *= segment.gain;
        if (!std::isfinite(v)) {
            v = 0;
            ++invalid;
        }
    }
    if (invalid > 0)
        warnings.append(tr("Expression produced invalid values in segment %1 (%2 samples set to 0)").arg(k + 1).arg(invalid));
    return acc;
}

double effectiveRate(const Document& document, double rate)
{
    return rate > 0 ? rate : document.sampleRate;
}

QString opKey(Operator op)
{
    switch (op) {
    case Operator::Add: return QStringLiteral("+");
    case Operator::Subtract: return QStringLiteral("-");
    case Operator::Multiply: return QStringLiteral("*");
    case Operator::Divide: return QStringLiteral("/");
    case Operator::FM: return QStringLiteral("fm");
    }
    return QStringLiteral("+");
}

std::optional<Operator> opFromKey(const QString& key)
{
    const QString k = key.trimmed().toLower();
    if (k == u"+" || k.isEmpty())
        return Operator::Add;
    if (k == u"-" || k == u"--")
        return Operator::Subtract;
    if (k == u"*" || k == u"x")
        return Operator::Multiply;
    if (k == u"/")
        return Operator::Divide;
    if (k == u"fm")
        return Operator::FM;
    return std::nullopt;
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

}  // namespace

// --- Library info -------------------------------------------------------------------------------

const FunctionInfo& functionInfo(LibraryFunction function)
{
    const auto& table = library();
    const int index = std::clamp(int(function), 0, kLibraryFunctionCount - 1);
    return table[size_t(index)];
}

std::optional<LibraryFunction> functionFromId(const QString& id)
{
    if (id.compare(u"triangleNoise"_s, Qt::CaseInsensitive) == 0)
        return LibraryFunction::TriangularNoise;   // gen.md 4.3.10 spelling
    for (const FunctionInfo& info : library()) {
        if (id.compare(QLatin1StringView(info.id), Qt::CaseInsensitive) == 0)
            return info.function;
    }
    return std::nullopt;
}

double coercePercentParam(const Component& component, const char* key, double value)
{
    if (component.type != ComponentType::Library
        || (component.function != LibraryFunction::Trapezoid && component.function != LibraryFunction::Pulse))
        return value;
    static const char* keys[] = {"delay", "rise", "hold", "fall"};
    double others = 0;
    bool breakpoint = false;
    for (const char* k : keys) {
        if (std::strcmp(k, key) == 0)
            breakpoint = true;
        else
            others += std::clamp(component.param(k), 0.0, 100.0);
    }
    return breakpoint ? std::clamp(value, 0.0, std::max(0.0, 100.0 - others)) : value;
}

QString functionName(LibraryFunction function)
{
    return QCoreApplication::translate("Waveform", functionInfo(function).name);
}

QString operatorSymbol(Operator op)
{
    switch (op) {
    case Operator::Add: return QStringLiteral("+");
    case Operator::Subtract: return QStringLiteral("-");
    case Operator::Multiply: return QStringLiteral("x");
    case Operator::Divide: return QStringLiteral("/");
    case Operator::FM: return QStringLiteral("FM");
    }
    return {};
}

QString unitName(FrequencyUnit unit)
{
    switch (unit) {
    case FrequencyUnit::kHz: return QStringLiteral("kHz");
    case FrequencyUnit::MHz: return QStringLiteral("MHz");
    case FrequencyUnit::Hz: break;
    }
    return QStringLiteral("Hz");
}

double unitScale(FrequencyUnit unit)
{
    switch (unit) {
    case FrequencyUnit::kHz: return 1e3;
    case FrequencyUnit::MHz: return 1e6;
    case FrequencyUnit::Hz: break;
    }
    return 1.0;
}

// --- Component / document ---------------------------------------------------------------------

double Component::param(const char* key, double fallback) const
{
    const int index = paramIndex(functionInfo(function), key);
    return index >= 0 ? params[size_t(index)] : fallback;
}

void Component::setParam(const char* key, double value)
{
    const int index = paramIndex(functionInfo(function), key);
    if (index >= 0)
        params[size_t(index)] = value;
}

Component Component::library(LibraryFunction function)
{
    Component c;
    c.type = ComponentType::Library;
    c.function = function;
    c.params.fill(0.0);
    const FunctionInfo& info = functionInfo(function);
    for (size_t i = 0; i < info.params.size(); ++i)
        c.params[i] = info.params[i].defaultValue;
    return c;
}

Component Component::makeExpression(const QString& text)
{
    Component c;
    c.type = ComponentType::Expression;
    c.expression = text;
    return c;
}

Component Component::makeSketch(const QString& name, QList<double> samples)
{
    Component c;
    c.type = ComponentType::Sketch;
    c.name = name;
    c.samples = std::move(samples);
    if (c.samples.size() < 2)
        c.samples.resize(2);
    return c;
}

void Component::setFunction(LibraryFunction newFunction)
{
    const FunctionInfo& oldInfo = functionInfo(function);
    const auto oldParams = params;
    const bool wasLibrary = type == ComponentType::Library;
    const Component fresh = library(newFunction);
    params = fresh.params;
    const FunctionInfo& newInfo = functionInfo(newFunction);
    if (wasLibrary) {
        for (size_t i = 0; i < newInfo.params.size(); ++i) {
            const int old = paramIndex(oldInfo, newInfo.params[i].key);
            if (old >= 0)
                params[i] = oldParams[size_t(old)];
        }
    }
    function = newFunction;
    type = ComponentType::Library;
}

size_t segmentSamples(double duration, double rate)
{
    if (!(rate > 0) || !std::isfinite(duration) || duration <= 0)
        return 1;
    const double n = std::round(duration * rate);
    return n < 1 ? 1 : n > 1e12 ? size_t(1e12) : size_t(n);
}

size_t Document::totalSamples(double rate) const
{
    const double r = rate > 0 ? rate : sampleRate;
    size_t total = 0;
    for (const Segment& s : segments)
        total += segmentSamples(s.duration, r);
    return total;
}

double Document::totalDuration() const
{
    double total = 0;
    for (const Segment& s : segments)
        total += s.duration;
    return total;
}

bool componentAliases(const Component& component, double rate)
{
    if (component.type != ComponentType::Library)
        return false;
    const FunctionInfo& info = functionInfo(component.function);
    for (size_t i = 0; i < info.params.size(); ++i) {
        if (info.params[i].kind == ParamKind::Frequency && component.params[i] > rate / 2)
            return true;
    }
    return false;
}

// --- Rendering ----------------------------------------------------------------------------------

RenderResult render(const Document& document, double rate)
{
    RenderResult result;
    const double r = effectiveRate(document, rate);
    result.sampleRate = r;
    const size_t total = document.totalSamples(r);
    if (total > kMaxWaveformSamples) {
        result.warnings.append(tr("The waveform would have %1 samples (max 10 000 000).").arg(total));
        return result;
    }
    result.samples.reserve(total);
    for (size_t k = 0; k < document.segments.size(); ++k) {
        result.segmentStarts.push_back(result.samples.size());
        const std::vector<double> y =
            renderSegmentSamples(document.segments[k], int(k), r, -1, result.warnings, result.alias);
        result.samples.insert(result.samples.end(), y.begin(), y.end());
    }
    if (result.alias)
        result.warnings.append(tr("A frequency is above half the sample rate: the waveform will alias."));
    return result;
}

RenderResult renderSegment(const Document& document, size_t segment, double rate)
{
    RenderResult result;
    const double r = effectiveRate(document, rate);
    result.sampleRate = r;
    if (segment >= document.segments.size())
        return result;
    if (segmentSamples(document.segments[segment].duration, r) > kMaxWaveformSamples)
        return result;
    result.segmentStarts.push_back(0);
    result.samples = renderSegmentSamples(document.segments[segment], int(segment), r, -1, result.warnings, result.alias);
    return result;
}

RenderResult renderComponent(const Document& document, size_t segment, size_t component, double rate)
{
    RenderResult result;
    const double r = effectiveRate(document, rate);
    result.sampleRate = r;
    if (segment >= document.segments.size())
        return result;
    if (segmentSamples(document.segments[segment].duration, r) > kMaxWaveformSamples)
        return result;
    result.segmentStarts.push_back(0);
    result.samples = renderSegmentSamples(document.segments[segment], int(segment), r, int(component),
                                          result.warnings, result.alias);
    return result;
}

// --- JSON -----------------------------------------------------------------------------------------

QJsonObject componentToJson(const Component& c)
{
    QJsonObject o;
    o[u"op"] = opKey(c.op);
    switch (c.type) {
    case ComponentType::Library: {
        const FunctionInfo& info = functionInfo(c.function);
        o[u"type"] = u"library"_s;
        o[u"function"] = QString::fromLatin1(info.id);
        QJsonObject params;
        for (size_t i = 0; i < info.params.size(); ++i)
            params[QString::fromLatin1(info.params[i].key)] = c.params[i];
        if (info.noise)
            params[u"seed"] = double(c.seed);
        o[u"params"] = params;
        break;
    }
    case ComponentType::Expression:
        o[u"type"] = u"expression"_s;
        o[u"expression"] = c.expression;
        o[u"from"] = c.from;
        o[u"to"] = c.to;
        o[u"cycles"] = c.cycles;
        o[u"gain"] = c.gain;
        break;
    case ComponentType::Sketch: {
        o[u"type"] = u"sketch"_s;
        o[u"name"] = c.name;
        o[u"cycles"] = c.cycles;
        o[u"gain"] = c.gain;
        if (c.samples.size() > 10000) {
            QByteArray raw(c.samples.size() * 4, Qt::Uninitialized);
            for (qsizetype i = 0; i < c.samples.size(); ++i) {
                const float f = float(c.samples[i]);
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                for (int b = 0; b < 4; ++b)
                    raw[i * 4 + b] = char((bits >> (8 * b)) & 0xFF);
            }
            o[u"samplesBase64"] = QString::fromLatin1(raw.toBase64());
        } else {
            QJsonArray samples;
            for (double v : c.samples)
                samples.append(v);
            o[u"samples"] = samples;
        }
        if (!c.vertices.isEmpty()) {
            QJsonArray vertices;
            for (const QPointF& v : c.vertices)
                vertices.append(QJsonArray{v.x(), v.y()});
            o[u"vertices"] = vertices;
        }
        break;
    }
    }
    return o;
}

std::optional<Component> componentFromJson(const QJsonObject& o, QString* error)
{
    const auto op = opFromKey(o.value(u"op").toString());
    if (!op) {
        setError(error, tr("unknown operator '%1'").arg(o.value(u"op").toString()));
        return std::nullopt;
    }
    const QString type = o.value(u"type").toString().toLower();
    Component c;
    if (type == u"library") {
        const auto function = functionFromId(o.value(u"function").toString());
        if (!function) {
            setError(error, tr("unknown library function '%1'").arg(o.value(u"function").toString()));
            return std::nullopt;
        }
        c = Component::library(*function);
        const FunctionInfo& info = functionInfo(*function);
        const QJsonObject params = o.value(u"params").toObject();
        for (size_t i = 0; i < info.params.size(); ++i) {
            const QString key = QString::fromLatin1(info.params[i].key);
            // Accept the parameter in "params" or at the top level (older/hand-written files).
            const QJsonValue v = params.contains(key) ? params.value(key) : o.value(key);
            if (v.isDouble())
                c.params[i] = v.toDouble();
        }
        if (params.value(u"seed").isDouble())
            c.seed = uint32_t(std::clamp(params.value(u"seed").toDouble(), 0.0, 2147483647.0));
    } else if (type == u"expression") {
        c = Component::makeExpression(o.value(u"expression").toString(QStringLiteral("sin(2*pi*x)")));
        c.from = o.value(u"from").toDouble(0.0);
        c.to = o.value(u"to").toDouble(1.0);
        c.cycles = o.value(u"cycles").toDouble(1.0);
        c.gain = o.value(u"gain").toDouble(1.0);
    } else if (type == u"sketch") {
        QList<double> samples;
        if (o.contains(u"samplesBase64")) {
            const QByteArray raw = QByteArray::fromBase64(o.value(u"samplesBase64").toString().toLatin1());
            samples.reserve(raw.size() / 4);
            for (qsizetype i = 0; i + 3 < raw.size(); i += 4) {
                uint32_t bits = 0;
                for (int b = 0; b < 4; ++b)
                    bits |= uint32_t(uint8_t(raw[i + b])) << (8 * b);
                float f;
                std::memcpy(&f, &bits, 4);
                samples.append(std::isfinite(f) ? double(f) : 0.0);
            }
        } else {
            for (const QJsonValue& v : o.value(u"samples").toArray())
                samples.append(v.toDouble());
        }
        c = Component::makeSketch(o.value(u"name").toString(), samples);
        c.cycles = o.value(u"cycles").toDouble(1.0);
        c.gain = o.value(u"gain").toDouble(1.0);
        for (const QJsonValue& v : o.value(u"vertices").toArray()) {
            const QJsonArray pair = v.toArray();
            if (pair.size() == 2)
                c.vertices.append(QPointF(pair[0].toDouble(), pair[1].toDouble()));
        }
    } else {
        setError(error, tr("unknown component type '%1'").arg(type));
        return std::nullopt;
    }
    if (!(c.cycles > 0))
        c.cycles = 1.0;
    c.op = *op;
    return c;
}

QJsonObject segmentToJson(const Segment& segment)
{
    QJsonObject o;
    o[u"name"] = segment.name;
    o[u"duration"] = segment.duration;
    o[u"gain"] = segment.gain;
    QJsonArray components;
    for (const Component& c : segment.components)
        components.append(componentToJson(c));
    o[u"components"] = components;
    return o;
}

std::optional<Segment> segmentFromJson(const QJsonObject& o, QString* error)
{
    Segment s;
    s.name = o.value(u"name").toString();
    s.duration = o.value(u"duration").toDouble(1.0);
    if (!(s.duration > 0) || !std::isfinite(s.duration)) {
        setError(error, tr("invalid segment duration"));
        return std::nullopt;
    }
    s.duration = std::clamp(s.duration, 1e-4, 1e5);
    s.gain = o.value(u"gain").toDouble(1.0);
    for (const QJsonValue& v : o.value(u"components").toArray()) {
        auto c = componentFromJson(v.toObject(), error);
        if (!c)
            return std::nullopt;
        s.components.push_back(std::move(*c));
    }
    return s;
}

QJsonObject toJson(const Document& document)
{
    QJsonObject o;
    o[u"format"] = u"mybench-waveform"_s;
    o[u"version"] = 1;
    o[u"name"] = document.name;
    o[u"info"] = document.info;
    o[u"sampleRate"] = document.sampleRate;
    o[u"units"] = unitName(document.units);
    QJsonArray segments;
    for (const Segment& s : document.segments)
        segments.append(segmentToJson(s));
    o[u"segments"] = segments;
    return o;
}

std::optional<Document> fromJson(const QJsonObject& o, QString* error)
{
    if (o.value(u"format").toString() != u"mybench-waveform") {
        setError(error, tr("not a MYBENCH waveform"));
        return std::nullopt;
    }
    if (o.value(u"version").toInt(1) > 1) {
        setError(error, tr("This waveform was made by a newer MYBENCH"));
        return std::nullopt;
    }
    Document d;
    d.name = o.value(u"name").toString(QStringLiteral("Untitled"));
    d.info = o.value(u"info").toString();
    d.sampleRate = std::clamp(o.value(u"sampleRate").toDouble(10000.0), 1.0, 1e6);
    const QString units = o.value(u"units").toString();
    d.units = units == u"kHz" ? FrequencyUnit::kHz : units == u"MHz" ? FrequencyUnit::MHz : FrequencyUnit::Hz;
    d.segments.clear();
    for (const QJsonValue& v : o.value(u"segments").toArray()) {
        auto s = segmentFromJson(v.toObject(), error);
        if (!s)
            return std::nullopt;
        d.segments.push_back(std::move(*s));
    }
    if (d.segments.empty())
        d.segments.push_back(Segment{QString(), 10.0, 1.0, {}});
    return d;
}

QByteArray serialize(const Document& document)
{
    return QJsonDocument(toJson(document)).toJson(QJsonDocument::Indented);
}

std::optional<Document> parse(const QByteArray& data, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (!json.isObject()) {
        setError(error, tr("not a MYBENCH waveform"));
        return std::nullopt;
    }
    return fromJson(json.object(), error);
}

Document documentFromSamples(const std::vector<double>& samples, double rate, const QString& name)
{
    Document d;
    d.name = name;
    d.sampleRate = std::clamp(rate > 0 ? rate : 10000.0, 1.0, 1e6);
    Segment s;
    s.duration = std::max(1e-4, double(std::max<size_t>(samples.size(), 1)) / d.sampleRate);
    QList<double> values(samples.begin(), samples.end());
    // One cycle of M values spans M - 1 intervals: append the first value so that N samples read
    // back exactly the imported values.
    if (!values.isEmpty())
        values.append(values.front());
    s.components.push_back(Component::makeSketch(name, values));
    d.segments = {s};
    return d;
}

// --- Noise ------------------------------------------------------------------------------------------

NoiseGenerator::NoiseGenerator(uint64_t seed)
{
    uint64_t x = seed;
    for (uint64_t& s : m_s) {
        x += 0x9E3779B97F4A7C15ULL;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        s = z ^ (z >> 31);
    }
}

uint64_t NoiseGenerator::next()
{
    auto rotl = [](uint64_t v, int k) { return (v << k) | (v >> (64 - k)); };
    const uint64_t result = rotl(m_s[1] * 5, 7) * 9;
    const uint64_t t = m_s[1] << 17;
    m_s[2] ^= m_s[0];
    m_s[3] ^= m_s[1];
    m_s[1] ^= m_s[2];
    m_s[0] ^= m_s[3];
    m_s[2] ^= t;
    m_s[3] = rotl(m_s[3], 45);
    return result;
}

double NoiseGenerator::uniform()
{
    return double(next() >> 11) * 0x1.0p-53;
}

double NoiseGenerator::gaussian()
{
    if (m_hasSpare) {
        m_hasSpare = false;
        return m_spare;
    }
    double u1 = uniform();
    while (u1 <= 0.0)
        u1 = uniform();
    const double u2 = uniform();
    const double r = std::sqrt(-2.0 * std::log(u1));
    m_spare = r * std::sin(kTwoPi * u2);
    m_hasSpare = true;
    return r * std::cos(kTwoPi * u2);
}

}  // namespace mybench::instruments::gen
