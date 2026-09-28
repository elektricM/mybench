// WaveformEditor: the Waveform Editor of the generator (docs/instruments/gen.md 4.3; ELVISmx
// WaveformEditor.exe). Holds one document (segments of components), the selection, full undo/redo
// (every edit is a document snapshot on a QUndoStack), the preview rendered in a background thread,
// sketch drawing (freehand and line vertices), the import/export wizards' logic and the clipboard.
// QML shows it in the generator panel's "Waveform Editor" tab.
#pragma once

#include "genwaveform.h"

#include <mybench/plot/tracebuffer.h>

#include <QAbstractListModel>
#include <QObject>
#include <QPointF>
#include <QThreadPool>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace mybench::instruments {

// Rows of the segment list: a header per segment followed by its components.
class WaveformRowModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by WaveformEditor")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { TextRole = Qt::UserRole + 1, KindRole, SegmentRole, ComponentRole, WarningRole, IconRole, DetailRole };
    struct Row {
        QString text;
        QString detail;
        int kind = 0;   // 0 segment header, 1 component
        int segment = 0;
        int component = -1;
        bool warning = false;
        QString icon;
    };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return m_rows; }

signals:
    void countChanged();

private:
    std::vector<Row> m_rows;
};

class WaveformEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by GenController")

    // Document
    Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY documentChanged)   // "Untitled*"
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY documentChanged)
    Q_PROPERTY(QString info READ info WRITE setInfo NOTIFY documentChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY documentChanged)
    Q_PROPERTY(int units READ units WRITE setUnits NOTIFY documentChanged)   // 0 Hz, 1 kHz, 2 MHz
    Q_PROPERTY(QString unitName READ unitName NOTIFY documentChanged)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY documentChanged)
    Q_PROPERTY(int totalSamples READ totalSamples NOTIFY documentChanged)
    Q_PROPERTY(double totalDuration READ totalDuration NOTIFY documentChanged)
    Q_PROPERTY(mybench::instruments::WaveformRowModel* rows READ rows CONSTANT)

    // View (not undoable)
    Q_PROPERTY(int view READ view WRITE setView NOTIFY viewChanged)       // 0 Waveform, 1 Segment, 2 Component
    Q_PROPERTY(int xAxis READ xAxis WRITE setXAxis NOTIFY viewChanged)    // 0 Time, 1 Frequency
    Q_PROPERTY(bool showCursors READ showCursors WRITE setShowCursors NOTIFY viewChanged)
    Q_PROPERTY(int plotStyle READ plotStyle WRITE setPlotStyle NOTIFY viewChanged)   // 0 Line, 1 Line + Points, 2 Points
    Q_PROPERTY(bool stepPlot READ stepPlot WRITE setStepPlot NOTIFY viewChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY viewChanged)

    // Selection
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY selectionChanged)
    Q_PROPERTY(int selectionKind READ selectionKind NOTIFY selectionChanged)   // 0 none, 1 segment, 2 component, 3 all
    Q_PROPERTY(int segmentIndex READ segmentIndex WRITE setSegmentIndex NOTIFY selectionChanged)   // 0-based
    Q_PROPERTY(int componentIndex READ componentIndex NOTIFY selectionChanged)
    // Selected segment
    Q_PROPERTY(QString segmentName READ segmentName WRITE setSegmentName NOTIFY selectionChanged)
    Q_PROPERTY(double segmentDuration READ segmentDuration WRITE setSegmentDuration NOTIFY selectionChanged)
    Q_PROPERTY(double segmentGain READ segmentGain WRITE setSegmentGain NOTIFY selectionChanged)
    // Selected component
    Q_PROPERTY(int componentType READ componentType WRITE setComponentType NOTIFY selectionChanged)   // 0 Library, 1 Expression, 2 Sketch
    Q_PROPERTY(int componentOperator READ componentOperator WRITE setComponentOperator NOTIFY selectionChanged)
    Q_PROPERTY(bool operatorEnabled READ operatorEnabled NOTIFY selectionChanged)
    Q_PROPERTY(int libraryFunction READ libraryFunction WRITE setLibraryFunction NOTIFY selectionChanged)
    Q_PROPERTY(QStringList functionNames READ functionNames CONSTANT)
    Q_PROPERTY(QVariantList params READ params NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSeed READ hasSeed NOTIFY selectionChanged)
    Q_PROPERTY(int seed READ seed WRITE setSeed NOTIFY selectionChanged)
    Q_PROPERTY(QString expression READ expression WRITE setExpression NOTIFY selectionChanged)
    Q_PROPERTY(QString expressionError READ expressionError NOTIFY selectionChanged)
    Q_PROPERTY(double expressionFrom READ expressionFrom WRITE setExpressionFrom NOTIFY selectionChanged)
    Q_PROPERTY(double expressionTo READ expressionTo WRITE setExpressionTo NOTIFY selectionChanged)
    Q_PROPERTY(double componentCycles READ componentCycles WRITE setComponentCycles NOTIFY selectionChanged)
    Q_PROPERTY(double componentGain READ componentGain WRITE setComponentGain NOTIFY selectionChanged)
    Q_PROPERTY(QString sketchName READ sketchName WRITE setSketchName NOTIFY selectionChanged)
    Q_PROPERTY(int sketchSamples READ sketchSamples WRITE setSketchSamples NOTIFY selectionChanged)

    // Sketch drawing
    Q_PROPERTY(bool drawMode READ drawMode WRITE setDrawMode NOTIFY drawChanged)
    Q_PROPERTY(int drawTool READ drawTool WRITE setDrawTool NOTIFY drawChanged)   // 0 Freehand, 1 Line
    Q_PROPERTY(QVariantList vertices READ vertices NOTIFY drawChanged)            // points in plot coordinates
    Q_PROPERTY(double drawSpan READ drawSpan NOTIFY drawChanged)                  // s: one cycle of the sketch

    // Rendered preview
    Q_PROPERTY(mybench::plot::TraceBuffer* preview READ preview CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* importPreview READ importPreview CONSTANT)
    Q_PROPERTY(double viewStart READ viewStart NOTIFY renderChanged)
    Q_PROPERTY(double viewEnd READ viewEnd NOTIFY renderChanged)
    Q_PROPERTY(double viewMin READ viewMin NOTIFY renderChanged)
    Q_PROPERTY(double viewMax READ viewMax NOTIFY renderChanged)
    Q_PROPERTY(double highlightStart READ highlightStart NOTIFY renderChanged)
    Q_PROPERTY(double highlightEnd READ highlightEnd NOTIFY renderChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY renderChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool aliasWarning READ aliasWarning NOTIFY renderChanged)

    // Undo
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)

    // Channel link ("Use in channel", Edit from a channel card)
    Q_PROPERTY(int linkedChannel READ linkedChannel WRITE setLinkedChannel NOTIFY linkChanged)   // -1 none
    Q_PROPERTY(bool liveUpdate READ liveUpdate WRITE setLiveUpdate NOTIFY linkChanged)

