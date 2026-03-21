#ifndef WIDGETGENERATOR_H
#define WIDGETGENERATOR_H

#include <QModelIndex>
#include <QWidget>

#include <QCoro/QCoroTask>

class AbstractGenerator;

namespace Ui {
class WidgetGenerator;
}

// Per-generator control widget displayed inside PaneDatabaseGen.
//
// Layout (top to bottom):
//   • Button bar  — Get Jobs (+ count spinbox), Copy Jobs, Record Reply, Copy command,
//                   stats label | Run (checkable) + Sessions spinbox
//   • Parameter table (QTableView) — shown only when the generator declares params.
//   • Error label (red)  — shown when checkParams() returns a non-empty error.
//   • Log label          — last operation result.
//   • Splitter (vertical): Jobs panel / Reply panel.
//
// Manual workflow:
//   1. Configure params in the parameter table.
//   2. Click "Get Jobs" → JSON array displayed.
//   3. Send to Claude, paste reply, click "Record Reply".
//
// Auto-run workflow (Run button):
//   1. Configure params, then press "Run" (it becomes checked / highlighted).
//   2. The widget retrieves jobs one at a time, passes each to the Claude CLI
//      (claude -p <job> --dangerously-skip-permissions) in a temporary directory,
//      parses Claude's JSON reply, and records it automatically.
//   3. N parallel sessions run concurrently (configured via the Sessions spinbox).
//      The sessions count is persisted in QSettings per generator.
//   4. Press "Run" again to stop after the current job(s) finish.
//      Buttons are disabled during auto-run and re-enabled when all sessions finish.
class WidgetGenerator : public QWidget
{
    Q_OBJECT

public:
    explicit WidgetGenerator(QWidget *parent = nullptr);

    // Binds the widget to a generator instance.  The generator must outlive this
    // widget; ownership is NOT transferred.
    void init(AbstractGenerator *generator);

    ~WidgetGenerator();

public slots:
    void getJobs();
    void copyJobs();
    void recordReply();
    void copyCommand();

private slots:
    void _onParamDoubleClicked(const QModelIndex &index);
    void _onParamChanged();
    void _onRunToggled(bool checked);
    void _onAllSessionsDone();

private:
    // Coroutine executed for each parallel Claude session while m_stopRequested is false.
    // Calls getNextJob() → runClaudeJob() → recordReply() in a loop.
    // sessionIndex is 0-based and used only for display (shown as sessionIndex+1).
    // Failed jobs (Claude error / no JSON) are logged and skipped; the session
    // continues with the next job rather than stopping, unless claude is not found.
    QCoro::Task<void> _runSession(int sessionIndex);

    void _validateParams();
    void _updateStats();

    // Enables or disables all interactive controls while auto-run is active.
    void _setRunningUi(bool running);

    // Returns the QSettings key used to persist the sessions count.
    QString _sessionsSettingsKey() const;

    Ui::WidgetGenerator *ui;
    AbstractGenerator   *m_generator         = nullptr;
    bool                 m_stopRequested     = false;
    int                  m_activeSessionCount = 0;
    int                  m_jobsDone          = 0;
    int                  m_jobsFailed        = 0;
};

#endif // WIDGETGENERATOR_H
