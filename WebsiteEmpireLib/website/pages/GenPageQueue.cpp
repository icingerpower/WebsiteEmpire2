#include "GenPageQueue.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/shortcodes/AbstractShortCodeImage.h"

#include "aicli/AbstractCli.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

GenPageQueue::GenPageQueue(const QString   &pageTypeId,
                            bool             nonSvgImages,
                            IPageRepository &pageRepo,
                            CategoryTable   &categoryTable,
                            const QString   &customInstructions,
                            const QString   &svgInstructions,
                            int              limit,
                            const QDir      &workingDir,
                            const QString   &imageInstructions,
                            int              imageCountMin,
                            int              imageCountMax)
    : m_pageTypeId(pageTypeId)
    , m_nonSvgImages(nonSvgImages)
    , m_customInstructions(customInstructions)
    , m_svgInstructions(svgInstructions)
    , m_imageInstructions(imageInstructions)
    , m_imageCountMin(imageCountMin)
    , m_imageCountMax(imageCountMax)
    , m_categoryTable(categoryTable)
    , m_workingDir(workingDir)
{
    m_pending = pageRepo.findPendingByTypeId(pageTypeId);

    if (limit >= 0 && m_pending.size() > limit) {
        m_pending = m_pending.mid(0, limit);
    }
}

GenPageQueue::GenPageQueue(const QString          &pageTypeId,
                            bool                    nonSvgImages,
                            const QList<PageRecord> &virtualPages,
                            CategoryTable          &categoryTable,
                            const QString          &customInstructions,
                            const QString          &svgInstructions,
                            const QDir             &workingDir,
                            const QString          &imageInstructions,
                            int                     imageCountMin,
                            int                     imageCountMax)
    : m_pageTypeId(pageTypeId)
    , m_nonSvgImages(nonSvgImages)
    , m_customInstructions(customInstructions)
    , m_svgInstructions(svgInstructions)
    , m_imageInstructions(imageInstructions)
    , m_imageCountMin(imageCountMin)
    , m_imageCountMax(imageCountMax)
    , m_categoryTable(categoryTable)
    , m_workingDir(workingDir)
    , m_pending(virtualPages)
{
}

// ---- Queue interface --------------------------------------------------------

bool GenPageQueue::hasNext() const
{
    return m_cursor < m_pending.size();
}

const PageRecord &GenPageQueue::peekNext() const
{
    return m_pending.at(m_cursor);
}

void GenPageQueue::advance()
{
    ++m_cursor;
}

int GenPageQueue::pendingCount() const
{
    return m_pending.size() - m_cursor;
}

// ---- Prompt building --------------------------------------------------------

QString GenPageQueue::buildStep1Prompt(const PageRecord &page,
                                        AbstractEngine   &engine,
                                        int               websiteIndex,
                                        const QString    &extraContext) const
{
    const QString lang = engine.getLangCode(websiteIndex);
    const QString effectiveLang = lang.isEmpty() ? page.lang : lang;

    QString prompt = QStringLiteral(
               "Write comprehensive, high-quality content for a web page about the topic "
               "described by this permalink: %1\n\n"
               "Page type : %2\n"
               "Language  : %3\n"
               "Images    : %4\n\n"
               "Write freely and naturally. Cover all important aspects of the topic with "
               "relevant, engaging content. Include a title, introduction, main body with "
               "key information, and a conclusion. Write entirely in %3.\n"
               "%5")
        .arg(page.permalink,
             m_pageTypeId,
             effectiveLang,
             m_nonSvgImages ? QStringLiteral("include non-SVG images")
                            : QStringLiteral("no images"),
             // Deliberately NOT "use relative paths like /images/foo.jpg": that
             // contradicted the [IMGFIX] rules below (which specify a bare
             // fileName="image.jpg") and made the AI alternate between the two
             // spellings run to run, silently breaking every image on a page
             // whenever it chose the path-prefixed form. Bare file name only.
             m_nonSvgImages
                 ? QStringLiteral("Give each image a bare file name only "
                                  "(e.g. \"foo.jpg\") — never a path or leading slash.\n")
                 : QString());

    if (!m_customInstructions.isEmpty()) {
        prompt += QStringLiteral("\n\nAdditional instructions:\n");
        QString instructions = m_customInstructions;
        instructions.replace(QStringLiteral("[TOPIC]"), page.permalink);
        prompt += instructions;
    }

    if (!extraContext.isEmpty()) {
        prompt += QStringLiteral("\n\nImprovement context:\n");
        prompt += extraContext;
    }

    return prompt;
}

