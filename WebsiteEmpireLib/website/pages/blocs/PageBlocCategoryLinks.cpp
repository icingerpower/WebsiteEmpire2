#include "PageBlocCategoryLinks.h"

#include "website/AbstractEngine.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/CategoryEditorWidget.h"

#include <QCoreApplication>
#include <QMap>
#include <QRegularExpression>

// =============================================================================
// Construction
// =============================================================================

PageBlocCategoryLinks::PageBlocCategoryLinks(CategoryTable &table)
    : m_table(table)
{
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocCategoryLinks::getName() const
{
    return QCoreApplication::translate("PageBlocCategoryLinks", "Cross-Reference Links");
}

// =============================================================================
// getAiKeyClues
// =============================================================================

QHash<QString, QString> PageBlocCategoryLinks::getAiKeyClues() const
{
    const auto &rows = m_table.categories();
    if (rows.isEmpty()) {
        return {};
    }
    QStringList vocab;
    vocab.reserve(rows.size());
    for (const auto &row : std::as_const(rows)) {
        vocab.append(QString::number(row.id) + QStringLiteral("=") + row.name);
    }
    return {{QLatin1String(KEY_CATEGORIES),
             QCoreApplication::translate("PageBlocCategoryLinks",
                 "Comma-separated IDs for cross-reference categories "
                 "(body parts, organs, related attributes shown below the breadcrumb). "
                 "Choose ONLY from: %1")
                 .arg(vocab.join(QStringLiteral(", ")))}};
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocCategoryLinks::load(const QHash<QString, QString> &values)
{
    m_selectedIds = _parseIds(values.value(QLatin1String(KEY_CATEGORIES)));
}

void PageBlocCategoryLinks::save(QHash<QString, QString> &values) const
{
    QStringList parts;
    parts.reserve(m_selectedIds.size());
    for (const int id : std::as_const(m_selectedIds)) {
        parts.append(QString::number(id));
    }
    parts.sort();
    values.insert(QLatin1String(KEY_CATEGORIES), parts.join(QLatin1Char(',')));
}

// =============================================================================
// addCode
// =============================================================================

static QString _buildCatLinkPermalink(const QString &name)
{
    static const QRegularExpression s_nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower();
    slug.replace(s_nonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-'))) { slug.chop(1); }
    if (slug.isEmpty()) {
        return {};
    }
    return QStringLiteral("/") + slug + QStringLiteral(".html");
}

void PageBlocCategoryLinks::addCode(QStringView     /*origContent*/,
                                    AbstractEngine  &engine,
                                    int              websiteIndex,
                                    QString         &html,
                                    QString         &css,
                                    QString         &js,
                                    QSet<QString>   &cssDoneIds,
                                    QSet<QString>   &jsDoneIds) const
{
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_selectedIds.isEmpty()) {
        return;
    }

    // Group selected categories by parent ID preserving selection order per group.
    QMap<int, QList<int>> groups; // parentId → [categoryId, ...]
    for (const int id : std::as_const(m_selectedIds)) {
        const CategoryTable::CategoryRow *row = m_table.categoryById(id);
        if (!row) { continue; }
        groups[row->parentId].append(id);
    }
    if (groups.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("page-bloc-catlinks");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".cat-xref{margin:0 0 1em}"
            ".cat-xref p{margin:0.2em 0;display:flex;flex-wrap:wrap;"
            "gap:0.25em;align-items:baseline}"
            ".cat-xref strong{white-space:nowrap;margin-right:0.25em}"
            ".cat-xref a{text-decoration:none;color:inherit}"
            ".cat-xref a:hover{text-decoration:underline}");
    }

    const QString langCode = engine.getLangCode(websiteIndex);

    html += QStringLiteral("<div class=\"cat-xref\">");
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const int parentId       = it.key();
        const QList<int> &catIds = it.value();

        html += QStringLiteral("<p>");

        // Emit group label from the parent category name when it exists.
        if (parentId != 0) {
            const CategoryTable::CategoryRow *parentRow = m_table.categoryById(parentId);
            if (parentRow) {
                const QString parentName = langCode.isEmpty()
                    ? parentRow->name
                    : m_table.translationFor(parentId, langCode);
                html += QStringLiteral("<strong>");
                html += parentName;
                html += QStringLiteral(":</strong>");
            }
        }

        for (const int id : catIds) {
            const CategoryTable::CategoryRow *row = m_table.categoryById(id);
            if (!row) { continue; }

            const QString displayName = langCode.isEmpty()
                ? row->name
                : m_table.translationFor(id, langCode);
            // Resolve to translated hub permalink if available; fall back to English.
            const QString permalink = engine.resolvePermalink(
                _buildCatLinkPermalink(row->name), websiteIndex);

            html += QLatin1Char(' ');
            if (!permalink.isEmpty() && engine.isPageAvailable(permalink, websiteIndex)) {
                html += QStringLiteral("<a href=\"");
                html += permalink;
                html += QStringLiteral("\">");
                html += displayName;
                html += QStringLiteral("</a>");
            } else {
                html += displayName;
            }
        }

        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</div>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocCategoryLinks::createEditWidget()
{
    return new CategoryEditorWidget(m_table, true /* allowSelection */);
}

// =============================================================================
// _parseIds (private static)
// =============================================================================

QList<int> PageBlocCategoryLinks::_parseIds(const QString &content)
{
    QList<int> ids;
    if (content.trimmed().isEmpty()) {
        return ids;
    }
    const QStringList parts = content.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : std::as_const(parts)) {
        const int id = part.trimmed().toInt();
        if (id > 0) {
            ids.append(id);
        }
    }
    return ids;
}
