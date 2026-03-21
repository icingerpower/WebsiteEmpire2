#include "ClaudeRunner.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <QCoro/QCoroProcess>

// ---------------------------------------------------------------------------
// JSON extraction from Claude's raw stdout (fallback when reply.json absent)
// ---------------------------------------------------------------------------

static QString extractJson(const QString &output)
{
    const QString trimmed = output.trimmed();

    // Try 1: whole output is valid JSON.
    if (!QJsonDocument::fromJson(trimmed.toUtf8()).isNull()) {
        return trimmed;
    }

    // Try 2: ```json...``` or ``` ... ``` fenced code block.
    static const QRegularExpression reBlock(
        QStringLiteral(R"(```(?:json)?\s*\n([\s\S]*?)\n```)"));
    const QRegularExpressionMatch m = reBlock.match(output);
    if (m.hasMatch()) {
        const QString candidate = m.captured(1).trimmed();
        if (!QJsonDocument::fromJson(candidate.toUtf8()).isNull()) {
            return candidate;
        }
    }

    // Try 3: Scan for the first '{' or '[' and try to parse from there.
    for (int i = 0; i < output.size(); ++i) {
        if (output.at(i) == QLatin1Char('{') || output.at(i) == QLatin1Char('[')) {
            const QString candidate = output.mid(i).trimmed();
            if (!QJsonDocument::fromJson(candidate.toUtf8()).isNull()) {
                return candidate;
            }
        }
    }

    return {};
}

// ---------------------------------------------------------------------------

QCoro::Task<ClaudeJobResult> runClaudeJob(const QString &jobJson)
{
    ClaudeJobResult res;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        co_return res;
    }

    // Inject a file-writing instruction into the prompt.
    // Claude's Write tool produces a clean file; reading it is more reliable
    // than extracting JSON from potentially decorated stdout.
    QJsonDocument doc = QJsonDocument::fromJson(jobJson.toUtf8());
    QJsonObject obj = doc.object();
    obj[QStringLiteral("writeReplyTo")] = QStringLiteral("reply.json");
    obj[QStringLiteral("writeInstruction")] =
        QStringLiteral("Use the Write tool to save ONLY the filled replyFormat "
                       "JSON object to the file 'reply.json' in the current directory. "
                       "The file must contain valid JSON only — no other text.");
    const QString prompt =
        QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));

    QElapsedTimer timer;
    timer.start();

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(QStringLiteral("claude"));
    process.setArguments({
        QStringLiteral("-p"),
        prompt,
        QStringLiteral("--dangerously-skip-permissions"),
    });
    // Close stdin so Claude cannot wait for interactive input.
    process.setStandardInputFile(QProcess::nullDevice());

    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    res.durationMs     = timer.elapsed();
    res.exitCode       = process.exitCode();
    res.processStarted = (process.error() != QProcess::FailedToStart);
    res.rawOutput      = QString::fromUtf8(process.readAllStandardOutput());
    res.stderrOutput   = QString::fromUtf8(process.readAllStandardError());

    if (res.processStarted && res.exitCode == 0) {
        // Primary: read reply.json written by Claude's Write tool.
        QFile replyFile(tempDir.path() + QStringLiteral("/reply.json"));
        if (replyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString fileContent = QString::fromUtf8(replyFile.readAll()).trimmed();
            if (!QJsonDocument::fromJson(fileContent.toUtf8()).isNull()) {
                res.json = fileContent;
            }
        }

        // Fallback: extract JSON from stdout if the file was not produced.
        if (res.json.isEmpty()) {
            res.json = extractJson(res.rawOutput);
        }
    }

    co_return res;
}

// ---------------------------------------------------------------------------

void writeClaudeJobLog(const QDir           &logDir,
                       const QString        &jobId,
                       const ClaudeJobResult &result)
{
    logDir.mkpath(QStringLiteral("."));

    // Sanitize jobId: slashes and colons are invalid in most filenames.
    QString safeName = jobId;
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    safeName.replace(QLatin1Char(':'), QLatin1Char('_'));
    if (safeName.length() > 60) {
        safeName = safeName.left(60);
    }

    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString filePath =
        logDir.filePath(ts + QLatin1Char('_') + safeName + QStringLiteral(".txt"));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "=== Claude Job Log ===\n"
        << "Job ID:      " << jobId << "\n"
        << "Time:        " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
        << "Duration:    " << result.durationMs << " ms\n"
        << "Exit code:   " << result.exitCode << "\n"
        << "Process:     " << (result.processStarted ? "started OK" : "FAILED TO START") << "\n"
        << "JSON found:  " << (result.json.isEmpty() ? "NO" : "YES") << "\n"
        << "\n--- stdout ---\n"
        << result.rawOutput;

    if (!result.rawOutput.endsWith(QLatin1Char('\n'))) {
        out << "\n";
    }

    if (!result.stderrOutput.isEmpty()) {
        out << "\n--- stderr ---\n"
            << result.stderrOutput;
        if (!result.stderrOutput.endsWith(QLatin1Char('\n'))) {
            out << "\n";
        }
    }

    if (!result.json.isEmpty()) {
        out << "\n--- Extracted JSON ---\n"
            << result.json << "\n";
    }
}
