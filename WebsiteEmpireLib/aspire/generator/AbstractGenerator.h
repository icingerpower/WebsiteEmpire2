#ifndef ABSTRACTGENERATOR_H
#define ABSTRACTGENERATOR_H

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QObject>
#include <QScopedPointer>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

class AbstractPageAttributes;
class DownloadedPagesTable;

// Base class for Claude-assisted data generation tasks.
//
// A generator maintains an ordered list of pending jobs (keyed by string ID).
// Each job produces a JSON payload that is sent to Claude; Claude's filled reply
// is persisted via recordReply().  Completed jobs are tracked in a QSettings .ini
// file inside workingDir() so that the process can be resumed across runs.
//
// Subclasses self-register via DECLARE_GENERATOR() and are accessible through
// ALL_GENERATORS().  The registered prototype is constructed with QDir{} and
// must NOT call getNextJob()/recordReply(); use createInstance() to obtain a
// working instance bound to a real directory.
//
// getNextJob() advances an internal cursor on each call so that n successive
// calls return n distinct jobs.  recordReply() identifies the target job by the
// "jobId" key present in the JSON reply and is independent of the cursor.
//
// Optional runtime parameters (e.g. a CSV file path) are declared via getParams()
// and stored in the generator's .ini under [Params]/<id>.  The UI reads and writes
// them through paramValue() / saveParamValue().
class AbstractGenerator : public QObject
{
    Q_OBJECT
public:
    // Describes one configurable parameter for this generator.
    struct Param {
        QString  id;           // Stable machine-readable key; used for persistence.
        QString  name;         // Human-readable label (may be translated; not persisted).
        QString  tooltip;      // Explanatory tooltip shown in the parameter editor.
        QVariant defaultValue; // Default value when no setting has been saved yet.
        bool isFile   = false; // Value is a file path — UI shows a file picker on edit.
        bool isFolder = false; // Value is a folder path — UI shows a folder picker on edit.
    };

    // Metadata for one result table, used by the empire layer to count and route
    // articles without depending on the display-name string used by the UI.
    struct TableDescriptor {
        QString id;        // Stable — matches AbstractPageAttributes::getId()
        QString name;      // Human-readable; may be translated; not persisted
        QString tablePath; // Absolute path to the .db file for this generator instance
    };

    // Typed grouping of all result tables declared by a generator, keyed by
    // TableDescriptor::id, for empire-layer article generation.
    //   primary    — exactly one entry when non-empty: one row = one article/page
    //   category   — 0..N controlled-vocabulary / lookup tables
    //   referredTo — 0..N child/detail tables (multiple rows may belong to one Primary row)
    struct GeneratorTables {
        QHash<QString, TableDescriptor> primary;    // Q_ASSERT: size == 1 when non-empty
        QHash<QString, TableDescriptor> category;
        QHash<QString, TableDescriptor> referredTo;
    };

    explicit AbstractGenerator(const QDir &workingDir = QDir(), QObject *parent = nullptr);

    // Unique machine-readable identifier for this generator (used as .ini filename stem).
    virtual QString getId() const = 0;

    // Human-readable display name.
    virtual QString getName() const = 0;

    // Factory: returns a new instance bound to workingDir (caller takes ownership).
    virtual AbstractGenerator *createInstance(const QDir &workingDir) const = 0;

    // Returns the next pending job as a compact JSON string, or an empty QString
    // when all jobs are complete.  The JSON always includes a "jobId" key.
    // Successive calls advance an internal cursor — each call returns a distinct job.
    QString getNextJob();

    // Returns the complete ordered list of initial (static) job IDs supplied by the
    // subclass via buildInitialJobIds().  Does not include dynamically discovered jobs.
    QStringList getAllJobIds() const;

    // Returns the working directory this generator instance is bound to.
    const QDir &workingDir() const;

    // Returns the number of jobs not yet completed (includes jobs already retrieved
    // via getNextJob() but not yet recorded via recordReply()).  Loads state on first call.
    int pendingCount();

    // Records Claude's filled JSON reply.  The reply must contain the "jobId"
    // that was present in the job returned by getNextJob().  Marks that job as done
    // and persists the state.
    // Returns false (without throwing) if the JSON is malformed, "jobId" is absent,
    // or the job is unknown / already completed.
    // Throws ExceptionWithTitleText when the reply content fails validation.
    bool recordReply(const QString &jsonReply);

    // Returns all registered generator prototypes keyed by getId().
    static const QMap<QString, const AbstractGenerator *> &ALL_GENERATORS();

    // Used by DECLARE_GENERATOR to register a prototype at startup.
    class Recorder
    {
    public:
        explicit Recorder(AbstractGenerator *generator);
    };

    // ---- Results table ----------------------------------------------------

    // Override to return a map of freshly allocated AbstractPageAttributes that
    // describe the schemas of this generator's result tables.
    // Key   = human-readable display name for the table (use tr(); not persisted).
    // Value = AbstractPageAttributes* whose getId() is used as the stable DB
    //         table-id filename stem.  Ownership transfers to openResultsTable().
    // Default: empty map — no results tables are created.
    virtual QMap<QString, AbstractPageAttributes *> createResultPageAttributes() const;

