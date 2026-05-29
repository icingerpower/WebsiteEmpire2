#include "PageBlocCategory.h"

#include "website/AbstractEngine.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/CategoryEditorWidget.h"

#include <QCoreApplication>
#include <QRegularExpression>

// =============================================================================
// getAiKeyClues
// =============================================================================

QHash<QString, QString> PageBlocCategory::getAiKeyClues() const
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
             QCoreApplication::translate("PageBlocCategory",
                 "Comma-separated category IDs, no more than 3. Choose ONLY from: %1")
                 .arg(vocab.join(QStringLiteral(", ")))}};
}

// =============================================================================
// getAiUpdateSpec
// =============================================================================

std::optional<AbstractPageBloc::AiUpdateSpec> PageBlocCategory::getAiUpdateSpec() const
{
    AiUpdateSpec spec;
    spec.dataKey     = QLatin1String(KEY_CATEGORIES);
    spec.displayName = QCoreApplication::translate("PageBlocCategory", "Categories");
    spec.formatPrompt = QCoreApplication::translate("PageBlocCategory",
        "Based on the analysis above, output ONLY comma-separated category IDs "
        "(e.g. \"14,23\"). Choose 1-3 IDs from the vocabulary. "
        "No explanation, no other text.");
    spec.validator   = AiUpdateSpec::Validator::CommaSeparatedInts;
    return spec;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocCategory::getName() const
{
    return tr("Categories");
}

PageBlocCategory::PageBlocCategory(CategoryTable &table, QObject *parent)
    : QObject(parent)
    , m_table(table)
{
    connect(&m_table, &CategoryTable::categoryRemoved,
            this,     &PageBlocCategory::_onCategoriesRemoved);
}

void PageBlocCategory::setContent(const QString &content)
{
    m_selectedIds = _parseIds(content);
    _rebuildAttributes();
}

// ---- load / save ------------------------------------------------------------

void PageBlocCategory::load(const QHash<QString, QString> &values)
{
    // Unknown keys are silently ignored for forward compatibility.
    setContent(values.value(QLatin1String(KEY_CATEGORIES)));
}

void PageBlocCategory::save(QHash<QString, QString> &values) const
{
    QStringList parts;
    parts.reserve(m_selectedIds.size());
    for (const int id : std::as_const(m_selectedIds)) {
        parts.append(QString::number(id));
    }
    parts.sort();
    values.insert(QLatin1String(KEY_CATEGORIES), parts.join(QLatin1Char(',')));
}

// ---- WebCodeAdder -----------------------------------------------------------

// Converts a category name to a page permalink slug, e.g.
// "Bone Conditions" → "/bone-conditions.html".
// Returns an empty string if the name produces an empty slug.
static QString _categoryPermalink(const QString &name)
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

void PageBlocCategory::addCode(QStringView     /*origContent*/,
                               AbstractEngine &engine,
                               int             websiteIndex,
                               QString        &html,
                               QString        &css,
                               QString        &js,
                               QSet<QString>  &cssDoneIds,
                               QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_selectedIds.isEmpty()) {
        return;
    }

    // Group selected IDs by root ancestor; keep the deepest (most specific) leaf per
    // root. Insertion order follows m_selectedIds so the first-seen root comes first.
    struct RootGroup { int leafId = 0; int depth = -1; };
    QList<int>           rootOrder;
    QMap<int, RootGroup> byRoot;

    for (const int id : std::as_const(m_selectedIds)) {
        int root  = id;
        int depth = 0;
        int cur   = id;
        while (cur != 0) {
            const CategoryTable::CategoryRow *r = m_table.categoryById(cur);
            if (!r || r->parentId == cur) { root = cur; break; }
            root = cur;
            cur  = r->parentId;
            ++depth;
        }

        if (!byRoot.contains(root)) {
            rootOrder.append(root);
        }
        RootGroup &g = byRoot[root];
        if (depth > g.depth) {
            g.depth  = depth;
            g.leafId = id;
        }
    }

    if (rootOrder.size() > 3) {
        rootOrder.resize(3);
    }

    // Build one trail per root branch.
    const QString langCode = engine.getLangCode(websiteIndex);
    QStringList trails;

    for (const int root : std::as_const(rootOrder)) {
        const int leafId = byRoot.value(root).leafId;

        QList<int> chain;
        int cur = leafId;
        while (cur != 0) {
            chain.prepend(cur);
            const CategoryTable::CategoryRow *r = m_table.categoryById(cur);
            if (!r || r->parentId == cur) { break; }
            cur = r->parentId;
        }
        if (chain.isEmpty()) { continue; }

        QStringList items;
        for (const int id : std::as_const(chain)) {
            const CategoryTable::CategoryRow *row = m_table.categoryById(id);
            if (!row) { continue; }

            const QString name = langCode.isEmpty()
                ? row->name
                : m_table.translationFor(id, langCode);
            // Resolve to translated hub permalink if available; fall back to English.
            const QString permalink = engine.resolvePermalink(
                _categoryPermalink(row->name), websiteIndex);

            QString item;
            if (!permalink.isEmpty() && engine.isPageAvailable(permalink, websiteIndex)) {
                item += QStringLiteral("<a href=\"");
                item += permalink;
                item += QStringLiteral("\" style=\"color:#666;text-decoration:none\">");
                item += name;
                item += QStringLiteral("</a>");
            } else {
                item += name;
            }
            items.append(item);
        }
        if (items.isEmpty()) { continue; }

        QStringList parts;
        for (int i = 0; i < items.size(); ++i) {
            if (i > 0) {
                parts.append(QStringLiteral(" › "));
            }
            parts.append(items.at(i));
        }
        trails.append(parts.join(QString()));
    }

    if (trails.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("PageBlocCategory_breadcrumb");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".breadcrumb{list-style:none;padding:0;margin:0 0 0.75em;"
            "display:flex;flex-wrap:wrap;gap:0.25em 0;"
            "font-size:0.8em;color:#aaa;"
            "border-bottom:1px solid #eee;padding-bottom:0.35em}"
            ".breadcrumb li{display:inline}"
            ".breadcrumb li+li::before{content:\" › \";color:#ccc}"
            ".breadcrumb a{color:#aaa;text-decoration:none}"
            ".breadcrumb a:hover{text-decoration:underline}");
    }

    html += QStringLiteral("<nav aria-label=\"breadcrumb\"><ol class=\"breadcrumb\">");
    for (int i = 0; i < trails.size(); ++i) {
        html += QStringLiteral("<li>");
        html += trails.at(i);
        html += QStringLiteral("</li>");
    }
    html += QStringLiteral("</ol></nav>");
}

