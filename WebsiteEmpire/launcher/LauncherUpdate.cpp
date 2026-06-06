#include "LauncherUpdate.h"

#include "gui/panes/UpdateStrategyTree.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <csignal>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

#include "aicli/AbstractCli.h"

const QString LauncherUpdate::OPTION_NAME     = QStringLiteral("update");
const QString LauncherUpdate::OPTION_STRATEGY = QStringLiteral("strategy");
const QString LauncherUpdate::OPTION_PROMPT   = QStringLiteral("prompt");
const QString LauncherUpdate::OPTION_LIMIT    = QStringLiteral("limit");
const QString LauncherUpdate::OPTION_PAGES    = QStringLiteral("pages");

DECLARE_LAUNCHER(LauncherUpdate)

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_updateStopRequested = 0;

static void handleUpdateSignal(int)
{
    g_updateStopRequested = 1;
}

// ---------------------------------------------------------------------------
// Validation — same rules as generation
// ---------------------------------------------------------------------------

static bool isUpdatedArticleValid(const QString &text)
{
    static const QRegularExpression reSvg(
        QStringLiteral("<svg\\b[^>]*>.*?</svg>"),
        QRegularExpression::DotMatchesEverythingOption
        | QRegularExpression::CaseInsensitiveOption);
    static const QString kTitle = QStringLiteral("[TITLE level=\"1\"]");

    QString clean = text.trimmed();
    clean.remove(reSvg);
    clean = clean.trimmed();
    if (!clean.startsWith(QLatin1Char('['))) {
        const int pos = clean.indexOf(kTitle);
        if (pos > 0) {
            clean = clean.mid(pos);
        }
    }
    return clean.startsWith(kTitle) && clean.size() >= 2000;
}

static bool isCommaSeparatedIntsValid(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }
    for (const QString &p : std::as_const(parts)) {
        bool ok;
        p.trimmed().toInt(&ok);
        if (!ok) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// AI CLI helper — blocking (no QCoro needed; subprocess has no GUI).
// ---------------------------------------------------------------------------

// Print a heartbeat line every 2 minutes while waiting for the Claude subprocess
// so PaneUpdate's 25-minute inactivity timer does not fire on slow responses.
static constexpr int kClaudeHeartbeatMs = 2 * 60 * 1000;

static QString runUpdateClaudePrompt(const QString &prompt,
                                      AbstractCli   *cli,
                                      QTextStream   *out)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return {};
    }

    const QString promptSubdir = tempDir.path() + QStringLiteral("/prompt");
    QDir().mkdir(promptSubdir);
    const QString promptPath = promptSubdir + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            return {};
        }
        f.write(prompt.toUtf8());
    }

    const QString outputPath = tempDir.path() + QStringLiteral("/output.txt");

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(cli->getExecutable());
    process.setArguments(cli->promptArgs());
    process.setStandardInputFile(promptPath);
    process.setStandardOutputFile(outputPath);
    process.start();
    process.waitForStarted(-1);

    // Poll in short intervals and emit a heartbeat so the parent GUI process
    // knows the subprocess is alive and does not trigger its inactivity timer.
    int elapsedMs = 0;
    while (!process.waitForFinished(kClaudeHeartbeatMs)) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        elapsedMs += kClaudeHeartbeatMs;
        *out << QStringLiteral("  Still waiting for Claude... (%1 min elapsed)\n")
                    .arg(elapsedMs / 60000);
        out->flush();
    }

    if (process.error() == QProcess::FailedToStart) {
        return QStringLiteral("__ERROR__: %1 executable not found in PATH")
                   .arg(cli->getExecutable());
    }
    if (process.exitCode() != 0) {
        const QString errMsg = QString::fromUtf8(process.readAllStandardError()).trimmed();
        const QString detail = !errMsg.isEmpty() ? errMsg
                             : QStringLiteral("exit code %1").arg(process.exitCode());
        return QStringLiteral("__ERROR__: ") + detail;
    }

    QString result;
    QFile f(outputPath);
    if (f.open(QIODevice::ReadOnly)) {
        result = QString::fromUtf8(f.readAll()).trimmed();
    }
    return result;
}