    // Returns table-role metadata for empire-layer article generation.
    // Generators that produce articles must override this.  When any field is
    // non-empty, primary must contain exactly one entry (Q_ASSERT enforced in
    // each override — violation signals a dev correction is needed).
    // Default: returns an empty GeneratorTables{} (no article tables).
    virtual GeneratorTables getTables() const;

    // Creates (if not yet open) all results tables declared by
    // createResultPageAttributes() and returns the first one (keyed alphabetically
    // by attrs->getId()).  Returns nullptr when createResultPageAttributes() is empty.
    // Tables are parented to this generator and live as long as the generator does.
    DownloadedPagesTable *openResultsTable();

    // Same as openResultsTable() but returns all tables as ordered pairs of
    // (displayName, table), in the same order as createResultPageAttributes().
    // Useful for building multi-table UIs.
    QList<QPair<QString, DownloadedPagesTable *>> openResultsTables();

    // Returns the first already-opened results table, or nullptr if
    // openResultsTable() was never called or createResultPageAttributes() was empty.
    DownloadedPagesTable *resultsTable() const;

    // Returns the already-opened results table whose AbstractPageAttributes::getId()
    // equals attrId, or nullptr if no such table was opened.
    DownloadedPagesTable *resultsTable(const QString &attrId) const;

    // ---- Parameters -------------------------------------------------------

    // Returns the list of configurable parameters for this generator.
    // Default: empty list (no parameters needed).
    virtual QList<Param> getParams() const;

    // Validates the supplied parameter values.  The list should come from
    // currentParams() so each entry's defaultValue holds the current saved value.
    // Returns an empty string when all values are valid, or a human-readable
    // error message when one or more are invalid.
    // Default: always returns QString() (no validation).
    virtual QString checkParams(const QList<Param> &params) const;

    // Returns the persisted value for param 'id', or the param's defaultValue
    // when no setting has been saved yet.
    QVariant paramValue(const QString &id) const;

    // Persists the value for param 'id' to the generator's .ini and calls
    // onParamChanged().
    void saveParamValue(const QString &id, const QVariant &value);

    // Returns getParams() with each entry's defaultValue replaced by its current
    // saved value.  Pass this to checkParams() for validation.
    QList<Param> currentParams() const;

protected:
    // Subclass hook: returns the complete ordered list of initial job IDs (static,
    // known up front).  The base class caches and manages the result.
    // Do NOT call this directly — use getAllJobIds() instead.
    virtual QStringList buildInitialJobIds() const = 0;

    // Builds the JSON payload for jobId, without the "jobId" key (base class injects it).
    virtual QJsonObject buildJobPayload(const QString &jobId) const = 0;

    // Persists Claude's reply for jobId.  Throw ExceptionWithTitleText on invalid content.
    // Called by recordReply() after extracting and validating jobId.
    // May call addDiscoveredJob() to inject new jobs that arise from this reply
    // (e.g. step-2 jobs discovered when step-1 reveals >N factory kinds).
    virtual void processReply(const QString &jobId, const QJsonObject &reply) = 0;

    // Constructs a TableDescriptor for attrId using this instance's workingDir.
    // tablePath follows the results_db/ convention used by openResultsTable().
    TableDescriptor _makeDescriptor(const QString &id, const QString &name) const;

    // Adds a job that was not part of the original buildInitialJobIds() list.
    // Safe to call from processReply(); the job is appended to the pending queue and
    // immediately persisted to [Discovered]/ids so it survives a program restart.
    // No-op if jobId is already pending, done, or already discovered.
    void addDiscoveredJob(const QString &jobId);

    // Called after saveParamValue() whenever a param is modified.  Subclasses
    // may override to react (e.g. to invalidate a data cache).  Default: no-op.
    virtual void onParamChanged(const QString &id);

    // Resets the in-memory state so loadState() runs again on the next getNextJob()
    // call.  Call this from onParamChanged() when a param change invalidates the
    // initial job list (e.g. a CSV path change that reloads the city list).
    // Persisted Done/Discovered data in the .ini is NOT modified.
    void resetState();

    // Inserts attrs as a new row in the results table identified by attrId
    // (the AbstractPageAttributes::getId() value of the target table).
    // No-op when no table with that id was opened.
    // May throw ExceptionWithTitleText on validation failure.
    void recordResultPage(const QString &attrId, const QHash<QString, QString> &attrs);

private:
    QSettings &settings() const;
    void loadState();

    QDir m_workingDir;
    bool m_stateLoaded = false;
    QStringList m_pending;
    int m_jobIndex = 0;
    mutable QScopedPointer<QSettings> m_settings;

    static QMap<QString, const AbstractGenerator *> &getGenerators();

    // Keyed by AbstractPageAttributes::getId() — stable across tr() renames.
    QMap<QString, DownloadedPagesTable *> m_resultsTables;
    // Ordered list of {displayName, attrId} in createResultPageAttributes() order.
    QList<QPair<QString, QString>> m_resultTableOrder;
};

// Registers a generator prototype.  Passes QDir{} explicitly so subclasses
// are not required to provide a no-arg constructor.
#define DECLARE_GENERATOR(NEW_CLASS)                                        \
    NEW_CLASS instance##NEW_CLASS{QDir{}};                                  \
    AbstractGenerator::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTGENERATOR_H
