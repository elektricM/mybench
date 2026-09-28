// Waveform documents of the Waveform Editor and their renderer (docs/instruments/gen.md 4.3,
// docs/instruments/algorithms/gen.md 3). UI-free (QtCore only): used by the editor, by the
// Arbitrary channels (.mbw files) and by the unit tests.
//
// A document is a sequence of segments; each segment has a duration and a list of components
// combined left to right with an operator (+, -, x, /, FM). A component is a Library function (the
// 21 functions of the ELVISmx editor, with their own parameter lists), an Expression of x, or a
// freehand Sketch. Segment k is rendered with N_k = round(duration_k * sampleRate) samples; the
// waveform is the concatenation of the segments.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace mybench::instruments::gen {

// Largest rendered waveform (samples), editor and file loading.
inline constexpr size_t kMaxWaveformSamples = 10'000'000;

enum class ComponentType { Library, Expression, Sketch };
enum class Operator { Add, Subtract, Multiply, Divide, FM };
enum class FrequencyUnit { Hz, kHz, MHz };

// The ELVISmx editor's function library, in its list order (index = ELVISmx function index).
enum class LibraryFunction {
    DcLevel, Sine, Square, Triangle, Sawtooth, UniformNoise, TriangularNoise, GaussianNoise,
    RisingExponential, FallingExponential, Hanning, Ramp, LinearSweep, LogSweep, Sinc, Multitone,
    Trapezoid, Stairstep, Circle, Haversine, Pulse,
};
inline constexpr int kLibraryFunctionCount = 21;
inline constexpr int kMaxLibraryParams = 5;

enum class ParamKind { Voltage, Frequency, Degrees, Percent, Seconds, Count };

struct ParamInfo {
    const char* key;      // stable id in .mbw files ("amplitude", "startFrequency"...)
    const char* label;    // UI label ("Amplitude", "Start Freq")
    ParamKind kind;
    double defaultValue;
    double minimum;
    double maximum;
};

struct FunctionInfo {
    LibraryFunction function;
    const char* id;          // .mbw id ("sine", "gaussianNoise"...)
    const char* name;        // list name ("Gaussian Noise")
    const char* shortName;   // segment-list name ("Gaussian Noise", "Rising Exp", "Lin Sweep"...)
    std::vector<ParamInfo> params;   // P0.. in ELVISmx order
    bool periodic;           // repeats with a frequency parameter (Freq = 0: one cycle per segment)
    bool noise;              // uses the seeded noise generator
};

const FunctionInfo& functionInfo(LibraryFunction function);
std::optional<LibraryFunction> functionFromId(const QString& id);
QString functionName(LibraryFunction function);
QString operatorSymbol(Operator op);   // "+", "-", "x", "/", "FM" (as in the editor)
QString unitName(FrequencyUnit unit);  // "Hz", "kHz", "MHz"
double unitScale(FrequencyUnit unit);  // 1, 1e3, 1e6

struct Component {
    ComponentType type = ComponentType::Library;
    Operator op = Operator::Add;
    // Library
    LibraryFunction function = LibraryFunction::Sine;
    std::array<double, kMaxLibraryParams> params{1.0, 0.0, 1.0, 0.0, 0.0};
    uint32_t seed = 1;   // noise functions [MYBENCH]: reproducible rendering
    // Expression
    QString expression = QStringLiteral("sin(2*pi*x)");
    double from = 0.0;
    double to = 1.0;
    // Expression and Sketch
    double cycles = 1.0;
    double gain = 1.0;   // Scale Selection factor of Expression/Sketch components
    // Sketch
    QString name;
    QList<double> samples;     // one cycle
    QList<QPointF> vertices;   // Line-mode vertices: (position 0..1 in the cycle, value)

    double param(const char* key, double fallback = 0.0) const;
    void setParam(const char* key, double value);
    static Component library(LibraryFunction function);   // with that function's defaults
    static Component makeExpression(const QString& text = QStringLiteral("sin(2*pi*x)"));
    static Component makeSketch(const QString& name, QList<double> samples = QList<double>(1000, 0.0));
    // Switches the library function: defaults of the new function, keeping the values of the
    // parameters both functions have (amplitude, offset, frequency...).
    void setFunction(LibraryFunction newFunction);
    bool operator==(const Component&) const = default;
};

struct Segment {
    QString name;
    double duration = 1.0;   // s
    double gain = 1.0;       // Scale Selection
    std::vector<Component> components;
    bool operator==(const Segment&) const = default;
};

struct Document {
    QString name = QStringLiteral("Untitled");
    QString info;
    double sampleRate = 10000.0;   // S/s
    FrequencyUnit units = FrequencyUnit::Hz;
    std::vector<Segment> segments{Segment{QString(), 10.0, 1.0, {}}};

    size_t totalSamples(double rate = 0) const;   // 0: the document's rate
    double totalDuration() const;
    bool operator==(const Document&) const = default;
};

// Samples of segment `duration` at `rate` (at least 1).
size_t segmentSamples(double duration, double rate);

// --- Rendering --------------------------------------------------------------------------------

struct RenderResult {
    std::vector<double> samples;
    std::vector<size_t> segmentStarts;   // index of each segment's first sample
    QStringList warnings;                // user-facing, one per problem
    bool alias = false;                  // a frequency exceeds rate / 2
    double sampleRate = 0;
};

// The whole waveform at `rate` (0: the document's rate). Refuses (warning, empty samples) beyond
// kMaxWaveformSamples.
RenderResult render(const Document& document, double rate = 0);
// One segment (local time) and one component alone (before combination), for the editor views.
RenderResult renderSegment(const Document& document, size_t segment, double rate = 0);
RenderResult renderComponent(const Document& document, size_t segment, size_t component, double rate = 0);
// A frequency parameter of this component exceeds rate / 2.
bool componentAliases(const Component& component, double rate);

// --- .mbw files (JSON, docs/instruments/gen.md 4.3.10) -----------------------------------------

QJsonObject toJson(const Document& document);
std::optional<Document> fromJson(const QJsonObject& json, QString* error = nullptr);
QByteArray serialize(const Document& document);
std::optional<Document> parse(const QByteArray& data, QString* error = nullptr);
QJsonObject componentToJson(const Component& component);
std::optional<Component> componentFromJson(const QJsonObject& json, QString* error = nullptr);
QJsonObject segmentToJson(const Segment& segment);
std::optional<Segment> segmentFromJson(const QJsonObject& json, QString* error = nullptr);

// A document holding `samples` as one Sketch component (imports; WAV/CSV opened in the editor).
Document documentFromSamples(const std::vector<double>& samples, double rate, const QString& name);

// Seeded uniform generator of the noise functions: xoshiro256** seeded through SplitMix64.
class NoiseGenerator
{
public:
    explicit NoiseGenerator(uint64_t seed);
    uint64_t next();
    double uniform();    // [0, 1)
    double gaussian();   // standard normal (Box-Muller)

private:
    std::array<uint64_t, 4> m_s{};
    bool m_hasSpare = false;
    double m_spare = 0;
};

}  // namespace mybench::instruments::gen