// Non-agentic variant for SVG generation and review.  Uses plain `-p -` without
// `--dangerously-skip-permissions` so the model cannot invoke any tools.
// SVG output is pure text — tools are never needed, and the agentic mode causes
// Claude Code to enter a coding loop when given large prompts, inflating runtimes
// from seconds to 30+ minutes.
static QString runSvgClaudePrompt(const QString &prompt,
                                   AbstractCli   *cli,
                                   QTextStream   *out)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return {};
    }

    const QString promptSubdir = tempDir.path() + QStringLiteral("/prompt");
    QDir().mkdir(promptSubdir);
    const QString promptPath = promptSubdir + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            return {};
        }
        f.write(prompt.toUtf8());
    }

    const QString outputPath = tempDir.path() + QStringLiteral("/output.txt");

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(cli->getExecutable());
    process.setArguments({QStringLiteral("-p"), QStringLiteral("-")});
    process.setStandardInputFile(promptPath);
    process.setStandardOutputFile(outputPath);
    process.start();
    process.waitForStarted(-1);

    int elapsedMs = 0;
    while (!process.waitForFinished(kClaudeHeartbeatMs)) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        elapsedMs += kClaudeHeartbeatMs;
        *out << QStringLiteral("  Still waiting for Claude... (%1 min elapsed)\n")
                    .arg(elapsedMs / 60000);
        out->flush();
    }

    if (process.error() == QProcess::FailedToStart) {
        return QStringLiteral("__ERROR__: %1 executable not found in PATH")
                   .arg(cli->getExecutable());
    }
    if (process.exitCode() != 0) {
        const QString errMsg = QString::fromUtf8(process.readAllStandardError()).trimmed();
        const QString detail = !errMsg.isEmpty() ? errMsg
                             : QStringLiteral("exit code %1").arg(process.exitCode());
        return QStringLiteral("__ERROR__: ") + detail;
    }

    QString result;
    QFile f(outputPath);
    if (f.open(QIODevice::ReadOnly)) {
        result = QString::fromUtf8(f.readAll()).trimmed();
    }
    return result;
}

// Programmatic guard: count <text> elements that contain non-whitespace content.
// A table SVG with only headers and empty data rows will have very few populated
// text nodes.  This catches the "skeleton with no data" failure without an LLM call.
static constexpr int kMinSvgTextElements = 5;

static bool hasSufficientSvgContent(const QString &svg)
{
    static const QRegularExpression reText(
        QStringLiteral("<text\\b[^>]*>([^<]+)</text>"),
        QRegularExpression::CaseInsensitiveOption);
    int count = 0;
    auto it = reText.globalMatch(svg);
    while (it.hasNext()) {
        if (!it.next().captured(1).trimmed().isEmpty()) {
            if (++count >= kMinSvgTextElements) {
                return true;
            }
        }
    }
    return false;
}

// After the programmatic guard passes, send the SVG together with the article
// context and original instructions to a second Claude call that verifies content
// accuracy and instruction compliance.
// Returns the raw reviewer response (starts with "OK" or "FAIL: <reason>").
static QString runSvgReview(const QString &svgResult,
                             const QString &articleForPrompt,
                             const QString &svgInstructions,
                             AbstractCli   *cli,
                             QTextStream   *out)
{
    const QString reviewPrompt =
        QStringLiteral(
            "You are reviewing an SVG image generated for a web article.\n\n"
            "Article context:\n<article>\n")
        + articleForPrompt
        + QStringLiteral(
            "\n</article>\n\n"
            "Update instructions the SVG must follow:\n<instructions>\n")
        + svgInstructions
        + QStringLiteral(
            "\n</instructions>\n\n"
            "Generated SVG:\n<svg_output>\n")
        + svgResult
        + QStringLiteral(
            "\n</svg_output>\n\n"
            "To respond OK, ALL of the following must be true:\n"
            "1. Every data row (not just section headers) contains actual filled-in text — "
            "no row is empty or contains only whitespace.\n"
            "2. The specific data from the article (names, values, descriptions) appears "
            "in the SVG rows — not invented placeholder text.\n"
            "3. The SVG correctly follows ALL update instructions above.\n\n"
            "Respond with ONLY one of:\n"
            "- \"OK\" — if all three conditions above are met.\n"
            "- \"FAIL: <reason>\" — if any condition is not met. Be specific: name which "
            "rows are empty or which instructions were not followed.\n"
            "Do not write anything else.");
    return runSvgClaudePrompt(reviewPrompt, cli, out);
}

// ---------------------------------------------------------------------------
// Per-prompt update session — blocking, no coroutines
// ---------------------------------------------------------------------------

struct UpdateRunState {
    int          jobsCompleted = 0;
    int          jobsLimit     = -1;
    QTextStream *out           = nullptr;
};

