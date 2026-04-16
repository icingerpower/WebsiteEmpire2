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
                            int              limit)
    : m_pageTypeId(pageTypeId)
    , m_nonSvgImages(nonSvgImages)
    , m_categoryTable(categoryTable)
{
    m_pending = pageRepo.findPendingByTypeId(pageTypeId);

    if (limit >= 0 && m_pending.size() > limit) {
        m_pending = m_pending.mid(0, limit);
    }
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

QString GenPageQueue::buildPrompt(const PageRecord &page,
                                   AbstractEngine   &engine,
                                   int               websiteIndex) const
{
    const QHash<QString, QString> &schema = _schema();

    // Serialise the schema (all values empty) as a JSON skeleton.
    QJsonObject skeleton;
    for (auto it = schema.cbegin(); it != schema.cend(); ++it) {
        skeleton[it.key()] = it.value();
    }
    const QString schemaJson = QString::fromUtf8(
        QJsonDocument(skeleton).toJson(QJsonDocument::Indented));

    const QString lang = engine.getLangCode(websiteIndex);

    return QStringLiteral(
               "Generate content for a web page of type \"%1\".\n\n"
               "Permalink : %2\n"
               "Language  : %3\n"
               "Images    : %4\n\n"
               "Fill in ALL values in the JSON structure below with high-quality, "
               "relevant content based on the permalink topic.  Do NOT change any key.\n\n"
               "%5\n\n"
               "Rules:\n"
               "- Write entirely in %3\n"
               "- Content must be relevant and appropriate for the permalink\n"
               "- HTML tags inside values are allowed but not required\n"
               "%6"
               "- Return ONLY a valid JSON object — no markdown fences, no explanation")
        .arg(m_pageTypeId,
             page.permalink,
             lang.isEmpty() ? page.lang : lang,
             m_nonSvgImages ? QStringLiteral("include non-SVG images") : QStringLiteral("no images"),
             schemaJson,
             m_nonSvgImages
                 ? QStringLiteral("- Reference images as relative paths (e.g. /images/foo.jpg)\n")
                 : QString());
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
