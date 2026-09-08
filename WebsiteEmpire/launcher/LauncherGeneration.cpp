#include "LauncherGeneration.h"

#include "aspire/attributes/AbstractPageAttributes.h"
#include "gui/panes/GenStrategyTable.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/ImageWriter.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/GenPageQueue.h"
#include "website/pages/GenScheduler.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/ArticleContentValidator.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/PageFlag.h"
#include "website/pages/blocs/AbstractSecondaryPageBloc.h"
#include "website/perf/GscDataSource.h"
#include "website/perf/GscSettings.h"
#include "website/perf/StatsDbDataSource.h"
#include "website/social/AbstractSocialMedia.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <csignal>
#include <atomic>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTextStream>

#include <QCoro/QCoroProcess>
#include <QCoro/QCoroTask>
#include <QCoro/QCoroTimer>

#include "aicli/AbstractCli.h"

const QString LauncherGeneration::OPTION_NAME     = QStringLiteral("generation");
const QString LauncherGeneration::OPTION_SESSIONS = QStringLiteral("sessions");
const QString LauncherGeneration::OPTION_LIMIT    = QStringLiteral("limit");
const QString LauncherGeneration::OPTION_STRATEGY = QStringLiteral("strategy");
const QString LauncherGeneration::OPTION_TOPIC    = QStringLiteral("topic");
const QString LauncherGeneration::OPTION_NEW_ONLY   = QStringLiteral("new-only");
const QString LauncherGeneration::OPTION_RETRY_ONLY = QStringLiteral("retry-only");
const QString LauncherGeneration::OPTION_DELAY      = QStringLiteral("delay");

DECLARE_LAUNCHER(LauncherGeneration)

// ---------------------------------------------------------------------------
// Signal handling — graceful stop after current page(s) finish
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_stopRequested = 0;

static void handleSignal(int)
{
    g_stopRequested = 1;
}

// ---------------------------------------------------------------------------
// Shared counters (heap-allocated, outlive run()'s stack frame)
// ---------------------------------------------------------------------------

struct GenRunState {
    std::atomic<int> jobsCompleted{0};
    int              jobsLimit        = -1; // -1 = unlimited
    int              activeCount      = 0;
    int              interPageDelayMs = 0;  // pause between pages to avoid rate limits
    QTextStream     *out              = nullptr;
};

// ---------------------------------------------------------------------------
// Article pre-validation — delegates to ArticleContentValidator, shared with
// LauncherUpdate so both launchers reject the same malformed content
// (missing/short title, malformed shortcodes, raw Markdown artifacts).
// ---------------------------------------------------------------------------

static bool isArticleValid(const QString &text)
{
    return ArticleContentValidator::isValid(text);
}

// ---------------------------------------------------------------------------
// AI CLI helper — spawns the selected CLI process for content generation.
// No API key needed; the CLI handles authentication itself.
// ---------------------------------------------------------------------------

static QCoro::Task<QString> runClaudePrompt(QString prompt, AbstractCli *cli)
{
    // result declared first — single co_return at end, mirrors ClaudeRunner.
    QString result;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        co_return result;
    }

    // Write the prompt into a subdirectory so the process working directory
    // stays clean — Claude Code auto-loads files from cwd, which would cause
    // it to read prompt.txt as a file in context and produce a preamble like
    // "The file looks good. Let me output the full article content now:".
    const QString promptSubdir = tempDir.path() + QStringLiteral("/prompt");
    QDir().mkdir(promptSubdir);
    const QString promptPath = promptSubdir + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            co_return result;
        }
        f.write(prompt.toUtf8());
    }

    // CLI response is redirected to a file instead of a pipe.
    // For large articles (50 000+ chars), the OS pipe buffer (≈64 KB) fills up
    // before QProcess can drain it, causing the beginning of the output to be
    // silently dropped.  Writing to a file bypasses this limitation entirely.
    const QString outputPath = tempDir.path() + QStringLiteral("/output.txt");

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(cli->getExecutable());
    cli->configurePromptProcess(&process, cli->promptArgs(), prompt, promptPath);
    process.setStandardOutputFile(outputPath);
    // SVG generation can produce large XML outputs.  Raise the Claude Code output
    // token limit so SVG responses are never truncated by the default 32 k cap.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("CLAUDE_CODE_MAX_OUTPUT_TOKENS"), QStringLiteral("100000"));
    process.setProcessEnvironment(env);

    // Exactly two co_awaits, nothing between them — mirrors ClaudeRunner.
    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    // All error checks and output reading happen after both co_awaits.
    if (process.error() == QProcess::FailedToStart) {
        result = QStringLiteral("__ERROR__: %1 executable not found in PATH")
                     .arg(cli->getExecutable());
    } else if (process.exitCode() != 0) {
        const QString errMsg = QString::fromUtf8(process.readAllStandardError()).trimmed();
        // Some CLIs (e.g. Claude with --output-format stream-json) write errors to stdout.
        // Fall back to the output file content so rate-limit / API errors are visible.
        QString detail = errMsg;
        if (detail.isEmpty()) {
            QFile f(outputPath);
            if (f.open(QIODevice::ReadOnly)) {
                detail = QString::fromUtf8(f.readAll()).trimmed().left(300);
            }
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("exit code %1").arg(process.exitCode());
        }
        result = QStringLiteral("__ERROR__: ") + detail;
    } else {
        QFile f(outputPath);
        if (f.open(QIODevice::ReadOnly)) {
            result = QString::fromUtf8(f.readAll()).trimmed();
        }
        // Fallback: Claude sometimes uses its Write tool to save the SVG to a file
        // in the working directory instead of printing it to stdout.  If stdout has
        // no <svg element, scan the temp dir for any .svg file Claude may have created
        // and return its content before QTemporaryDir deletes everything.
        if (!result.contains(QStringLiteral("<svg"), Qt::CaseInsensitive)) {
            const QStringList svgFiles = QDir(tempDir.path()).entryList(
                QStringList() << QStringLiteral("*.svg"), QDir::Files);
            if (!svgFiles.isEmpty()) {
                QFile svgF(tempDir.path() + QLatin1Char('/') + svgFiles.first());
                if (svgF.open(QIODevice::ReadOnly)) {
                    result = QString::fromUtf8(svgF.readAll()).trimmed();
                }
            }
        }
    }
    co_return result;
}

// ---------------------------------------------------------------------------
// Policy-violation retry wrapper
//
// When the CLI rejects a prompt with a Usage Policy error, the sensitive
// "Image description" line is replaced in-code with a generic fallback and
// the prompt is resubmitted once.  No second CLI call is used for reformulation
// because any request that embeds the original prompt text would carry the same
// sensitive terms and be rejected again.
// ---------------------------------------------------------------------------

static bool isUsagePolicyError(const QString &response)
{
    return response.startsWith(QStringLiteral("__ERROR__"))
        && response.contains(QStringLiteral("Usage Policy"));
}

// Strips the Image description line (which may contain a flagged disease name)
// and replaces it with a neutral clinical fallback.  Returns an empty string if
// no Image description line was found (caller should give up rather than retry).
static QString sanitizeDescriptionLine(const QString &prompt)
{
    static const QRegularExpression reDesc(
        QStringLiteral("^(Image description\\s*:).*$"),
        QRegularExpression::MultilineOption);
    const auto m = reDesc.match(prompt);
    if (!m.hasMatch()) {
        return {};
    }
    QString sanitized = prompt;
    sanitized.replace(reDesc,
        QStringLiteral("\\1 medical biomarker and genetic factors diagram"));
    return sanitized;
}

// Runs prompt via CLI; on a Usage Policy rejection, sanitizes the Image
// description line and retries once without any extra round-trip to the CLI.
static QCoro::Task<QString> runWithPolicyRetry(const QString &prompt,
                                               AbstractCli   *cli,
                                               QTextStream   *out,
                                               int            sNum)
{
    QString result = co_await runClaudePrompt(prompt, cli);
    if (!isUsagePolicyError(result)) {
        co_return result;
    }

    *out << QStringLiteral("[S%1] Policy violation — sanitizing description and retrying...\n").arg(sNum);
    out->flush();

    const QString sanitized = sanitizeDescriptionLine(prompt);
    if (sanitized.isEmpty()) {
        *out << QStringLiteral("[S%1] WARN: no Image description line found, cannot sanitize.\n").arg(sNum);
        out->flush();
        co_return result;
    }

    result = co_await runClaudePrompt(sanitized, cli);
    if (isUsagePolicyError(result)) {
        *out << QStringLiteral("[S%1] WARN: still rejected after sanitization: %2\n").arg(sNum).arg(result);
        out->flush();
    }
    co_return result;
}

// ---------------------------------------------------------------------------
// Raster (non-SVG) image generation — bounded-timeout generate/review/retry.
//
// Unlike runClaudePrompt() above (used for content and SVG generation, where
// waitForFinished(-1) never times out), every CLI call here is bounded and
// kills the process on timeout.  A page can request 8-12 raster images, each
// needing up to kRasterMaxAttempts x 2 CLI calls (generate + review) — an
// unbounded wait on any single one of those calls would hang the whole
// session, and the risk compounds with image count.  This is the direct fix
// for "asking for many images leads to job not finished."
// ---------------------------------------------------------------------------

static constexpr int kRasterImageTimeoutMs = 5 * 60 * 1000; // 5 min per CLI call
static constexpr int kRasterMaxAttempts    = 3;

enum class CliCallOutcome { Ok, QuotaOrAuth, OtherError };

// Pure classification of a runBoundedClaudePrompt() result — no I/O, no
// process, unit-testable in isolation.  A quota/auth hit is not a content
// failure: the caller must pause rather than burn a retry attempt on it, so
// that "usage limit reached" leaves the image queue untouched for the next run.
static CliCallOutcome classifyCliResponse(const QString &response, AbstractCli *cli)
{
    if (!response.startsWith(QStringLiteral("__ERROR__"))) {
        return CliCallOutcome::Ok;
    }
    const CliErrorKind kind = cli->classifyError(response);
    if (kind == CliErrorKind::QuotaExceeded || kind == CliErrorKind::AuthRequired) {
        return CliCallOutcome::QuotaOrAuth;
    }
    return CliCallOutcome::OtherError;
}