QString GenPageQueue::buildStep2Prompt() const
{
    const QHash<QString, QString> &schema = _schema();
    const QHash<QString, QString> &clues  = _aiKeyClues();

    QJsonObject skeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        skeleton[it.key()] = clues.value(it.key(), it.value());
    }
    const QString schemaJson = QString::fromUtf8(
        QJsonDocument(skeleton).toJson(QJsonDocument::Indented));

    return QStringLiteral(
        "Now clean and format your previous response into the JSON structure below.\n"
        "Apply ALL of the following rules to every text value before saving it.\n\n"

        "── CLEANING ─────────────────────────────────────────────────────────────\n"
        "• Remove all meta-commentary: sentences that reference the prompt, the\n"
        "  writing task, or yourself (e.g. \"Here is the content:\", \"I'll cover…\",\n"
        "  \"Note:\", \"As requested…\", \"This article…\"). Pure content only.\n"
        "• Remove all raw HTML tags (<b>, <strong>, <em>, <i>, <a>, <img>, <p>,\n"
        "  <h1>…<h6>, <br>, etc.). Replace them with shortcodes (see below) or\n"
        "  plain text. No HTML may remain in any value.\n\n"

        "── SHORTCODES ───────────────────────────────────────────────────────────\n"
        "Use ONLY the shortcodes below. Do NOT invent other tags.\n\n"
        "Headings (use levels 2–4 for sections, never level 1 inside body text):\n"
        "  [TITLE level=\"2\"]Section heading[/TITLE]\n\n"
        "Bold (key terms, important facts):\n"
        "  [BOLD]important text[/BOLD]\n\n"
        "Italic (technical terms, titles of works, light emphasis):\n"
        "  [ITALIC]emphasized text[/ITALIC]\n\n"
        "Links — use LINKFIX for every hyperlink:\n"
        "  [LINKFIX url=\"https://example.com\"]anchor text[/LINKFIX]\n"
        "  [LINKFIX url=\"https://example.com\" rel=\"nofollow\"]anchor text[/LINKFIX]\n"
        "  RULES:\n"
        "  • Only use URLs that appeared verbatim in your previous response.\n"
        "  • Do NOT invent or guess URLs. If no real URL is known, write the\n"
        "    reference as plain text (e.g. \"According to the WHO report (2023)\").\n"
        "  • Study citations written as [1], [2], etc. must be kept ONLY when a\n"
        "    real URL is available; wrap as:\n"
        "    [LINKFIX url=\"https://...\"]source title [1][/LINKFIX]\n"
        "    Otherwise remove the bracketed number entirely.\n\n"
        "Images — use IMGFIX for every image reference:\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"image.jpg\" alt=\"description\"][/IMGFIX]\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"diagram.svg\" alt=\"description\"\n"
        "          caption=\"short visible caption\"][/IMGFIX]\n"
        "  RULES:\n"
        "  • Place images at the most relevant position in the content flow\n"
        "    (not all grouped at the top or bottom).\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • caption is optional — a short sentence displayed under the image.\n"
        "  • SVG images are fully supported; use the same IMGFIX syntax. An article\n"
        "    may contain more than one SVG when each covers a genuinely distinct,\n"
        "    useful visual — do not pad the article with redundant diagrams.\n"
        "  • If your previous response contained inline SVG code (<svg>…</svg>),\n"
        "    replace it with an IMGFIX shortcode referencing a .svg file name.\n\n"

        "── JSON OUTPUT ──────────────────────────────────────────────────────────\n"
        "Fill ALL values in the structure below with the cleaned, shortcode-formatted\n"
        "content from your previous response. Do NOT change any key.\n\n"
        "%1\n\n"
        "Return ONLY a valid JSON object — no markdown fences, no explanation.")
        .arg(schemaJson);
}

QString GenPageQueue::buildCombinedPrompt(const PageRecord &page,
                                           AbstractEngine   &engine,
                                           int               websiteIndex,
                                           const QString    &extraContext) const
{
    const QString lang = engine.getLangCode(websiteIndex);
    const QString effectiveLang = lang.isEmpty() ? page.lang : lang;

    // ---- Part 1: topic, page type, language, images (mirrors buildStep1Prompt
    //      but replaces "Write freely" with "write AND output as JSON") --------

    QString prompt = QStringLiteral(
               "Write comprehensive, high-quality content for a web page about the topic "
               "described by this permalink: %1\n\n"
               "Page type : %2\n"
               "Language  : %3\n"
               "Images    : %4\n\n"
               "Write the content and output it DIRECTLY as the JSON object below — "
               "no preamble, no markdown. Cover all important aspects of the topic with "
               "relevant, engaging content. Include a title, introduction, main body with "
               "key information, and a conclusion. Write entirely in %3.\n"
               "%5")
        .arg(page.permalink,
             m_pageTypeId,
             effectiveLang,
             m_nonSvgImages ? QStringLiteral("include non-SVG images")
                            : QStringLiteral("no images"),
             // Deliberately NOT "use relative paths like /images/foo.jpg": that
             // contradicted the [IMGFIX] rules below (which specify a bare
             // fileName="image.jpg") and made the AI alternate between the two
             // spellings run to run, silently breaking every image on a page
             // whenever it chose the path-prefixed form. Bare file name only.
             m_nonSvgImages
                 ? QStringLiteral("Give each image a bare file name only "
                                  "(e.g. \"foo.jpg\") — never a path or leading slash.\n")
                 : QString());

    if (!m_customInstructions.isEmpty()) {
        prompt += QStringLiteral("\n\nAdditional instructions:\n");
        QString instructions = m_customInstructions;
        instructions.replace(QStringLiteral("[TOPIC]"), page.permalink);
        prompt += instructions;
    }

    if (!extraContext.isEmpty()) {
        prompt += QStringLiteral("\n\nImprovement context:\n");
        prompt += extraContext;
    }

    // ---- Part 2: cleaning rules, shortcodes, JSON schema (mirrors
    //      buildStep2Prompt with rephrased intro lines) ------------------------

    const QHash<QString, QString> &schema = _schema();
    const QHash<QString, QString> &clues  = _aiKeyClues();

    QJsonObject skeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        skeleton[it.key()] = clues.value(it.key(), it.value());
    }
    const QString schemaJson = QString::fromUtf8(
        QJsonDocument(skeleton).toJson(QJsonDocument::Indented));

    prompt += QStringLiteral(
        "\n\nOutput the content DIRECTLY as a JSON object. "
        "Apply ALL of the following rules to every text value:\n\n"

        "── CLEANING ─────────────────────────────────────────────────────────────\n"
        "• Remove all meta-commentary: sentences that reference the prompt, the\n"
        "  writing task, or yourself (e.g. \"Here is the content:\", \"I'll cover…\",\n"
        "  \"Note:\", \"As requested…\", \"This article…\"). Pure content only.\n"
        "• Remove all raw HTML tags (<b>, <strong>, <em>, <i>, <a>, <img>, <p>,\n"
        "  <h1>…<h6>, <br>, etc.). Replace them with shortcodes (see below) or\n"
        "  plain text. No HTML may remain in any value.\n\n"

        "── SHORTCODES ───────────────────────────────────────────────────────────\n"
        "Use ONLY the shortcodes below. Do NOT invent other tags.\n\n"
        "Headings (use levels 2–4 for sections, never level 1 inside body text):\n"
        "  [TITLE level=\"2\"]Section heading[/TITLE]\n\n"
        "Bold (key terms, important facts):\n"
        "  [BOLD]important text[/BOLD]\n\n"
        "Italic (technical terms, titles of works, light emphasis):\n"
        "  [ITALIC]emphasized text[/ITALIC]\n\n"
        "Links — use LINKFIX for every hyperlink:\n"
        "  [LINKFIX url=\"https://example.com\"]anchor text[/LINKFIX]\n"
        "  [LINKFIX url=\"https://example.com\" rel=\"nofollow\"]anchor text[/LINKFIX]\n"
        "  RULES:\n"
        "  • Only use URLs you are certain are real and correct.\n"
        "  • Do NOT invent or guess URLs. If no real URL is known, write the\n"
        "    reference as plain text (e.g. \"According to the WHO report (2023)\").\n"
        "  • Study citations written as [1], [2], etc. must be kept ONLY when a\n"
        "    real URL is available; wrap as:\n"
        "    [LINKFIX url=\"https://...\"]source title [1][/LINKFIX]\n"
        "    Otherwise remove the bracketed number entirely.\n\n"
        "Images — use IMGFIX for every image reference:\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"image.jpg\" alt=\"description\"][/IMGFIX]\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"diagram.svg\" alt=\"description\"\n"
        "          caption=\"short visible caption\"][/IMGFIX]\n"
        "  RULES:\n"
        "  • Place images at the most relevant position in the content flow\n"
        "    (not all grouped at the top or bottom).\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • caption is optional — a short sentence displayed under the image.\n"
        "  • SVG images are fully supported; use the same IMGFIX syntax. An article\n"
        "    may contain more than one SVG when each covers a genuinely distinct,\n"
        "    useful visual — do not pad the article with redundant diagrams.\n"
        "  • If you include inline SVG code (<svg>…</svg>), replace it with an\n"
        "    IMGFIX shortcode referencing a .svg file name instead.\n\n"

        "── JSON OUTPUT ──────────────────────────────────────────────────────────\n"
        "Fill ALL values in the structure below with the content you write. "
        "Do NOT change any key.\n\n"
        "%1\n\n"
        "Return ONLY a valid JSON object — no markdown fences, no explanation.")
        .arg(schemaJson);

    return prompt;
}

