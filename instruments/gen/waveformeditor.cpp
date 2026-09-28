#include "waveformeditor.h"

#include "genexpression.h"
#include "genfileio.h"

#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/units.h>

#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

using namespace gen;

namespace {

const QString kMime = u"application/x-mybench-waveform+json"_s;
const QString kPreferences = u"instruments/gen/editor"_s;
// Segment duration limits and Sample Rate limits, in the document's Units (s/ms/us, S/s/kS/s/MS/s).
constexpr double kMinDuration = 1e-4;
constexpr double kMaxDuration = 1e5;
constexpr double kMinRate = 1.0;
constexpr double kMaxRate = 1e6;
constexpr int kDefaultMaxPoints = 500000;   // ELVISmx Max Points
constexpr int kMinMaxPoints = 1000;
// Undo merge ids: consecutive edits of the same expression text are one undo step.
constexpr int kExpressionMergeBase = 0x10000;

// "500 000", "10 000 000".
QString groupDigits(qint64 n)
{
    QString digits = QString::number(n);
    for (qsizetype i = digits.size() - 3; i > 0; i -= 3)
        digits.insert(i, u' ');
    return digits;
}

QString shortDuration(double seconds)
{
    return QString::number(seconds, 'g', 6);
}

QString componentText(const Component& c)
{
    switch (c.type) {
    case ComponentType::Library: return QString::fromLatin1(functionInfo(c.function).shortName);
    case ComponentType::Expression: return u"Expr: "_s + c.expression;
    case ComponentType::Sketch: return c.name.isEmpty() ? QObject::tr("Sketch") : c.name;
    }
    return {};
}

QString componentIcon(const Component& c)
{
    if (c.type == ComponentType::Expression)
        return u"edit"_s;
    if (c.type == ComponentType::Sketch)
        return u"wave-arbitrary-line"_s;
    switch (c.function) {
    case LibraryFunction::Sine:
    case LibraryFunction::LinearSweep:
    case LibraryFunction::LogSweep:
    case LibraryFunction::Multitone: return u"wave-sine-line"_s;
    case LibraryFunction::Square:
    case LibraryFunction::Pulse:
    case LibraryFunction::Trapezoid:
    case LibraryFunction::Stairstep: return u"wave-square-line"_s;
    case LibraryFunction::Triangle: return u"wave-triangle-line"_s;
    case LibraryFunction::Sawtooth:
    case LibraryFunction::Ramp: return u"wave-sawtooth-line"_s;
    case LibraryFunction::UniformNoise:
    case LibraryFunction::TriangularNoise:
    case LibraryFunction::GaussianNoise: return u"wave-noise-line"_s;
    case LibraryFunction::DcLevel: return u"wave-dc-line"_s;
    default: break;
    }
    return u"wave-arbitrary-line"_s;
}

QString paramUnit(ParamKind kind, const QString& frequencyUnit)
{
    switch (kind) {
    case ParamKind::Voltage: return u"V"_s;
    case ParamKind::Frequency: return frequencyUnit;
    case ParamKind::Degrees: return u"°"_s;
    case ParamKind::Percent: return u"%"_s;
    case ParamKind::Number:
    case ParamKind::Count: break;
    }
    return {};
}

std::vector<double> resampleLinear(const QList<double>& values, int count)
{
    std::vector<double> out(size_t(std::max(count, 2)));
    const qsizetype m = values.size();
    if (m == 0)
        return out;
    for (size_t j = 0; j < out.size(); ++j) {
        const double position = double(j) * double(m - 1) / double(out.size() - 1);
        const qsizetype k = std::min<qsizetype>(qsizetype(position), std::max<qsizetype>(m - 2, 0));
        const double f = position - double(k);
        out[j] = m == 1 ? values[0] : values[k] + (values[k + 1] - values[k]) * f;
    }
    return out;
}

}  // namespace

// --- Undo command -------------------------------------------------------------------------------------

class SnapshotCommand final : public QUndoCommand
{
public:
    SnapshotCommand(WaveformEditor* editor, WaveformEditor::State before, WaveformEditor::State after,
                    const QString& text, int mergeId)
        : QUndoCommand(text), m_editor(editor), m_before(std::move(before)), m_after(std::move(after)), m_id(mergeId)
    {
    }
    void redo() override
    {
        if (m_first) {
            m_first = false;   // the editor already shows `after`
            m_editor->documentEdited();
            return;
        }
        m_editor->restore(m_after);
    }
    void undo() override { m_editor->restore(m_before); }
    int id() const override { return m_id; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = static_cast<const SnapshotCommand*>(other);
        m_after = o->m_after;
        return true;
    }

private:
    WaveformEditor* m_editor;
    WaveformEditor::State m_before;
    WaveformEditor::State m_after;
    int m_id;
    bool m_first = true;
};

// --- Row model -----------------------------------------------------------------------------------------

QVariant WaveformRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const Row& r = m_rows[size_t(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return r.text;
    case KindRole: return r.kind;
    case SegmentRole: return r.segment;
    case ComponentRole: return r.component;
    case WarningRole: return r.warning;
    case IconRole: return r.icon;
    case DetailRole: return r.detail;
    default: break;
    }
    return {};
}

QHash<int, QByteArray> WaveformRowModel::roleNames() const
{
    return {{TextRole, "text"},           {KindRole, "kind"},       {SegmentRole, "segment"},
            {ComponentRole, "component"}, {WarningRole, "warning"}, {IconRole, "iconName"},
            {DetailRole, "detail"}};
}

void WaveformRowModel::setRows(std::vector<Row> rows)
{
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

// --- Editor ---------------------------------------------------------------------------------------------

WaveformEditor::WaveformEditor(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(50);
    connect(&m_renderTimer, &QTimer::timeout, this, &WaveformEditor::startRender);
    connect(&m_undo, &QUndoStack::canUndoChanged, this, &WaveformEditor::undoChanged);
    connect(&m_undo, &QUndoStack::canRedoChanged, this, &WaveformEditor::undoChanged);
    connect(&m_undo, &QUndoStack::undoTextChanged, this, &WaveformEditor::undoChanged);
    connect(&m_undo, &QUndoStack::redoTextChanged, this, &WaveformEditor::undoChanged);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &WaveformEditor::documentChanged);
    m_undo.setUndoLimit(200);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app->clipboard(), &QClipboard::dataChanged, this, &WaveformEditor::clipboardChanged);
    // Default look: a 1.5 px line (the theme accent unless Settings chose a Plot Color).
    m_traceStyle.setLineWidth(1.5);
    m_traceStyle.setInterpolation(mybench::plot::TraceStyle::Linear);
    m_traceStyle.setPointStyle(mybench::plot::TraceStyle::NoPoints);
    m_traceStyle.setAsDefault();
    loadPreferences();
    connect(&m_traceStyle, &mybench::plot::TraceStyle::changed, this, [this] {
        if (m_traceStyle.lineWidth() > 0)
            m_lastLineWidth = m_traceStyle.lineWidth();
        if (!m_loadingPreferences)
            savePreferences();
        emit viewChanged();
        emit preferencesChanged();
    });
    rebuildRows();
    scheduleRender();
}

WaveformEditor::~WaveformEditor()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void WaveformEditor::loadPreferences()
{
    QSettings settings;
    settings.beginGroup(kPreferences);
    m_loadingPreferences = true;
    m_showCursors = settings.value(u"showCursors"_s, false).toBool();
    m_traceStyle.reset();
    const QJsonObject style = QJsonDocument::fromJson(settings.value(u"traceStyle"_s).toByteArray()).object();
    if (!style.isEmpty())
        m_traceStyle.fromJson(style.toVariantMap());
    if (m_traceStyle.lineWidth() > 0)
        m_lastLineWidth = m_traceStyle.lineWidth();
    m_loadingPreferences = false;
    auto color = [&](const QString& key) {
        const QString name = settings.value(key).toString();
        return name.isEmpty() ? QColor() : QColor(name);
    };
    m_selectionColor = color(u"selectionColor"_s);
    m_gridColor = color(u"gridColor"_s);
    m_cursorColor = color(u"cursorColor"_s);
    m_backgroundColor = color(u"backgroundColor"_s);
    m_maxPoints = std::clamp(settings.value(u"maxPoints"_s, kDefaultMaxPoints).toInt(), kMinMaxPoints, int(kMaxWaveformSamples));
    m_animateGraphs = settings.value(u"animateGraphs"_s, false).toBool();
}

