#include "GenPageQueue.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

GenPageQueue::GenPageQueue(const QString   &pageTypeId,
                            bool             nonSvgImages,
                            IPageRepository &pageRepo,
                            CategoryTable   &categoryTable,
                            const QString   &customInstructions,
                            int              limit)
    : m_pageTypeId(pageTypeId)
    , m_nonSvgImages(nonSvgImages)
    , m_customInstructions(customInstructions)
    , m_categoryTable(categoryTable)
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
                            const QString          &customInstructions)
    : m_pageTypeId(pageTypeId)
    , m_nonSvgImages(nonSvgImages)
    , m_customInstructions(customInstructions)
    , m_categoryTable(categoryTable)
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
             m_nonSvgImages
                 ? QStringLiteral("Reference images as relative paths (e.g. /images/foo.jpg).\n")
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

    QJsonObject skeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        skeleton[it.key()] = it.value();
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
        "  [IMGFIX id=\"unique-slug\" fileName=\"diagram.svg\" alt=\"description\"][/IMGFIX]\n"
        "  RULES:\n"
        "  • Place images at the most relevant position in the content flow\n"
        "    (not all grouped at the top or bottom).\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • SVG images are fully supported; use the same IMGFIX syntax.\n"
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
             m_nonSvgImages
                 ? QStringLiteral("Reference images as relative paths (e.g. /images/foo.jpg).\n")
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

    QJsonObject skeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        skeleton[it.key()] = it.value();
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
        "  [IMGFIX id=\"unique-slug\" fileName=\"diagram.svg\" alt=\"description\"][/IMGFIX]\n"
        "  RULES:\n"
        "  • Place images at the most relevant position in the content flow\n"
        "    (not all grouped at the top or bottom).\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • SVG images are fully supported; use the same IMGFIX syntax.\n"
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
             m_nonSvgImages
                 ? QStringLiteral("Reference images as relative paths (e.g. /images/foo.jpg).\n")
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
        "  RULES:\n"
        "  • Each image must have a unique id (use a short descriptive slug).\n"
        "  • Use fileName=\"name.svg\" for diagrams and charts; \"name.jpg\" for photos.\n"
        "  • NEVER write raw SVG, HTML, or XML anywhere in the article body.\n"
        "  • Any diagram, chart, or illustration MUST be represented solely as an\n"
        "    [IMGFIX] shortcode — the SVG will be generated separately.\n\n"
        "ABSOLUTE RULES (violations will corrupt the output):\n"
        "  • The article body must contain NO raw HTML, SVG, or XML tags whatsoever.\n"
        "  • Only the shortcodes listed above are permitted.\n"
        "  • The article must begin with text or a shortcode — never with a tag like <svg>, <text>, or <div>.\n\n"
        "Return ONLY the article content — no preamble, no meta-commentary about the task.");

    return prompt;
}

QString GenPageQueue::buildMetadataPrompt(const PageRecord &page,
                                           const QString   &articleText) const
{
    const QHash<QString, QString> &schema = _schema();

    // Build metadata-only schema: exclude all keys starting with "1_"
    QJsonObject metaSkeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        if (!it.key().startsWith(QStringLiteral("1_"))) {
            metaSkeleton[it.key()] = it.value();
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

    // 1_text: the article body from the content call, sanitized.
    // Strip any inline SVG blocks (<svg>…</svg>) that Claude may have embedded
    // despite the prompt instruction, then trim leading XML/HTML tags so they
    // don't corrupt the rendered article.
    static const QRegularExpression reSvgBlock(
        QStringLiteral("<svg\\b[^>]*>.*?</svg>"),
        QRegularExpression::DotMatchesEverythingOption
        | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reLeadingTag(
        QStringLiteral("^\\s*<[^>]+>.*?(?=\\[|[A-Z]|[a-z])"),
        QRegularExpression::DotMatchesEverythingOption);

    QString cleanText = articleText.trimmed();
    cleanText.remove(reSvgBlock);         // remove complete <svg>…</svg> blocks
    cleanText.remove(reLeadingTag);       // strip any remaining leading XML fragment
    cleanText = cleanText.trimmed();

    if (cleanText.isEmpty()) {
        return false;
    }

    data[QStringLiteral("1_text")] = cleanText;

    // Metadata fields: parsed from the metadata JSON call.
    if (!metadataJson.isEmpty()) {
        const QHash<QString, QString> meta = _parseJson(metadataJson);
        for (auto it = meta.cbegin(); it != meta.cend(); ++it) {
            // Never overwrite 1_text with anything from the metadata call.
            if (!it.key().startsWith(QStringLiteral("1_")) && data.contains(it.key())) {
                data[it.key()] = it.value();
            }
        }
    }

    pageRepo.saveData(pageId, data);
    pageRepo.setGeneratedAt(pageId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return true;
}

// ---- Image helpers ----------------------------------------------------------

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
                ref.fileName = val;
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
                                      const QString   &permalink,
                                      const QString   &lang)
{
    return QStringLiteral(
               "Create a standalone SVG image for a web article.\n\n"
               "Article permalink : %1\n"
               "Image id          : %2\n"
               "Image filename    : %3\n"
               "Image description : %4\n"
               "Language          : %5\n\n"
               "Requirements:\n"
               "• Return ONLY the raw SVG — no preamble, no code fences, no explanation.\n"
               "• The output must start with <svg and end with </svg>.\n"
               "• Include a viewBox attribute; do NOT set a fixed pixel width/height on the root element.\n"
               "• All text labels must be written in %5.\n"
               "• Make the image informative, visually clean, and self-contained.\n"
               "• Use only inline styles — no <style> blocks, no external CSS or fonts.\n"
               "• Use only web-safe fonts (Arial, Helvetica, sans-serif).\n"
               "• No JavaScript, no external references, no raster images embedded inside the SVG.")
        .arg(permalink, ref.id, ref.fileName, ref.alt, lang);
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
    auto type = AbstractPageType::createForTypeId(m_pageTypeId, m_categoryTable);
    if (type) {
        type->save(m_schema);
    }
    m_schemaCached = true;
    return m_schema;
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