// Spawns cli with preparedPrompt (caller decides whether to route through
// cli->preparePrompt() or supply custom instructions), bounded by timeoutMs.
// Returns raw stdout, or "__ERROR__: <detail>" on failure/timeout.  Unlike
// runClaudePrompt(), this kills the process when it does not finish in time
// instead of waiting forever.
static QCoro::Task<QString> runBoundedClaudePrompt(QString      preparedPrompt,
                                                    AbstractCli *cli,
                                                    int          timeoutMs,
                                                    QString      workDir)
{
    QString result;
    QDir().mkpath(workDir);
    const QString promptSubdir = workDir + QStringLiteral("/prompt");
    QDir().mkdir(promptSubdir);
    const QString promptPath = promptSubdir + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            co_return QStringLiteral("__ERROR__: cannot write prompt file");
        }
        f.write(preparedPrompt.toUtf8());
    }
    const QString outputPath = workDir + QStringLiteral("/output.txt");

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProgram(cli->getExecutable());
    cli->configurePromptProcess(&process, cli->promptArgs(), preparedPrompt, promptPath);
    process.setStandardOutputFile(outputPath);

    co_await qCoro(process).start();
    const bool finished = co_await qCoro(process).waitForFinished(timeoutMs);
    if (!finished && process.state() != QProcess::NotRunning) {
        // Still running past the deadline — kill it. A stuck call must never
        // block the rest of the page/session (see kRasterImageTimeoutMs doc above).
        process.kill();
        co_await qCoro(process).waitForFinished(5000);
        co_return QStringLiteral("__ERROR__: timed out after %1s").arg(timeoutMs / 1000);
    }

    if (process.error() == QProcess::FailedToStart) {
        co_return QStringLiteral("__ERROR__: %1 executable not found in PATH")
                     .arg(cli->getExecutable());
    }
    if (process.exitCode() != 0) {
        const QString errMsg = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QString detail = errMsg;
        if (detail.isEmpty()) {
            QFile f(outputPath);
            if (f.open(QIODevice::ReadOnly)) {
                detail = QString::fromUtf8(f.readAll()).trimmed().left(300);
            }
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("exit code %1").arg(process.exitCode());
        }
        co_return QStringLiteral("__ERROR__: ") + detail;
    }

    QFile f(outputPath);
    if (f.open(QIODevice::ReadOnly)) {
        result = QString::fromUtf8(f.readAll()).trimmed();
    }
    co_return result;
}

// Builds the prompt asking a CLI to open/inspect a just-generated image file
// and grade it OK / FAIL — the raster-image equivalent of LauncherUpdate's
// runSvgReview().  Deliberately NOT routed through cli->preparePrompt(): that
// function's branches are tuned for "write a new file" (raster generation) vs
// "pure text output" (article/SVG text), neither of which fits "read an
// existing file and describe it" — the instructions here are explicit and
// self-contained instead.
static QString buildRasterImageReviewPrompt(const QString &imagePath,
                                             const QString &description,
                                             const QString &styleInstructions,
                                             const QString &articleSection)
{
    QString prompt = QStringLiteral(
        "You are reviewing an AI-generated image for a web article.\n\n"
        "Open and inspect the image file at this exact path using your file-reading "
        "or vision tool: %1\n\n"
        "The image should show: %2\n")
        .arg(imagePath, description);
    if (!articleSection.isEmpty()) {
        prompt += QStringLiteral(
            "\nThe article section this image illustrates (the image must match the "
            "SPECIFIC recommendation below — colors, garments, pairing — not just the "
            "general topic):\n---\n%1\n---\n").arg(articleSection);
    }
    if (!styleInstructions.isEmpty()) {
        prompt += QStringLiteral("\nStyle requirements the image must follow:\n%1\n")
                     .arg(styleInstructions);
    }
    prompt += QStringLiteral(
        "\nTo respond OK, ALL of the following must be true:\n"
        "1. The image file opens correctly and is not blank, corrupted, or a placeholder.\n"
        "2. The subject described above is clearly visible and correctly depicted.\n"
        "3. When an article section is given, the image matches that section's specific "
        "recommendation (e.g. the exact colors/garments named) — not a generic or "
        "mismatched substitute.\n"
        "4. The image follows the style requirements above (when given).\n"
        "5. The image contains no text, watermark, or logo.\n\n"
        "Respond with ONLY one of:\n"
        "- \"OK\" — if all conditions above are met.\n"
        "- \"FAIL: <reason>\" — if any condition is not met. Be specific.\n"
        "Do not write anything else.");
    return prompt;
}

enum class RasterAttemptResult { Success, RetryableFailure, QuotaOrAuthPause };

// One full generate-then-review cycle for a single raster image ref.
// tempDir must outlive the returned QImage's use — the caller loads the file
// on success and stores it via ImageWriter before this function's own
// temporary directory (owned by the caller) is destroyed.
static QCoro::Task<RasterAttemptResult> runOneRasterAttempt(
    const GenPageQueue::ImgFixRef &ref,
    const QString                 &articleText,
    const QString                 &imageInstructions,
    const QString                 &lang,
    const QString                 &domain,
    const QString                 &reviewFeedback, // empty on the first attempt
    GenPageQueue                  *queue,
    AbstractCli                   *cli,
    ImageWriter                   &imageWriter,
    QTextStream                   *out,
    int                             sNum,
    QString                        *lastError)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        *lastError = QStringLiteral("could not create temp dir");
        co_return RasterAttemptResult::RetryableFailure;
    }

    // The AI is expected to have given a non-.svg fileName already (callers
    // filter .svg refs out before reaching this function) — normalize just in
    // case so the output path always has an extension QImage::load() can sniff.
    QString ext = QFileInfo(ref.fileName).suffix().toLower();
    static const QSet<QString> validExts = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                            QStringLiteral("png"), QStringLiteral("webp")};
    if (!validExts.contains(ext)) {
        ext = QStringLiteral("png");
    }
    const QString outputPath = tempDir.path() + QStringLiteral("/output.") + ext;

    QString prompt = queue->buildRasterImagePrompt(ref, articleText, lang, outputPath);
    if (!reviewFeedback.isEmpty()) {
        prompt += QStringLiteral(
            "\n\nA previous attempt at this image failed review for this reason: %1\n"
            "Fix this specific issue in the new image.").arg(reviewFeedback);
    }
    const QString prepared = cli->preparePrompt(prompt);

    *out << QStringLiteral("[S%1] Generating raster image: %2...\n").arg(sNum).arg(ref.fileName);
    out->flush();

    const QString genResponse = co_await runBoundedClaudePrompt(
        prepared, cli, kRasterImageTimeoutMs, tempDir.path() + QStringLiteral("/gen"));

    if (classifyCliResponse(genResponse, cli) == CliCallOutcome::QuotaOrAuth) {
        *lastError = genResponse;
        co_return RasterAttemptResult::QuotaOrAuthPause;
    }
    if (genResponse.startsWith(QStringLiteral("__ERROR__"))) {
        *lastError = genResponse;
        *out << QStringLiteral("[S%1] WARN (raster gen): %2 — %3\n")
                    .arg(sNum).arg(ref.fileName, genResponse);
        out->flush();
        co_return RasterAttemptResult::RetryableFailure;
    }

    QImage image(outputPath);
    if (image.isNull()) {
        *lastError = QStringLiteral("no valid image file produced at the expected path");
        *out << QStringLiteral("[S%1] WARN (raster gen): %2 — %3\n")
                    .arg(sNum).arg(ref.fileName, *lastError);
        out->flush();
        co_return RasterAttemptResult::RetryableFailure;
    }

    *out << QStringLiteral("[S%1] Reviewing raster image: %2...\n").arg(sNum).arg(ref.fileName);
    out->flush();
    const QString reviewPrompt =
        buildRasterImageReviewPrompt(outputPath, ref.alt, imageInstructions,
                                      GenPageQueue::extractRelevantSection(articleText, ref.id));
    const QString reviewResponse = co_await runBoundedClaudePrompt(
        reviewPrompt, cli, kRasterImageTimeoutMs, tempDir.path() + QStringLiteral("/review"));

    if (classifyCliResponse(reviewResponse, cli) == CliCallOutcome::QuotaOrAuth) {
        *lastError = reviewResponse;
        co_return RasterAttemptResult::QuotaOrAuthPause;
    }
    if (reviewResponse.startsWith(QStringLiteral("__ERROR__"))
        || !reviewResponse.startsWith(QStringLiteral("OK"), Qt::CaseInsensitive)) {
        *lastError = reviewResponse.startsWith(QStringLiteral("FAIL:"), Qt::CaseInsensitive)
                        ? reviewResponse.mid(5).trimmed()
                        : reviewResponse;
        *out << QStringLiteral("[S%1] WARN (raster review): %2 — %3\n")
                    .arg(sNum).arg(ref.fileName, *lastError);
        out->flush();
        co_return RasterAttemptResult::RetryableFailure;
    }

    // image was loaded from a file inside tempDir, which is still alive here
    // (destructs when this function returns) — safe to hand to ImageWriter now.
    imageWriter.writeQImage(image, domain, ref.fileName);
    *out << QStringLiteral("[S%1] Raster image saved: %2\n").arg(sNum).arg(ref.fileName);
    out->flush();
    co_return RasterAttemptResult::Success;
}