AbstractPageBlockWidget *PageBlocCategory::createEditWidget()
{
    return new CategoryEditorWidget(m_table, true /* allowSelection */);
}

const QList<const AbstractAttribute *> &PageBlocCategory::getAttributes() const
{
    return m_attributes;
}

// ---- Private slots ----------------------------------------------------------

void PageBlocCategory::_onCategoriesRemoved(const QList<int> &removedIds)
{
    bool changed = false;
    for (const int id : std::as_const(removedIds)) {
        if (m_selectedIds.removeAll(id) > 0) {
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    _rebuildAttributes();

    QStringList parts;
    parts.reserve(m_selectedIds.size());
    for (const int id : std::as_const(m_selectedIds)) {
        parts.append(QString::number(id));
    }
    parts.sort();
    emit contentChanged(parts.join(','));
}

// ---- Private ----------------------------------------------------------------

void PageBlocCategory::_rebuildAttributes()
{
    m_categoryStore.clear();
    m_attributes.clear();

    // Build the store completely before taking any pointers into it so that
    // QList does not invalidate addresses during append().
    m_categoryStore.reserve(m_selectedIds.size());
    for (const int id : std::as_const(m_selectedIds)) {
        const CategoryTable::CategoryRow *row = m_table.categoryById(id);
        if (row) {
            m_categoryStore.append(AttributeCategory(id, row->name, row->parentId));
        }
    }

    // Store is fully built — addresses are now stable.
    for (const auto &cat : std::as_const(m_categoryStore)) {
        m_attributes.append(&cat);
    }
}

QList<int> PageBlocCategory::_parseIds(const QString &content)
{
    QList<int> ids;
    if (content.trimmed().isEmpty()) {
        return ids;
    }
    const QStringList parts = content.split(',', Qt::SkipEmptyParts);
    for (const QString &part : std::as_const(parts)) {
        const int id = part.trimmed().toInt();
        if (id > 0) {
            ids.append(id);
        }
    }
    return ids;
}