public:
    explicit WaveformEditor(QObject* parent = nullptr);
    ~WaveformEditor() override;

    const gen::Document& document() const { return m_doc; }
    // Replaces the document (new/open/restore/edit from a channel); clears undo.
    void setDocument(const gen::Document& document, const QString& filePath = {}, bool modified = false);
    QJsonObject saveState() const;
    void restoreState(const QJsonObject& state);
    // Rendered samples of the whole document (null before the first render).
    std::shared_ptr<const std::vector<double>> renderedSamples() const { return m_rendered; }

    QString documentName() const { return m_doc.name; }
    QString windowTitle() const;
    QString filePath() const { return m_filePath; }
    bool modified() const { return !m_undo.isClean() || m_forcedModified; }
    QString info() const { return m_doc.info; }
    void setInfo(const QString& info);
    double sampleRate() const { return m_doc.sampleRate; }
    void setSampleRate(double rate);
    int units() const { return int(m_doc.units); }
    void setUnits(int units);
    QString unitName() const { return gen::unitName(m_doc.units); }
    int segmentCount() const { return int(m_doc.segments.size()); }
    int totalSamples() const { return int(m_doc.totalSamples()); }
    double totalDuration() const { return m_doc.totalDuration(); }
    WaveformRowModel* rows() { return &m_rows; }

    int view() const { return m_view; }
    void setView(int view);
    int xAxis() const { return m_xAxis; }
    void setXAxis(int axis);
    bool showCursors() const { return m_showCursors; }
    void setShowCursors(bool show);
    int plotStyle() const { return m_plotStyle; }
    void setPlotStyle(int style);
    bool stepPlot() const { return m_stepPlot; }
    void setStepPlot(bool step);
    double lineWidth() const { return m_lineWidth; }
    void setLineWidth(double width);

    int currentRow() const;
    void setCurrentRow(int row);
    int selectionKind() const;
    int segmentIndex() const { return m_segment; }
    void setSegmentIndex(int index);
    int componentIndex() const { return m_component; }
    QString segmentName() const;
    void setSegmentName(const QString& name);
    double segmentDuration() const;
    void setSegmentDuration(double seconds);
    double segmentGain() const;
    void setSegmentGain(double gain);
    int componentType() const;
    void setComponentType(int type);
    int componentOperator() const;
    void setComponentOperator(int op);
    bool operatorEnabled() const { return m_component > 0 && !m_all; }
    int libraryFunction() const;
    void setLibraryFunction(int function);
    QStringList functionNames() const;
    QVariantList params() const;
    bool hasSeed() const;
    int seed() const;
    void setSeed(int seed);
    QString expression() const;
    void setExpression(const QString& text);
    QString expressionError() const;
    double expressionFrom() const;
    void setExpressionFrom(double v);
    double expressionTo() const;
    void setExpressionTo(double v);
    double componentCycles() const;
    void setComponentCycles(double v);
    double componentGain() const;
    void setComponentGain(double v);
    QString sketchName() const;
    void setSketchName(const QString& name);
    int sketchSamples() const;
    void setSketchSamples(int count);

    bool drawMode() const { return m_drawMode; }
    void setDrawMode(bool on);
    int drawTool() const { return m_drawTool; }
    void setDrawTool(int tool);
    QVariantList vertices() const;
    double drawSpan() const;

    mybench::plot::TraceBuffer* preview() { return &m_preview; }
    mybench::plot::TraceBuffer* importPreview() { return &m_importPreview; }
    double viewStart() const { return m_viewStart; }
    double viewEnd() const { return m_viewEnd; }
    double viewMin() const { return m_viewMin; }
    double viewMax() const { return m_viewMax; }
    double highlightStart() const { return m_highlightStart; }
    double highlightEnd() const { return m_highlightEnd; }
    bool rendering() const { return m_rendering; }
    QStringList warnings() const { return m_warnings; }
    QString statusText() const { return m_statusText; }
    bool aliasWarning() const { return m_alias; }

    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    bool canPaste() const;

    int linkedChannel() const { return m_linkedChannel; }
    void setLinkedChannel(int channel);
    bool liveUpdate() const { return m_liveUpdate; }
    void setLiveUpdate(bool live);