// Drives up to kRasterMaxAttempts attempts for one image ref, recording the
// outcome via pageRepo after every attempt.  This durability is what makes
// generation resumable: a crash mid-loop leaves the last recorded attempt
// count/status intact in page_raster_images, so the next --generation run
// continues from there instead of losing progress, repeating forever, or
// (via PageGenerator's publish-time gate) ever letting a page with unresolved
// images go live.
//
// Returns false when a CLI usage-limit/auth error means the WHOLE session
// should stop attempting further images/pages now — the caller must check
// this and break out, exactly like the existing g_stopRequested convention.
static QCoro::Task<bool> runRasterImageGeneration(
    const GenPageQueue::ImgFixRef &ref,
    const QString                 &articleText,
    const QString                 &imageInstructions,
    const QString                 &lang,
    const QString                 &domain,
    GenPageQueue                  *queue,
    AbstractCli                   *cli,
    ImageWriter                   &imageWriter,
    IPageRepository               &pageRepo,
    int                             pageId,
    QTextStream                   *out,
    int                             sNum)
{
    pageRepo.ensureRasterImagePending(pageId, ref.id, ref.fileName);

    if (pageRepo.rasterImageStatus(pageId, ref.id) != RasterImageStatus::Pending) {
        co_return true; // already resolved by a previous run
    }

    QString reviewFeedback;
    for (int attempt = pageRepo.rasterImageAttempts(pageId, ref.id) + 1;
         attempt <= kRasterMaxAttempts; ++attempt) {
        if (g_stopRequested) {
            co_return true; // external stop request, not a quota/auth pause
        }

        QString lastError;
        const RasterAttemptResult outcome = co_await runOneRasterAttempt(
            ref, articleText, imageInstructions, lang, domain, reviewFeedback,
            queue, cli, imageWriter, out, sNum, &lastError);

        if (outcome == RasterAttemptResult::QuotaOrAuthPause) {
            *out << QStringLiteral(
                "[S%1] Usage limit hit or auth required (%2) — pausing raster "
                "image generation for %3; will resume on next run.\n")
                .arg(sNum).arg(lastError, ref.fileName);
            out->flush();
            // Not recorded as a failed attempt: status stays Pending, attempts
            // unchanged, so the next run retries this exact image without
            // burning one of its kRasterMaxAttempts on a transient condition.
            co_return false;
        }

        if (outcome == RasterAttemptResult::Success) {
            pageRepo.recordRasterImageAttempt(pageId, ref.id,
                                              RasterImageStatus::Success, QString());
            co_return true;
        }

        reviewFeedback = lastError;
        const bool exhausted = (attempt >= kRasterMaxAttempts);
        pageRepo.recordRasterImageAttempt(
            pageId, ref.id,
            exhausted ? RasterImageStatus::FailedFinal : RasterImageStatus::Pending,
            lastError);
        if (exhausted) {
            *out << QStringLiteral("[S%1] Raster image FAILED after %2 attempts: %3 — %4\n")
                        .arg(sNum).arg(kRasterMaxAttempts).arg(ref.fileName, lastError);
            out->flush();
        }
    }
    co_return true;
}

// ---------------------------------------------------------------------------
// Per-session coroutine
// ---------------------------------------------------------------------------