void WaveformEditor::savePreferences() const
{
    QSettings settings;
    settings.beginGroup(kPreferences);
    settings.setValue(u"showCursors"_s, m_showCursors);
    settings.setValue(u"traceStyle"_s, QJsonDocument(QJsonObject::fromVariantMap(m_traceStyle.toJson())).toJson(QJsonDocument::Compact));
    auto color = [&](const QString& key, const QColor& c) { settings.setValue(key, c.isValid() ? c.name(QColor::HexArgb) : QString()); };
    color(u"selectionColor"_s, m_selectionColor);
    color(u"gridColor"_s, m_gridColor);
    color(u"cursorColor"_s, m_cursorColor);
    color(u"backgroundColor"_s, m_backgroundColor);
    settings.setValue(u"maxPoints"_s, m_maxPoints);
    settings.setValue(u"animateGraphs"_s, m_animateGraphs);
}

void WaveformEditor::resetPreferences()
{
    m_loadingPreferences = true;
    m_traceStyle.reset();
    m_loadingPreferences = false;
    m_selectionColor = m_gridColor = m_cursorColor = m_backgroundColor = QColor();
    m_maxPoints = kDefaultMaxPoints;
    m_animateGraphs = false;
    m_showCursors = false;
    savePreferences();
    emit preferencesChanged();
    emit viewChanged();
}

void WaveformEditor::setPlotColor(const QColor& color)
{
    // The trace style's line color (invalid = the theme accent).
    if (m_traceStyle.lineColor() == stored(color))
        return;
    m_traceStyle.setLineColor(stored(color));
}

void WaveformEditor::setSelectionColor(const QColor& color)
{
    if (m_selectionColor == stored(color))
        return;
    m_selectionColor = stored(color);
    savePreferences();
    emit preferencesChanged();
}

void WaveformEditor::setGridColor(const QColor& color)
{
    if (m_gridColor == stored(color))
        return;
    m_gridColor = stored(color);
    savePreferences();
    emit preferencesChanged();
}

void WaveformEditor::setCursorColor(const QColor& color)
{
    if (m_cursorColor == stored(color))
        return;
    m_cursorColor = stored(color);
    savePreferences();
    emit preferencesChanged();
}

void WaveformEditor::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor == stored(color))
        return;
    m_backgroundColor = stored(color);
    savePreferences();
    emit preferencesChanged();
}

void WaveformEditor::setMaxPoints(int points)
{
    points = std::clamp(points, kMinMaxPoints, int(kMaxWaveformSamples));
    if (m_maxPoints == points)
        return;
    m_maxPoints = points;
    savePreferences();
    emit preferencesChanged();
}

void WaveformEditor::setAnimateGraphs(bool animate)
{
    if (m_animateGraphs == animate)
        return;
    m_animateGraphs = animate;
    savePreferences();
    emit preferencesChanged();
}

QString WaveformEditor::timeUnit() const
{
    switch (m_doc.units) {
    case FrequencyUnit::kHz: return u"ms"_s;
    case FrequencyUnit::MHz: return u"µs"_s;
    case FrequencyUnit::Hz: break;
    }
    return u"s"_s;
}

QString WaveformEditor::timeLabel() const
{
    return m_doc.units == FrequencyUnit::Hz ? u"sec"_s : timeUnit();
}

QString WaveformEditor::rateUnit() const
{
    switch (m_doc.units) {
    case FrequencyUnit::kHz: return u"kS/s"_s;
    case FrequencyUnit::MHz: return u"MS/s"_s;
    case FrequencyUnit::Hz: break;
    }
    return u"S/s"_s;
}

void WaveformEditor::setDocument(const Document& document, const QString& filePath, bool modified)
{
    m_doc = document;
    if (m_doc.segments.empty())
        m_doc.segments.push_back(Segment{QString(), 10.0, 1.0, {}});
    m_filePath = filePath;
    m_segment = 0;
    m_component = -1;
    m_all = false;
    m_drawMode = false;
    m_undo.clear();
    m_undo.setClean();
    m_forcedModified = modified;
    documentEdited();
    emit undoChanged();
    const size_t total = m_doc.totalSamples();
    if (total > size_t(m_maxPoints)) {
        setStatus(tr("%1 samples: above Max Points (%2). Edits may not make it longer; raise Max Points in Settings to lengthen it.")
                      .arg(groupDigits(qint64(total)), groupDigits(m_maxPoints)));
    }
}

QJsonObject WaveformEditor::saveState() const
{
    QJsonObject o;
    o[u"document"_s] = toJson(m_doc);
    o[u"filePath"_s] = m_filePath;
    o[u"modified"_s] = modified();
    o[u"linkedChannel"_s] = m_linkedChannel;
    o[u"liveUpdate"_s] = m_liveUpdate;
    o[u"view"_s] = m_view;
    o[u"xAxis"_s] = m_xAxis;
    o[u"segment"_s] = m_segment;
    o[u"component"_s] = m_component;
    return o;
}

void WaveformEditor::restoreState(const QJsonObject& state)
{
    std::optional<Document> doc = fromJson(state.value(u"document"_s).toObject());
    setDocument(doc ? *doc : Document{}, state.value(u"filePath"_s).toString(), state.value(u"modified"_s).toBool());
    m_linkedChannel = std::clamp(state.value(u"linkedChannel"_s).toInt(-1), -1, 1);
    m_liveUpdate = state.value(u"liveUpdate"_s).toBool(true);
    m_view = std::clamp(state.value(u"view"_s).toInt(0), 0, 2);
    m_xAxis = std::clamp(state.value(u"xAxis"_s).toInt(0), 0, 1);
    m_segment = std::clamp(state.value(u"segment"_s).toInt(0), 0, int(m_doc.segments.size()) - 1);
    m_component = std::clamp(state.value(u"component"_s).toInt(-1), -1,
                             int(m_doc.segments[size_t(m_segment)].components.size()) - 1);
    emit selectionChanged();
    emit linkChanged();
    emit viewChanged();
    scheduleRender();
}

QString WaveformEditor::windowTitle() const
{
    return m_doc.name + (modified() ? u"*"_s : QString());
}

// --- Edits -------------------------------------------------------------------------------------------------

bool WaveformEditor::refuseTooLarge(const Document& candidate)
{
    // A document that is already above Max Points (a large channel waveform, a file, a lower
    // setting) stays editable: only edits that make it longer than both are refused.
    const size_t total = candidate.totalSamples();
    if (total <= std::max(size_t(m_maxPoints), m_doc.totalSamples()))
        return false;
    setStatus(tr("The waveform would have %1 samples (max %2).").arg(groupDigits(qint64(total)), groupDigits(m_maxPoints)));
    emit message(2, tr("Waveform Editor"), m_statusText);
    return true;
}

bool WaveformEditor::edit(const QString& text, const std::function<void(Document&)>& mutate, int mergeId)
{
    Document copy = m_doc;
    mutate(copy);
    if (copy == m_doc)
        return false;
    if (refuseTooLarge(copy)) {
        emit selectionChanged();   // fields revert to the document's values
        return false;
    }
    const State before = state();
    m_doc = std::move(copy);
    pushState(text, before, mergeId);
    return true;
}

void WaveformEditor::pushState(const QString& text, const State& before, int mergeId)
{
    m_undo.push(new SnapshotCommand(this, before, state(), text, mergeId));
}