public slots:
    // File
    void newDocument();
    bool openFile(const QUrl& file);
    bool save();                       // false when there is no path yet (use saveAs)
    bool saveAs(const QUrl& file);
    bool exportFile(const QUrl& file);
    // Import wizards: inspect (fills importPreview, returns {ok, error, kind, channels, sampleRate,
    // count, lines, firstNumericLine}) then accept. options: {automatic, delimiters, column,
    // startLine, count, channel}. mode 0: insert a new segment after the current one, 1: replace
    // the selected sketch's values.
    QVariantMap inspectImport(const QUrl& file, const QVariantMap& options = {});
    bool acceptImport(const QUrl& file, const QVariantMap& options, int mode);
    // Edit
    void undo() { m_undo.undo(); }
    void redo() { m_undo.redo(); }
    void newSegment();
    void newComponent();
    void deleteSelection();   // Clear
    void cut();
    void copy();
    void paste();
    void selectAll();
    void selectSegment(int index);
    void selectRow(int row) { setCurrentRow(row); }
    void selectAt(double x);   // click on the plot (Waveform view): the segment under x
    void moveSelection(int delta);   // Move up/down (segments and components)
    void moveRow(int from, int to);  // drag and drop in the list
    void setParam(int index, double displayValue);
    void scaleSelection(double timeFactor, double amplitudeFactor);
    // Sketch drawing (plot coordinates: x in s within one cycle, y in V)
    void drawBegin(double x, double y);
    void drawTo(double x, double y);
    void drawEnd();
    void addVertex(double x, double y);
    void moveVertex(int index, double x, double y);
    void removeVertex(int index);
    void vertexDragBegin();
    void vertexDragEnd();
    // Channel
    void useInChannel(int channel);