static QCoro::Task<void> runGenerationSession(GenPageQueue   *queue,
                                               QString         strategyId,
                                               QString         endPermalink,
                                               AbstractEngine *engine,
                                               int             websiteIndex,
                                               CategoryTable  &categoryTable,
                                               GenRunState    *state,
                                               int             sessionIndex,
                                               AbstractCli    *cli)
{
    int sNum = sessionIndex + 1;
    // PageRepositoryDb holds a PageDb& (reference member) so both types have deleted
    // move/default constructors.  Declaring them here — before any co_await — means
    // GCC knows they are always initialised in the frame and never needs to generate
    // a default-constructor call for them.
    QDir             workDir = WorkingDirectoryManager::instance()->workingDir();
    PageDb           pageDb(workDir);
    PageRepositoryDb pageRepo(pageDb);
    ImageWriter      imageWriter(workDir);

    // Set when a raster-image CLI call hits a quota/auth error — stops this
    // session from attempting further pages too, not just the current page's
    // remaining images, since the same CLI/account will likely fail again
    // immediately.  See runRasterImageGeneration()'s return value.
    bool sessionRasterPaused = false;

    while (!g_stopRequested && !sessionRasterPaused && queue->hasNext()) {
        // Honour --limit if set.
        if (state->jobsLimit >= 0
            && state->jobsCompleted.load() >= state->jobsLimit) {
            break;
        }

        PageRecord page = queue->peekNext();
        queue->advance();

        *(state->out) << QStringLiteral("[S%1] Generating page %2 (%3, %4 remaining)\n")
                             .arg(sNum)
                             .arg(page.permalink)
                             .arg(page.typeId)
                             .arg(queue->pendingCount());
        state->out->flush();

        // ---- Smart retry: MainImageReady pages with SocialMedia flag ---------
        // These pages already have saved content and their SVG is ready.  The
        // SocialMedia flag was set by --review or the UI, requesting the second
        // pass.  Skip content regeneration and run only the social-media second
        // pass.
        if (page.id != 0) {
            const int     pageId = page.id;
            const QString domain = engine->data(
                engine->index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();
            const QString lang   = engine->getLangCode(websiteIndex);

            *(state->out) << QStringLiteral("[S%1] Smart retry (second-pass only): %2\n")
                                 .arg(sNum).arg(page.permalink);
            state->out->flush();

            // Load existing article text to find the SVG filename.
            const QHash<QString, QString> retryData = pageRepo.loadData(pageId);
            const QString retryText = retryData.value(QStringLiteral("1_text"));
            if (retryText.isEmpty()) {
                *(state->out) << QStringLiteral("[S%1] SKIP retry (no 1_text): %2\n")
                                     .arg(sNum).arg(page.permalink);
                state->out->flush();
                continue;
            }

            // Try to load the existing main SVG from images.db.
            QString retrySvg;
            const QList<GenPageQueue::ImgFixRef> retryRefs =
                GenPageQueue::parseImgFixRefs(retryText);
            for (const auto &ref : std::as_const(retryRefs)) {
                if (!ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                    continue;
                }
                const QByteArray svgBlob = imageWriter.readSvg(domain, ref.fileName);
                if (!svgBlob.isEmpty()) {
                    retrySvg = QString::fromUtf8(svgBlob);
                    *(state->out) << QStringLiteral("[S%1] Loaded existing SVG: %2\n")
                                         .arg(sNum).arg(ref.fileName);
                    state->out->flush();
                    break;
                }
            }

            // SVG not in images.db — regenerate it from the existing text.
            if (retrySvg.isEmpty() && !g_stopRequested) {
                *(state->out) << QStringLiteral(
                    "[S%1] SVG not in images.db — regenerating from existing text...\n")
                    .arg(sNum);
                state->out->flush();
                for (const auto &ref : std::as_const(retryRefs)) {
                    if (g_stopRequested) { break; }
                    if (!ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                        continue;
                    }
                    *(state->out) << QStringLiteral("[S%1] SVG regen: %2 (desc: %3)\n")
                                         .arg(sNum).arg(ref.fileName, ref.alt.left(80));
                    state->out->flush();
                    const QString svgPrompt = queue->buildSvgPrompt(ref, retryText, lang);
                    const QString svgResp   = co_await runWithPolicyRetry(svgPrompt, cli, state->out, sNum);
                    if (svgResp.startsWith(QStringLiteral("__ERROR__"))) {
                        *(state->out) << QStringLiteral("[S%1] WARN (SVG regen): %2\n")
                                             .arg(sNum).arg(svgResp);
                        state->out->flush();
                        continue;
                    }
                    const auto svgResultR = GenPageQueue::parseSvgDelimitedResponse(svgResp);
                    if (svgResultR.svgCode.isEmpty()
                        || !svgResultR.svgCode.contains(
                               QStringLiteral("</svg>"), Qt::CaseInsensitive)) {
                        *(state->out) << QStringLiteral("[S%1] WARN (SVG regen): no valid SVG for %2\n")
                                             .arg(sNum).arg(ref.fileName);
                        state->out->flush();
                        continue;
                    }
                    const QString &svgContent = svgResultR.svgCode;
                    imageWriter.writeSvg(svgContent.toUtf8(), domain, ref.fileName);
                    retrySvg = svgContent;
                    *(state->out) << QStringLiteral("[S%1] SVG regenerated: %2\n")
                                         .arg(sNum).arg(ref.fileName);
                    state->out->flush();
                    break;
                }
            }

            // ---- Raster image repair --------------------------------------
            // The retry queue's main job for a raster strategy: an article held
            // at ContentReady because one or more of its images is still
            // Pending or went FailedFinal.  Re-runs the per-image
            // generate/review loop against the STORED article text, so the
            // prose and every already-good image are untouched and only the
            // broken ones are redone.
            //
            // resetFailedRasterImages() must come first: runRasterImageGeneration()
            // returns early for any row whose status is not Pending, so without
            // the reset a FailedFinal row is skipped and the page would cycle
            // through the retry queue forever making no progress.  One fresh
            // budget per explicit retry run — the reset deliberately does NOT
            // live inside the generate loop, which would spin without end on an
            // image the AI genuinely cannot produce.
            bool rasterRepairOk    = true;
            bool retryRasterPaused = false;
            if (queue->wantsRasterImage() && !pageRepo.allRasterImagesSuccess(pageId)) {
                if (!cli->canGenImages()) {
                    // Not a failure of this page: a capable CLI simply never
                    // looked at it.  Never fabricate a failed attempt here.
                    *(state->out) << QStringLiteral(
                        "[S%1] WARN: %2 — images need repair but %3 cannot generate "
                        "images; stays ContentReady for a capable CLI\n")
                        .arg(sNum).arg(page.permalink, cli->getName());
                    state->out->flush();
                    rasterRepairOk = false;
                } else {
                    const int nReset = pageRepo.resetFailedRasterImages(pageId);
                    *(state->out) << QStringLiteral(
                        "[S%1] Repairing raster images: %2 (%3 previously failed)\n")
                        .arg(sNum).arg(page.permalink).arg(nReset);
                    state->out->flush();

                    for (const auto &ref : std::as_const(retryRefs)) {
                        if (g_stopRequested || retryRasterPaused) {
                            break;
                        }
                        if (ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                            continue;
                        }
                        // Never regenerate an image that already succeeded —
                        // it would burn a CLI call and could replace a good
                        // image with a worse one.
                        if (pageRepo.rasterImageStatus(pageId, ref.id)
                                == RasterImageStatus::Success) {
                            continue;
                        }
                        const bool ok = co_await runRasterImageGeneration(
                            ref, retryText, queue->rasterImageInstructions(), lang, domain,
                            queue, cli, imageWriter, pageRepo, pageId, state->out, sNum);
                        if (!ok) {
                            retryRasterPaused = sessionRasterPaused = true;
                        }
                    }
                    // Re-read rather than trusting the loop: an image that
                    // exhausted its fresh budget is FailedFinal again, and the
                    // page must stay ContentReady rather than be called done.
                    rasterRepairOk = pageRepo.allRasterImagesSuccess(pageId);
                }
            }

            // Run second pass only when the SocialMedia flag is set — set by
            // --review (stats threshold reached) or the UI (manual override).
            const bool wantsSocialSecondPass =
                page.flags & static_cast<quint32>(PageFlag::SocialMedia);
            auto retryPageType = AbstractPageType::createForTypeId(page.typeId, categoryTable);
            const bool svgExpectedR = queue->wantsSvgImage()
                                      && retryPageType
                                      && retryPageType->hasSvg();

            // Tracks whether the social-media second pass saved at least one
            // WebP variant — determines Complete vs SocialComplete final state.
            bool socialPassRan = false;

            if (!retrySvg.isEmpty() && !g_stopRequested) {
                pageRepo.setGenerationState(pageId, PageGenerationState::MainImageReady);
                // Run the second pass only when the SocialMedia flag was set by
                // --review or the UI.  Pages without the flag still get their SVG
                // stored and are marked Complete without social-media variants.
                if (wantsSocialSecondPass && retryPageType) {
                    QHash<QString, QString> retryPageData = pageRepo.loadData(pageId);
                    retryPageType->load(retryPageData);
                    const QList<const AbstractPageBloc *> &blocs =
                        retryPageType->getPageBlocs();
                    for (int bIdx = 0; bIdx < blocs.size() && !g_stopRequested; ++bIdx) {
                        const AbstractPageBloc *bloc = blocs.at(bIdx);
                        if (!bloc->isSecondTimeGeneration()) { continue; }
                        const auto *secBloc =
                            static_cast<const AbstractSecondaryPageBloc *>(bloc);
                        const QList<QString> prompts =
                            secBloc->buildSecondPassPrompts(retrySvg, page);
                        const QList<AbstractSocialMedia::ImageSize> sizes =
                            secBloc->requiredImageSizes();

                        bool anyVariantSaved = false;
                        for (int vi = 0; vi < prompts.size() && !g_stopRequested; ++vi) {
                            const AbstractSocialMedia::ImageSize imgSize = sizes.at(vi);
                            const QSize dims = AbstractSocialMedia::imageSizeDimensions(imgSize);
                            const QString &webpSuffix = AbstractSocialMedia::webpSuffix(imgSize);

                            QString slug = page.permalink;
                            while (slug.startsWith(QLatin1Char('/'))) { slug.remove(0, 1); }
                            const QString webpFilename = slug + webpSuffix;

                            *(state->out) << QStringLiteral(
                                "[S%1] 2nd pass (retry, bloc %2, variant %3/%4): %5...\n")
                                .arg(sNum).arg(bIdx).arg(vi + 1).arg(prompts.size()).arg(webpFilename);
                            state->out->flush();

                            const QString svgRaw = co_await runClaudePrompt(prompts.at(vi), cli);
                            if (svgRaw.startsWith(QStringLiteral("__ERROR__"))) {
                                *(state->out) << QStringLiteral("[S%1] WARN (2nd pass retry): %2\n")
                                                     .arg(sNum).arg(svgRaw);
                                state->out->flush();
                                continue;
                            }
                            QString validatedSvg;
                            try {
                                validatedSvg = secBloc->validateSecondPassResult(svgRaw, vi);
                            } catch (...) {
                                *(state->out) << QStringLiteral(
                                    "[S%1] WARN (2nd pass retry): validation failed, variant %2\n")
                                    .arg(sNum).arg(vi);
                                state->out->flush();
                                continue;
                            }
                            QSvgRenderer renderer(validatedSvg.toUtf8());
                            if (!renderer.isValid()) {
                                *(state->out) << QStringLiteral(
                                    "[S%1] WARN (2nd pass retry): not renderable, variant %2\n")
                                    .arg(sNum).arg(vi);
                                state->out->flush();
                                continue;
                            }
                            QImage img(dims, QImage::Format_ARGB32);
                            img.fill(Qt::white);
                            QPainter painter(&img);
                            renderer.render(&painter);
                            painter.end();

                            // Save the source SVG so --translate --svg can generate
                            // language-specific WebP variants from it later.
                            const QString svgFilename = webpFilename.left(
                                webpFilename.size() - 5) + QStringLiteral(".svg");
                            imageWriter.writeSvg(validatedSvg.toUtf8(), domain, svgFilename);

                            imageWriter.writeQImage(img, domain, webpFilename);
                            const QString dataKey =
                                QStringLiteral("%1_").arg(bIdx) + secBloc->variantDataKey(vi);
                            retryPageData.insert(dataKey, webpFilename);
                            anyVariantSaved = true;

                            *(state->out) << QStringLiteral("[S%1] 2nd pass SVG+WebP saved (retry): %2\n")
                                                 .arg(sNum).arg(webpFilename);
                            state->out->flush();
                        }
                        if (anyVariantSaved) {
                            pageRepo.saveData(pageId, retryPageData);
                            socialPassRan = true;
                        }
                    }
                }
            }

            // rasterRepairOk is part of the gate: without it this branch marked
            // a page Complete whenever no SVG was expected — which is every
            // raster-only article — so a re-queued page with a broken image was
            // promoted to Complete on its next run having repaired nothing.
            if ((!svgExpectedR || !retrySvg.isEmpty()) && rasterRepairOk) {
                const PageGenerationState finalState = socialPassRan
                    ? PageGenerationState::SocialComplete
                    : PageGenerationState::Complete;
                pageRepo.setGenerationState(pageId, finalState);
                state->jobsCompleted.fetch_add(1);
                *(state->out) << QStringLiteral("[S%1] OK (retry): %2\n")
                                     .arg(sNum).arg(page.permalink);
            } else if (!rasterRepairOk && !g_stopRequested && !retryRasterPaused) {
                *(state->out) << QStringLiteral(
                    "[S%1] WARN: %2 — raster image(s) still unresolved after repair, "
                    "stays ContentReady\n")
                    .arg(sNum).arg(page.permalink);
            } else if (rasterRepairOk) {
                *(state->out) << QStringLiteral(
                    "[S%1] WARN: %2 — SVG still missing after retry, stays ContentReady\n")
                    .arg(sNum).arg(page.permalink);
            }
            state->out->flush();
            continue; // Skip first-pass content generation entirely.
        }

        // ---- Call 1: write article content (up to 3 attempts) ---------------
        *(state->out) << QStringLiteral("[S%1] Building prompt...\n").arg(sNum);
        state->out->flush();

        const QString contentPrompt = queue->buildContentPrompt(page, *engine, websiteIndex);
        const QString lang   = engine->getLangCode(websiteIndex);
        const QString domain = engine->data(
            engine->index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();

        static const int kMaxContentAttempts = 3;
        QString articleText;
        bool contentValid  = false;
        bool hadPolicyError = false;

        for (int attempt = 1; attempt <= kMaxContentAttempts; ++attempt) {
            if (g_stopRequested) {
                break;
            }

            *(state->out) << QStringLiteral("[S%1] Prompt ready (%2 chars) — launching %3 (content, attempt %4/%5)...\n")
                                 .arg(sNum).arg(contentPrompt.size()).arg(cli->getName()).arg(attempt).arg(kMaxContentAttempts);
            state->out->flush();

            // First attempt uses the original prompt.
            // Retries show Claude exactly what was wrong and ask for a clean restart.
            const QString prompt = (attempt == 1)
                ? contentPrompt
                : QStringLiteral(
                      "Your previous response for this article was invalid.\n"
                      "It started with: \"%1\"\n\n"
                      "This is wrong: a valid article must start with "
                      "[TITLE level=\"1\"]…[/TITLE] on the very first line.\n"
                      "Either your output was truncated at the beginning, or you started "
                      "mid-content by mistake.\n\n"
                      "Please write the COMPLETE article from the very beginning. "
                      "Do not continue from where you stopped — write the whole article fresh, "
                      "starting with [TITLE level=\"1\"].\n\n")
                  .arg(articleText.left(200))
                  + contentPrompt;

            const QString raw = co_await runClaudePrompt(prompt, cli);

            if (raw.startsWith(QStringLiteral("__ERROR__"))) {
                *(state->out) << QStringLiteral("[S%1] FAIL (content attempt %2): %3\n")
                                     .arg(sNum).arg(attempt).arg(raw);
                state->out->flush();
                if (isUsagePolicyError(raw)) {
                    hadPolicyError = true;
                }
                break; // process error — no point retrying
            }

            articleText = raw;
            if (isArticleValid(articleText)) {
                contentValid = true;
                break;
            }

            *(state->out) << QStringLiteral("[S%1] WARN (content attempt %2/%3): invalid structure — first 120 chars: %4\n")
                                 .arg(sNum).arg(attempt).arg(kMaxContentAttempts)
                                 .arg(articleText.left(120));
            state->out->flush();
        }

        if (!contentValid) {
            *(state->out) << QStringLiteral("[S%1] FAIL (content): %2 — all %3 attempts invalid, skipping\n")
                                 .arg(sNum).arg(page.permalink).arg(kMaxContentAttempts);
            state->out->flush();
            // Policy blocks are per-CLI: record which CLI failed so other CLIs
            // can still retry this page.
            if (hadPolicyError) {
                if (page.id == 0) {
                    // New page: create a Complete stub with the blocking CLI recorded.
                    const int blockedId = pageRepo.create(page.typeId, page.permalink, page.lang);
                    if (blockedId > 0) {
                        pageRepo.setGenerationState(blockedId, PageGenerationState::Complete);
                        QHash<QString, QString> blockData;
                        blockData.insert(QStringLiteral("policy_blocked_cli"), cli->getName());
                        pageRepo.saveData(blockedId, blockData);
                        *(state->out) << QStringLiteral("[S%1] POLICY-BLOCK: %2 — stub created, blocked for %3\n")
                                             .arg(sNum).arg(page.permalink, cli->getName());
                        state->out->flush();
                    }
                } else {
                    // Existing page (cross-CLI retry that also failed): append CLI to the block list.
                    QHash<QString, QString> data = pageRepo.loadData(page.id);
                    const QString &existing = data.value(QStringLiteral("policy_blocked_cli"));
                    if (!existing.split(QLatin1Char(','), Qt::SkipEmptyParts).contains(cli->getName())) {
                        data.insert(QStringLiteral("policy_blocked_cli"),
                                    existing.isEmpty() ? cli->getName()
                                                       : existing + QLatin1Char(',') + cli->getName());
                        pageRepo.saveData(page.id, data);
                    }
                    pageRepo.setGenerationState(page.id, PageGenerationState::Complete);
                    *(state->out) << QStringLiteral("[S%1] POLICY-BLOCK: %2 — %3 added to blocked CLIs\n")
                                         .arg(sNum).arg(page.permalink, cli->getName());
                    state->out->flush();
                }
            }
            continue;
        }

        // ---- Call 1.5: inject missing SVG IMGFIX if the strategy needs one ---
        if (!g_stopRequested && queue->wantsSvgImage() && !GenPageQueue::hasSvgImgFix(articleText)) {
            *(state->out) << QStringLiteral("[S%1] SVG IMGFIX missing — running repair (1.5/2: SVG ref)...\n")
                                 .arg(sNum);
            state->out->flush();

            const QString repairPrompt =
                queue->buildSvgRepairPrompt(page, articleText, lang);
            const QString repairResponse = co_await runClaudePrompt(repairPrompt, cli);

            if (!repairResponse.startsWith(QStringLiteral("__ERROR__"))) {
                static const QRegularExpression reImgFix(
                    QStringLiteral("\\[IMGFIX\\b[^\\]]*\\][^\\[]*\\[/IMGFIX\\]"),
                    QRegularExpression::CaseInsensitiveOption);
                const auto m = reImgFix.match(repairResponse);
                if (m.hasMatch()) {
                    articleText = GenPageQueue::insertImgFix(articleText, m.captured(0));
                    *(state->out) << QStringLiteral("[S%1] SVG IMGFIX injected.\n").arg(sNum);
                } else {
                    *(state->out) << QStringLiteral("[S%1] WARN (repair): no [IMGFIX] found in response.\n")
                                         .arg(sNum);
                }
            } else {
                *(state->out) << QStringLiteral("[S%1] WARN (repair): %2\n")
                                     .arg(sNum).arg(repairResponse);
            }
            state->out->flush();
        }

        // ---- Fix a known AI mistake: title starting with the raw permalink --
        // slug instead of a human-readable topic name (e.g. Antigravity writing
        // "/osteoarthritis-dos-and-dont Dos and Don'ts: ..." instead of
        // "Osteoarthritis Dos and Don'ts: ..."). No-op when the title is clean.
        articleText = GenPageQueue::fixSlugTitle(articleText, page.permalink, endPermalink);

        *(state->out) << QStringLiteral("[S%1] Content done: %2 (%3 chars) — launching %4 (2/2: metadata)...\n")
                             .arg(sNum).arg(page.permalink).arg(articleText.size()).arg(cli->getName());
        state->out->flush();

        // ---- Call 2: metadata JSON from the article -------------------------
        // Skip the metadata call (save content only) if the user pressed Ctrl+C.
        QString metadataJson;
        if (!g_stopRequested) {
            const QString metadataPrompt = queue->buildMetadataPrompt(page, articleText);
            metadataJson = co_await runClaudePrompt(metadataPrompt, cli);
            // Metadata failures are tolerated: processContentAndMetadata() falls back
            // to saving 1_text only if metadataJson is empty or unparseable.
            if (metadataJson.startsWith(QStringLiteral("__ERROR__"))) {
                *(state->out) << QStringLiteral("[S%1] WARN (metadata): %2 — %3 — saving content only\n")
                                     .arg(sNum).arg(page.permalink, metadataJson);
                state->out->flush();
            }
        }

        // ---- For source-DB-backed pages (id == 0), create the row first -----
        // Only reached when content is valid — no orphan rows on failure.
        // Pages with id != 0 (ContentReady/MainImageReady retries) are handled
        // by the smart retry path above and have already continued.
        int pageId = page.id;
        const bool pageCreatedHere = (pageId == 0);
        if (pageId == 0) {
            pageId = pageRepo.create(page.typeId, page.permalink, page.lang);
            if (pageId <= 0) {
                // Permalink exists (race between sessions) — skip.
                *(state->out) << QStringLiteral("[S%1] SKIP (exists): %2\n")
                                     .arg(sNum).arg(page.permalink);
                state->out->flush();
                continue;
            }
            if (!endPermalink.isEmpty()) {
                pageRepo.setEndPermalink(pageId, endPermalink);
            }
        }

        // ---- Save result ----------------------------------------------------
        const QString safeMetadata = metadataJson.startsWith(QStringLiteral("__ERROR__"))
                                     ? QString{} : metadataJson;
        if (queue->processContentAndMetadata(pageId, articleText, safeMetadata, pageRepo)) {
            pageRepo.recordStrategyAttempt(pageId, strategyId);
            pageRepo.setGenerationState(pageId, PageGenerationState::ContentReady);
            state->jobsCompleted.fetch_add(1);
            *(state->out) << QStringLiteral("[S%1] OK: %2\n")
                                 .arg(sNum).arg(page.permalink);

            // ---- Generate SVG images referenced in the article --------------
            auto pageType = AbstractPageType::createForTypeId(page.typeId, categoryTable);
            // SVG is required when the strategy wants it AND the page type declares
            // it needs one.  If SVG generation fails, the page stays in ContentReady
            // so that the next generation run can retry rather than silently marking
            // the page Complete without any social-media images.
            const bool svgExpected = queue->wantsSvgImage()
                                     && pageType
                                     && pageType->hasSvg();

            const QList<GenPageQueue::ImgFixRef> parsedImgRefs =
                GenPageQueue::parseImgFixRefs(articleText);
            const QString domain = engine->data(
                engine->index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();
            const QString lang = engine->getLangCode(websiteIndex);

            // Raster generation requires an image-capable CLI (Codex/Antigravity).
            // Warn once per page rather than once per image, and never fabricate
            // a failure for a CLI that was simply never asked to try — the page
            // just stays ContentReady until a capable CLI processes it.
            const bool rasterWanted    = queue->wantsRasterImage();
            const bool rasterCliReady  = rasterWanted && cli->canGenImages();
            if (rasterWanted && !rasterCliReady) {
                *(state->out) << QStringLiteral(
                    "[S%1] WARN: %2 — strategy requests raster images but %3 cannot "
                    "generate images; skipping this run, will retry with a capable CLI\n")
                    .arg(sNum).arg(page.permalink, cli->getName());
                state->out->flush();
            }

            // Cap the raster refs attempted at imageCountMax() (when set) so an
            // AI response that over-produces images (e.g. 20 when 8-12 were
            // asked for) doesn't multiply this page's CLI-call cost unbounded.
            // SVG refs are never trimmed — only the new non-SVG raster count is
            // strategy-configurable.
            QList<GenPageQueue::ImgFixRef> imgRefs;
            int rasterRefCount = 0;
            for (const auto &ref : std::as_const(parsedImgRefs)) {
                const bool isSvg = ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
                if (isSvg) {
                    imgRefs.append(ref);
                    continue;
                }
                ++rasterRefCount;
                if (queue->imageCountMax() > 0 && rasterRefCount > queue->imageCountMax()) {
                    continue; // drop the excess — logged once below, not per ref
                }
                imgRefs.append(ref);
            }
            if (queue->imageCountMax() > 0 && rasterRefCount > queue->imageCountMax()) {
                *(state->out) << QStringLiteral(
                    "[S%1] %2 — article included %3 raster image(s), capping at %4\n")
                    .arg(sNum).arg(page.permalink).arg(rasterRefCount).arg(queue->imageCountMax());
                state->out->flush();
            }

            QString sourceSvgForSocialMedia; // first SVG content — seed for second pass
            bool    rasterPaused = false;     // true on a quota/auth pause for THIS page
            for (const auto &ref : std::as_const(imgRefs)) {
                if (g_stopRequested || rasterPaused) {
                    break;
                }
                if (!ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                    if (!rasterCliReady) {
                        continue; // not expected, or no capable CLI this run
                    }
                    const bool ok = co_await runRasterImageGeneration(
                        ref, articleText, queue->rasterImageInstructions(), lang, domain,
                        queue, cli, imageWriter, pageRepo, pageId, state->out, sNum);
                    if (!ok) {
                        // Propagate to the outer per-page loop too: the same
                        // CLI/account will likely fail again immediately, so
                        // stop this whole session rather than churn through
                        // remaining pages just to hit the same wall.
                        rasterPaused = sessionRasterPaused = true;
                    }
                    continue;
                }
                *(state->out) << QStringLiteral("[S%1] Generating SVG: %2...\n")
                                     .arg(sNum).arg(ref.fileName);
                state->out->flush();

                const QString svgPrompt = queue->buildSvgPrompt(ref, articleText, lang);
                const QString svgResponse = co_await runWithPolicyRetry(svgPrompt, cli, state->out, sNum);

                if (svgResponse.startsWith(QStringLiteral("__ERROR__"))) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — %3\n")
                                         .arg(sNum).arg(ref.fileName, svgResponse);
                    state->out->flush();
                    continue;
                }

                const auto svgResult = GenPageQueue::parseSvgDelimitedResponse(svgResponse);
                if (svgResult.svgCode.isEmpty()) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — no <svg> element in response (%3 chars): %4\n")
                                         .arg(sNum).arg(ref.fileName)
                                         .arg(svgResponse.size())
                                         .arg(svgResponse.left(120));
                    state->out->flush();
                    continue;
                }
                if (!svgResult.svgCode.contains(QStringLiteral("</svg>"), Qt::CaseInsensitive)) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — SVG truncated (no </svg>, %3 chars)\n")
                                         .arg(sNum).arg(ref.fileName).arg(svgResponse.size());
                    state->out->flush();
                    continue;
                }

                const QString &svgContent = svgResult.svgCode;
                imageWriter.writeSvg(svgContent.toUtf8(), domain, ref.fileName);
                if (sourceSvgForSocialMedia.isEmpty()) {
                    sourceSvgForSocialMedia = svgContent;
                }
                *(state->out) << QStringLiteral("[S%1] SVG saved: %2\n")
                                     .arg(sNum).arg(ref.fileName);
                state->out->flush();
            }

            // Mark Complete only when BOTH SVG and raster-image requirements are
            // satisfied.  If either is outstanding, leave the page in ContentReady
            // so the next generation run retries only the work still needed —
            // this is what lets a page interrupted mid-image-generation (crash or
            // CLI usage-limit pause) resume cleanly instead of silently completing
            // with images missing.  The social-media second pass is NOT run here —
            // it runs separately via --review + --generation once the page
            // accumulates enough traffic and gets the SocialMedia flag.
            // An under-count (fewer raster refs than imageCountMin()) is treated
            // the same as "not fully ready" — the page stays ContentReady so the
            // next run's content regeneration gets another chance at a better
            // count, exactly like an SVG that the AI failed to reference at all.
            // allRasterImagesSuccess(), NOT allRasterImagesTerminal(): a
            // FailedFinal image is terminal but the article is NOT finished —
            // it cannot publish (PageGenerator's gate rejects it), so calling
            // it Complete stranded it forever, counted as done while being
            // permanently unpublishable and never retried. Staying
            // ContentReady instead puts it back in the retry queue, where the
            // raster repair pass above grants it a fresh attempt budget.
            bool rasterSatisfied = true;
            if (rasterWanted) {
                const bool countSatisfied = queue->imageCountMin() <= 0
                                            || rasterRefCount >= queue->imageCountMin();
                rasterSatisfied = rasterCliReady && countSatisfied
                                 && pageRepo.allRasterImagesSuccess(pageId);
                if (rasterCliReady && !countSatisfied && !g_stopRequested && !rasterPaused) {
                    *(state->out) << QStringLiteral(
                        "[S%1] WARN: %2 — only %3 raster image(s) found, %4 required minimum\n")
                        .arg(sNum).arg(page.permalink).arg(rasterRefCount).arg(queue->imageCountMin());
                    state->out->flush();
                }
            }
            if ((!svgExpected || !sourceSvgForSocialMedia.isEmpty()) && rasterSatisfied) {
                pageRepo.setGenerationState(pageId, PageGenerationState::Complete);
            } else if (!g_stopRequested && !rasterPaused) {
                *(state->out) << QStringLiteral(
                    "[S%1] WARN: %2 — SVG and/or raster images not fully ready; "
                    "page stays ContentReady for retry on next run\n")
                    .arg(sNum).arg(page.permalink);
                state->out->flush();
            }
        } else {
            *(state->out) << QStringLiteral("[S%1] FAIL (invalid content): %2 — check qWarning log\n")
                                 .arg(sNum).arg(page.permalink);
            // Remove the page row we just created so no empty page is left behind.
            if (pageCreatedHere) {
                pageRepo.remove(pageId);
            }
        }
        state->out->flush();

        // Inter-page delay: spread requests to reduce API rate-limit hits.
        if (state->interPageDelayMs > 0 && queue->hasNext() && !g_stopRequested) {
            *(state->out) << QStringLiteral("[S%1] Waiting %2s before next page...\n")
                                 .arg(sNum).arg(state->interPageDelayMs / 1000);
            state->out->flush();
            QTimer pageTimer;
            pageTimer.setSingleShot(true);
            pageTimer.start(state->interPageDelayMs);
            co_await qCoro(pageTimer).waitForTimeout();
        }
    }

    if (g_stopRequested) {
        *(state->out) << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
        state->out->flush();
    } else if (sessionRasterPaused) {
        *(state->out) << QStringLiteral(
            "[S%1] Session paused after a raster-image usage-limit/auth error — "
            "no article was left half-published; re-run --generation once the "
            "limit clears or after re-authenticating.\n").arg(sNum);
        state->out->flush();
    }

    if (--(state->activeCount) == 0) {
        *(state->out) << QStringLiteral("Generation complete. %1 page(s) generated.\n")
                             .arg(state->jobsCompleted.load());
        state->out->flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit,
                                  Qt::QueuedConnection);
    }
    co_return;
}

