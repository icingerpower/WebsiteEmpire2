#include "PageBlocCategory.h"

#include "website/AbstractEngine.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/CategoryEditorWidget.h"

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

void PageBlocCategory::addCode(QStringView     /*origContent*/,
                               AbstractEngine &engine,
                               int             websiteIndex,
                               QString        &html,
                               QString        &css,
                               QString        &js,
                               QSet<QString>  &cssDoneIds,
                               QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    // Use the state loaded via load() / setContent().
    if (m_selectedIds.isEmpty()) {
        return;
    }

    // Resolve names first — skip the whole block if every id is unknown.
    QStringList names;
    names.reserve(m_selectedIds.size());
    for (const int id : std::as_const(m_selectedIds)) {
        const CategoryTable::CategoryRow *row = m_table.categoryById(id);
        if (row) {
            names.append(row->name);
        }
    }
    if (names.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("page-bloc-category");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".categories{display:flex;flex-wrap:wrap;gap:0.5em;"
            "list-style:none;padding:0;margin:0}"
            ".categories .category{background:#eee;padding:0.2em 0.6em;"
            "border-radius:1em;font-size:0.875em}");
    }

    html += QStringLiteral("<ul class=\"categories\">");
    for (const QString &name : std::as_const(names)) {
        html += QStringLiteral("<li class=\"category\">");
        html += name;
        html += QStringLiteral("</li>");
    }
    html += QStringLiteral("</ul>");
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