static void runUpdateSession(const QString                          &pageTypeId,
                              const UpdateStrategyTree::PromptInfo   &prompt,
                              UpdateRunState                         *state,
                              AbstractCli                            *cli,
                              const QSet<int>                        &pageIdsFilter = {})
{
    QDir             workDir = WorkingDirectoryManager::instance()->workingDir();
    PageDb           pageDb(workDir);
    PageRepositoryDb pageRepo(pageDb);

    const QString &promptId     = prompt.id;
    const QString &saveKey      = prompt.saveKey;

    // Prepend mode-specific constraints for non-SVG targets only.
    // The SVG preamble is applied per-image inside the loop because its wording
    // differs depending on whether an existing SVG was found (improve vs. create).
    QString instructions = prompt.instructions;
    if (prompt.updateImages) {
        instructions.prepend(QStringLiteral(
            "Your sole task is to improve the non-SVG images referenced in this article. "
            "Every part of the article other than image references or descriptions must "
            "remain byte-for-byte identical. Do not touch any text, headings, or SVG content.\n\n"));
    }
    const bool     skipIfSet     = prompt.skipIfKeyNonEmpty;

    const QString effectiveSaveKey = saveKey.isEmpty()
        ? QStringLiteral("1_text")
        : saveKey;

    AbstractPageBloc::AiUpdateSpec::Validator validator =
        AbstractPageBloc::AiUpdateSpec::Validator::ArticleFormat;
    QString formatPrompt;
    QString aiKeyClue;

    if (!saveKey.isEmpty()) {
        CategoryTable catTable(workDir);
        auto pageType = AbstractPageType::createForTypeId(pageTypeId, catTable);
        if (pageType) {
            const auto targets = pageType->aiUpdateTargets();
            for (const auto &t : std::as_const(targets)) {
                if (t.prefixedKey == saveKey) {
                    validator    = t.validator;
                    formatPrompt = t.formatPrompt;
                    aiKeyClue    = t.aiKeyClue;
                    break;
                }
            }
        }
    }

    const bool isTwoCall = (validator != AbstractPageBloc::AiUpdateSpec::Validator::ArticleFormat);

    const int remaining = (state->jobsLimit >= 0)
        ? state->jobsLimit - state->jobsCompleted
        : -1;

    const QString filterKey = skipIfSet ? effectiveSaveKey : QString{};
    QList<PageRecord> pages =
        pageRepo.findPagesForUpdate(pageTypeId, promptId, remaining, filterKey);

    if (!pageIdsFilter.isEmpty()) {
        auto it = std::remove_if(pages.begin(), pages.end(),
                                 [&pageIdsFilter](const PageRecord &p) {
                                     return !pageIdsFilter.contains(p.id);
                                 });
        pages.erase(it, pages.end());
    }

    if (pages.isEmpty()) {
        *(state->out) << QStringLiteral("No pages to update for prompt %1.\n").arg(promptId);
        state->out->flush();
    }

    for (const PageRecord &page : std::as_const(pages)) {
        if (g_updateStopRequested) {
            break;
        }
        if (state->jobsLimit >= 0 && state->jobsCompleted >= state->jobsLimit) {
            break;
        }

        *(state->out) << QStringLiteral("Updating %1 with prompt %2...\n")
                             .arg(page.permalink, promptId);
        state->out->flush();

        QHash<QString, QString> data = pageRepo.loadData(page.id);

        const bool alreadyProcessedByThisPrompt =
            !pageRepo.lastUpdateAttemptAt(page.id, promptId).isEmpty();
        if (skipIfSet
                && alreadyProcessedByThisPrompt
                && !data.value(effectiveSaveKey).trimmed().isEmpty()) {
            *(state->out) << QStringLiteral("SKIP %1: %2 already set.\n")
                                 .arg(page.permalink, effectiveSaveKey);
            state->out->flush();
            continue;
        }

        const QString existingText = data.value(QStringLiteral("1_text"));
        if (existingText.isEmpty()) {
            *(state->out) << QStringLiteral("SKIP %1: no 1_text found.\n")
                                 .arg(page.permalink);
            state->out->flush();
            continue;
        }

        static const int kMaxAttempts = 3;
        QString result;
        bool valid = false;

        if (prompt.updateSvg) {
            // SVG path: find [IMGFIX fileName="*.svg"] shortcodes, load the blob
            // from images.db, ask Claude to regenerate, save back to images.db.
            // Does NOT touch 1_text.
            static const QRegularExpression reImgFix(
                QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"([^\"]+\\.svg)\""),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression reSvgStart(
                QStringLiteral("\\A\\s*<svg\\b"),
                QRegularExpression::CaseInsensitiveOption);

            QStringList svgFilenames;
            {
                auto it = reImgFix.globalMatch(existingText);
                while (it.hasNext()) {
                    const QString fn = it.next().captured(1);
                    if (!svgFilenames.contains(fn)) {
                        svgFilenames.append(fn);
                    }
                }
            }

            if (svgFilenames.isEmpty()) {
                *(state->out) << QStringLiteral("SKIP %1: no [IMGFIX] SVG shortcode found.\n")
                                     .arg(page.permalink);
                state->out->flush();
                continue;
            }

            const QString imgDbPath = workDir.filePath(QStringLiteral("images.db"));
            bool allValid = true;

            for (const QString &svgFilename : std::as_const(svgFilenames)) {
                if (g_updateStopRequested) {
                    allValid = false;
                    break;
                }

                // Load existing SVG blob from images.db.
                QString existingSvg;
                int imageId = -1;
                const QString readConn = QStringLiteral("svg_read_") + svgFilename;
                {
                    QSqlDatabase imgDb = QSqlDatabase::addDatabase(
                        QStringLiteral("QSQLITE"), readConn);
                    imgDb.setDatabaseName(imgDbPath);
                    if (imgDb.open()) {
                        QSqlQuery q(imgDb);
                        q.prepare(QStringLiteral(
                            "SELECT b.id, b.blob FROM images b"
                            " JOIN image_names n ON n.image_id = b.id"
                            " WHERE n.filename = :fn LIMIT 1"));
                        q.bindValue(QStringLiteral(":fn"), svgFilename);
                        if (q.exec() && q.next()) {
                            imageId     = q.value(0).toInt();
                            existingSvg = QString::fromUtf8(q.value(1).toByteArray());
                        }
                    }
                }
                QSqlDatabase::removeDatabase(readConn);

                // Choose the right preamble: "improve" when the SVG exists,
                // "create" when it does not.  Using the improve preamble for a
                // missing SVG tells Claude to "only modify <svg> elements" while
                // simultaneously asking it to output a complete SVG — a
                // contradiction that causes it to spin indefinitely.
                const QString svgInstructions = existingSvg.isEmpty()
                    ? QStringLiteral(
                          "Your sole task is to create a new SVG image for this article. "
                          "Do not modify any article text, headings, paragraphs, or shortcodes. "
                          "Only produce the SVG image as described in the instructions below.\n\n")
                      + instructions
                    : QStringLiteral(
                          "Your sole task is to produce an improved version of the SVG image shown below. "
                          "Do not modify any article text, headings, paragraphs, or shortcodes — "
                          "only produce the new SVG markup.\n\n")
                      + instructions;

                // Send the full article so the model has complete context for the SVG
                // content (the SVG may summarise any part of the article).  This is
                // safe because runSvgClaudePrompt uses non-agentic mode — without
                // --dangerously-skip-permissions the model outputs SVG text directly
                // and cannot enter the coding loop that caused 30-minute runtimes.
                const QString &articleForPrompt = existingText;

                // Build prompt: article excerpt/full + current SVG (if any) + instructions.
                const QString svgPrompt =
                    (page.permalink.isEmpty()
                        ? QString{}
                        : QStringLiteral("Page permalink: ") + page.permalink + QStringLiteral("\n\n"))
                    + QStringLiteral("SVG filename to produce: ") + svgFilename + QStringLiteral("\n\n")
                    + QStringLiteral("Article context:\n\n<article>\n")
                    + articleForPrompt
                    + QStringLiteral("\n</article>\n\n")
                    + (existingSvg.isEmpty()
                        ? QString{}
                        : QStringLiteral("Current SVG (filename: ") + svgFilename
                          + QStringLiteral("):\n\n<current_svg>\n") + existingSvg
                          + QStringLiteral("\n</current_svg>\n\n"))
                    + svgInstructions
                    + QStringLiteral(
                        "\n\nRespond with ONLY the raw SVG markup, starting with <svg and ending "
                        "with </svg>. Do NOT write any code, do NOT use any tools or shell "
                        "commands, do NOT explain anything. Your entire response must be the SVG "
                        "markup and nothing else.");

                *(state->out) << QStringLiteral("  SVG prompt ready (%1 chars) for %2 — calling Claude...\n")
                                     .arg(svgPrompt.size()).arg(svgFilename);
                state->out->flush();

                QString svgResult;
                bool    svgValid        = false;
                QString reviewerFeedback;

                for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
                    if (g_updateStopRequested) {
                        break;
                    }

                    QString attemptPrompt;
                    if (attempt == 1) {
                        attemptPrompt = svgPrompt;
                    } else if (!reviewerFeedback.isEmpty()) {
                        attemptPrompt =
                            QStringLiteral(
                                "Your previous SVG was reviewed and rejected.\n"
                                "Reviewer feedback: ")
                            + reviewerFeedback
                            + QStringLiteral("\n\nPlease generate a corrected SVG.\n\n")
                            + svgPrompt;
                    } else {
                        attemptPrompt =
                            QStringLiteral(
                                "Your previous response was invalid. "
                                "It must start with <svg and end with </svg>.\n"
                                "Previous response started with: \"%1\"\n\n"
                                "Please try again.\n\n")
                            .arg(svgResult.left(200))
                            + svgPrompt;
                    }

                    const QString raw = runSvgClaudePrompt(attemptPrompt, cli, state->out);
                    if (raw.startsWith(QStringLiteral("__ERROR__"))) {
                        *(state->out) << QStringLiteral("  FAIL (attempt %1): %2\n")
                                             .arg(attempt).arg(raw);
                        state->out->flush();
                        break;
                    }

                    svgResult = raw.trimmed();
                    if (!reSvgStart.match(svgResult).hasMatch()
                            || !svgResult.endsWith(QStringLiteral("</svg>"),
                                                    Qt::CaseInsensitive)) {
                        reviewerFeedback.clear();
                        *(state->out) << QStringLiteral(
                                             "  WARN (attempt %1/%2): not a valid SVG — starts with: %3\n")
                                             .arg(attempt).arg(kMaxAttempts).arg(svgResult.left(120));
                        state->out->flush();
                        continue;
                    }

                    // Fast programmatic check: require a minimum number of non-empty
                    // <text> elements before spending an LLM call on the reviewer.
                    if (!hasSufficientSvgContent(svgResult)) {
                        reviewerFeedback = QStringLiteral(
                            "The SVG has too few text elements with actual content — "
                            "the data rows appear empty. Fill in every row with the "
                            "real names, values, and descriptions from the article.");
                        *(state->out) << QStringLiteral(
                                             "  WARN (attempt %1/%2): SVG content check failed"
                                             " — too few populated <text> elements\n")
                                             .arg(attempt).arg(kMaxAttempts);
                        state->out->flush();
                        continue;
                    }

                    // Programmatic check passed — run the LLM reviewer for deeper
                    // content accuracy and instruction compliance.
                    *(state->out) << QStringLiteral("  Reviewing SVG (attempt %1)...\n").arg(attempt);
                    state->out->flush();

                    const QString review = runSvgReview(
                        svgResult, articleForPrompt, svgInstructions, cli, state->out);

                    if (review.startsWith(QStringLiteral("OK"), Qt::CaseInsensitive)) {
                        svgValid = true;
                        break;
                    }

                    reviewerFeedback = review.startsWith(QStringLiteral("FAIL:"),
                                                          Qt::CaseInsensitive)
                        ? review.mid(5).trimmed()
                        : review.trimmed();

                    *(state->out) << QStringLiteral(
                                         "  WARN (attempt %1/%2): reviewer rejected — %3\n")
                                         .arg(attempt).arg(kMaxAttempts)
                                         .arg(reviewerFeedback.left(200));
                    state->out->flush();
                }

                if (!svgValid) {
                    allValid = false;
                    break;
                }

                // Save updated SVG blob to images.db.
                bool saved = false;
                const QString writeConn = QStringLiteral("svg_write_") + svgFilename;
                {
                    QSqlDatabase imgDb = QSqlDatabase::addDatabase(
                        QStringLiteral("QSQLITE"), writeConn);
                    imgDb.setDatabaseName(imgDbPath);
                    if (imgDb.open()) {
                        QSqlQuery q(imgDb);
                        if (imageId >= 0) {
                            q.prepare(QStringLiteral(
                                "UPDATE images SET blob = :blob WHERE id = :id"));
                            q.bindValue(QStringLiteral(":blob"), svgResult.toUtf8());
                            q.bindValue(QStringLiteral(":id"),   imageId);
                            saved = q.exec();
                        } else {
                            // New image: include both NOT NULL columns.
                            q.prepare(QStringLiteral(
                                "INSERT INTO images (blob, mime_type)"
                                " VALUES (:blob, 'image/svg+xml')"));
                            q.bindValue(QStringLiteral(":blob"), svgResult.toUtf8());
                            if (q.exec()) {
                                // Update outer imageId so the invalidation step can protect
                                // this newly created row.
                                imageId = q.lastInsertId().toInt();
                                QSqlQuery q2(imgDb);
                                // domain='' marks a canonical (non-translated) entry.
                                q2.prepare(QStringLiteral(
                                    "INSERT OR IGNORE INTO image_names (domain, filename, image_id)"
                                    " VALUES ('', :fn, :id)"));
                                q2.bindValue(QStringLiteral(":fn"), svgFilename);
                                q2.bindValue(QStringLiteral(":id"), imageId);
                                saved = q2.exec();
                            }
                        }
                        if (!saved) {
                            *(state->out) << QStringLiteral("  FAIL SVG save: %1\n")
                                                 .arg(q.lastError().text());
                            state->out->flush();
                        }
                    } else {
                        *(state->out) << QStringLiteral("  FAIL SVG save: cannot open images.db\n");
                        state->out->flush();
                    }
                }
                QSqlDatabase::removeDatabase(writeConn);

                if (!saved) {
                    allValid = false;
                    break;
                }

                // Invalidate all other image_names rows for this filename (translated
                // variants stored under different image_ids) so PageTranslator
                // re-generates them.  We protect the row we just wrote by filtering on
                // image_id != imageId — domain != '' would delete every row because
                // image_names.domain is always a real hostname, never an empty string.
                {
                    const QString invConn = QStringLiteral("svg_inv_") + svgFilename;
                    {
                        QSqlDatabase invDb = QSqlDatabase::addDatabase(
                            QStringLiteral("QSQLITE"), invConn);
                        invDb.setDatabaseName(imgDbPath);
                        if (invDb.open()) {
                            QSqlQuery qInv(invDb);
                            qInv.prepare(QStringLiteral(
                                "DELETE FROM image_names"
                                " WHERE filename = :fn AND image_id != :id"));
                            qInv.bindValue(QStringLiteral(":fn"), svgFilename);
                            qInv.bindValue(QStringLiteral(":id"), imageId);
                            qInv.exec();

                            // Register the SVG under every real hostname found in
                            // images.db so the image server and generation pipeline
                            // (which always query by real domain) can find it.
                            // Must run after the DELETE so INSERT OR IGNORE is not
                            // blocked by stale rows pointing to old image_ids.
                            {
                                QSqlQuery qDomains(invDb);
                                if (qDomains.exec(QStringLiteral(
                                        "SELECT DISTINCT domain"
                                        " FROM image_names WHERE domain != ''"))) {
                                    while (qDomains.next()) {
                                        const QString dom = qDomains.value(0).toString();
                                        QSqlQuery qReg(invDb);
                                        qReg.prepare(QStringLiteral(
                                            "INSERT OR IGNORE INTO image_names"
                                            " (domain, filename, image_id)"
                                            " VALUES (:domain, :fn, :id)"));
                                        qReg.bindValue(QStringLiteral(":domain"), dom);
                                        qReg.bindValue(QStringLiteral(":fn"),     svgFilename);
                                        qReg.bindValue(QStringLiteral(":id"),     imageId);
                                        qReg.exec();
                                    }
                                }
                            }

                            QSqlQuery qOrph(invDb);
                            qOrph.exec(QStringLiteral(
                                "DELETE FROM images"
                                " WHERE id NOT IN (SELECT image_id FROM image_names)"));
                        }
                    }
                    QSqlDatabase::removeDatabase(invConn);
                }

                *(state->out) << QStringLiteral("  SVG saved: %1 (%2 chars)\n")
                                     .arg(svgFilename).arg(svgResult.size());
                state->out->flush();
            }

            if (!allValid) {
                *(state->out) << QStringLiteral("FAIL %1: SVG update failed.\n")
                                     .arg(page.permalink);
                state->out->flush();
                continue;
            }

            // SVG changed → social media image variants are stale; reset state so
            // the second pass re-runs, and clear per-language completion flags.
            pageRepo.setGenerationState(page.id, PageGenerationState::ContentReady);
            pageRepo.invalidateTranslationImages(page.id);

            // Record the attempt without touching 1_text.
            pageRepo.recordUpdateAttempt(page.id, promptId);
            ++state->jobsCompleted;
            *(state->out) << QStringLiteral("OK: %1\n").arg(page.permalink);
            state->out->flush();
            continue;
        } else if (isTwoCall) {
            // --- Call 1: analysis ---
            QString call1Prompt;
            if (!page.permalink.isEmpty()) {
                call1Prompt += QStringLiteral("Page permalink: ") + page.permalink
                             + QStringLiteral("\n\n");
            }
            call1Prompt += QStringLiteral(
                    "Here is the current version of a web article:\n\n"
                    "<article>\n")
                + existingText
                + QStringLiteral("\n</article>\n\n");
            if (!aiKeyClue.isEmpty()) {
                call1Prompt += QStringLiteral("Vocabulary hint: ") + aiKeyClue + QStringLiteral("\n\n");
            }
            call1Prompt += instructions;

            *(state->out) << QStringLiteral("  Call 1 (analysis, %1 chars) — calling Claude...\n")
                                 .arg(call1Prompt.size());
            state->out->flush();

            const QString analysis = runUpdateClaudePrompt(call1Prompt, cli, state->out);

            if (analysis.startsWith(QStringLiteral("__ERROR__"))) {
                *(state->out) << QStringLiteral("  FAIL call 1: %1\n").arg(analysis);
                state->out->flush();
                continue;
            }

            // --- Call 2: format (up to kMaxAttempts) ---
            QString call2Base =
                QStringLiteral("Analysis result:\n") + analysis + QStringLiteral("\n\n");
            if (!aiKeyClue.isEmpty()) {
                call2Base += QStringLiteral("Vocabulary hint: ") + aiKeyClue + QStringLiteral("\n\n");
            }
            call2Base += formatPrompt;

            for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
                if (g_updateStopRequested) {
                    break;
                }

                const QString attemptPrompt = (attempt == 1)
                    ? call2Base
                    : QStringLiteral(
                          "Your previous response was invalid: \"%1\"\n\n"
                          "Please try again.\n\n")
                          .arg(result.left(200))
                      + call2Base;

                *(state->out) << QStringLiteral("  Call 2 attempt %1/%2 — calling Claude...\n")
                                     .arg(attempt).arg(kMaxAttempts);
                state->out->flush();

                const QString raw = runUpdateClaudePrompt(attemptPrompt, cli, state->out);

                if (raw.startsWith(QStringLiteral("__ERROR__"))) {
                    *(state->out) << QStringLiteral("  FAIL call 2 (attempt %1): %2\n")
                                         .arg(attempt).arg(raw);
                    state->out->flush();
                    break;
                }

                result = raw;

                bool formatOk = false;
                if (validator == AbstractPageBloc::AiUpdateSpec::Validator::CommaSeparatedInts) {
                    formatOk = isCommaSeparatedIntsValid(result);
                } else {
                    formatOk = !result.trimmed().isEmpty();
                }

                if (formatOk) {
                    valid = true;
                    break;
                }

                *(state->out) << QStringLiteral("  WARN call 2 (attempt %1/%2): invalid — got: %3\n")
                                     .arg(attempt).arg(kMaxAttempts).arg(result.left(120));
                state->out->flush();
            }
        } else {
            // --- Single call: article rewrite ---
            const QString singlePrompt =
                QStringLiteral(
                    "Here is the current version of a web article:\n\n"
                    "<article>\n")
                + existingText
                + QStringLiteral(
                    "\n</article>\n\n"
                    "Please update the article according to these instructions:\n")
                + instructions
                + QStringLiteral(
                    "\n\n"
                    "Output ONLY the complete updated article, starting with "
                    "[TITLE level=\"1\"] on the very first line. "
                    "Do not include any explanation, preamble, or commentary — "
                    "only the article itself.");

            *(state->out) << QStringLiteral("  Prompt ready (%1 chars) — calling Claude...\n")
                                 .arg(singlePrompt.size());
            state->out->flush();

            for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
                if (g_updateStopRequested) {
                    break;
                }

                const QString attemptPrompt = (attempt == 1)
                    ? singlePrompt
                    : QStringLiteral(
                          "Your previous response was invalid.\n"
                          "It started with: \"%1\"\n\n"
                          "A valid article must start with [TITLE level=\"1\"] "
                          "on the very first line.\n\n"
                          "Please write the COMPLETE updated article from the beginning.\n\n")
                          .arg(result.left(200))
                      + singlePrompt;

                const QString raw = runUpdateClaudePrompt(attemptPrompt, cli, state->out);

                if (raw.startsWith(QStringLiteral("__ERROR__"))) {
                    *(state->out) << QStringLiteral("  FAIL (attempt %1): %2\n")
                                         .arg(attempt).arg(raw);
                    state->out->flush();
                    break;
                }

                result = raw;
                if (isUpdatedArticleValid(result)) {
                    valid = true;
                    break;
                }

                *(state->out) << QStringLiteral("  WARN (attempt %1/%2): invalid — starts with: %3\n")
                                     .arg(attempt).arg(kMaxAttempts).arg(result.left(120));
                state->out->flush();
            }
        }

        if (!valid) {
            *(state->out) << QStringLiteral("FAIL %1: all %2 attempts invalid, skipping.\n")
                                 .arg(page.permalink).arg(kMaxAttempts);
            state->out->flush();
            continue;
        }

        data.insert(effectiveSaveKey, result);
        pageRepo.saveData(page.id, data);
        pageRepo.recordUpdateAttempt(page.id, promptId);
        ++state->jobsCompleted;

        *(state->out) << QStringLiteral("OK: %1\n").arg(page.permalink);
        state->out->flush();
    }

    if (g_updateStopRequested) {
        *(state->out) << QStringLiteral("Stop requested — update session finished.\n");
    }

    *(state->out) << QStringLiteral("Update complete. %1 page(s) updated.\n")
                         .arg(state->jobsCompleted);
    state->out->flush();
}

