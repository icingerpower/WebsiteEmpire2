#include "GenPageQueue.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

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