// ---- Two-call split: content + metadata ------------------------------------

QString GenPageQueue::buildContentPrompt(const PageRecord &page,
                                          AbstractEngine   &engine,
                                          int               websiteIndex,
                                          const QString    &extraContext) const
{
    const QString lang = engine.getLangCode(websiteIndex);
    const QString effectiveLang = lang.isEmpty() ? page.lang : lang;

    QString prompt = QStringLiteral(
               "Write comprehensive, high-quality content for a web page about the topic "
               "described by this permalink: %1\n\n"
               "Page type : %2\n"
               "Language  : %3\n"
               "Images    : %4\n\n"
               "Write freely and naturally. Cover all important aspects of the topic with "
               "relevant, engaging content. Include a title, introduction, main body with "
               "key information, and a conclusion. Write entirely in %3.\n"
               "%5")
        .arg(page.permalink,
             m_pageTypeId,
             effectiveLang,
             m_nonSvgImages ? QStringLiteral("include non-SVG images")
                            : QStringLiteral("no images"),
             // Deliberately NOT "use relative paths like /images/foo.jpg": that
             // contradicted the [IMGFIX] rules below (which specify a bare
             // fileName="image.jpg") and made the AI alternate between the two
             // spellings run to run, silently breaking every image on a page
             // whenever it chose the path-prefixed form. Bare file name only.
             m_nonSvgImages
                 ? QStringLiteral("Give each image a bare file name only "
                                  "(e.g. \"foo.jpg\") — never a path or leading slash.\n")
                 : QString());

    if (wantsRasterImage()) {
        prompt += QStringLiteral(
            "\nRaster image generation is enabled for this strategy: each [IMGFIX] "
            "image reference below will be generated as a real (non-SVG) photo by AI. ");
        if (m_imageCountMin > 0 && m_imageCountMax > 0) {
            prompt += QStringLiteral(
                "Include between %1 and %2 distinct [IMGFIX] raster images, one per "
                "idea/section.\n")
                .arg(m_imageCountMin).arg(m_imageCountMax);
        } else {
            prompt += QStringLiteral("\n");
        }
        prompt += m_imageInstructions;
        prompt += QStringLiteral("\n");
    }

    if (!m_customInstructions.isEmpty()) {
        prompt += QStringLiteral("\n\nAdditional instructions:\n");
        QString instructions = m_customInstructions;
        instructions.replace(QStringLiteral("[TOPIC]"), page.permalink);
        prompt += instructions;
    }

    if (!extraContext.isEmpty()) {
        prompt += QStringLiteral("\n\nImprovement context:\n");
        prompt += extraContext;
    }

    prompt += QStringLiteral(
        "\n\n"
        "── FORMATTING ───────────────────────────────────────────────────────────\n"
        "Use ONLY the shortcodes below. Do NOT use HTML tags.\n\n"
        "Headings (use levels 2–4 for sections, never level 1 inside body text):\n"
        "  [TITLE level=\"2\"]Section heading[/TITLE]\n\n"
        "Bold (key terms, important facts):\n"
        "  [BOLD]important text[/BOLD]\n\n"
        "Italic (technical terms, titles of works, light emphasis):\n"
        "  [ITALIC]emphasized text[/ITALIC]\n\n"
        "Links — use LINKFIX for every hyperlink:\n"
        "  [LINKFIX url=\"https://example.com\"]anchor text[/LINKFIX]\n"
        "  RULES:\n"
        "  • Only use URLs you are certain are real and correct.\n"
        "  • Do NOT invent or guess URLs. If no real URL is known, write the\n"
        "    reference as plain text (e.g. \"According to the WHO report (2023)\").\n\n"
        "Images — use IMGFIX for every image reference:\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"image.jpg\" alt=\"description\"][/IMGFIX]\n"
        "  [IMGFIX id=\"unique-slug\" fileName=\"diagram.svg\" alt=\"description\"\n"
        "          caption=\"short visible caption\"][/IMGFIX]\n"
        "  RULES:\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • Use fileName=\"name.svg\" for diagrams and charts; \"name.jpg\" for photos.\n"
        "  • caption is optional — a short sentence displayed under the image.\n"
        "    Omit it when the alt text already says enough.\n"
        "  • NEVER write raw SVG, HTML, or XML anywhere in the article body.\n"
        "  • Any diagram, chart, or illustration MUST be represented solely as an\n"
        "    [IMGFIX] shortcode — the SVG will be generated separately.\n\n"
        "ABSOLUTE RULES (violations will corrupt the output):\n"
        "  • The article body must contain NO raw HTML, SVG, or XML tags whatsoever.\n"
        "  • Only the shortcodes listed above are permitted.\n"
        "  • NEVER write Markdown syntax of any kind — no pipe-delimited tables\n"
        "    (\"| col | col |\"), no \"| --- | --- |\" separator rows, no \"**bold**\",\n"
        "    no \"[NEWLINE]\" or similar placeholder tokens for line breaks. If content\n"
        "    would naturally be a table, write it as plain sentences or as separate\n"
        "    [TITLE level=\"3\"] items instead — there is no table shortcode.\n"
        "  • The article must begin with [TITLE level=\"1\"]…[/TITLE] — the very first\n"
        "    characters must be the opening bracket of this shortcode.\n"
        "  • If the additional instructions above contain an SVG image section, insert\n"
        "    the [IMGFIX id=\"slug\" fileName=\"name.svg\" alt=\"...\"][/IMGFIX] shortcode(s)\n"
        "    it asks for — one required shortcode plus, when those instructions allow\n"
        "    it, a small number of additional distinct SVG shortcodes for genuinely\n"
        "    useful extra visuals. Give every shortcode a unique id and fileName, and\n"
        "    write its alt text to clearly describe THAT SPECIFIC visual (this alt is\n"
        "    the only brief the image will be generated from later). Do NOT write any\n"
        "    <svg>…</svg> code here — every SVG image is generated in a separate step.\n\n"
        "Return ONLY the article content — no preamble, no meta-commentary about the task.");

    return prompt;
}

