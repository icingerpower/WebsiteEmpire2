#include "LauncherGeneration.h"

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
#include "website/pages/attributes/CategoryTable.h"
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
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTextStream>

#include <QCoro/QCoroProcess>
#include <QCoro/QCoroTask>

const QString LauncherGeneration::OPTION_NAME     = QStringLiteral("generation");
const QString LauncherGeneration::OPTION_SESSIONS = QStringLiteral("sessions");
const QString LauncherGeneration::OPTION_LIMIT    = QStringLiteral("limit");
const QString LauncherGeneration::OPTION_STRATEGY = QStringLiteral("strategy");

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
    int              jobsLimit     = -1; // -1 = unlimited
    int              activeCount   = 0;
    QTextStream     *out           = nullptr;
};

// ---------------------------------------------------------------------------
// Article pre-validation — mirrors processContentAndMetadata's checks without saving.
// ---------------------------------------------------------------------------

static bool isArticleValid(const QString &text)
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

// ---------------------------------------------------------------------------
// claude CLI helper — mirrors ClaudeRunner in WebsiteAspire; no API key needed
// ---------------------------------------------------------------------------

static QCoro::Task<QString> runClaudePrompt(QString prompt)
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

    // Claude's response is redirected to a file instead of a pipe.
    // For large articles (50 000+ chars), the OS pipe buffer (≈64 KB) fills up
    // before QProcess can drain it, causing the beginning of the output to be
    // silently dropped.  Writing to a file bypasses this limitation entirely.
    const QString outputPath = tempDir.path() + QStringLiteral("/output.txt");

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(QStringLiteral("claude"));
    process.setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                          QStringLiteral("--dangerously-skip-permissions")});
    process.setStandardInputFile(promptPath);
    process.setStandardOutputFile(outputPath);

    // Exactly two co_awaits, nothing between them — mirrors ClaudeRunner.
    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    // All error checks and output reading happen after both co_awaits.
    if (process.error() == QProcess::FailedToStart) {
        result = QStringLiteral("__ERROR__: claude executable not found in PATH");
    } else if (process.exitCode() != 0) {
        const QString errMsg = QString::fromUtf8(process.readAllStandardError()).trimmed();
        // Errors go to stderr; stdout (the output file) is not read on failure.
        const QString detail = !errMsg.isEmpty() ? errMsg
                             : QStringLiteral("exit code %1").arg(process.exitCode());
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
// Per-session coroutine
// ---------------------------------------------------------------------------

static QCoro::Task<void> runGenerationSession(GenPageQueue   *queue,
                                               QString         strategyId,
                                               AbstractEngine *engine,
                                               int             websiteIndex,
                                               CategoryTable  &categoryTable,
                                               GenRunState    *state,
                                               int             sessionIndex)
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

    while (!g_stopRequested && queue->hasNext()) {
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

        // ---- Smart retry: ContentReady / MainImageReady pages ----------------
        // These pages already have saved content; skip content regeneration and
        // run only the social-media second pass.  The SVG is loaded from
        // images.db if available; otherwise it is re-generated from the existing
        // article text (1_text from page_data) without touching the content.
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
                    const QString svgPrompt = queue->buildSvgPrompt(ref, page.permalink, lang);
                    const QString svgResp   = co_await runClaudePrompt(svgPrompt);
                    if (svgResp.startsWith(QStringLiteral("__ERROR__"))) {
                        *(state->out) << QStringLiteral("[S%1] WARN (SVG regen): %2\n")
                                             .arg(sNum).arg(svgResp);
                        state->out->flush();
                        continue;
                    }
                    static const QRegularExpression reSvgOpenR(
                        QStringLiteral("<svg[\\s>]"), QRegularExpression::CaseInsensitiveOption);
                    const auto mr    = reSvgOpenR.match(svgResp);
                    const int  sS    = mr.hasMatch() ? mr.capturedStart() : -1;
                    const int  sE    = svgResp.lastIndexOf(QStringLiteral("</svg>"));
                    if (sS < 0 || sE < sS) {
                        *(state->out) << QStringLiteral("[S%1] WARN (SVG regen): no valid SVG for %2\n")
                                             .arg(sNum).arg(ref.fileName);
                        state->out->flush();
                        continue;
                    }
                    const QString svgContent = svgResp.mid(sS, sE + 6 - sS);
                    imageWriter.writeSvg(svgContent.toUtf8(), domain, ref.fileName);
                    retrySvg = svgContent;
                    *(state->out) << QStringLiteral("[S%1] SVG regenerated: %2\n")
                                         .arg(sNum).arg(ref.fileName);
                    state->out->flush();
                    break;
                }
            }

            // Run second pass.
            auto retryPageType = AbstractPageType::createForTypeId(page.typeId, categoryTable);
            const bool svgExpectedR = queue->wantsSvgImage()
                                      && retryPageType
                                      && retryPageType->hasSvg();

            if (!retrySvg.isEmpty() && !g_stopRequested) {
                pageRepo.setGenerationState(pageId, PageGenerationState::MainImageReady);
                if (retryPageType) {
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

                            const QString svgRaw = co_await runClaudePrompt(prompts.at(vi));
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

                            imageWriter.writeQImage(img, domain, webpFilename);
                            const QString dataKey =
                                QStringLiteral("%1_").arg(bIdx) + secBloc->variantDataKey(vi);
                            retryPageData.insert(dataKey, webpFilename);
                            anyVariantSaved = true;

                            *(state->out) << QStringLiteral("[S%1] 2nd pass WebP saved (retry): %2\n")
                                                 .arg(sNum).arg(webpFilename);
                            state->out->flush();
                        }
                        if (anyVariantSaved) {
                            pageRepo.saveData(pageId, retryPageData);
                        }
                    }
                }
            }

            if (!svgExpectedR || !retrySvg.isEmpty()) {
                pageRepo.setGenerationState(pageId, PageGenerationState::Complete);
                state->jobsCompleted.fetch_add(1);
                *(state->out) << QStringLiteral("[S%1] OK (retry): %2\n")
                                     .arg(sNum).arg(page.permalink);
            } else {
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
        bool contentValid = false;

        for (int attempt = 1; attempt <= kMaxContentAttempts; ++attempt) {
            if (g_stopRequested) {
                break;
            }

            *(state->out) << QStringLiteral("[S%1] Prompt ready (%2 chars) — launching Claude (content, attempt %3/%4)...\n")
                                 .arg(sNum).arg(contentPrompt.size()).arg(attempt).arg(kMaxContentAttempts);
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

            const QString raw = co_await runClaudePrompt(prompt);

            if (raw.startsWith(QStringLiteral("__ERROR__"))) {
                *(state->out) << QStringLiteral("[S%1] FAIL (content attempt %2): %3\n")
                                     .arg(sNum).arg(attempt).arg(raw);
                state->out->flush();
                break; // API/process error — no point retrying
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
            continue; // no page row was created — nothing to clean up
        }

        // ---- Call 1.5: inject missing SVG IMGFIX if the strategy needs one ---
        if (!g_stopRequested && queue->wantsSvgImage() && !GenPageQueue::hasSvgImgFix(articleText)) {
            *(state->out) << QStringLiteral("[S%1] SVG IMGFIX missing — running repair (1.5/2: SVG ref)...\n")
                                 .arg(sNum);
            state->out->flush();

            const QString repairPrompt =
                queue->buildSvgRepairPrompt(page, articleText, lang);
            const QString repairResponse = co_await runClaudePrompt(repairPrompt);

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

        *(state->out) << QStringLiteral("[S%1] Content done: %2 (%3 chars) — launching Claude (2/2: metadata)...\n")
                             .arg(sNum).arg(page.permalink).arg(articleText.size());
        state->out->flush();

        // ---- Call 2: metadata JSON from the article -------------------------
        // Skip the metadata call (save content only) if the user pressed Ctrl+C.
        QString metadataJson;
        if (!g_stopRequested) {
            const QString metadataPrompt = queue->buildMetadataPrompt(page, articleText);
            metadataJson = co_await runClaudePrompt(metadataPrompt);
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

            const QList<GenPageQueue::ImgFixRef> imgRefs =
                GenPageQueue::parseImgFixRefs(articleText);
            const QString domain = engine->data(
                engine->index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();
            const QString lang = engine->getLangCode(websiteIndex);
            QString sourceSvgForSocialMedia; // first SVG content — seed for second pass
            for (const auto &ref : std::as_const(imgRefs)) {
                if (g_stopRequested) {
                    break;
                }
                if (!ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                    continue;
                }
                *(state->out) << QStringLiteral("[S%1] Generating SVG: %2...\n")
                                     .arg(sNum).arg(ref.fileName);
                state->out->flush();

                const QString svgPrompt = queue->buildSvgPrompt(ref, page.permalink, lang);
                const QString svgResponse = co_await runClaudePrompt(svgPrompt);

                if (svgResponse.startsWith(QStringLiteral("__ERROR__"))) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — %3\n")
                                         .arg(sNum).arg(ref.fileName, svgResponse);
                    state->out->flush();
                    continue;
                }

                // Match <svg as a real XML element: must be followed by whitespace or '>'.
                // A bare indexOf("<svg") would match text like `<svg` inside backtick
                // explanations that Claude sometimes prepends before the actual SVG.
                static const QRegularExpression reSvgOpen(
                    QStringLiteral("<svg[\\s>]"),
                    QRegularExpression::CaseInsensitiveOption);
                const auto svgOpenMatch = reSvgOpen.match(svgResponse);
                const int svgStart = svgOpenMatch.hasMatch() ? svgOpenMatch.capturedStart() : -1;
                const int svgEnd   = svgResponse.lastIndexOf(QStringLiteral("</svg>"));
                if (svgStart < 0) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — no <svg> element in response (%3 chars): %4\n")
                                         .arg(sNum).arg(ref.fileName)
                                         .arg(svgResponse.size())
                                         .arg(svgResponse.left(120));
                    state->out->flush();
                    continue;
                }
                if (svgEnd < svgStart) {
                    *(state->out) << QStringLiteral("[S%1] WARN (SVG): %2 — SVG truncated (no </svg>, %3 chars) — response likely hit output token limit\n")
                                         .arg(sNum).arg(ref.fileName).arg(svgResponse.size());
                    state->out->flush();
                    continue;
                }

                const QString svgContent = svgResponse.mid(svgStart, svgEnd + 6 - svgStart);
                imageWriter.writeSvg(svgContent.toUtf8(), domain, ref.fileName);
                if (sourceSvgForSocialMedia.isEmpty()) {
                    sourceSvgForSocialMedia = svgContent;
                }
                *(state->out) << QStringLiteral("[S%1] SVG saved: %2\n")
                                     .arg(sNum).arg(ref.fileName);
                state->out->flush();
            }

            // ---- Second pass: social media image variants -------------------
            if (!g_stopRequested && !sourceSvgForSocialMedia.isEmpty()) {
                pageRepo.setGenerationState(pageId, PageGenerationState::MainImageReady);

                if (pageType) {
                    QHash<QString, QString> pageData = pageRepo.loadData(pageId);
                    pageType->load(pageData);

                    const QList<const AbstractPageBloc *> &blocs = pageType->getPageBlocs();
                    for (int blocIdx = 0; blocIdx < blocs.size() && !g_stopRequested; ++blocIdx) {
                        const AbstractPageBloc *bloc = blocs.at(blocIdx);
                        if (!bloc->isSecondTimeGeneration()) {
                            continue;
                        }
                        const auto *secBloc =
                            static_cast<const AbstractSecondaryPageBloc *>(bloc);
                        const QList<QString> prompts =
                            secBloc->buildSecondPassPrompts(sourceSvgForSocialMedia, page);
                        const QList<AbstractSocialMedia::ImageSize> sizes =
                            secBloc->requiredImageSizes();

                        bool anyVariantSaved = false;
                        for (int vi = 0; vi < prompts.size() && !g_stopRequested; ++vi) {
                            const AbstractSocialMedia::ImageSize imgSize = sizes.at(vi);
                            const QSize dims = AbstractSocialMedia::imageSizeDimensions(imgSize);
                            const QString &webpSuffix = AbstractSocialMedia::webpSuffix(imgSize);

                            QString slug = page.permalink;
                            while (slug.startsWith(QLatin1Char('/'))) {
                                slug.remove(0, 1);
                            }
                            const QString webpFilename = slug + webpSuffix;

                            *(state->out) << QStringLiteral("[S%1] 2nd pass SVG (bloc %2, variant %3/%4): %5...\n")
                                                 .arg(sNum).arg(blocIdx).arg(vi + 1)
                                                 .arg(prompts.size()).arg(webpFilename);
                            state->out->flush();

                            const QString svgRaw = co_await runClaudePrompt(prompts.at(vi));
                            if (svgRaw.startsWith(QStringLiteral("__ERROR__"))) {
                                *(state->out) << QStringLiteral("[S%1] WARN (2nd pass): %2\n")
                                                     .arg(sNum).arg(svgRaw);
                                state->out->flush();
                                continue;
                            }

                            QString validatedSvg;
                            try {
                                validatedSvg = secBloc->validateSecondPassResult(svgRaw, vi);
                            } catch (...) {
                                *(state->out) << QStringLiteral(
                                    "[S%1] WARN (2nd pass): SVG validation failed for variant %2\n")
                                    .arg(sNum).arg(vi);
                                state->out->flush();
                                continue;
                            }

                            QSvgRenderer renderer(validatedSvg.toUtf8());
                            if (!renderer.isValid()) {
                                *(state->out) << QStringLiteral(
                                    "[S%1] WARN (2nd pass): SVG not renderable for variant %2\n")
                                    .arg(sNum).arg(vi);
                                state->out->flush();
                                continue;
                            }
                            QImage img(dims, QImage::Format_ARGB32);
                            img.fill(Qt::white);
                            QPainter painter(&img);
                            renderer.render(&painter);
                            painter.end();

                            imageWriter.writeQImage(img, domain, webpFilename);

                            const QString dataKey = QStringLiteral("%1_").arg(blocIdx)
                                                    + secBloc->variantDataKey(vi);
                            pageData.insert(dataKey, webpFilename);
                            anyVariantSaved = true;

                            *(state->out) << QStringLiteral("[S%1] 2nd pass WebP saved: %2\n")
                                                 .arg(sNum).arg(webpFilename);
                            state->out->flush();
                        }

                        if (anyVariantSaved) {
                            pageRepo.saveData(pageId, pageData);
                        }
                    }
                }
            }
            // Only mark Complete when SVG requirements are satisfied.  If SVG
            // was expected (strategy + page type) but none was generated, leave
            // the page in ContentReady so the next run retries SVG generation.
            if (!svgExpected || !sourceSvgForSocialMedia.isEmpty()) {
                pageRepo.setGenerationState(pageId, PageGenerationState::Complete);
            } else {
                *(state->out) << QStringLiteral(
                    "[S%1] WARN: %2 — SVG expected but not generated; "
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
    }

    if (g_stopRequested) {
        *(state->out) << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
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

    int     numSessions  = 1;
    int     jobsLimit    = -1;
    QString strategyFilter; // empty = all priority-1 strategies

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
        } else if (arg == QStringLiteral("--") + OPTION_STRATEGY) {
            strategyFilter = args.at(i + 1);
        }
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
    for (int i = 0; i < engine->rowCount(); ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == editingLang) {
            primaryDomain    = engine->data(
                engine->index(i, AbstractEngine::COL_DOMAIN)).toString();
            editingLangIndex = i;
            break;
        }
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
        info.primaryAttrId      = strategyTable->primaryAttrIdForRow(row);
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

            if (!dbPath.isEmpty()) {
                const QString connName = QStringLiteral("gen_src_") + info.strategyId;
                QString nameColumn;
                QStringList names;
                {
                    QSqlDatabase srcDb = QSqlDatabase::addDatabase(
                        QStringLiteral("QSQLITE"), connName);
                    srcDb.setDatabaseName(dbPath);
                    if (srcDb.open()) {
                        QSqlQuery pq(srcDb);
                        if (pq.exec(QStringLiteral("PRAGMA table_info(records)"))) {
                            while (pq.next()) {
                                const QString col = pq.value(1).toString();
                                if (col != QStringLiteral("id") && nameColumn.isEmpty()) {
                                    nameColumn = col;
                                }
                            }
                        }
                        if (!nameColumn.isEmpty()) {
                            QSqlQuery q(srcDb);
                            q.exec(QStringLiteral("SELECT ") + nameColumn
                                   + QStringLiteral(" FROM records ORDER BY id"));
                            while (q.next()) {
                                const QString v = q.value(0).toString().trimmed();
                                if (!v.isEmpty()) {
                                    names << v;
                                }
                            }
                        }
                    }
                }
                QSqlDatabase::removeDatabase(connName);

                for (const QString &name : std::as_const(names)) {
                    QString slug = name.toLower();
                    slug.replace(reNonAlnum, QStringLiteral("-"));
                    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
                    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
                    if (slug.isEmpty()) { continue; }

                    const QString permalink = QLatin1Char('/') + slug;
                    if (allExistingPermalinks.contains(permalink)) { continue; }

                    PageRecord vp;
                    vp.id        = 0;
                    vp.typeId    = info.pageTypeId;
                    vp.permalink = permalink;
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

        // Prepend ContentReady / MainImageReady pages of this type so they are
        // retried by the smart second-pass path in runGenerationSession.  These
        // pages already have saved content and (usually) an existing SVG in
        // images.db; the session loads the SVG and runs only the second pass
        // without touching the article text.
        {
            const QList<PageRecord> crPages =
                schedRepo.findByGenerationState(info.pageTypeId,
                                               PageGenerationState::ContentReady);
            const QList<PageRecord> mirPages =
                schedRepo.findByGenerationState(info.pageTypeId,
                                               PageGenerationState::MainImageReady);

            if (!crPages.isEmpty() || !mirPages.isEmpty()) {
                // For classic strategies that didn't build a virtualPages list,
                // also harvest Pending pages from pages.db so they still get
                // processed alongside the retry pages.
                const bool isClassicStrategy =
                    !virtualPagesByStrategyId.contains(info.strategyId);
                QList<PageRecord> &vp = virtualPagesByStrategyId[info.strategyId];
                if (isClassicStrategy) {
                    const QList<PageRecord> pending =
                        schedRepo.findPendingByTypeId(info.pageTypeId);
                    vp.append(pending);
                }
                // MainImageReady first (only second pass needed), then ContentReady.
                for (const PageRecord &p : std::as_const(mirPages)) {
                    vp.prepend(p);
                }
                for (const PageRecord &p : std::as_const(crPages)) {
                    vp.prepend(p);
                }
                info.pendingCountOverride = vp.size();
                *out << QStringLiteral("  → %1 retry page(s) (ContentReady/MainImageReady) queued for %2\n")
                            .arg(crPages.size() + mirPages.size())
                            .arg(info.pageTypeId);
                out->flush();
            }
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
    state->jobsLimit = jobsLimit;
    state->out       = out;

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
                                     alloc.customInstructions);
        } else {
            // Classic strategy: read pending pages from pages.db.
            PageDb           *queueDb   = new PageDb(workingDir);
            PageRepositoryDb *queueRepo = new PageRepositoryDb(*queueDb);
            queue = new GenPageQueue(alloc.pageTypeId,
                                     alloc.nonSvgImages,
                                     *queueRepo,
                                     *categoryTable,
                                     alloc.customInstructions,
                                     jobsLimit);
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
            runGenerationSession(queue, alloc.strategyId,
                                  engine, editingLangIndex,
                                  *categoryTable,
                                  state, state->activeCount - alloc.sessionCount + s);
        }
    }

    out->flush();
    // app.exec() in main() drives the event loop until all sessions quit().
}