void WaveformEditor::restore(const State& s)
{
    m_doc = s.doc;
    m_segment = s.segment;
    m_component = s.component;
    m_all = s.all;
    documentEdited();
}

void WaveformEditor::documentEdited()
{
    m_segment = std::clamp(m_segment, 0, int(m_doc.segments.size()) - 1);
    const int components = int(m_doc.segments[size_t(m_segment)].components.size());
    if (m_component >= components)
        m_component = components - 1;
    if (m_drawMode) {
        const Component* c = selectedComponent();
        if (!c || c->type != ComponentType::Sketch) {
            m_drawMode = false;
            m_view = m_viewBeforeDraw;
            m_xAxis = m_xAxisBeforeDraw;
            emit viewChanged();
        }
    }
    rebuildRows();
    emit documentChanged();
    emit selectionChanged();
    emit drawChanged();
    setStatus(tr("%1 samples · %2 %3").arg(groupDigits(qint64(m_doc.totalSamples())),
                                           shortDuration(m_doc.totalDuration() * displayScale()), timeUnit()));
    scheduleRender();
}

void WaveformEditor::rebuildRows()
{
    std::vector<WaveformRowModel::Row> rows;
    for (size_t k = 0; k < m_doc.segments.size(); ++k) {
        const Segment& s = m_doc.segments[k];
        WaveformRowModel::Row header;
        header.kind = 0;
        header.segment = int(k);
        const QString duration = shortDuration(s.duration * displayScale()) + u' ' + timeLabel();
        header.text = s.name.isEmpty() ? u"%1: (%2)"_s.arg(k + 1).arg(duration)
                                       : u"%1: %2 (%3)"_s.arg(k + 1).arg(s.name, duration);
        header.detail = s.gain != 1.0 ? tr("gain %1").arg(s.gain) : QString();
        header.icon = u"layers"_s;
        rows.push_back(header);
        for (size_t c = 0; c < s.components.size(); ++c) {
            const Component& comp = s.components[c];
            WaveformRowModel::Row row;
            row.kind = 1;
            row.segment = int(k);
            row.component = int(c);
            row.text = (c == 0 ? QString() : operatorSymbol(comp.op) + u' ') + componentText(comp);
            row.icon = componentIcon(comp);
            row.warning = (comp.type == ComponentType::Expression && !Expression::parse(comp.expression).ok())
                          || componentAliases(comp, m_doc.sampleRate);
            if (comp.type == ComponentType::Library) {
                QStringList parts;
                const FunctionInfo& info = functionInfo(comp.function);
                for (size_t i = 0; i < info.params.size() && i < 3; ++i) {
                    const ParamInfo& p = info.params[i];
                    const double scale = p.kind == ParamKind::Frequency ? unitScale(m_doc.units) : 1.0;
                    parts << QString::number(comp.params[i] / scale, 'g', 4) + u' ' + paramUnit(p.kind, gen::unitName(m_doc.units));
                }
                row.detail = parts.join(u" · "_s);
            } else if (comp.type == ComponentType::Sketch) {
                row.detail = tr("%n values", nullptr, int(comp.samples.size()));
            }
            rows.push_back(row);
        }
    }
    m_rows.setRows(std::move(rows));
}

Segment* WaveformEditor::selectedSegment()
{
    return m_doc.segments.empty() ? nullptr : &m_doc.segments[size_t(m_segment)];
}

const Segment* WaveformEditor::selectedSegment() const
{
    return m_doc.segments.empty() ? nullptr : &m_doc.segments[size_t(m_segment)];
}

Component* WaveformEditor::selectedComponent()
{
    Segment* s = selectedSegment();
    if (m_all || !s || m_component < 0 || m_component >= int(s->components.size()))
        return nullptr;
    return &s->components[size_t(m_component)];
}

const Component* WaveformEditor::selectedComponent() const
{
    const Segment* s = selectedSegment();
    if (m_all || !s || m_component < 0 || m_component >= int(s->components.size()))
        return nullptr;
    return &s->components[size_t(m_component)];
}

// --- Document properties -----------------------------------------------------------------------------------

void WaveformEditor::setInfo(const QString& info)
{
    edit(tr("Edit Info"), [&](Document& d) { d.info = info; });
}

void WaveformEditor::setSampleRate(double rate)
{
    if (!std::isfinite(rate))
        return;
    // 1.0 ... 1 000 000.0 in the current Units, one decimal (gen.md 4.3.7).
    const double scale = displayScale();
    const double r = std::round(std::clamp(rate / scale, kMinRate, kMaxRate) * 10.0) / 10.0 * scale;
    edit(tr("Change Sample Rate"), [&](Document& d) { d.sampleRate = r; });
    emit documentChanged();
}

void WaveformEditor::setUnits(int units)
{
    const FrequencyUnit to = FrequencyUnit(std::clamp(units, 0, 2));
    if (to == m_doc.units)
        return;
    // The numbers stay, their meaning changes (ELVISmx): 10000.0 S/s -> 10000.0 kS/s, 10 s ->
    // 10 ms, 1 Hz -> 1 kHz. Sample counts and shapes are unchanged.
    const double k = unitScale(to) / unitScale(m_doc.units);
    edit(tr("Change Units"), [&](Document& d) {
        d.units = to;
        d.sampleRate *= k;
        for (Segment& s : d.segments) {
            s.duration /= k;
            for (Component& c : s.components) {
                if (c.type != ComponentType::Library)
                    continue;
                const FunctionInfo& info = functionInfo(c.function);
                for (size_t i = 0; i < info.params.size(); ++i) {
                    if (info.params[i].kind == ParamKind::Frequency)
                        c.params[i] *= k;
                }
            }
        }
    });
}

// --- View -------------------------------------------------------------------------------------------------

void WaveformEditor::setView(int view)
{
    view = std::clamp(view, 0, 2);
    if (m_view == view)
        return;
    m_view = view;
    emit viewChanged();
    scheduleRender();
}

void WaveformEditor::setXAxis(int axis)
{
    axis = std::clamp(axis, 0, 1);
    if (m_xAxis == axis)
        return;
    m_xAxis = axis;
    emit viewChanged();
    scheduleRender();
}

void WaveformEditor::setShowCursors(bool show)
{
    if (m_showCursors == show)
        return;
    m_showCursors = show;
    savePreferences();
    emit viewChanged();
}

int WaveformEditor::plotStyle() const
{
    if (m_traceStyle.lineWidth() <= 0)
        return 2;
    return m_traceStyle.pointStyle() != mybench::plot::TraceStyle::NoPoints ? 1 : 0;
}

void WaveformEditor::setPlotStyle(int style)
{
    using TS = mybench::plot::TraceStyle;
    style = std::clamp(style, 0, 2);
    if (plotStyle() == style)
        return;
    const TS::PointStyle points = m_traceStyle.pointStyle() == TS::NoPoints ? TS::FilledCircle : m_traceStyle.pointStyle();
    m_traceStyle.setPointStyle(style == 0 ? TS::NoPoints : points);
    m_traceStyle.setLineWidth(style == 2 ? 0.0 : m_lastLineWidth);
}

bool WaveformEditor::stepPlot() const
{
    return m_traceStyle.interpolation() == mybench::plot::TraceStyle::Step;
}

void WaveformEditor::setStepPlot(bool step)
{
    m_traceStyle.setInterpolation(step ? mybench::plot::TraceStyle::Step : mybench::plot::TraceStyle::Linear);
}

double WaveformEditor::lineWidth() const
{
    return m_traceStyle.lineWidth() > 0 ? m_traceStyle.lineWidth() : m_lastLineWidth;
}

void WaveformEditor::setLineWidth(double width)
{
    width = std::clamp(width, 1.0, 5.0);
    m_lastLineWidth = width;
    if (m_traceStyle.lineWidth() > 0)
        m_traceStyle.setLineWidth(width);
}