QString GenPageQueue::buildMetadataPrompt(const PageRecord &page,
                                           const QString   &articleText) const
{
    const QHash<QString, QString> &schema = _schema();
    const QHash<QString, QString> &clues  = _aiKeyClues();

    // Text and hub identity are authored outside the metadata call. Social
    // metadata can live at index 1 on hub types, so do not exclude that index.
    // Substitute the AI hint as the value where one is available so Claude sees
    // field guidance inline (e.g. "Comma-separated IDs. Choose ONLY from: 1=X, 2=Y").
    QJsonObject metaSkeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        if (!it.key().endsWith(QStringLiteral("_text"))
            && it.key() != QStringLiteral("0_dimension")
            && it.key() != QStringLiteral("0_tag_value")) {
            metaSkeleton[it.key()] = clues.value(it.key(), it.value());
        }
    }
    const QString schemaJson = QString::fromUtf8(
        QJsonDocument(metaSkeleton).toJson(QJsonDocument::Indented));

    return QStringLiteral(
        "The following is an article for the web page: %1\n\n"
        "=== Article Content ===\n"
        "%2\n\n"
        "=== Task ===\n"
        "Fill the JSON structure below with metadata derived from the article above.\n"
        "Do NOT include the article body — only fill the fields shown.\n"
        "Do NOT change any key.\n\n"
        "%3\n\n"
        "Return ONLY a valid JSON object — no markdown fences, no explanation.")
        .arg(page.permalink, articleText, schemaJson);
}