// ---------------------------------------------------------------------------
// LauncherGeneration
// ---------------------------------------------------------------------------

QString LauncherGeneration::getOptionName() const { return OPTION_NAME; }
bool    LauncherGeneration::isFlag()        const { return true; }

void LauncherGeneration::run(const QString & /*value*/)
{
    auto *out = new QTextStream(stdout);

    // ---- Parse sub-options from raw args ----------------------------------
    const QStringList args = QCoreApplication::arguments();

    int         numSessions      = 1;
    int         jobsLimit        = -1;
    int         interPageDelayMs = 0;
    QString     strategyFilter; // empty = all priority-1 strategies
    QStringList customTopics; // non-empty = GUI "Custom topic" bypass (one entry per --topic)
    bool        newOnly          = false; // true = skip retry queue, only generate new articles
    bool        retryOnly        = false; // true = skip new items, only process retry queue
    AbstractCli *cli             = nullptr;

    for (int i = 0; i < args.size() - 1; ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--") + OPTION_SESSIONS) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1 && n <= 10) {
                numSessions = n;
            }
        } else if (arg == QStringLiteral("--") + OPTION_LIMIT) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1) {
                jobsLimit = n;
            }
        } else if (arg == QStringLiteral("--") + OPTION_DELAY) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 0) {
                interPageDelayMs = n * 1000;
            }
        } else if (arg == QStringLiteral("--") + OPTION_STRATEGY) {
            strategyFilter = args.at(i + 1);
        } else if (arg == QStringLiteral("--") + OPTION_TOPIC) {
            const QString t = args.at(i + 1).trimmed();
            if (!t.isEmpty()) {
                customTopics.append(t);
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
    // --new-only and --retry-only are flags (no value), check for presence separately.
    newOnly    = args.contains(QStringLiteral("--") + OPTION_NEW_ONLY);
    retryOnly  = args.contains(QStringLiteral("--") + OPTION_RETRY_ONLY);

    // Resolve CLI: fall back to the first registered CLI (CliClaude) when not specified.
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

    // ---- Resolve engine and API key ---------------------------------------
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    const auto &settings  = WorkingDirectoryManager::instance()->settings();

    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        *out << QStringLiteral("ERROR: no engine configured (engineId = %1).\n").arg(engineId);
        out->flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit,
                                  Qt::QueuedConnection);
        return;
    }

    // ---- Build object graph -----------------------------------------------
    auto *holder = new QObject(nullptr);

    auto *hostTable  = new HostTable(workingDir, holder);
    auto *engine     = proto->create(holder);
    engine->init(workingDir, *hostTable);

    auto *categoryTable = new CategoryTable(workingDir, holder);

    // Determine primary domain and website index for the editing language.
    const WebsiteSettingsTable settingsTable(workingDir);
    const QString editingLang = settingsTable.editingLangCode();
    QString primaryDomain;
    int editingLangIndex = 0;
    bool foundEditingLangRow = false;
    for (int i = 0; i < engine->rowCount(); ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == editingLang) {
            primaryDomain      = engine->data(
                engine->index(i, AbstractEngine::COL_DOMAIN)).toString();
            editingLangIndex   = i;
            foundEditingLangRow = true;
            break;
        }
    }
    if (!foundEditingLangRow && engine->rowCount() > 0) {
        // AbstractEngine::_reconcileRows() should always create a row for the
        // editing language, so reaching this means engine_domains.csv was
        // hand-edited into an inconsistent state. Falling back to row 0 would
        // silently generate content in the wrong language, so warn loudly.
        const QString fallbackLang = engine->data(
            engine->index(0, AbstractEngine::COL_LANG_CODE)).toString();
        *out << QStringLiteral(
            "WARNING: editing language '%1' not found in engine_domains.csv — "
            "falling back to row 0 ('%2'). Content will be generated in '%2' "
            "instead of '%1'. Check engine_domains.csv / the Domains pane.\n")
                    .arg(editingLang, fallbackLang);
        out->flush();
    }
    if (primaryDomain.isEmpty() && engine->rowCount() > 0) {
        primaryDomain = engine->data(
            engine->index(0, AbstractEngine::COL_DOMAIN)).toString();
    }

    // ---- Performance data source ------------------------------------------
    AbstractPerformanceDataSource *dataSource = nullptr;
    GscSettings gscSettings(workingDir);
    GscDataSource gscSource(gscSettings);
    StatsDbDataSource statsSource(workingDir);

    if (gscSettings.isConfigured()) {
        dataSource = &gscSource;
        *out << QStringLiteral("Performance source: Google Search Console\n");
    } else if (statsSource.isConfigured()) {
        dataSource = &statsSource;
        *out << QStringLiteral("Performance source: stats.db\n");
    } else {
        *out << QStringLiteral("Performance source: none (even distribution)\n");
    }
    out->flush();

    // ---- Strategy table and scheduler -------------------------------------
    auto *strategyTable = new GenStrategyTable(workingDir, holder);

    // Use a temporary page DB for scheduling (read-only queries).
    PageDb           schedDb(workingDir);
    PageRepositoryDb schedRepo(schedDb);

    // ---- Build virtual pages for source-DB-backed strategies ----------------
    // For strategies that reference an aspire DB (primaryAttrId != ""), we read
    // items from results_db/<primaryAttrId>.db and build PageRecord items with
    // id == 0 for those not yet present in pages.db.  The page row is created
    // atomically in runGenerationSession just before saving the generated content.

    static const QRegularExpression reNonAlnum(QStringLiteral("[^a-z0-9]+"));

    // Pre-load all existing permalinks once (avoid repeated findAll() calls).
    // ALL pages (any state) are excluded from the source-DB virtual-page list so
    // that existing articles are never regenerated from scratch unintentionally.
    // ContentReady / MainImageReady pages are re-queued explicitly below with
    // their real ids so the smart retry path runs the second pass without
    // touching already-saved content.
    const QList<PageRecord> allExistingPages = schedRepo.findAll();
    QSet<QString> allExistingPermalinks;
    allExistingPermalinks.reserve(allExistingPages.size());
    for (const PageRecord &p : std::as_const(allExistingPages)) {
        allExistingPermalinks.insert(p.permalink);
    }

    // Policy-blocked pages: keyed by permalink → {page_id, comma-separated CLIs}.
    // Pages blocked for the current CLI are permanently skipped.
    // Pages blocked only by OTHER CLIs are re-queued so this CLI can try them.
    const auto policyBlocked = schedRepo.findPolicyBlockedPages();
    const QString currentCliName = cli ? cli->getName() : QString{};
    // Build a set of permalinks that should be re-queued for the current CLI.
    QHash<QString, int> policyRetryIds; // permalink → page_id for cross-CLI retries
    for (auto it = policyBlocked.cbegin(); it != policyBlocked.cend(); ++it) {
        const QStringList blockedClis = it.value().second.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!blockedClis.contains(currentCliName)) {
            policyRetryIds.insert(it.key(), it.value().first);
        }
    }

    // ---- Custom topic bypass (--topic, triggered by PaneGeneration UI only) -----
    // Each --topic arg is one topic; all are slugified and queued in a single
    // session, bypassing the source DB and scheduler entirely.
    if (!customTopics.isEmpty()) {
        if (strategyFilter.isEmpty()) {
            *out << QStringLiteral("ERROR: --topic requires --strategy <id>.\n");
            out->flush();
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      &QCoreApplication::quit, Qt::QueuedConnection);
            return;
        }

        // Locate the strategy in the table.
        GenScheduler::StrategyInfo info;
        bool found = false;
        for (int row = 0; row < strategyTable->rowCount(); ++row) {
            if (strategyTable->idForRow(row) != strategyFilter) { continue; }
            info.strategyId         = strategyTable->idForRow(row);
            info.pageTypeId         = strategyTable->data(
                strategyTable->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();
            info.customInstructions = strategyTable->customInstructionsForRow(row);
            info.svgInstructions    = strategyTable->svgInstructionsForRow(row);
            info.endPermalink       = strategyTable->endPermalinkForRow(row);
            info.imageInstructions  = strategyTable->imageInstructionsForRow(row);
            info.imageCountMin      = strategyTable->imageCountMinForRow(row);
            info.imageCountMax      = strategyTable->imageCountMaxForRow(row);
            info.nonSvgImages       = strategyTable->data(
                strategyTable->index(row, GenStrategyTable::COL_NON_SVG_IMAGES)).toString()
                == QStringLiteral("Yes");
            found = true;
            break;
        }
        if (!found) {
            *out << QStringLiteral("ERROR: strategy not found: %1\n").arg(strategyFilter);
            out->flush();
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      &QCoreApplication::quit, Qt::QueuedConnection);
            return;
        }

        // Slugify each topic and build the virtual page list.
        // Topics whose permalink already exists are skipped with a warning.
        QList<PageRecord> virtualPages;
        for (const QString &topic : std::as_const(customTopics)) {
            QString slug = topic.toLower();
            slug.replace(reNonAlnum, QStringLiteral("-"));
            while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
            while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
            if (slug.isEmpty()) { continue; }
            if (!info.endPermalink.isEmpty()) {
                slug += QLatin1Char('-') + info.endPermalink;
            }
            const QString permalink = QLatin1Char('/') + slug;

            if (allExistingPermalinks.contains(permalink)) {
                if (policyRetryIds.contains(permalink)) {
                    PageRecord vp;
                    vp.id        = policyRetryIds.value(permalink);
                    vp.typeId    = info.pageTypeId;
                    vp.permalink = permalink;
                    vp.lang      = editingLang;
                    virtualPages.append(vp);
                    *out << QStringLiteral("RETRY (cross-CLI): \"%1\" → %2 (id=%3)\n")
                                .arg(topic, permalink).arg(vp.id);
                } else {
                    *out << QStringLiteral("SKIP (exists): \"%1\" → %2\n").arg(topic, permalink);
                }
                out->flush();
                continue;
            }

            *out << QStringLiteral("Custom topic: \"%1\" → %2\n").arg(topic, permalink);
            out->flush();

            PageRecord vp;
            vp.id        = 0;
            vp.typeId    = info.pageTypeId;
            vp.permalink = permalink;
            vp.lang      = editingLang;
            virtualPages.append(vp);
        }

        if (virtualPages.isEmpty()) {
            *out << QStringLiteral("Nothing to generate — all topics already exist.\n");
            out->flush();
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      &QCoreApplication::quit, Qt::QueuedConnection);
            return;
        }

        std::signal(SIGINT,  handleSignal);
        std::signal(SIGTERM, handleSignal);

        auto *state = new GenRunState;
        state->jobsLimit        = -1; // queue contains exactly the requested topics
        state->interPageDelayMs = interPageDelayMs;
        state->out              = out;
        state->activeCount      = 1;

        auto *queue = new GenPageQueue(info.pageTypeId, info.nonSvgImages,
                                        virtualPages,
                                        *categoryTable,
                                        info.customInstructions,
                                        info.svgInstructions,
                                        workingDir,
                                        info.imageInstructions,
                                        info.imageCountMin,
                                        info.imageCountMax);
        runGenerationSession(queue, info.strategyId, info.endPermalink,
                              engine, editingLangIndex,
                              *categoryTable, state, 0, cli);
        return;
    }

    // virtualPagesByStrategyId — keyed by stable strategy UUID
    QHash<QString, QList<PageRecord>> virtualPagesByStrategyId;

    // Build GUI-independent strategy list for the scheduler.
    // Normal generation only runs priority-1 strategies; higher priorities are
    // reserved for the --improve pass on underperforming pages.
    QList<GenScheduler::StrategyInfo> strategyInfos;
    strategyInfos.reserve(strategyTable->rowCount());
    for (int row = 0; row < strategyTable->rowCount(); ++row) {
        const int prio = strategyTable->priorityForRow(row);
        if (prio != 1) {
            continue;
        }
        GenScheduler::StrategyInfo info;
        info.strategyId         = strategyTable->idForRow(row);
        if (!strategyFilter.isEmpty() && info.strategyId != strategyFilter) {
            continue;
        }
        info.pageTypeId         = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();
        info.themeId            = strategyTable->themeIdForRow(row);
        info.customInstructions = strategyTable->customInstructionsForRow(row);
        info.svgInstructions    = strategyTable->svgInstructionsForRow(row);
        info.primaryAttrId      = strategyTable->primaryAttrIdForRow(row);
        info.endPermalink       = strategyTable->endPermalinkForRow(row);
        info.imageInstructions  = strategyTable->imageInstructionsForRow(row);
        info.imageCountMin      = strategyTable->imageCountMinForRow(row);
        info.imageCountMax      = strategyTable->imageCountMaxForRow(row);
        info.nonSvgImages       = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_NON_SVG_IMAGES)).toString()
            == QStringLiteral("Yes");
        info.priority           = prio;

        // For source-DB-backed strategies, read aspire DB and compute pending items.
        // A strategy qualifies when either primaryAttrId is set (convention path) or
        // a DB was manually linked via the GUI (primaryDbPath, may be relative).
        {
            // 1. Stored path — may be relative to working dir (portable Dropbox format).
            QString dbPath = strategyTable->primaryDbPathForRow(row);
            if (!dbPath.isEmpty() && !QFile::exists(dbPath)) {
                const QString abs = workingDir.absoluteFilePath(dbPath);
                if (QFile::exists(abs)) {
                    dbPath = QDir::cleanPath(abs);
                } else {
                    dbPath.clear(); // neither absolute nor relative resolved
                }
            }
            // 2. Convention path — only when primaryAttrId is set and stored path missing.
            if (dbPath.isEmpty() && !info.primaryAttrId.isEmpty()) {
                const QString stdPath = workingDir.filePath(
                    QStringLiteral("results_db/") + info.primaryAttrId
                    + QStringLiteral(".db"));
                if (QFile::exists(stdPath)) {
                    dbPath = stdPath;
                }
            }

            QList<PageRecord> virtualPages;

            if (!dbPath.isEmpty()) {
                *out << QStringLiteral("Source DB for '%1': %2 — found\n")
                            .arg(info.pageTypeId, dbPath);
                out->flush();
            } else if (!info.primaryAttrId.isEmpty()) {
                // Had a primaryAttrId but DB not found — report it so user can debug.
                const QString expected = workingDir.filePath(
                    QStringLiteral("results_db/") + info.primaryAttrId
                    + QStringLiteral(".db"));
                *out << QStringLiteral("Source DB for '%1': %2 — NOT FOUND\n")
                            .arg(info.pageTypeId, expected);
                out->flush();
            }

            if (!retryOnly && !dbPath.isEmpty()) {
                const QString connName = QStringLiteral("gen_src_") + info.strategyId;
                // Combo tables (e.g. Fashion's 19 PageAttributesFashionCombo*)
                // self-compose their topic text via composeArticleTopic() —
                // see AbstractPageAttributes.h. nullptr/empty result (every
                // other, single-name-column table) falls back to the legacy
                // "first non-id column" behavior below, unchanged.
                const AbstractPageAttributes *attrsProto =
                    AbstractPageAttributes::ALL_PAGE_ATTRIBUTES().value(info.primaryAttrId, nullptr);
                QStringList names;
                {
                    QSqlDatabase srcDb = QSqlDatabase::addDatabase(
                        QStringLiteral("QSQLITE"), connName);
                    srcDb.setDatabaseName(dbPath);
                    if (srcDb.open()) {
                        QSqlQuery probe(srcDb);
                        QString orderBy = QStringLiteral("id");
                        QString firstNonIdColumn;
                        if (probe.exec(QStringLiteral("SELECT * FROM records LIMIT 0"))) {
                            const QSqlRecord schema = probe.record();
                            for (int i = 0; i < schema.count(); ++i) {
                                const QString col = schema.fieldName(i);
                                if (col == QStringLiteral("id")) {
                                    continue;
                                }
                                if (firstNonIdColumn.isEmpty()) {
                                    firstNonIdColumn = col;
                                }
                            }
                            if (schema.indexOf(QStringLiteral("combo_msv")) >= 0) {
                                orderBy = QStringLiteral("combo_msv DESC, id ASC");
                            }
                            // Formula first, so consecutive generations walk
                            // DIFFERENT slot values instead of the same one
                            // rendered through each of a table's formulas in
                            // turn. Without this, a multi-formula table like
                            // ColorProduct emits "What to wear with Black
                            // Boots", then "How to style Black Boots", ... —
                            // near-duplicate articles back to back, because
                            // every formula of one slot pair shares the same
                            // combo_msv and sits adjacent in id order.
                            // No-op for single-formula tables (constant key).
                            if (schema.indexOf(QStringLiteral("combo_formula_id")) >= 0) {
                                orderBy = QStringLiteral("combo_formula_id ASC, ") + orderBy;
                            }
                        }

                        QSqlQuery q(srcDb);
                        if (q.exec(QStringLiteral("SELECT * FROM records ORDER BY ") + orderBy)) {
                            const QSqlRecord schema = q.record();
                            while (q.next()) {
                                QString name;
                                if (attrsProto) {
                                    QHash<QString, QString> rowValues;
                                    for (int i = 0; i < schema.count(); ++i) {
                                        rowValues.insert(schema.fieldName(i), q.value(i).toString());
                                    }
                                    // Row-level veto: valid research data that
                                    // must not become an article (see
                                    // AbstractPageAttributes::isArticleTopicEligible).
                                    if (!attrsProto->isArticleTopicEligible(rowValues)) {
                                        continue;
                                    }
                                    name = attrsProto->composeArticleTopic(rowValues);
                                }
                                if (name.isEmpty() && !firstNonIdColumn.isEmpty()) {
                                    name = q.value(firstNonIdColumn).toString();
                                }
                                name = name.trimmed();
                                if (!name.isEmpty()) {
                                    names << name;
                                }
                            }
                        }
                    }
                }
                QSqlDatabase::removeDatabase(connName);

                // Symptom hub pages use /symptoms/<slug> prefix.
                const bool isSymptomHub = (info.pageTypeId == QStringLiteral("symptom_hub"));

                // Guards against two different source rows in THIS batch
                // composing to the same slug — without this, the collision is
                // only caught later by pageRepo.create()'s UNIQUE constraint,
                // after a full (wasted) AI generation call already ran.
                QSet<QString> seenThisRun;

                for (const QString &name : std::as_const(names)) {
                    QString slug = name.toLower();
                    slug.replace(reNonAlnum, QStringLiteral("-"));
                    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
                    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
                    if (slug.isEmpty()) { continue; }

                    if (!info.endPermalink.isEmpty()) {
                        slug += QLatin1Char('-') + info.endPermalink;
                    }

                    const QString permalink = isSymptomHub
                        ? QStringLiteral("/symptoms/") + slug
                        : QLatin1Char('/') + slug;
                    if (seenThisRun.contains(permalink)) {
                        continue;
                    }
                    seenThisRun.insert(permalink);
                    if (allExistingPermalinks.contains(permalink)) {
                        if (policyRetryIds.contains(permalink)) {
                            PageRecord vp;
                            vp.id        = policyRetryIds.value(permalink);
                            vp.typeId    = info.pageTypeId;
                            vp.permalink = permalink;
                            vp.lang      = editingLang;
                            virtualPages.append(vp);
                        }
                        continue;
                    }

                    PageRecord vp;
                    vp.id        = 0;
                    vp.typeId    = info.pageTypeId;
                    vp.permalink = permalink;
                    vp.lang      = editingLang;
                    virtualPages.append(vp);
                }

                // Symptom index: one fixed page at /symptoms when not yet present.
                if (info.pageTypeId == QStringLiteral("symptom_index")
                    && !allExistingPermalinks.contains(QStringLiteral("/symptoms"))) {
                    PageRecord vp;
                    vp.id        = 0;
                    vp.typeId    = info.pageTypeId;
                    vp.permalink = QStringLiteral("/symptoms");
                    vp.lang      = editingLang;
                    virtualPages.append(vp);
                }

                if (jobsLimit >= 0 && virtualPages.size() > jobsLimit) {
                    virtualPages = virtualPages.mid(0, jobsLimit);
                }
            }

            if (!virtualPages.isEmpty()) {
                info.pendingCountOverride = virtualPages.size();
                *out << QStringLiteral("  → %1 item(s) pending (already in pages.db excluded)\n")
                            .arg(info.pendingCountOverride);
                out->flush();
                virtualPagesByStrategyId.insert(info.strategyId, std::move(virtualPages));
            }
            // When virtualPages is empty (all items already in pages.db), leave
            // pendingCountOverride == -1 so the scheduler falls back to
            // findPendingByTypeId() — existing pending stubs still get processed.
        }

        // ---- Housekeeping: advance MainImageReady without SocialMedia flag ----
        // Pages left in MainImageReady by an interrupted run or old pipeline
        // that did not set the SocialMedia flag should be promoted to Complete
        // so they are not stuck in the retry queue indefinitely.
        // NOTE: we intentionally do NOT auto-promote Complete + SocialMedia flag
        // → SocialComplete here. That migration was removed because it could
        // corrupt state by promoting a page whose second pass never ran.
        {
            const QList<PageRecord> allMirPages =
                schedRepo.findByGenerationState(info.pageTypeId,
                                               PageGenerationState::MainImageReady);
            for (const PageRecord &p : std::as_const(allMirPages)) {
                if (!(p.flags & static_cast<quint32>(PageFlag::SocialMedia))) {
                    schedRepo.setGenerationState(p.id, PageGenerationState::Complete);
                }
            }
        }

        // ---- Housekeeping: demote "Complete" pages with broken images --------
        // An article with an unresolved raster image is not complete: it can
        // never publish (PageGenerator's gate rejects it).  Runs before the
        // retry queue is built, so a demoted page is picked up as a ContentReady
        // retry in this same run and gets repaired instead of sitting there
        // permanently unpublishable while counted as done.
        //
        // This also heals pages stranded by the older completion rule, which
        // used allRasterImagesTerminal() and therefore treated a FailedFinal
        // image as good enough to mark the page Complete.
        //
        // SocialComplete is deliberately NOT demoted: its social-media second
        // pass already ran, and re-running the first pass would be wasteful.
        {
            const QList<PageRecord> unresolved =
                schedRepo.findPagesWithUnresolvedRasterImages(info.pageTypeId);
            int demoted = 0;
            for (const PageRecord &p : std::as_const(unresolved)) {
                if (p.generationState != PageGenerationState::Complete) {
                    continue;
                }
                schedRepo.setGenerationState(p.id, PageGenerationState::ContentReady);
                ++demoted;
            }
            if (demoted > 0) {
                *out << QStringLiteral(
                    "  → %1 page(s) demoted to ContentReady: raster image(s) unresolved\n")
                    .arg(demoted);
                out->flush();
            }
        }

        // ---- Prepend retry pages (skipped with --new-only) -------------------
        // --new-only: only new articles from the source DB are processed;
        //   retries (SVG failures and social-media second pass) are skipped.
        //   Use for spot-checks or manual topic generation via the GUI.
        if (!newOnly) {
            const QList<PageRecord> crPages =
                schedRepo.findByGenerationState(info.pageTypeId,
                                               PageGenerationState::ContentReady);

            // SocialMedia-flagged pages: findByFlag returns ALL types, so filter
            // to this strategy's page type and only MainImageReady state.
            const QList<PageRecord> allFlagged = schedRepo.findByFlag(PageFlag::SocialMedia);
            QList<PageRecord> mirPages;
            for (const PageRecord &p : std::as_const(allFlagged)) {
                if (p.typeId == info.pageTypeId
                    && p.generationState == PageGenerationState::MainImageReady) {
                    mirPages.append(p);
                }
            }

            if (!crPages.isEmpty() || !mirPages.isEmpty()) {
                // For classic strategies that didn't build a virtualPages list,
                // also harvest Pending pages from pages.db so they still get
                // processed alongside the retry pages.
                const bool isClassicStrategy =
                    !virtualPagesByStrategyId.contains(info.strategyId);
                QList<PageRecord> &vp = virtualPagesByStrategyId[info.strategyId];
                if (isClassicStrategy && !retryOnly) {
                    const QList<PageRecord> pending =
                        schedRepo.findPendingByTypeId(info.pageTypeId);
                    vp.append(pending);
                }
                // When a --limit is set, new pages go first so the limit slot is
                // consumed by fresh article generation, not retries.  Without a
                // limit (full run), retries go first: SocialMedia then SVG-retry.
                if (jobsLimit >= 0) {
                    vp.append(mirPages);
                    vp.append(crPages);
                } else {
                    for (const PageRecord &p : std::as_const(mirPages)) {
                        vp.prepend(p);
                    }
                    for (const PageRecord &p : std::as_const(crPages)) {
                        vp.prepend(p);
                    }
                }
                info.pendingCountOverride = vp.size();
                *out << QStringLiteral(
                    "  → %1 retry page(s) (%2 social-media, %3 SVG-retry) queued for %4\n")
                    .arg(crPages.size() + mirPages.size())
                    .arg(mirPages.size())
                    .arg(crPages.size())
                    .arg(info.pageTypeId);
                out->flush();
            }
        } else {
            *out << QStringLiteral("  → retry queue skipped (--new-only)\n");
            out->flush();
        }

        // --retry-only: ensure every strategy has a pre-built (possibly empty) queue
        // so no strategy ever falls into the classic path that reads Pending pages from
        // pages.db (which would generate new articles, defeating the purpose of the flag).
        if (retryOnly && !virtualPagesByStrategyId.contains(info.strategyId)) {
            virtualPagesByStrategyId.insert(info.strategyId, {});
        }

        strategyInfos.append(info);
    }

    GenScheduler scheduler(strategyInfos, schedRepo, dataSource, primaryDomain);
    const QList<GenScheduler::StrategyAllocation> allocations =
        scheduler.computeAllocations(numSessions);

    if (allocations.isEmpty()) {
        *out << QStringLiteral("Nothing to generate. All pages are up to date.\n");
        out->flush();
        holder->deleteLater();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit,
                                  Qt::QueuedConnection);
        return;
    }

    // ---- Spawn coroutines -------------------------------------------------
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    auto *state = new GenRunState;
    state->jobsLimit        = jobsLimit;
    state->interPageDelayMs = interPageDelayMs;
    state->out              = out;

    for (const auto &alloc : std::as_const(allocations)) {
        GenPageQueue *queue = nullptr;

        if (virtualPagesByStrategyId.contains(alloc.strategyId)) {
            // Source-DB-backed strategy: use pre-built virtual pages
            // (works whether primaryAttrId is set or a DB was linked directly).
            auto &vPages = virtualPagesByStrategyId[alloc.strategyId];
            queue = new GenPageQueue(alloc.pageTypeId,
                                     alloc.nonSvgImages,
                                     vPages,
                                     *categoryTable,
                                     alloc.customInstructions,
                                     alloc.svgInstructions,
                                     workingDir,
                                     alloc.imageInstructions,
                                     alloc.imageCountMin,
                                     alloc.imageCountMax);
        } else {
            // Classic strategy: read pending pages from pages.db.
            PageDb           *queueDb   = new PageDb(workingDir);
            PageRepositoryDb *queueRepo = new PageRepositoryDb(*queueDb);
            queue = new GenPageQueue(alloc.pageTypeId,
                                     alloc.nonSvgImages,
                                     *queueRepo,
                                     *categoryTable,
                                     alloc.customInstructions,
                                     alloc.svgInstructions,
                                     jobsLimit,
                                     workingDir,
                                     alloc.imageInstructions,
                                     alloc.imageCountMin,
                                     alloc.imageCountMax);
            // queueDb / queueRepo intentionally leak — coroutines hold references
            // across suspension points; process exit cleans up.
            Q_UNUSED(queueDb)
            Q_UNUSED(queueRepo)
        }

        state->activeCount += alloc.sessionCount;

        *out << QStringLiteral("Strategy '%1': %2 session(s), %3 page(s) pending\n")
                    .arg(alloc.pageTypeId)
                    .arg(alloc.sessionCount)
                    .arg(queue->pendingCount());

        for (int s = 0; s < alloc.sessionCount; ++s) {
            runGenerationSession(queue, alloc.strategyId, alloc.endPermalink,
                                  engine, editingLangIndex,
                                  *categoryTable,
                                  state, state->activeCount - alloc.sessionCount + s,
                                  cli);
        }
    }

    out->flush();
    // app.exec() in main() drives the event loop until all sessions quit().
}