void WaveformEditor::setPlotVisible(bool visible)
{
    if (m_plotVisible == visible)
        return;
    m_plotVisible = visible;
    emit viewChanged();
}

bool WaveformEditor::copyData()
{
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    const auto samples = m_rendered;
    if (!app || !samples || samples->empty())
        return false;
    QString text;
    text.reserve(qsizetype(samples->size()) * 24 + 32);
    text += tr("Time (s)") + u'\t' + tr("Amplitude (V)") + u'\n';
    const double rate = m_doc.sampleRate;
    for (size_t i = 0; i < samples->size(); ++i) {
        text += QString::number(double(i) / rate, 'g', 10);
        text += u'\t';
        text += QString::number((*samples)[i], 'g', 10);
        text += u'\n';
    }
    app->clipboard()->setText(text);
    emit message(1, tr("Data copied"), tr("%n samples, tab separated", nullptr, int(samples->size())));
    return true;
}

// --- Selection ------------------------------------------------------------------------------------------------

int WaveformEditor::currentRow() const
{
    if (m_all)
        return -1;
    int row = 0;
    for (int k = 0; k < int(m_doc.segments.size()); ++k) {
        if (k == m_segment)
            return row + (m_component >= 0 ? m_component + 1 : 0);
        row += 1 + int(m_doc.segments[size_t(k)].components.size());
    }
    return -1;
}

void WaveformEditor::setCurrentRow(int row)
{
    const auto& rows = m_rows.rows();
    if (row < 0 || row >= int(rows.size()))
        return;
    const auto& r = rows[size_t(row)];
    if (!m_all && r.segment == m_segment && r.component == m_component)
        return;
    m_all = false;
    m_segment = r.segment;
    m_component = r.component;
    if (m_drawMode && (!selectedComponent() || selectedComponent()->type != ComponentType::Sketch))
        setDrawMode(false);
    emit selectionChanged();
    emit drawChanged();
    scheduleRender();
}

int WaveformEditor::selectionKind() const
{
    if (m_all)
        return 3;
    return m_component >= 0 ? 2 : 1;
}

void WaveformEditor::setSegmentIndex(int index)
{
    selectSegment(index);
}

void WaveformEditor::selectSegment(int index)
{
    index = std::clamp(index, 0, int(m_doc.segments.size()) - 1);
    if (!m_all && m_segment == index && m_component < 0)
        return;
    m_all = false;
    m_segment = index;
    m_component = -1;
    setDrawMode(false);
    emit selectionChanged();
    scheduleRender();
}

void WaveformEditor::selectAll()
{
    m_all = true;
    m_component = -1;
    setDrawMode(false);
    emit selectionChanged();
    scheduleRender();
}

void WaveformEditor::selectAt(double x)
{
    if (m_view != 0 || m_xAxis != 0 || m_drawMode)
        return;
    double t = 0;
    for (int k = 0; k < int(m_doc.segments.size()); ++k) {
        const double d = double(segmentSamples(m_doc.segments[size_t(k)].duration, m_doc.sampleRate)) / m_doc.sampleRate;
        if (x < t + d || k == int(m_doc.segments.size()) - 1) {
            selectSegment(k);
            return;
        }
        t += d;
    }
}

// --- Segment parameters ---------------------------------------------------------------------------------------

QString WaveformEditor::segmentName() const
{
    const Segment* s = selectedSegment();
    return s ? s->name : QString();
}

void WaveformEditor::setSegmentName(const QString& name)
{
    edit(tr("Rename Segment"), [&](Document& d) { d.segments[size_t(m_segment)].name = name; });
}

double WaveformEditor::segmentDuration() const
{
    const Segment* s = selectedSegment();
    return s ? s->duration : 0.0;
}

void WaveformEditor::setSegmentDuration(double seconds)
{
    if (!std::isfinite(seconds))
        return;
    const double scale = displayScale();
    const double v = std::clamp(seconds, kMinDuration / scale, kMaxDuration / scale);
    edit(tr("Change Duration"), [&](Document& d) { d.segments[size_t(m_segment)].duration = v; });
}

double WaveformEditor::segmentGain() const
{
    const Segment* s = selectedSegment();
    return s ? s->gain : 1.0;
}

void WaveformEditor::setSegmentGain(double gain)
{
    if (std::isfinite(gain))
        edit(tr("Change Gain"), [&](Document& d) { d.segments[size_t(m_segment)].gain = gain; });
}

// --- Component parameters -----------------------------------------------------------------------------------

int WaveformEditor::componentType() const
{
    const Component* c = selectedComponent();
    return c ? int(c->type) : -1;
}

void WaveformEditor::setComponentType(int type)
{
    const Component* current = selectedComponent();
    if (!current || int(current->type) == type || type < 0 || type > 2)
        return;
    int sketches = 0;
    for (const Segment& s : m_doc.segments)
        for (const Component& c : s.components)
            sketches += c.type == ComponentType::Sketch;
    edit(tr("Change Component Type"), [&](Document& d) {
        Component& c = d.segments[size_t(m_segment)].components[size_t(m_component)];
        const Operator op = c.op;
        switch (ComponentType(type)) {
        case ComponentType::Library: c = Component::library(m_lastFunction); break;
        case ComponentType::Expression: c = Component::makeExpression(); break;
        case ComponentType::Sketch: c = Component::makeSketch(tr("Sketch %1").arg(sketches + 1)); break;
        }
        c.op = op;
    });
}

int WaveformEditor::componentOperator() const
{
    const Component* c = selectedComponent();
    return c && m_component > 0 ? int(c->op) : 0;
}

void WaveformEditor::setComponentOperator(int op)
{
    if (!operatorEnabled() || op < 0 || op > 4)
        return;
    edit(tr("Change Operator"),
         [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].op = Operator(op); });
}

int WaveformEditor::libraryFunction() const
{
    const Component* c = selectedComponent();
    return c && c->type == ComponentType::Library ? int(c->function) : -1;
}

void WaveformEditor::setLibraryFunction(int function)
{
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Library || function < 0 || function >= kLibraryFunctionCount
        || int(c->function) == function)
        return;
    m_lastFunction = LibraryFunction(function);   // New Component uses it (gen.md 4.3.4)
    edit(tr("Change Function"), [&](Document& d) {
        d.segments[size_t(m_segment)].components[size_t(m_component)].setFunction(LibraryFunction(function));
    });
}

QStringList WaveformEditor::functionNames() const
{
    QStringList names;
    for (int i = 0; i < kLibraryFunctionCount; ++i)
        names << functionName(LibraryFunction(i));
    return names;
}

QVariantList WaveformEditor::params() const
{
    QVariantList list;
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Library)
        return list;
    const FunctionInfo& info = functionInfo(c->function);
    const QString unit = gen::unitName(m_doc.units);
    for (size_t i = 0; i < info.params.size(); ++i) {
        const ParamInfo& p = info.params[i];
        const bool frequency = p.kind == ParamKind::Frequency;
        const double scale = frequency ? unitScale(m_doc.units) : 1.0;
        QString label = tr(p.label);
        if (frequency)
            label += u" (%1)"_s.arg(unit);
        list.append(QVariantMap{
            {u"index"_s, int(i)},
            {u"key"_s, QString::fromLatin1(p.key)},
            {u"label"_s, label},
            {u"unit"_s, frequency ? QString() : paramUnit(p.kind, unit)},
            {u"value"_s, c->params[i] / scale},
            {u"min"_s, p.minimum / scale},
            {u"max"_s, p.maximum / scale},
            {u"decimals"_s, p.kind == ParamKind::Count ? 0 : 4},
            {u"stepSize"_s, p.kind == ParamKind::Count ? 1.0 : frequency ? 1.0 : p.kind == ParamKind::Degrees ? 1.0 : 0.1},
            {u"alias"_s, frequency && c->params[i] > m_doc.sampleRate / 2},
        });
    }
    return list;
}