// ---------------------------------------------------------------------------
// LauncherUpdate
// ---------------------------------------------------------------------------

QString LauncherUpdate::getOptionName() const { return OPTION_NAME; }
bool    LauncherUpdate::isFlag()        const { return true; }

void LauncherUpdate::run(const QString & /*value*/)
{
    auto *out = new QTextStream(stdout);

    const QStringList args = QCoreApplication::arguments();

    QString      strategyFilter;
    QString      promptFilter;
    int          jobsLimit = -1;
    QSet<int>    pageIdsFilter;
    AbstractCli *cli       = nullptr;

    for (int i = 0; i < args.size() - 1; ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--") + OPTION_STRATEGY) {
            strategyFilter = args.at(i + 1);
        } else if (arg == QStringLiteral("--") + OPTION_PROMPT) {
            promptFilter = args.at(i + 1);
        } else if (arg == QStringLiteral("--") + OPTION_LIMIT) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1) {
                jobsLimit = n;
            }
        } else if (arg == QStringLiteral("--") + OPTION_PAGES) {
            const QStringList parts = args.at(i + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : std::as_const(parts)) {
                bool ok = false;
                const int id = p.trimmed().toInt(&ok);
                if (ok) {
                    pageIdsFilter.insert(id);
                }
            }
        } else if (arg == QStringLiteral("--") + AbstractLauncher::OPTION_CLI) {
            const QString name = args.at(i + 1);
            for (AbstractCli *c : AbstractCli::ALL_CLIS()) {
                if (c->getName() == name) {
                    cli = c;
                    break;
                }
            }
        }
    }

    if (!cli && !AbstractCli::ALL_CLIS().isEmpty()) {
        cli = AbstractCli::ALL_CLIS().first();
    }
    if (!cli) {
        *out << QStringLiteral("ERROR: no AI CLI registered.\n");
        out->flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit,
                                  Qt::QueuedConnection);
        return;
    }

    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    UpdateStrategyTree tree(workingDir);

    const QList<UpdateStrategyTree::StrategyInfo> allStrategies = tree.strategies();
    if (allStrategies.isEmpty()) {
        *out << QStringLiteral("No update strategies defined.\n");
        out->flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                              &QCoreApplication::quit,
                              Qt::QueuedConnection);
        return;
    }

    for (const auto &strat : std::as_const(allStrategies)) {
        if (!strategyFilter.isEmpty() && strat.id != strategyFilter) {
            continue;
        }
        for (const auto &prompt : std::as_const(strat.prompts)) {
            if (!promptFilter.isEmpty() && prompt.id != promptFilter) {
                continue;
            }

            *out << QStringLiteral("Strategy '%1', prompt '%2' — page type: %3\n")
                        .arg(strat.name, prompt.name, strat.pageTypeId);
            out->flush();

            std::signal(SIGINT,  handleUpdateSignal);
            std::signal(SIGTERM, handleUpdateSignal);

            UpdateRunState state;
            state.jobsLimit = jobsLimit;
            state.out       = out;

            runUpdateSession(strat.pageTypeId, prompt, &state, cli, pageIdsFilter);
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      &QCoreApplication::quit,
                                      Qt::QueuedConnection);
            return;
        }
    }

    *out << QStringLiteral("Nothing to update — no matching strategy/prompt found.\n");
    out->flush();
    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              &QCoreApplication::quit,
                              Qt::QueuedConnection);
}
