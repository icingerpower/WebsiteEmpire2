#ifndef DIALOGTRANSFERLOG_H
#define DIALOGTRANSFERLOG_H

#include <QDialog>

namespace Ui {
class DialogTransferLog;
}

/**
 * Application-modal progress dialog with a scrolling log, used for long
 * remote transfers (rsync/ssh) so the user sees per-step output instead of a
 * frozen main window.
 *
 * Intended usage from the caller (which drives the actual work):
 *   1. construct + show()
 *   2. setTotalSteps(n), then per step: appendLine(...) / advanceStep()
 *      (the caller must keep the event loop pumping, e.g. by waiting on
 *      QProcess::finished through a QEventLoop instead of waitForFinished())
 *   3. markFinished(ok), then exec() to keep the log on screen until the
 *      user clicks Close.
 *
 * The Close button is disabled and closeEvent is ignored until
 * markFinished() is called — the dialog owns no worker, so closing it
 * mid-run would leave the caller's loop running headless.
 */
class DialogTransferLog : public QDialog
{
    Q_OBJECT

public:
    explicit DialogTransferLog(const QString &title, QWidget *parent = nullptr);
    ~DialogTransferLog() override;

    void setTotalSteps(int total);
    void advanceStep();
    void appendLine(const QString &line);
    void markFinished(bool success);

public slots:
    // Guarded: ignored (like closeEvent) until markFinished() — Escape would
    // otherwise dismiss the dialog while the caller's transfer loop still runs.
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::DialogTransferLog *ui;
    bool                   m_finished = false;
};

#endif // DIALOGTRANSFERLOG_H