void WaveformEditor::setParam(int index, double displayValue)
{
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Library || !std::isfinite(displayValue))
        return;
    const FunctionInfo& info = functionInfo(c->function);
    if (index < 0 || index >= int(info.params.size()))
        return;
    const ParamInfo& p = info.params[size_t(index)];
    const double scale = p.kind == ParamKind::Frequency ? unitScale(m_doc.units) : 1.0;
    double value = std::clamp(displayValue * scale, p.minimum, p.maximum);
    if (p.kind == ParamKind::Count)
        value = std::round(value);
    value = coercePercentParam(*c, p.key, value);   // Trapezoid/Pulse: Delay + Rise + Hold + Fall <= 100 %
    edit(tr("Change %1").arg(tr(p.label)),
         [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].params[size_t(index)] = value; });
}

bool WaveformEditor::hasSeed() const
{
    const Component* c = selectedComponent();
    return c && c->type == ComponentType::Library && functionInfo(c->function).noise;
}

int WaveformEditor::seed() const
{
    const Component* c = selectedComponent();
    return c ? int(c->seed) : 1;
}

void WaveformEditor::setSeed(int seed)
{
    if (!hasSeed())
        return;
    edit(tr("Change Seed"), [&](Document& d) {
        d.segments[size_t(m_segment)].components[size_t(m_component)].seed = uint32_t(std::max(0, seed));
    });
}

QString WaveformEditor::expression() const
{
    const Component* c = selectedComponent();
    return c && c->type == ComponentType::Expression ? c->expression : QString();
}

void WaveformEditor::setExpression(const QString& text)
{
    // Applied while typing; consecutive edits of this expression are one undo step.
    edit(tr("Edit Expression"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].expression = text; },
         kExpressionMergeBase + m_segment * 1024 + m_component);
}

QString WaveformEditor::expressionError() const
{
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Expression)
        return {};
    return Expression::parse(c->expression).error();
}

double WaveformEditor::expressionFrom() const
{
    const Component* c = selectedComponent();
    return c ? c->from : 0.0;
}

void WaveformEditor::setExpressionFrom(double v)
{
    if (std::isfinite(v))
        edit(tr("Change X Range"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].from = v; });
}

double WaveformEditor::expressionTo() const
{
    const Component* c = selectedComponent();
    return c ? c->to : 1.0;
}

void WaveformEditor::setExpressionTo(double v)
{
    if (std::isfinite(v))
        edit(tr("Change X Range"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].to = v; });
}

double WaveformEditor::componentCycles() const
{
    const Component* c = selectedComponent();
    return c ? c->cycles : 1.0;
}

void WaveformEditor::setComponentCycles(double v)
{
    if (std::isfinite(v) && v > 0)
        edit(tr("Change Cycles"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].cycles = v; });
}

double WaveformEditor::componentGain() const
{
    const Component* c = selectedComponent();
    return c ? c->gain : 1.0;
}

void WaveformEditor::setComponentGain(double v)
{
    if (std::isfinite(v))
        edit(tr("Change Gain"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].gain = v; });
}

QString WaveformEditor::sketchName() const
{
    const Component* c = selectedComponent();
    return c && c->type == ComponentType::Sketch ? c->name : QString();
}

void WaveformEditor::setSketchName(const QString& name)
{
    edit(tr("Rename Sketch"), [&](Document& d) { d.segments[size_t(m_segment)].components[size_t(m_component)].name = name; });
}

int WaveformEditor::sketchSamples() const
{
    const Component* c = selectedComponent();
    return c && c->type == ComponentType::Sketch ? int(c->samples.size()) : 0;
}

void WaveformEditor::setSketchSamples(int count)
{
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Sketch)
        return;
    count = std::clamp(count, 2, 1'000'000);
    if (count == c->samples.size())
        return;
    edit(tr("Change Samples"), [&](Document& d) {
        Component& comp = d.segments[size_t(m_segment)].components[size_t(m_component)];
        const std::vector<double> values = resampleLinear(comp.samples, count);
        comp.samples = QList<double>(values.begin(), values.end());
    });
}

// --- Structure --------------------------------------------------------------------------------------------------

void WaveformEditor::newSegment()
{
    const int at = m_all ? int(m_doc.segments.size()) : m_segment + 1;
    const State before = state();
    Document copy = m_doc;
    copy.segments.insert(copy.segments.begin() + at, Segment{QString(), 1.0, 1.0, {}});
    if (refuseTooLarge(copy))
        return;
    m_doc = copy;
    m_segment = at;
    m_component = -1;
    m_all = false;
    pushState(tr("New Segment"), before);
}

void WaveformEditor::newComponent()
{
    if (m_all)
        return;
    const State before = state();
    Segment& s = m_doc.segments[size_t(m_segment)];
    const int at = m_component >= 0 ? m_component + 1 : int(s.components.size());
    s.components.insert(s.components.begin() + at, Component::library(m_lastFunction));
    m_component = at;
    pushState(tr("New Component"), before);
}

void WaveformEditor::deleteSelection()
{
    const State before = state();
    if (m_all) {
        m_doc.segments = {Segment{QString(), 10.0, 1.0, {}}};
        m_segment = 0;
        m_component = -1;
        m_all = false;
    } else if (m_component >= 0) {
        Segment& s = m_doc.segments[size_t(m_segment)];
        s.components.erase(s.components.begin() + m_component);
        m_component = std::min(m_component, int(s.components.size()) - 1);
    } else if (m_doc.segments.size() > 1) {
        m_doc.segments.erase(m_doc.segments.begin() + m_segment);
        m_segment = std::min(m_segment, int(m_doc.segments.size()) - 1);
    } else {
        if (m_doc.segments[0].components.empty())
            return;
        m_doc.segments[0].components.clear();
    }
    pushState(tr("Clear"), before);
}

void WaveformEditor::copy()
{
    QJsonObject o;
    if (m_all) {
        o = toJson(m_doc);
        o[u"kind"_s] = u"document"_s;
    } else if (const Component* c = selectedComponent()) {
        o = componentToJson(*c);
        o[u"kind"_s] = u"component"_s;
    } else if (const Segment* s = selectedSegment()) {
        o = segmentToJson(*s);
        o[u"kind"_s] = u"segment"_s;
    }
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (o.isEmpty() || !app)
        return;
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    auto* mime = new QMimeData;
    mime->setData(kMime, json);
    mime->setText(QString::fromUtf8(json));
    app->clipboard()->setMimeData(mime);
    emit clipboardChanged();
}

void WaveformEditor::cut()
{
    copy();
    deleteSelection();
}

bool WaveformEditor::canPaste() const
{
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app)
        return false;
    const QMimeData* mime = app->clipboard()->mimeData();
    return mime && (mime->hasFormat(kMime) || mime->text().contains(u"\"kind\""_s));
}

void WaveformEditor::paste()
{
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app)
        return;
    const QMimeData* mime = app->clipboard()->mimeData();
    if (!mime)
        return;
    const QByteArray data = mime->hasFormat(kMime) ? mime->data(kMime) : mime->text().toUtf8();
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    const QString kind = o.value(u"kind"_s).toString();
    const State before = state();
    QString error;
    if (kind == u"component") {
        auto c = componentFromJson(o, &error);
        if (!c || m_all)
            return;
        Segment& s = m_doc.segments[size_t(m_segment)];
        const int at = m_component >= 0 ? m_component + 1 : int(s.components.size());
        s.components.insert(s.components.begin() + at, *c);
        m_component = at;
    } else if (kind == u"segment" || kind == u"document") {
        std::vector<Segment> segments;
        if (kind == u"segment") {
            auto s = segmentFromJson(o, &error);
            if (!s)
                return;
            segments.push_back(*s);
        } else {
            QJsonObject d = o;
            d.remove(u"kind"_s);
            auto doc = fromJson(d, &error);
            if (!doc)
                return;
            segments = doc->segments;
        }
        Document copy = m_doc;
        const int at = m_all ? int(copy.segments.size()) : m_segment + 1;
        copy.segments.insert(copy.segments.begin() + at, segments.begin(), segments.end());
        if (refuseTooLarge(copy))
            return;
        m_doc = copy;
        m_segment = at;
        m_component = -1;
        m_all = false;
    } else {
        return;
    }
    pushState(tr("Paste"), before);
}