bool GenPageQueue::processContentAndMetadata(int              pageId,
                                              const QString   &articleText,
                                              const QString   &metadataJson,
                                              IPageRepository &pageRepo)
{
    if (articleText.trimmed().isEmpty()) {
        return false;
    }

    // Start with the full schema (all keys empty) and fill in what we have.
    QHash<QString, QString> data = _schema();
    const auto existingData = pageRepo.loadData(pageId);
    for (auto it = existingData.cbegin(); it != existingData.cend(); ++it) {
        data[it.key()] = it.value();
    }

    // 1_text: the article body from the content call, sanitized.
    // Step 1: remove inline <svg>…</svg> blocks Claude may have embedded.
    // Step 2: if the result doesn't start with a shortcode, skip forward to the
    //         first [TITLE level="1"] (handles XML preambles and stray leading tags).
    //         The old reLeadingTag regex (DotMatchesEverythingOption) was removed
    //         because lazy .*? with that flag could silently consume the entire
    //         first half of the article when Claude opened with a large SVG block.
    static const QRegularExpression reSvgBlock(
        QStringLiteral("<svg\\b[^>]*>.*?</svg>"),
        QRegularExpression::DotMatchesEverythingOption
        | QRegularExpression::CaseInsensitiveOption);

    static const QString kExpectedStart = QStringLiteral("[TITLE level=\"1\"]");

    QString cleanText = articleText.trimmed();
    cleanText.remove(reSvgBlock);
    cleanText = cleanText.trimmed();

    // If the content doesn't start with the expected shortcode, seek it.
    // This handles non-shortcode preamble (e.g. XML declarations, stray text).
    if (!cleanText.startsWith(kExpectedStart)) {
        const int titlePos = cleanText.indexOf(kExpectedStart);
        if (titlePos > 0) {
            cleanText = cleanText.mid(titlePos);
        }
    }
    cleanText = cleanText.trimmed();

    // Normalize: if the model opened with [TITLE level="2"] (wrong level) as
    // the very first shortcode, promote it to level="1" — the position uniquely
    // identifies it as the article title regardless of which level Claude chose.
    static const QString kWrongLevel = QStringLiteral("[TITLE level=\"2\"]");
    if (cleanText.startsWith(kWrongLevel)) {
        cleanText = kExpectedStart + cleanText.mid(kWrongLevel.size());
    }

    // Reject articles that don't start with the mandatory title shortcode — this
    // catches both Claude disobeying the formatting rules and over-aggressive SVG
    // removal that wiped out the entire beginning.
    if (!cleanText.startsWith(kExpectedStart)) {
        qWarning() << "processContentAndMetadata: rejected page" << pageId
                   << "— does not start with [TITLE level=\"1\"]. First 200 chars:"
                   << cleanText.left(200);
        return false;
    }

    // Reject suspiciously short articles (likely truncated or empty strategies).
    if (cleanText.size() < 2000) {
        qWarning() << "processContentAndMetadata: rejected page" << pageId
                   << "— article too short:" << cleanText.size() << "chars";
        return false;
    }

    const QString textKey = m_pageTypeId == QStringLiteral("symptom_hub")
        ? QStringLiteral("0_text")
        : m_pageTypeId == QStringLiteral("fashion_tag_hub")
            ? QStringLiteral("3_text") : QStringLiteral("1_text");
    data[textKey] = cleanText;
    if (m_pageTypeId == QStringLiteral("symptom_hub")
        || m_pageTypeId == QStringLiteral("fashion_tag_hub")) {
        data[QStringLiteral("_taxonomyArticle")] = QStringLiteral("1");
    }

    // Metadata fields: parsed from the metadata JSON call.
    if (!metadataJson.isEmpty()) {
        const QHash<QString, QString> meta = _parseJson(metadataJson);
        for (auto it = meta.cbegin(); it != meta.cend(); ++it) {
            // Never overwrite 1_text with anything from the metadata call.
            if (it.key() != textKey && !it.key().endsWith(QStringLiteral("_text"))
                && it.key() != QStringLiteral("0_dimension")
                && it.key() != QStringLiteral("0_tag_value") && data.contains(it.key())) {
                data[it.key()] = it.value();
            }
        }
    }

    pageRepo.saveData(pageId, data);
    pageRepo.setGeneratedAt(pageId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return true;
}

// ---- Image helpers ----------------------------------------------------------

bool GenPageQueue::hasSvgImgFix(const QString &articleText)
{
    static const QRegularExpression re(
        QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"[^\"]+\\.svg\""),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(articleText).hasMatch();
}

int GenPageQueue::countRasterImgFixRefs(const QString &articleText)
{
    // Reuses parseImgFixRefs() rather than a second regex so this count can
    // never drift from the refs LauncherGeneration actually iterates.
    const QList<ImgFixRef> refs = parseImgFixRefs(articleText);
    int count = 0;
    for (const ImgFixRef &ref : std::as_const(refs)) {
        if (!ref.fileName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

bool GenPageQueue::wantsSvgImage() const
{
    return !m_svgInstructions.isEmpty();
}

bool GenPageQueue::wantsRasterImage() const
{
    return !m_imageInstructions.isEmpty();
}

QString GenPageQueue::rasterImageInstructions() const
{
    return m_imageInstructions;
}

int GenPageQueue::imageCountMin() const
{
    return m_imageCountMin;
}

int GenPageQueue::imageCountMax() const
{
    return m_imageCountMax;
}

GenPageQueue::SvgGenerationResult GenPageQueue::parseSvgDelimitedResponse(const QString &response)
{
    SvgGenerationResult result;

    static const QString kSvgMarker        = QStringLiteral("===SVG===");
    static const QString kInsertMarker     = QStringLiteral("===INSERT_AFTER===");
    static const QRegularExpression reSvgOpen(
        QStringLiteral("<svg[\\s>]"), QRegularExpression::CaseInsensitiveOption);

    const int svgMarkerPos = response.indexOf(kSvgMarker);
    if (svgMarkerPos >= 0) {
        // Delimiter format: extract everything between ===SVG=== and the next marker (or end).
        const int contentStart = svgMarkerPos + kSvgMarker.length();
        const int insertMarkerPos = response.indexOf(kInsertMarker, contentStart);
        const QString svgBlock = (insertMarkerPos >= 0)
            ? response.mid(contentStart, insertMarkerPos - contentStart).trimmed()
            : response.mid(contentStart).trimmed();

        const auto openMatch = reSvgOpen.match(svgBlock);
        const int svgStart   = openMatch.hasMatch() ? openMatch.capturedStart() : 0;
        const int svgEnd     = svgBlock.lastIndexOf(QStringLiteral("</svg>"));
        if (svgEnd >= svgStart) {
            result.svgCode = svgBlock.mid(svgStart, svgEnd + 6 - svgStart);
        }

        if (insertMarkerPos >= 0) {
            result.insertAfter = response.mid(
                insertMarkerPos + kInsertMarker.length()).trimmed();
        }
    } else {
        // Fallback: treat the whole response as a raw SVG (backward compatibility).
        const auto openMatch = reSvgOpen.match(response);
        const int svgStart   = openMatch.hasMatch() ? openMatch.capturedStart() : -1;
        const int svgEnd     = response.lastIndexOf(QStringLiteral("</svg>"));
        if (svgStart >= 0 && svgEnd >= svgStart) {
            result.svgCode = response.mid(svgStart, svgEnd + 6 - svgStart);
        }
    }

    return result;
}

QString GenPageQueue::buildSvgRepairPrompt(const PageRecord &page,
                                            const QString   &articleText,
                                            const QString   &lang) const
{
    return QStringLiteral(
               "The following article for the web page \"%1\" was generated correctly "
               "but is missing a required SVG summary image.\n\n"
               "=== Article ===\n"
               "%2\n\n"
               "=== Task ===\n"
               "Generate ONLY the single [IMGFIX] shortcode for the missing SVG summary "
               "table that belongs in this article. The shortcode must:\n"
               "• Use this exact format: [IMGFIX id=\"slug\" fileName=\"name.svg\" "
               "alt=\"description\"][/IMGFIX]\n"
               "• Have a short, descriptive id (kebab-case)\n"
               "• Have a fileName ending in .svg\n"
               "• Have an alt that clearly describes the table content in %3\n\n"
               "Return ONLY the [IMGFIX] shortcode — no preamble, no explanation, "
               "no surrounding text.")
        .arg(page.permalink, articleText, lang);
}

QString GenPageQueue::buildRasterCountRepairPrompt(const PageRecord &page,
                                                    const QString   &articleText,
                                                    const QString   &lang,
                                                    int              missing) const
{
    QString prompt = QStringLiteral(
                         "The following article for the web page \"%1\" is complete and "
                         "correct, but it contains too few outfit images.\n\n"
                         "=== Article ===\n"
                         "%2\n\n"
                         "=== Task ===\n"
                         "Write %3 ADDITIONAL outfit section(s) to append to this article, "
                         "each covering an outfit idea NOT already present above.\n\n"
                         "Each new section must follow exactly the same structure as the "
                         "existing sections:\n"
                         "• [TITLE level=\"3\"]An engaging, specific heading[/TITLE]\n"
                         "• At least 7 sentences of teaching/inspiring prose, written in %4\n"
                         "• Exactly one [IMGFIX id=\"slug\" fileName=\"name.jpg\" "
                         "alt=\"...\"][/IMGFIX] shortcode at the end of the section\n\n"
                         "Rules for the [IMGFIX] shortcode:\n"
                         "• id must be short, descriptive and kebab-case, and must not "
                         "duplicate any id already used above\n"
                         "• fileName must be a bare file name only (e.g. \"foo.jpg\") — "
                         "never a path and never a leading slash — ending in .jpg\n"
                         "• alt must start with \"Woman:\", \"Man:\" or \"Woman and man:\" "
                         "to state who appears, followed by the exact colors and garments "
                         "recommended in that section\n\n")
                     .arg(page.permalink, articleText)
                     .arg(missing)
                     .arg(lang);

    if (!m_imageInstructions.isEmpty()) {
        prompt += QStringLiteral("Image style requirements the alt text must be consistent with:\n")
                + m_imageInstructions
                + QStringLiteral("\n\n");
    }

    prompt += QStringLiteral(
        "Return ONLY the new section(s) — no preamble, no explanation, and do "
        "NOT repeat any part of the existing article.");

    return prompt;
}

QString GenPageQueue::insertImgFix(const QString &articleText, const QString &imgFixCode)
{
    // Find the start of the last [TITLE level="2"] heading to use as the
    // insertion point — the SVG summary sits naturally before the final section.
    static const QRegularExpression reTitle(
        QStringLiteral("\\[TITLE level=\"2\"\\]"));

    int lastPos = -1;
    auto it = reTitle.globalMatch(articleText);
    while (it.hasNext()) {
        lastPos = it.next().capturedStart();
    }

    if (lastPos < 0) {
        return articleText + QStringLiteral("\n\n") + imgFixCode;
    }
    return articleText.left(lastPos)
           + imgFixCode + QStringLiteral("\n\n")
           + articleText.mid(lastPos);
}

QString GenPageQueue::extractRelevantSection(const QString &articleText, const QString &imgFixId)
{
    if (imgFixId.isEmpty()) {
        return articleText;
    }

    // Locate this specific [IMGFIX id="imgFixId" ...] occurrence — ids are
    // unique per the content prompt's own rules, so this matches exactly one
    // tag when the AI followed instructions.
    const QRegularExpression reThisTag(
        QStringLiteral("\\[IMGFIX\\b[^\\]]*\\bid=\"%1\"[^\\]]*\\]")
            .arg(QRegularExpression::escape(imgFixId)),
        QRegularExpression::CaseInsensitiveOption);
    const auto tagMatch = reThisTag.match(articleText);
    if (!tagMatch.hasMatch()) {
        return articleText;
    }
    const int tagPos = tagMatch.capturedStart();

    // Single pass over every heading: the last one at-or-before tagPos is the
    // section start; the first one strictly after tagPos is the section end
    // (open-ended to the end of the article when there is no next heading).
    static const QRegularExpression reTitle(QStringLiteral(R"(\[TITLE level="\d+"\])"));

    int sectionStart = -1;
    int sectionEnd   = articleText.size();
    auto it = reTitle.globalMatch(articleText);
    while (it.hasNext()) {
        const int pos = it.next().capturedStart();
        if (pos <= tagPos) {
            sectionStart = pos;
        } else {
            sectionEnd = pos;
            break;
        }
    }

    if (sectionStart < 0) {
        return articleText;
    }
    return articleText.mid(sectionStart, sectionEnd - sectionStart);
}

QString GenPageQueue::fixSlugTitle(const QString &articleText,
                                     const QString &permalink,
                                     const QString &endPermalink)
{
    static const QRegularExpression reTitleBlock(
        QStringLiteral(R"(\[TITLE level="1"\](.*?)\[/TITLE\])"),
        QRegularExpression::DotMatchesEverythingOption);

    const auto titleMatch = reTitleBlock.match(articleText);
    if (!titleMatch.hasMatch()) {
        return articleText;
    }
    const QString titleText = titleMatch.captured(1);

    // Slug with and without the leading '/', so either form is matched.
    const QString slugWithSlash = permalink;
    const QString slugNoSlash   = permalink.startsWith(QLatin1Char('/'))
                                     ? permalink.mid(1) : permalink;
    if (slugNoSlash.isEmpty()) {
        return articleText;
    }

    int prefixLen = -1;
    if (titleText.startsWith(slugWithSlash, Qt::CaseInsensitive)) {
        prefixLen = slugWithSlash.size();
    } else if (titleText.startsWith(slugNoSlash, Qt::CaseInsensitive)) {
        prefixLen = slugNoSlash.size();
    }
    if (prefixLen < 0) {
        return articleText;
    }

    // Topic slug: the permalink without its leading '/' and, when endPermalink
    // is configured and the slug ends with "-<endPermalink>", without that
    // strategy-added suffix either — isolating the actual subject.
    QString topicSlug = slugNoSlash;
    if (!endPermalink.isEmpty()) {
        const QString suffix = QLatin1Char('-') + endPermalink;
        if (topicSlug.endsWith(suffix, Qt::CaseInsensitive)) {
            topicSlug.chop(suffix.size());
        }
    }
    if (topicSlug.isEmpty()) {
        return articleText;
    }

    // Title-case the topic slug: hyphens → spaces, capitalize each word.
    QStringList words = topicSlug.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    for (QString &word : words) {
        if (!word.isEmpty()) {
            word[0] = word[0].toUpper();
        }
    }
    const QString humanizedTopic = words.join(QLatin1Char(' '));

    // Drop only the whitespace immediately following the matched slug prefix.
    // A dash or colon right after it (e.g. "- 4 Genes..." or ": Subtitle") is
    // legitimate title punctuation the AI intended to keep, not slug noise.
    static const QRegularExpression reLeadingSpace(QStringLiteral(R"(^\s+)"));
    QString remainder = titleText.mid(prefixLen);
    remainder.remove(reLeadingSpace);

    const QString fixedTitle = remainder.isEmpty()
        ? humanizedTopic
        : humanizedTopic + QLatin1Char(' ') + remainder;

    QString result = articleText;
    result.replace(titleMatch.capturedStart(1), titleMatch.capturedLength(1), fixedTitle);
    return result;
}

QList<GenPageQueue::ImgFixRef> GenPageQueue::parseImgFixRefs(const QString &articleText)
{
    QList<ImgFixRef> result;

    static const QRegularExpression reTag(
        QStringLiteral(R"(\[IMGFIX\b([^\]]*)\])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reAttr(
        QStringLiteral("(\\w+)=\"([^\"]*)\""));

    auto tagIt = reTag.globalMatch(articleText);
    while (tagIt.hasNext()) {
        const auto tagMatch = tagIt.next();
        const QString attrs = tagMatch.captured(1);

        ImgFixRef ref;
        auto attrIt = reAttr.globalMatch(attrs);
        while (attrIt.hasNext()) {
            const auto attrMatch = attrIt.next();
            const QString &key = attrMatch.captured(1);
            const QString &val = attrMatch.captured(2);
            if (key == QStringLiteral("id")) {
                ref.id = val;
            } else if (key == QStringLiteral("fileName")) {
                // Normalised to a bare basename so the key a generated blob is
                // stored under in images.db always matches the one the rendered
                // <img src> asks for — see AbstractShortCodeImage::normalizedFileName().
                ref.fileName = AbstractShortCodeImage::normalizedFileName(val);
            } else if (key == QStringLiteral("alt")) {
                ref.alt = val;
            }
        }

        if (!ref.id.isEmpty() && !ref.fileName.isEmpty()) {
            result.append(ref);
        }
    }
    return result;
}

QString GenPageQueue::buildSvgPrompt(const ImgFixRef &ref,
                                      const QString   &articleText,
                                      const QString   &lang) const
{
    // Note: the image description (alt) is used instead of any permalink or disease
    // name to avoid policy-filter rejections on sensitive medical terminology.
    QString prompt = QStringLiteral(
                         "Create a standalone SVG image for the following web article.\n\n"
                         "Image id          : %1\n"
                         "Image filename    : %2\n"
                         "Image description : %3\n"
                         "Language          : %4\n\n")
                     .arg(ref.id, ref.fileName, ref.alt, lang);

    if (!articleText.isEmpty()) {
        prompt += QStringLiteral("Article content (for context — tailor the diagram to it):\n"
                                 "---\n")
                + articleText
                + QStringLiteral("\n---\n\n");
    }

    if (!m_svgInstructions.isEmpty()) {
        prompt += QStringLiteral("Strategy design requirements (follow these exactly):\n")
                + m_svgInstructions
                + QStringLiteral("\n\n");
    }

    prompt += QStringLiteral(
        "Output format — use EXACTLY these delimiters, nothing else:\n"
        "===SVG===\n"
        "<svg ...>...</svg>\n\n"
        "Technical requirements for the SVG:\n"
        "• Do NOT use any Write, Edit, or file-creation tools — output only as text.\n"
        "• Include a viewBox attribute; do NOT set a fixed pixel width/height on the root.\n"
        "• All text labels must be written in %1.\n"
        "• Make the image informative, visually clean, and self-contained.\n"
        "• Use only inline styles — no <style> blocks, no external CSS or fonts.\n"
        "• Use only web-safe fonts (Arial, Helvetica, sans-serif).\n"
        "• No JavaScript, no external references, no raster images embedded inside the SVG.\n"
        "• Keep the total SVG under 5000 characters. Show only the 6–8 most important rows\n"
        "  if a table would be longer. A truncated SVG is unusable.")
        .arg(lang);

    return prompt;
}

QString GenPageQueue::buildRasterImagePrompt(const ImgFixRef &ref,
                                              const QString   &articleText,
                                              const QString   &lang,
                                              const QString   &outputPath) const
{
    // Note: the image description (alt) is used instead of any permalink or topic
    // name, mirroring buildSvgPrompt(), to avoid policy-filter rejections and to
    // keep the per-image request focused on exactly what should appear in frame.
    QString prompt = QStringLiteral(
                         "Create a real (raster, photorealistic-capable) image for the "
                         "following web article.\n\n")
                     + QString::fromLatin1(RASTER_IMAGE_PROMPT_MARKER)
                     + QStringLiteral("\n")
                     + QStringLiteral(
                         "Image id          : %1\n"
                         "Image description : %2\n"
                         "Language          : %3\n"
                         "Output path       : %4\n\n")
                         .arg(ref.id, ref.alt, lang, outputPath);

    // Scoped to this image's own section (see extractRelevantSection()) rather
    // than the full article — a multi-section article must never let one
    // section's image be tailored to a DIFFERENT section's recommendation.
    const QString section = extractRelevantSection(articleText, ref.id);
    if (!section.isEmpty()) {
        prompt += QStringLiteral("Relevant article section (for context — tailor the image "
                                 "to THIS specific recommendation, not any other part of the "
                                 "article):\n"
                                 "---\n")
                + section
                + QStringLiteral("\n---\n\n");
    }

    if (!m_imageInstructions.isEmpty()) {
        prompt += QStringLiteral("Strategy style requirements (follow these exactly):\n")
                + m_imageInstructions
                + QStringLiteral("\n\n");
    }

    prompt += QStringLiteral(
        "Technical requirements:\n"
        "• Save the generated image to exactly this file path: %1\n"
        "• Do not print the image or any commentary as text — write the file, then stop.\n"
        "• The image must not contain any text, watermark, or logo.")
        .arg(outputPath);

    return prompt;
}

// ---- Reply processing -------------------------------------------------------

bool GenPageQueue::processReply(int             pageId,
                                 const QString  &responseText,
                                 IPageRepository &pageRepo)
{
    const QHash<QString, QString> parsed = _parseJson(responseText);
    if (parsed.isEmpty()) {
        return false;
    }

    // Keep only keys that exist in the schema; fill missing schema keys with "".
    QHash<QString, QString> data = _schema();
    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it) {
        if (data.contains(it.key())) {
            data[it.key()] = it.value();
        }
    }

    pageRepo.saveData(pageId, data);
    pageRepo.setGeneratedAt(pageId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return true;
}

// ---- Private helpers --------------------------------------------------------

const QHash<QString, QString> &GenPageQueue::_schema() const
{
    if (m_schemaCached) {
        return m_schema;
    }

    // Create a fresh page type instance and call save() to discover all keys.
    // Populate AI hints from the same instance to avoid creating it twice.
    // bindWorkingDir() gives taxonomy-aware blocs access to their vocabulary
    // before collectAiKeyClues() is called.
    auto type = AbstractPageType::createForTypeId(m_pageTypeId, m_categoryTable);
    if (type) {
        if (m_workingDir.exists()) {
            type->bindWorkingDir(m_workingDir);
        }
        type->save(m_schema);
        m_aiKeyClues       = type->collectAiKeyClues();
        m_aiKeyCluesCached = true;
    }
    m_schemaCached = true;
    return m_schema;
}

const QHash<QString, QString> &GenPageQueue::_aiKeyClues() const
{
    if (m_aiKeyCluesCached) {
        return m_aiKeyClues;
    }
    // _schema() populates both caches; call it now if not yet cached.
    _schema();
    return m_aiKeyClues;
}

QHash<QString, QString> GenPageQueue::_parseJson(const QString &text)
{
    QHash<QString, QString> result;

    QString clean = text.trimmed();

    // Strip optional markdown code block wrapper.
    if (clean.startsWith(QStringLiteral("```"))) {
        const int firstNewline = clean.indexOf(QLatin1Char('\n'));
        const int lastFence    = clean.lastIndexOf(QStringLiteral("```"));
        if (firstNewline > 0 && lastFence > firstNewline) {
            clean = clean.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
        }
    }

    // Fallback: find the first '{' if the above left non-JSON preamble.
    const int brace = clean.indexOf(QLatin1Char('{'));
    if (brace > 0) {
        clean = clean.mid(brace);
    }

    const QJsonDocument doc = QJsonDocument::fromJson(clean.toUtf8());
    if (!doc.isObject()) {
        return result;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        result.insert(it.key(), it.value().toString());
    }
    return result;
}