signals:
    void documentChanged();
    void viewChanged();
    void selectionChanged();
    void drawChanged();
    void renderChanged();
    void statusChanged();
    void undoChanged();
    void clipboardChanged();
    void linkChanged();
    void rendered();                              // renderedSamples() is up to date
    void useInChannelRequested(int channel);      // controller: load document() into the channel
    void message(int level, const QString& title, const QString& text);   // toast (Notifications levels)

private:
    friend class SnapshotCommand;
    struct State {
        gen::Document doc;
        int segment = 0;
        int component = -1;
        bool all = false;
    };
    State state() const { return {m_doc, m_segment, m_component, m_all}; }
    void restore(const State& state);
    // Applies `mutate` to a copy of the document and records an undo step when it changed.
    bool edit(const QString& text, const std::function<void(gen::Document&)>& mutate, int mergeId = -1);
    void pushState(const QString& text, const State& before, int mergeId = -1);
    gen::Segment* selectedSegment();
    const gen::Segment* selectedSegment() const;
    gen::Component* selectedComponent();
    const gen::Component* selectedComponent() const;
    void rebuildRows();
    void documentEdited();
    void scheduleRender();
    void startRender();
    void setStatus(const QString& text);
    void rasterizeVertices(gen::Component& component) const;
    int sketchIndexAt(double x, const gen::Component& component) const;
    bool refuseTooLarge(const gen::Document& candidate);
    void loadPreferences();
    void savePreferences() const;

    gen::Document m_doc;
    QString m_filePath;
    bool m_forcedModified = false;
    int m_segment = 0;
    int m_component = -1;
    bool m_all = false;
    QUndoStack m_undo;
    WaveformRowModel m_rows;

    int m_view = 0;
    int m_xAxis = 0;
    bool m_showCursors = false;
    int m_plotStyle = 0;
    bool m_stepPlot = false;
    double m_lineWidth = 1.5;

    bool m_drawMode = false;
    int m_drawTool = 0;
    int m_viewBeforeDraw = 0;
    std::optional<State> m_dragBefore;
    int m_lastDrawIndex = -1;

    mybench::plot::TraceBuffer m_preview;
    mybench::plot::TraceBuffer m_importPreview;
    double m_viewStart = 0, m_viewEnd = 10, m_viewMin = -1, m_viewMax = 1;
    double m_highlightStart = 0, m_highlightEnd = 10;
    bool m_rendering = false;
    QStringList m_warnings;
    QString m_statusText;
    bool m_alias = false;
    std::shared_ptr<const std::vector<double>> m_rendered;
    QTimer m_renderTimer;
    QThreadPool m_pool;
    quint64 m_generation = 0;

    int m_linkedChannel = -1;
    bool m_liveUpdate = true;
};

}  // namespace mybench::instruments