void WaveformEditor::moveSelection(int delta)
{
    if (m_all || delta == 0)
        return;
    const State before = state();
    if (m_component >= 0) {
        Segment& s = m_doc.segments[size_t(m_segment)];
        const int to = m_component + delta;
        if (to < 0 || to >= int(s.components.size()))
            return;
        std::swap(s.components[size_t(m_component)], s.components[size_t(to)]);
        m_component = to;
    } else {
        const int to = m_segment + delta;
        if (to < 0 || to >= int(m_doc.segments.size()))
            return;
        std::swap(m_doc.segments[size_t(m_segment)], m_doc.segments[size_t(to)]);
        m_segment = to;
    }
    pushState(delta < 0 ? tr("Move Up") : tr("Move Down"), before);
}

void WaveformEditor::moveRow(int from, int to)
{
    const auto& rows = m_rows.rows();
    if (from < 0 || to < 0 || from >= int(rows.size()) || to >= int(rows.size()) || from == to)
        return;
    const auto source = rows[size_t(from)];
    const auto target = rows[size_t(to)];
    const State before = state();
    if (source.kind == 0) {
        if (m_doc.segments.size() < 2 || source.segment == target.segment)
            return;
        Segment moved = m_doc.segments[size_t(source.segment)];
        m_doc.segments.erase(m_doc.segments.begin() + source.segment);
        m_doc.segments.insert(m_doc.segments.begin() + target.segment, moved);
        m_segment = target.segment;
        m_component = -1;
    } else {
        Component moved = m_doc.segments[size_t(source.segment)].components[size_t(source.component)];
        auto& from = m_doc.segments[size_t(source.segment)].components;
        from.erase(from.begin() + source.component);
        auto& into = m_doc.segments[size_t(target.segment)].components;
        int at = target.kind == 0 ? 0 : target.component;
        at = std::clamp(at, 0, int(into.size()));
        into.insert(into.begin() + at, moved);
        m_segment = target.segment;
        m_component = at;
    }
    m_all = false;
    pushState(tr("Move"), before);
}

void WaveformEditor::scaleSelection(double timeFactor, double amplitudeFactor)
{
    if (!(timeFactor > 0) || !std::isfinite(timeFactor) || !std::isfinite(amplitudeFactor))
        return;
    auto scaleFrequencies = [&](Component& c) {
        if (c.type == ComponentType::Library) {
            const FunctionInfo& info = functionInfo(c.function);
            for (size_t i = 0; i < info.params.size(); ++i) {
                if (info.params[i].kind == ParamKind::Frequency)
                    c.params[i] /= timeFactor;
            }
        } else {
            c.cycles /= timeFactor;
        }
    };
    edit(tr("Scale Selection"), [&](Document& d) {
        auto scaleSegment = [&](Segment& s) {
            s.duration = std::clamp(s.duration * timeFactor, kMinDuration / unitScale(d.units), kMaxDuration / unitScale(d.units));
            s.gain *= amplitudeFactor;
            for (Component& c : s.components) {
                if (c.type == ComponentType::Library)
                    scaleFrequencies(c);   // Expression/Sketch stretch with the segment
            }
        };
        if (m_all) {
            for (Segment& s : d.segments)
                scaleSegment(s);
        } else if (m_component >= 0) {
            Component& c = d.segments[size_t(m_segment)].components[size_t(m_component)];
            scaleFrequencies(c);
            if (c.type == ComponentType::Library) {
                const FunctionInfo& info = functionInfo(c.function);
                for (size_t i = 0; i < info.params.size(); ++i) {
                    if (info.params[i].kind == ParamKind::Voltage)
                        c.params[i] *= amplitudeFactor;
                }
            } else if (c.type == ComponentType::Expression) {
                c.gain *= amplitudeFactor;
            } else {
                for (double& v : c.samples)
                    v *= amplitudeFactor;
                for (QPointF& v : c.vertices)
                    v.setY(v.y() * amplitudeFactor);
            }
        } else {
            scaleSegment(d.segments[size_t(m_segment)]);
        }
    });
}

// --- Sketch drawing ----------------------------------------------------------------------------------------------

void WaveformEditor::setDrawMode(bool on)
{
    if (on) {
        const Component* c = selectedComponent();
        if (!c || c->type != ComponentType::Sketch) {
            setStatus(tr("Select a Sketch component to draw."));
            on = false;
        }
    }
    if (m_drawMode == on)
        return;
    m_drawMode = on;
    if (on) {
        m_viewBeforeDraw = m_view;
        m_xAxisBeforeDraw = m_xAxis;
        m_view = 2;
        m_xAxis = 0;
        // Freeze the Y range at its current extent (at least +-1 V).
        const Component* c = selectedComponent();
        double lo = -1, hi = 1;
        for (double v : c->samples) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const double margin = (hi - lo) * 0.1;
        m_viewMin = lo - margin;
        m_viewMax = hi + margin;
    } else {
        m_view = m_viewBeforeDraw;
        m_xAxis = m_xAxisBeforeDraw;
    }
    emit viewChanged();
    emit drawChanged();
    scheduleRender();
}

void WaveformEditor::setDrawTool(int tool)
{
    tool = std::clamp(tool, 0, 1);
    if (m_drawTool == tool)
        return;
    m_drawTool = tool;
    emit drawChanged();
}

double WaveformEditor::drawSpan() const
{
    const Component* c = selectedComponent();
    const Segment* s = selectedSegment();
    if (!c || !s)
        return 1.0;
    return s->duration / (c->cycles > 0 ? c->cycles : 1.0);
}

QVariantList WaveformEditor::vertices() const
{
    QVariantList list;
    const Component* c = selectedComponent();
    if (!c || c->type != ComponentType::Sketch)
        return list;
    const double span = drawSpan();
    for (const QPointF& v : c->vertices)
        list.append(QPointF(v.x() * span, v.y()));
    return list;
}

int WaveformEditor::sketchIndexAt(double x, const Component& component) const
{
    const qsizetype m = component.samples.size();
    const double position = std::clamp(x / drawSpan(), 0.0, 1.0);
    return int(std::clamp<qsizetype>(qsizetype(std::llround(position * double(m - 1))), 0, m - 1));
}

void WaveformEditor::drawBegin(double x, double y)
{
    Component* c = selectedComponent();
    if (!m_drawMode || !c)
        return;
    m_dragBefore = state();
    const int i = sketchIndexAt(x, *c);
    c->samples[i] = y;
    c->vertices.clear();
    m_lastDrawIndex = i;
    startRender();
}

void WaveformEditor::drawTo(double x, double y)
{
    Component* c = selectedComponent();
    if (!m_drawMode || !c || m_lastDrawIndex < 0)
        return;
    const int i = sketchIndexAt(x, *c);
    const double y0 = c->samples[m_lastDrawIndex];
    const int from = m_lastDrawIndex;
    const int step = i >= from ? 1 : -1;
    for (int k = from; k != i + step; k += step) {
        const double f = i == from ? 1.0 : double(k - from) / double(i - from);
        c->samples[k] = y0 + (y - y0) * f;
    }
    m_lastDrawIndex = i;
    startRender();
}

void WaveformEditor::drawEnd()
{
    m_lastDrawIndex = -1;
    if (!m_dragBefore)
        return;
    const State before = *m_dragBefore;
    m_dragBefore.reset();
    if (before.doc == m_doc)
        return;
    pushState(tr("Draw"), before);
}

