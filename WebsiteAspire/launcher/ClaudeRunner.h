#ifndef CLAUDERUNNER_H
#define CLAUDERUNNER_H

#include <QDir>
#include <QString>

#include <QCoro/QCoroTask>

// Result returned by runClaudeJob().
struct ClaudeJobResult {
    QString json;            // Extracted JSON reply — empty on any failure.
    QString rawOutput;       // Full stdout from the Claude process.
    QString stderrOutput;    // Stderr from the Claude process.
    int     exitCode       = -1;
    qint64  durationMs     = 0;
    bool    processStarted = false; // false when the claude executable was not found.
};

// Runs a single generator job through the Claude CLI and returns a ClaudeJobResult.
//
// Spawns: claude -p <jobJson> --dangerously-skip-permissions
// in a fresh QTemporaryDir so each session is fully isolated.
//
// ClaudeJobResult::json is populated only when processStarted is true,
// exitCode == 0, and at least one of the three extraction strategies succeeds:
//   1. Whole stdout is valid JSON.
//   2. A ```json...``` (or ``` ... ```) fenced code block contains JSON.
//   3. The substring starting at the first '{' or '[' is valid JSON.
QCoro::Task<ClaudeJobResult> runClaudeJob(const QString &jobJson);

// Writes a per-job log file to logDir/<timestamp>_<jobId>.txt.
// Creates logDir if it does not exist.  Silent on write failure.
// Content: metadata (duration, exit code), raw stdout, stderr, extracted JSON.
void writeClaudeJobLog(const QDir           &logDir,
                       const QString        &jobId,
                       const ClaudeJobResult &result);

#endif // CLAUDERUNNER_H
