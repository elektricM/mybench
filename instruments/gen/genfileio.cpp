#include "genfileio.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace mybench::instruments::gen {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("WaveformFile", text);
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

uint32_t le32(const char* p)
{
    return uint32_t(uint8_t(p[0])) | uint32_t(uint8_t(p[1])) << 8 | uint32_t(uint8_t(p[2])) << 16
           | uint32_t(uint8_t(p[3])) << 24;
}

uint16_t le16(const char* p)
{
    return uint16_t(uint8_t(p[0]) | uint8_t(p[1]) << 8);
}

uint64_t be64(const char* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | uint8_t(p[i]);
    return v;
}

int32_t be32(const char* p)
{
    return int32_t(uint32_t(uint8_t(p[0])) << 24 | uint32_t(uint8_t(p[1])) << 16 | uint32_t(uint8_t(p[2])) << 8
                   | uint32_t(uint8_t(p[3])));
}

double beDouble(const char* p)
{
    return std::bit_cast<double>(be64(p));
}

float beFloat(const char* p)
{
    return std::bit_cast<float>(uint32_t(be32(p)));
}

uint16_t be16(const char* p)
{
    return uint16_t(uint8_t(p[0]) << 8 | uint8_t(p[1]));
}

void putLe32(QByteArray& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

void putLe16(QByteArray& out, uint16_t v)
{
    out.append(char(v & 0xFF));
    out.append(char(v >> 8));
}

void putBe64(QByteArray& out, uint64_t v)
{
    for (int i = 7; i >= 0; --i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

void putBe32(QByteArray& out, uint32_t v)
{
    for (int i = 3; i >= 0; --i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

// Replaces NaN/inf by 0 and reports how many there were.
void cleanSamples(LoadedWaveform& w)
{
    size_t invalid = 0;
    for (double& v : w.samples) {
        if (!std::isfinite(v)) {
            v = 0;
            ++invalid;
        }
    }
    if (invalid > 0)
        w.notices.append(tr("%1 invalid samples (NaN or infinity) were set to 0.").arg(invalid));
}

std::optional<double> parseNumber(QString field, bool commaIsDecimal)
{
    field = field.trimmed();
    if (field.startsWith(u'"') && field.endsWith(u'"') && field.size() >= 2)
        field = field.mid(1, field.size() - 2).trimmed();
    if (commaIsDecimal)
        field.replace(u',', u'.');
    if (field.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double v = field.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return v;
}

QStringList splitFields(const QString& line, QChar delimiter, bool whitespace)
{
    if (whitespace)
        return line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return line.split(delimiter);
}

QStringList splitAny(const QString& line, const QString& delimiters)
{
    QStringList fields;
    QString current;
    for (QChar c : line) {
        if (delimiters.contains(c)) {
            if (!(c == u' ' && current.isEmpty()))   // collapse repeated spaces
                fields.append(current);
            current.clear();
            if (c == u' ' && !fields.isEmpty() && fields.back().isEmpty())
                fields.removeLast();
        } else {
            current.append(c);
        }
    }
    fields.append(current);
    return fields;
}

}  // namespace

// --- Text -------------------------------------------------------------------------------------------

std::optional<LoadedWaveform> parseText(const QByteArray& data, const TextImportOptions& options, QString* error)
{
    LoadedWaveform w;
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    static const QRegularExpression rateComment(
        QStringLiteral("^#\\s*sample\\s*rate\\s*[=:]\\s*([0-9.eE+-]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rateComment2(
        QStringLiteral("^#\\s*samplerate\\s*[=:]\\s*([0-9.eE+-]+)"), QRegularExpression::CaseInsensitiveOption);

    if (!options.automatic) {
        const int column = std::max(1, options.column) - 1;
        int lineNumber = 0;
        bool started = options.startLine <= 0;
        for (const QString& raw : lines) {
            ++lineNumber;
            if (!started) {
                if (lineNumber < options.startLine)
                    continue;
                started = true;
            }
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            if (const auto m = rateComment.match(line); m.hasMatch() && w.sampleRate == 0)
                w.sampleRate = m.captured(1).toDouble();
            const QStringList fields = splitAny(line, options.delimiters.isEmpty() ? QStringLiteral("\t") : options.delimiters);
            if (column >= fields.size())
                continue;
            const auto v = parseNumber(fields[column], !options.delimiters.contains(u','));
            if (!v)
                continue;
            w.samples.push_back(*v);
            if (options.count > 0 && w.samples.size() >= size_t(options.count))
                break;
            if (w.samples.size() > kMaxWaveformSamples)
                break;
        }
    } else {
        QChar delimiter;
        bool whitespace = false;
        bool delimiterKnown = false;
        bool headerChecked = false;
        int valueColumn = 0;
        bool timeColumn = false;
        std::vector<double> times;
        for (const QString& raw : lines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            if (line.startsWith(u'#')) {
                if (const auto m = rateComment.match(line); m.hasMatch())
                    w.sampleRate = m.captured(1).toDouble();
                else if (const auto m2 = rateComment2.match(line); m2.hasMatch())
                    w.sampleRate = m2.captured(1).toDouble();
                continue;
            }
            if (!delimiterKnown) {
                delimiterKnown = true;
                if (line.contains(u'\t'))
                    delimiter = u'\t';
                else if (line.contains(u';'))
                    delimiter = u';';
                else if (line.contains(u','))
                    delimiter = u',';
                else
                    whitespace = true;
            }
            const QStringList fields = splitFields(line, delimiter, whitespace);
            const bool commaDecimal = whitespace || delimiter != u',';
            if (!headerChecked) {
                headerChecked = true;
                bool header = false;
                for (const QString& f : fields) {
                    if (!parseNumber(f, commaDecimal)) {
                        header = true;
                        break;
                    }
                }
                if (header) {
                    if (!fields.isEmpty() && fields[0].trimmed().startsWith(u"time", Qt::CaseInsensitive)
                        && fields.size() >= 2) {
                        timeColumn = true;
                        valueColumn = 1;
                    }
                    continue;
                }
            }
            if (valueColumn >= fields.size())
                continue;
            const auto v = parseNumber(fields[valueColumn], commaDecimal);
            if (!v)
                continue;
            if (timeColumn && times.size() < 2) {
                if (const auto t = parseNumber(fields[0], commaDecimal))
                    times.push_back(*t);
            }
            w.samples.push_back(*v);
            if (w.samples.size() > kMaxWaveformSamples)
                break;
        }
        if (timeColumn && w.sampleRate == 0 && times.size() == 2 && times[1] > times[0])
            w.sampleRate = 1.0 / (times[1] - times[0]);
    }
    if (w.samples.empty()) {
        setError(error, tr("no numeric data"));
        return std::nullopt;
    }
    if (w.samples.size() > kMaxWaveformSamples) {
        setError(error, tr("too many samples (max 10 000 000)"));
        return std::nullopt;
    }
    if (!(w.sampleRate > 0) || !std::isfinite(w.sampleRate))
        w.sampleRate = 0;
    cleanSamples(w);
    return w;
}

// --- WAV --------------------------------------------------------------------------------------------

std::optional<WavData> parseWav(const QByteArray& data, QString* error)
{
    if (data.size() < 12 || std::memcmp(data.constData(), "RIFF", 4) != 0
        || std::memcmp(data.constData() + 8, "WAVE", 4) != 0) {
        setError(error, tr("not a WAV file"));
        return std::nullopt;
    }
    int format = 0;
    int channels = 0;
    int bits = 0;
    int blockAlign = 0;
    double rate = 0;
    const char* samples = nullptr;
    qsizetype sampleBytes = 0;
    qsizetype pos = 12;
    while (pos + 8 <= data.size()) {
        const char* chunk = data.constData() + pos;
        const qsizetype size = qsizetype(le32(chunk + 4));
        const qsizetype body = pos + 8;
        const qsizetype available = std::min<qsizetype>(size, data.size() - body);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            const char* f = data.constData() + body;
            format = le16(f);
            channels = le16(f + 2);
            rate = double(le32(f + 4));
            blockAlign = le16(f + 12);
            bits = le16(f + 14);
            if (format == 0xFFFE && available >= 26)
                format = le16(f + 24);   // subformat GUID starts with the format code
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            samples = data.constData() + body;
            sampleBytes = available;
        }
        pos = body + size + (size & 1);
    }
    if (channels <= 0 || rate <= 0 || !samples) {
        setError(error, tr("invalid WAV file (missing format or data)"));
        return std::nullopt;
    }
    const int bytes = bits / 8;
    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool ieee = format == 3 && (bits == 32 || bits == 64);
    if (!pcm && !ieee) {
        setError(error, tr("unsupported WAV encoding (format %1, %2 bits)").arg(format).arg(bits));
        return std::nullopt;
    }
    if (blockAlign < bytes * channels)
        blockAlign = bytes * channels;
    const qsizetype frames = sampleBytes / blockAlign;
    if (size_t(frames) > kMaxWaveformSamples) {
        setError(error, tr("too many samples (max 10 000 000)"));
        return std::nullopt;
    }
    WavData wav;
    wav.sampleRate = rate;
    wav.channels.assign(size_t(channels), std::vector<double>(size_t(frames)));
    for (qsizetype i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            const char* p = samples + i * blockAlign + c * bytes;
            double v = 0;
            if (ieee && bits == 32) {
                v = double(std::bit_cast<float>(le32(p)));
            } else if (ieee) {
                const uint64_t raw = uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32;
                v = std::bit_cast<double>(raw);
            } else if (bits == 8) {
                v = (double(uint8_t(p[0])) - 128.0) / 128.0;
            } else if (bits == 16) {
                v = double(int16_t(le16(p))) / 32768.0;
            } else if (bits == 24) {
                int32_t raw = int32_t(uint32_t(uint8_t(p[0])) << 8 | uint32_t(uint8_t(p[1])) << 16
                                      | uint32_t(uint8_t(p[2])) << 24);
                v = double(raw >> 8) / 8388608.0;
            } else {
                v = double(int32_t(le32(p))) / 2147483648.0;
            }
            wav.channels[size_t(c)][size_t(i)] = v;
        }
    }
    return wav;
}

QByteArray encodeWavFloat32(const std::vector<double>& samples, double sampleRate)
{
    QByteArray out;
    const uint32_t dataBytes = uint32_t(samples.size() * 4);
    out.append("RIFF");
    putLe32(out, 36 + dataBytes);
    out.append("WAVE");
    out.append("fmt ");
    putLe32(out, 16);
    putLe16(out, 3);   // IEEE float
    putLe16(out, 1);
    const uint32_t rate = uint32_t(std::max(1.0, std::round(sampleRate)));
    putLe32(out, rate);
    putLe32(out, rate * 4);
    putLe16(out, 4);
    putLe16(out, 32);
    out.append("data");
    putLe32(out, dataBytes);
    for (double v : samples)
        putLe32(out, std::bit_cast<uint32_t>(float(std::clamp(v, -1.0, 1.0))));
    return out;
}

// --- Binary -----------------------------------------------------------------------------------------

std::vector<double> parseBinaryI16(const QByteArray& data, double peakReference)
{
    std::vector<double> out(size_t(data.size() / 2));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = double(int16_t(le16(data.constData() + 2 * i))) * peakReference / 32768.0;
    return out;
}

QByteArray encodeBinaryI16(const std::vector<double>& samples, double peakReference)
{
    QByteArray out;
    out.reserve(qsizetype(samples.size() * 2));
    for (double v : samples) {
        const double code = std::clamp(std::round(v / peakReference * 32768.0), -32768.0, 32767.0);
        putLe16(out, uint16_t(int16_t(code)));
    }
    return out;
}

// --- WDT --------------------------------------------------------------------------------------------

std::vector<WdtWaveform> parseWdt(const QByteArray& data)
{
    std::vector<WdtWaveform> result;
    const char* p = data.constData();
    const qsizetype size = data.size();
    qsizetype o = 16;   // a timestamp precedes dt
    while (o + 12 <= size) {
        const double dt = beDouble(p + o);
        const int32_t n = be32(p + o + 8);
        const bool plausible = std::isfinite(dt) && dt >= 1e-9 && dt <= 1e4 && n >= 2
                               && qsizetype(n) <= (size - o - 12) / 8 && size_t(n) <= kMaxWaveformSamples;
        if (plausible) {
            WdtWaveform w;
            w.dt = dt;
            w.samples.resize(size_t(n));
            bool ok = true;
            for (int32_t i = 0; i < n; ++i) {
                const double v = beDouble(p + o + 12 + 8 * qsizetype(i));
                if (!std::isfinite(v) || std::abs(v) > 1e6) {
                    ok = false;
                    break;
                }
                w.samples[size_t(i)] = v;
            }
            if (ok) {
                result.push_back(std::move(w));
                o += 12 + 8 * qsizetype(n);
                continue;
            }
        }
        ++o;
    }
    return result;
}

QByteArray encodeWdt(const std::vector<WdtWaveform>& waveforms)
{
    QByteArray out("MYBENCH-WDT\0\0\0\0\0", 16);
    putBe32(out, uint32_t(waveforms.size()));
    for (const WdtWaveform& w : waveforms) {
        putBe64(out, 0);   // t0 seconds
        putBe64(out, 0);   // t0 fraction
        putBe64(out, std::bit_cast<uint64_t>(w.dt));
        putBe32(out, uint32_t(w.samples.size()));
        for (double v : w.samples)
            putBe64(out, std::bit_cast<uint64_t>(v));
        putBe32(out, 0);   // no attributes
    }
    return out;
}

// --- WFM (NI Waveform Editor document) ------------------------------------------------------------

namespace {

void putBe16(QByteArray& out, uint16_t v)
{
    out.append(char(v >> 8));
    out.append(char(v & 0xFF));
}

void putBeFloat(QByteArray& out, double v)
{
    putBe32(out, std::bit_cast<uint32_t>(float(v)));
}

// Sequential big-endian reader with bounds checks.
struct BeReader {
    const QByteArray& data;
    qsizetype o = 0;
    bool ok = true;
    bool need(qsizetype n)
    {
        ok = ok && n >= 0 && o + n <= data.size();
        return ok;
    }
    int32_t i32()
    {
        if (!need(4))
            return 0;
        const int32_t v = be32(data.constData() + o);
        o += 4;
        return v;
    }
    uint16_t u16()
    {
        if (!need(2))
            return 0;
        const uint16_t v = be16(data.constData() + o);
        o += 2;
        return v;
    }
    double sgl()
    {
        if (!need(4))
            return 0;
        const double v = beFloat(data.constData() + o);
        o += 4;
        return v;
    }
    QString string()
    {
        const int32_t n = i32();
        if (n < 0 || n > 1'000'000 || !need(n)) {
            ok = false;
            return {};
        }
        const QString s = QString::fromLatin1(data.constData() + o, n);
        o += n;
        return s;
    }
};

}  // namespace

std::optional<Document> parseWfm(const QByteArray& data, QString* error)
{
    const QString notRecognised = tr("not a recognised NI Waveform Editor file");
    BeReader r{data};
    const uint16_t units = r.u16();
    const int32_t count = r.i32();
    if (!r.ok || units > 2 || count < 1 || count > 100000) {
        setError(error, notRecognised);
        return std::nullopt;
    }
    Document doc;
    doc.units = FrequencyUnit(units);
    const double scale = unitScale(doc.units);   // Hz per display unit (and display unit per s)
    doc.sampleRate = 10000.0 * scale;
    doc.segments.clear();
    for (int32_t k = 0; k < count && r.ok; ++k) {
        const int32_t type = r.i32();
        const QString text = r.string();
        const int32_t op = r.i32();
        const int32_t a = r.i32();
        const double b = r.sgl();
        const int32_t n = r.i32();
        if (!r.ok || type < 0 || type > 3 || op < 0 || op > 4 || n < 0 || size_t(n) > kMaxWaveformSamples || !r.need(4 * qsizetype(n))) {
            setError(error, notRecognised);
            return std::nullopt;
        }
        std::vector<double> c(static_cast<size_t>(n));
        for (double& v : c)
            v = r.sgl();
        if (type == 0) {
            if (!(b > 0) || !std::isfinite(b)) {
                setError(error, notRecognised);
                return std::nullopt;
            }
            doc.segments.push_back(Segment{text, b / scale, 1.0, {}});
            continue;
        }
        if (doc.segments.empty())
            doc.segments.push_back(Segment{QString(), 10.0 / scale, 1.0, {}});
        Component comp;
        comp.op = Operator(op);
        if (type == 1) {
            if (a < 0 || a >= kLibraryFunctionCount) {
                setError(error, tr("unknown library function %1").arg(a));
                return std::nullopt;
            }
            comp = Component::library(LibraryFunction(a));
            comp.op = Operator(op);
            const FunctionInfo& info = functionInfo(comp.function);
            for (size_t i = 0; i < info.params.size() && i < c.size(); ++i)
                comp.params[i] = info.params[i].kind == ParamKind::Frequency ? c[i] * scale : c[i];
        } else if (type == 2) {
            comp = Component::makeExpression(text);
            comp.op = Operator(op);
            if (c.size() >= 3) {
                comp.from = c[0];
                comp.to = c[1];
                comp.cycles = c[2] > 0 ? c[2] : 1.0;
            }
        } else {
            comp = Component::makeSketch(text, QList<double>(c.begin(), c.end()));
            comp.op = Operator(op);
            comp.cycles = b > 0 ? b : 1.0;
            if (comp.samples.size() < 2)
                comp.samples = QList<double>(2, comp.samples.value(0));
        }
        doc.segments.back().components.push_back(std::move(comp));
    }
    if (!r.ok || r.o != data.size() || doc.segments.empty()) {
        setError(error, notRecognised);
        return std::nullopt;
    }
    return doc;
}

QByteArray encodeWfm(const Document& document)
{
    QByteArray out;
    const double scale = unitScale(document.units);
    putBe16(out, uint16_t(document.units));
    qsizetype records = 0;
    for (const Segment& s : document.segments)
        records += 1 + qsizetype(s.components.size());
    putBe32(out, uint32_t(records));
    auto record = [&](int32_t type, const QString& text, int32_t op, int32_t a, double b, const std::vector<double>& c) {
        putBe32(out, uint32_t(type));
        const QByteArray bytes = text.toLatin1();
        putBe32(out, uint32_t(bytes.size()));
        out.append(bytes);
        putBe32(out, uint32_t(op));
        putBe32(out, uint32_t(a));
        putBeFloat(out, b);
        putBe32(out, uint32_t(c.size()));
        for (double v : c)
            putBeFloat(out, v);
    };
    for (const Segment& s : document.segments) {
        record(0, s.name, 0, 0, s.duration * scale, {});
        for (const Component& comp : s.components) {
            switch (comp.type) {
            case ComponentType::Library: {
                const FunctionInfo& info = functionInfo(comp.function);
                std::vector<double> c;
                for (size_t i = 0; i < info.params.size(); ++i)
                    c.push_back(info.params[i].kind == ParamKind::Frequency ? comp.params[i] / scale : comp.params[i]);
                record(1, QString(), int32_t(comp.op), int32_t(comp.function), 0, c);
                break;
            }
            case ComponentType::Expression:
                record(2, comp.expression, int32_t(comp.op), 0, 0, {comp.from, comp.to, comp.cycles});
                break;
            case ComponentType::Sketch:
                record(3, comp.name, int32_t(comp.op), 0, comp.cycles, std::vector<double>(comp.samples.begin(), comp.samples.end()));
                break;
            }
        }
    }
    return out;
}

// --- Export -----------------------------------------------------------------------------------------

QByteArray encodeCsv(const std::vector<double>& samples, double sampleRate)
{
    QByteArray out;
    out.reserve(qsizetype(samples.size() * 12 + 32));
    out.append("# sampleRate=");
    out.append(QByteArray::number(sampleRate, 'g', 12));
    out.append('\n');
    for (double v : samples) {
        out.append(QByteArray::number(v, 'g', 10));
        out.append('\n');
    }
    return out;
}

bool saveWaveformFile(const QString& path, const std::vector<double>& samples, double sampleRate, QString* error,
                      size_t* clipped)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    QByteArray bytes;
    size_t beyond = 0;
    if (suffix == u"wav") {
        for (double v : samples)
            beyond += std::abs(v) > 1.0;
        bytes = encodeWavFloat32(samples, sampleRate);
    } else if (suffix == u"bin") {
        for (double v : samples)
            beyond += std::abs(v) > 5.0;
        bytes = encodeBinaryI16(samples, 5.0);
    } else if (suffix == u"wdt") {
        bytes = encodeWdt({WdtWaveform{sampleRate > 0 ? 1.0 / sampleRate : 1e-3, samples}});
    } else if (suffix == u"csv" || suffix == u"txt") {
        bytes = encodeCsv(samples, sampleRate);
    } else {
        setError(error, tr("unsupported file type '.%1' (use .csv, .txt, .wav, .bin or .wdt)").arg(suffix));
        return false;
    }
    if (clipped)
        *clipped = beyond;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

// --- Dispatch -----------------------------------------------------------------------------------------

std::optional<LoadedWaveform> loadWaveformFile(const QString& path, QString* error, int channel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return std::nullopt;
    }
    if (file.size() > qint64(400) * 1024 * 1024) {
        setError(error, tr("file too large"));
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    LoadedWaveform w;
    if (suffix == u"mbw") {
        auto document = parse(data, error);
        if (!document)
            return std::nullopt;
        RenderResult rendered = render(*document);
        if (rendered.samples.empty()) {
            setError(error, rendered.warnings.isEmpty() ? tr("empty waveform") : rendered.warnings.front());
            return std::nullopt;
        }
        w.samples = std::move(rendered.samples);
        w.sampleRate = document->sampleRate;
        w.notices = rendered.warnings;
        w.document = std::move(document);
    } else if (suffix == u"wfm") {
        auto document = parseWfm(data, error);
        if (!document)
            return std::nullopt;
        document->name = info.completeBaseName();
        RenderResult rendered = render(*document);
        if (rendered.samples.empty()) {
            setError(error, rendered.warnings.isEmpty() ? tr("empty waveform") : rendered.warnings.front());
            return std::nullopt;
        }
        w.samples = std::move(rendered.samples);
        w.sampleRate = document->sampleRate;
        w.notices = rendered.warnings;
        w.notices.append(tr("NI Waveform Editor files do not store the sample rate: rendered at %1 S/s.")
                             .arg(document->sampleRate));
        w.document = std::move(document);
    } else if (suffix == u"wav") {
        auto wav = parseWav(data, error);
        if (!wav)
            return std::nullopt;
        w.channels = int(wav->channels.size());
        w.samples = std::move(wav->channels[size_t(std::clamp(channel, 0, w.channels - 1))]);
        w.sampleRate = wav->sampleRate;
    } else if (suffix == u"bin") {
        w.samples = parseBinaryI16(data);
        if (w.samples.size() > kMaxWaveformSamples) {
            setError(error, tr("too many samples (max 10 000 000)"));
            return std::nullopt;
        }
    } else if (suffix == u"wdt") {
        std::vector<WdtWaveform> waveforms = parseWdt(data);
        if (waveforms.empty()) {
            setError(error, tr("no waveform found in this LabVIEW file"));
            return std::nullopt;
        }
        w.channels = int(waveforms.size());
        WdtWaveform& chosen = waveforms[size_t(std::clamp(channel, 0, w.channels - 1))];
        w.samples = std::move(chosen.samples);
        w.sampleRate = 1.0 / chosen.dt;
    } else {
        auto text = parseText(data, TextImportOptions{}, error);
        if (!text)
            return std::nullopt;
        w = std::move(*text);
    }
    if (w.samples.empty()) {
        setError(error, tr("no samples"));
        return std::nullopt;
    }
    w.name = info.fileName();
    cleanSamples(w);
    return w;
}

}  // namespace mybench::instruments::gen