void WaveformEditor::rasterizeVertices(Component& c) const
{
    if (c.vertices.size() < 2)
        return;
    const qsizetype m = c.samples.size();
    qsizetype seg = 0;
    for (qsizetype j = 0; j < m; ++j) {
        const double p = m > 1 ? double(j) / double(m - 1) : 0.0;
        while (seg + 2 < c.vertices.size() && p > c.vertices[seg + 1].x())
            ++seg;
        const QPointF a = c.vertices[seg];
        const QPointF b = c.vertices[seg + 1];
        const double f = b.x() > a.x() ? std::clamp((p - a.x()) / (b.x() - a.x()), 0.0, 1.0) : 0.0;
        c.samples[j] = a.y() + (b.y() - a.y()) * f;
    }
}

void WaveformEditor::addVertex(double x, double y)
{
    Component* c = selectedComponent();
    if (!m_drawMode || !c)
        return;
    const State before = state();
    if (c->vertices.size() < 2)
        c->vertices = {QPointF(0.0, c->samples.front()), QPointF(1.0, c->samples.back())};
    const double p = std::clamp(x / drawSpan(), 0.0, 1.0);
    const double tolerance = 1.0 / double(std::max<qsizetype>(c->samples.size() - 1, 1));
    if (p <= tolerance / 2)
        c->vertices.front().setY(y);
    else if (p >= 1.0 - tolerance / 2)
        c->vertices.back().setY(y);
    else {
        auto it = std::lower_bound(c->vertices.begin(), c->vertices.end(), p,
                                   [](const QPointF& v, double value) { return v.x() < value; });
        c->vertices.insert(it, QPointF(p, y));
    }
    rasterizeVertices(*c);
    pushState(tr("Add Point"), before);
}

void WaveformEditor::moveVertex(int index, double x, double y)
{
    Component* c = selectedComponent();
    if (!m_drawMode || !c || index < 0 || index >= c->vertices.size())
        return;
    const bool dragging = m_dragBefore.has_value();
    const State before = state();
    QPointF& v = c->vertices[index];
    if (index > 0 && index < c->vertices.size() - 1) {
        const double lo = c->vertices[index - 1].x();
        const double hi = c->vertices[index + 1].x();
        v.setX(std::clamp(x / drawSpan(), lo, hi));
    }
    v.setY(y);
    rasterizeVertices(*c);
    if (dragging) {
        emit drawChanged();
        startRender();
    } else {
        pushState(tr("Move Point"), before);
    }
}

void WaveformEditor::removeVertex(int index)
{
    Component* c = selectedComponent();
    if (!m_drawMode || !c || index <= 0 || index >= c->vertices.size() - 1)
        return;
    const State before = state();
    c->vertices.removeAt(index);
    rasterizeVertices(*c);
    pushState(tr("Delete Point"), before);
}

void WaveformEditor::vertexDragBegin()
{
    m_dragBefore = state();
}

void WaveformEditor::vertexDragEnd()
{
    drawEnd();
}

// --- Channel link -------------------------------------------------------------------------------------------------

void WaveformEditor::setLinkedChannel(int channel)
{
    channel = std::clamp(channel, -1, 1);
    if (m_linkedChannel == channel)
        return;
    m_linkedChannel = channel;
    emit linkChanged();
}

void WaveformEditor::setLiveUpdate(bool live)
{
    if (m_liveUpdate == live)
        return;
    m_liveUpdate = live;
    emit linkChanged();
}

void WaveformEditor::useInChannel(int channel)
{
    setLinkedChannel(channel);
    emit useInChannelRequested(channel);
}

// --- Files -------------------------------------------------------------------------------------------------------

void WaveformEditor::newDocument()
{
    setDocument(Document{});
    setLinkedChannel(-1);
}

bool WaveformEditor::openFile(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    if (QFileInfo(path).suffix().compare(u"mbw"_s, Qt::CaseInsensitive) == 0) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            emit message(3, tr("Could not open %1").arg(QFileInfo(path).fileName()), f.errorString());
            return false;
        }
        auto doc = parse(f.readAll(), &error);
        if (!doc) {
            emit message(3, tr("Could not open %1").arg(QFileInfo(path).fileName()), error);
            return false;
        }
        doc->name = QFileInfo(path).completeBaseName();
        setDocument(*doc, path);
        setLinkedChannel(-1);
        return true;
    }
    auto loaded = loadWaveformFile(path, &error);
    if (!loaded) {
        emit message(3, tr("Could not open %1").arg(QFileInfo(path).fileName()), error);
        return false;
    }
    if (loaded->document) {
        // NI Waveform Editor document (.wfm): segments and components are kept.
        Document doc = *loaded->document;
        doc.name = QFileInfo(path).completeBaseName();
        if (refuseTooLarge(doc))
            return false;
        setDocument(doc);
        setLinkedChannel(-1);
        for (const QString& notice : loaded->notices)
            emit message(1, doc.name, notice);
        return true;
    }
    const double rate = loaded->sampleRate > 0 ? std::clamp(loaded->sampleRate, 1.0, 1e6) : m_doc.sampleRate;
    Document doc = documentFromSamples(loaded->samples, rate, QFileInfo(path).completeBaseName());
    if (refuseTooLarge(doc))
        return false;
    setDocument(doc);
    setLinkedChannel(-1);
    return true;
}

bool WaveformEditor::save()
{
    if (m_filePath.isEmpty() || !m_filePath.endsWith(u".mbw"_s, Qt::CaseInsensitive))
        return false;
    return saveAs(QUrl::fromLocalFile(m_filePath));
}

bool WaveformEditor::saveAs(const QUrl& file)
{
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (QFileInfo(path).suffix().isEmpty())
        path += u".mbw"_s;
    Document doc = m_doc;
    doc.name = QFileInfo(path).completeBaseName();
    QSaveFile f(path);
    const QByteArray bytes = serialize(doc);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        emit message(3, tr("Could not save %1").arg(QFileInfo(path).fileName()), f.errorString());
        return false;
    }
    m_doc.name = doc.name;
    m_filePath = path;
    m_forcedModified = false;
    m_undo.setClean();
    emit documentChanged();
    emit message(1, tr("Saved %1").arg(QFileInfo(path).fileName()), QString());
    return true;
}

bool WaveformEditor::exportFile(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const RenderResult r = render(m_doc);
    if (r.samples.empty()) {
        emit message(3, tr("Nothing to export"), r.warnings.join(u'\n'));
        return false;
    }
    QString error;
    size_t clipped = 0;
    if (!saveWaveformFile(path, r.samples, m_doc.sampleRate, &error, &clipped)) {
        emit message(3, tr("Could not export %1").arg(QFileInfo(path).fileName()), error);
        return false;
    }
    if (clipped > 0)
        emit message(2, tr("Exported %1").arg(QFileInfo(path).fileName()),
                     tr("%n samples beyond the file's full scale were clipped.", nullptr, int(clipped)));
    else
        emit message(1, tr("Exported %1").arg(QFileInfo(path).fileName()), QString());
    return true;
}

namespace {

std::optional<LoadedWaveform> loadForImport(const QString& path, const QVariantMap& options, QString* error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool known = suffix == u"mbw" || suffix == u"wav" || suffix == u"bin" || suffix == u"wdt";
    if (!known && options.value(u"automatic"_s, true).toBool() == false) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            *error = f.errorString();
            return std::nullopt;
        }
        TextImportOptions o;
        o.automatic = false;
        o.delimiters = options.value(u"delimiters"_s, u"\t"_s).toString();
        o.column = options.value(u"column"_s, 1).toInt();
        o.startLine = options.value(u"startLine"_s, 0).toInt();
        o.count = options.value(u"count"_s, 0).toInt();
        auto r = parseText(f.readAll(), o, error);
        if (r)
            r->name = QFileInfo(path).fileName();
        return r;
    }
    return loadWaveformFile(path, error, options.value(u"channel"_s, 0).toInt());
}

}  // namespace

QVariantMap WaveformEditor::inspectImport(const QUrl& file, const QVariantMap& options)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QVariantMap result;
    const QString suffix = QFileInfo(path).suffix().toLower();
    result[u"kind"_s] = (suffix == u"mbw" || suffix == u"wav" || suffix == u"bin" || suffix == u"wdt") ? suffix : u"text"_s;
    if (result[u"kind"_s] == u"text"_s) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QStringList lines;
            int firstNumeric = 0;
            for (int n = 1; n <= 200 && !f.atEnd(); ++n) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (lines.size() < 40)
                    lines << line;
                if (!firstNumeric && !line.startsWith(u'#')) {
                    bool ok = false;
                    line.split(QRegularExpression(u"[\\t;, ]+"_s), Qt::SkipEmptyParts).value(0).toDouble(&ok);
                    if (ok)
                        firstNumeric = n;
                }
            }
            result[u"lines"_s] = lines;
            result[u"firstNumericLine"_s] = firstNumeric;
        }
    }
    QString error;
    const auto loaded = loadForImport(path, options, &error);
    result[u"ok"_s] = loaded.has_value();
    result[u"error"_s] = error;
    if (loaded) {
        result[u"channels"_s] = loaded->channels;
        result[u"sampleRate"_s] = loaded->sampleRate;
        result[u"count"_s] = int(loaded->samples.size());
        m_importPreview.setUniform(0.0, 1.0, loaded->samples);
    } else {
        m_importPreview.clear();
    }
    return result;
}

bool WaveformEditor::acceptImport(const QUrl& file, const QVariantMap& options, int mode)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    const auto loaded = loadForImport(path, options, &error);
    if (!loaded) {
        emit message(3, tr("Could not import %1").arg(QFileInfo(path).fileName()), error);
        return false;
    }
    const QList<double> values(loaded->samples.begin(), loaded->samples.end());
    if (mode == 1) {
        const Component* c = selectedComponent();
        if (!c || c->type != ComponentType::Sketch) {
            emit message(2, tr("Import"), tr("Select a Sketch component first."));
            return false;
        }
        return edit(tr("Import"), [&](Document& d) {
            Component& comp = d.segments[size_t(m_segment)].components[size_t(m_component)];
            comp.samples = values.size() >= 2 ? values : QList<double>{values.value(0), values.value(0)};
            comp.vertices.clear();
        });
    }
    const double rate = loaded->sampleRate > 0 ? loaded->sampleRate : m_doc.sampleRate;
    const Document imported = documentFromSamples(loaded->samples, rate, QFileInfo(path).completeBaseName());
    Segment segment = imported.segments.front();
    segment.name = QFileInfo(path).completeBaseName();
    const State before = state();
    Document copy = m_doc;
    const int at = m_all ? int(copy.segments.size()) : m_segment + 1;
    copy.segments.insert(copy.segments.begin() + at, segment);
    if (refuseTooLarge(copy))
        return false;
    m_doc = copy;
    m_segment = at;
    m_component = -1;
    m_all = false;
    pushState(tr("Import from File"), before);
    return true;
}

// --- Rendering ---------------------------------------------------------------------------------------------------

void WaveformEditor::setStatus(const QString& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusChanged();
}

void WaveformEditor::scheduleRender()
{
    m_renderTimer.start();
}

void WaveformEditor::startRender()
{
    m_renderTimer.stop();
    const quint64 generation = ++m_generation;

    // Draw mode: one cycle of the sketch values, directly (fast enough for interactive drawing).
    if (m_drawMode) {
        const Component* c = selectedComponent();
        if (c && c->samples.size() >= 2) {
            const double span = drawSpan();
            const std::vector<double> y(c->samples.begin(), c->samples.end());
            m_preview.setUniform(0.0, span / double(y.size() - 1), y);
            m_viewStart = 0;
            m_viewEnd = span;
            m_highlightStart = m_highlightEnd = 0;
            emit renderChanged();
        }
    }

    struct Job {
        Document doc;
        int view, xAxis, segment, component;
        bool all, draw;
    };
    const Job job{m_doc, m_view, m_xAxis, m_segment, m_component, m_all, m_drawMode};
    if (!m_rendering) {
        m_rendering = true;
        emit renderChanged();
    }
    QPointer<WaveformEditor> guard(this);
    m_pool.clear();
    m_pool.start([this, guard, job, generation] {
        RenderResult full = render(job.doc);
        auto samplesPtr = std::make_shared<const std::vector<double>>(full.samples);
        const double rate = job.doc.sampleRate;
        std::vector<double> view;
        double x0 = 0;
        double hlStart = 0, hlEnd = 0;
        if (job.view == 0 || job.all) {
            view = full.samples;
            if (job.all || full.segmentStarts.empty()) {
                hlStart = 0;
                hlEnd = double(full.samples.size()) / rate;
            } else {
                const size_t k = size_t(std::clamp(job.segment, 0, int(full.segmentStarts.size()) - 1));
                hlStart = double(full.segmentStarts[k]) / rate;
                const size_t end = k + 1 < full.segmentStarts.size() ? full.segmentStarts[k + 1] : full.samples.size();
                hlEnd = double(end) / rate;
            }
        } else if (job.view == 2 && job.component >= 0) {
            view = renderComponent(job.doc, size_t(job.segment), size_t(job.component)).samples;
            hlEnd = double(view.size()) / rate;
        } else {
            view = renderSegment(job.doc, size_t(job.segment)).samples;
            hlEnd = double(view.size()) / rate;
        }
        double lo = 0, hi = 0, start = 0, end = 1;
        bool flat = false;
        if (job.xAxis == 1 && view.size() >= 2) {
            // Power spectrum of the viewed part (Hann window), dBVrms floored at -200 dB.
            std::vector<double> part(view.begin(), view.begin() + qsizetype(std::min<size_t>(view.size(), 1 << 20)));
            const mydaq::dsp::PowerSpectrum ps = mydaq::dsp::powerSpectrum(part, rate, mydaq::dsp::WindowType::Hanning);
            std::vector<double> db = mydaq::dsp::formatSpectrum(ps, {});
            lo = 0;
            hi = -200;
            for (double& v : db) {
                v = std::max(v, -200.0);
                hi = std::max(hi, v);
            }
            lo = std::max(-200.0, hi - 140.0);
            for (double& v : db)
                v = std::max(v, lo);   // the plot's autoscale fits the shown 140 dB
            const double scale = unitScale(job.doc.units);
            if (!job.draw)
                m_preview.setUniform(0.0, ps.info.df / scale, db);
            start = 0;
            end = rate / 2 / scale;
            hi += 10;
            hlStart = hlEnd = 0;
        } else if (!view.empty()) {
            const auto [mn, mx] = std::minmax_element(view.begin(), view.end());
            lo = *mn;
            hi = *mx;
            if (hi - lo < 1e-12) {
                flat = true;
                lo -= 1;
                hi += 1;
            } else {
                const double margin = (hi - lo) * 0.05;
                lo -= margin;
                hi += margin;
            }
            if (!job.draw)
                m_preview.setUniform(x0, 1.0 / rate, view);
            start = 0;
            end = std::max(double(view.size()) / rate, 1.0 / rate);
        } else if (!job.draw) {
            m_preview.clear();
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [this, guard, generation, full = std::move(full), samplesPtr, lo, hi, start, end, hlStart, hlEnd, flat, draw = job.draw] {
                if (!guard || generation != m_generation)
                    return;
                m_rendering = false;
                m_rendered = samplesPtr;
                m_warnings = full.warnings;
                m_alias = full.alias;
                m_flat = flat;
                if (!draw) {
                    m_viewStart = start;
                    m_viewEnd = end;
                    m_viewMin = lo;
                    m_viewMax = hi;
                    m_highlightStart = hlStart;
                    m_highlightEnd = hlEnd;
                }
                emit renderChanged();
                emit rendered();
            },
            Qt::QueuedConnection);
    });
}

}  // namespace mybench::instruments
