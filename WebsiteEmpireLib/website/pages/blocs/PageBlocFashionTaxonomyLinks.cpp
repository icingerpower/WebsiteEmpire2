#include "PageBlocFashionTaxonomyLinks.h"

#include "website/AbstractEngine.h"
#include "website/pages/blocs/widgets/PageBlocFashionTaxonomyLinksWidget.h"
#include "website/taxonomy/TaxonomyDb.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <utility>

// =============================================================================
// dimensions
// =============================================================================

const QList<PageBlocFashionTaxonomyLinks::Dimension> &PageBlocFashionTaxonomyLinks::dimensions()
{
    static const QList<Dimension> s_dimensions = {
        {QStringLiteral("fashion_color"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Color"),
         QStringLiteral("/colors/"), QStringLiteral("fashion_color_name")},
        {QStringLiteral("fashion_season"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Season"),
         QStringLiteral("/seasons/"), QStringLiteral("fashion_season_name")},
        {QStringLiteral("fashion_occasion"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Occasion"),
         QStringLiteral("/occasions/"), QStringLiteral("fashion_event_name")},
        {QStringLiteral("fashion_material"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Material"),
         QStringLiteral("/materials/"), QStringLiteral("fashion_material_name")},
        {QStringLiteral("fashion_style"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Style Aesthetic"),
         QStringLiteral("/styles/"), QStringLiteral("fashion_style_aesthetic_name")},
        {QStringLiteral("fashion_product_type"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Product Type"),
         QStringLiteral("/product-types/"), QStringLiteral("fashion_product_type_name")},
        {QStringLiteral("fashion_demographic"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Demographic"),
         QStringLiteral("/demographics/"), QStringLiteral("fashion_demographic_name")},
        {QStringLiteral("fashion_fit"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Fit/Silhouette"),
         QStringLiteral("/fits/"), QStringLiteral("fashion_fit_silhouette_name")},
        {QStringLiteral("fashion_pattern"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Pattern"),
         QStringLiteral("/patterns/"), QStringLiteral("fashion_pattern_name")},
        {QStringLiteral("fashion_culture"),
         QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Culture"),
         QStringLiteral("/cultures/"), QStringLiteral("fashion_culture_name")},
    };
    return s_dimensions;
}

QString PageBlocFashionTaxonomyLinks::slugify(const QString &name)
{
    static const QRegularExpression s_nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression s_combining(QStringLiteral("[\\x{0300}-\\x{036F}]"));
    QString slug = name.toLower().normalized(QString::NormalizationForm_D);
    slug.remove(s_combining);
    slug.replace(s_nonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
    return slug;
}

namespace {

const PageBlocFashionTaxonomyLinks::Dimension *findDimension(const QString &taxonomyId)
{
    for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
        if (dim.taxonomyId == taxonomyId) {
            return &dim;
        }
    }
    return nullptr;
}

} // namespace

// =============================================================================
// setWorkingDir
// =============================================================================

void PageBlocFashionTaxonomyLinks::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocFashionTaxonomyLinks::getName() const
{
    return QCoreApplication::translate("PageBlocFashionTaxonomyLinks", "Fashion Tags");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocFashionTaxonomyLinks::load(const QHash<QString, QString> &values)
{
    m_selectedByDimension.clear();
    for (const auto &dim : dimensions()) {
        const QStringList parts = values.value(dim.taxonomyId)
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts);
        QStringList names;
        for (const QString &p : parts) {
            const QString &name = p.trimmed();
            if (!name.isEmpty()) {
                names.append(name);
            }
        }
        if (!names.isEmpty()) {
            m_selectedByDimension.insert(dim.taxonomyId, names);
        }
    }
}

void PageBlocFashionTaxonomyLinks::save(QHash<QString, QString> &values) const
{
    for (const auto &dim : dimensions()) {
        values.insert(dim.taxonomyId,
                      m_selectedByDimension.value(dim.taxonomyId).join(QLatin1Char(',')));
    }
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocFashionTaxonomyLinks::addCode(QStringView,
                                            AbstractEngine &engine,
                                            int             websiteIndex,
                                            QString        &html,
                                            QString        &css,
                                            QString        &,
                                            QSet<QString>  &cssDoneIds,
                                            QSet<QString>  &) const
{
    if (m_selectedByDimension.isEmpty()) {
        return;
    }

    const QString langCode = engine.getLangCode(websiteIndex);
    TaxonomyDb taxDb(m_workingDir);

    // Grouped by dimension (not flattened into one list) so a reader sees
    // "Colors: Black, Charcoal" / "Occasions: Workplace, Evening" as separate
    // labelled rows — mirroring PageBlocSymptomLinks' single "Related
    // symptoms:" row, generalized to this bloc's 10 dimensions.
    struct TagLink { QString name; QString href; };
    struct DimensionGroup { QString label; QList<TagLink> links; };
    QList<DimensionGroup> groups;
    for (const auto &dim : dimensions()) {
        const QStringList selected = m_selectedByDimension.value(dim.taxonomyId);
        QList<TagLink> links;
        for (const QString &name : selected) {
            const QString slug      = PageBlocFashionTaxonomyLinks::slugify(name);
            const QString permalink = dim.hubPrefix + slug;
            if (!engine.isPageAvailable(permalink, websiteIndex)) {
                continue;
            }
            const QString resolved = engine.resolveLinkHref(permalink, websiteIndex);
            if (resolved.isEmpty()) {
                continue;
            }
            const QString displayName = taxDb.translationFor(dim.taxonomyId, name, langCode);
            links.append({displayName, resolved});
        }
        if (!links.isEmpty()) {
            // NOTE: dim.displayName is English-only here — it is a Qt UI
            // translation (QCoreApplication::translate), which reflects the
            // desktop app's own locale, not langCode (the WEBSITE visitor's
            // language being generated). Per-langCode label translation would
            // need routing through the same AI-CLI pipeline used for every
            // other visitor-facing string (see feedback_translation_cli
            // memory: never hand-translate), not a hardcoded table — left as
            // a follow-up; today every language shows the English label.
            groups.append({dim.displayName, std::move(links)});
        }
    }

    if (groups.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("fashion-taxonomy-links-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".fashion-taxonomy-links{margin:1em 0}"
            ".fashion-taxonomy-links__group{margin:.4em 0}"
            ".fashion-taxonomy-links__label{font-weight:600;margin-right:.4em}"
            ".fashion-taxonomy-links a{"
            "display:inline-block;margin:.2em .25em .2em 0;"
            "padding:.2em .65em;border-radius:3em;"
            "background:#e8f0fe;color:#1a73e8;"
            "font-size:.875em;text-decoration:none;white-space:nowrap}"
            ".fashion-taxonomy-links a:hover{background:#c6d9fd}");
    }

    html += QStringLiteral("<div class=\"fashion-taxonomy-links\">");
    for (const auto &group : std::as_const(groups)) {
        html += QStringLiteral("<div class=\"fashion-taxonomy-links__group\">");
        html += QStringLiteral("<span class=\"fashion-taxonomy-links__label\">");
        html += group.label;
        html += QStringLiteral(":</span>");
        for (const auto &link : std::as_const(group.links)) {
            html += QStringLiteral("<a href=\"");
            html += link.href;
            html += QStringLiteral("\">");
            html += link.name;
            html += QStringLiteral("</a>");
        }
        html += QStringLiteral("</div>");
    }
    html += QStringLiteral("</div>");
}

// =============================================================================
// getAiKeyClues
// =============================================================================

QHash<QString, QString> PageBlocFashionTaxonomyLinks::getAiKeyClues() const
{
    QHash<QString, QString> clues;
    for (const auto &dim : dimensions()) {
        const QStringList vocab = loadDimension(dim.taxonomyId, m_workingDir);
        if (vocab.isEmpty()) {
            continue;
        }
        clues.insert(dim.taxonomyId,
            QCoreApplication::translate("PageBlocFashionTaxonomyLinks",
                "Comma-separated %1 names. Choose 0-3 values that are MOST CENTRAL to "
                "THIS article as a whole — not every value mentioned in every section "
                "(e.g. an article covering several outfit types should NOT list every "
                "color/material seen anywhere in it). Use ONLY exact names from this "
                "list: %2")
                .arg(dim.displayName, vocab.join(QStringLiteral(", "))));
    }
    return clues;
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocFashionTaxonomyLinks::createEditWidget()
{
    QHash<QString, QStringList> vocabByDimension;
    for (const auto &dim : dimensions()) {
        vocabByDimension.insert(dim.taxonomyId, loadDimension(dim.taxonomyId, m_workingDir));
    }
    return new PageBlocFashionTaxonomyLinksWidget(vocabByDimension);
}

// =============================================================================
// taxonomies / syncTaxonomy / loadDimension
// =============================================================================

QList<TaxonomyDescriptor> PageBlocFashionTaxonomyLinks::taxonomies() const
{
    QList<TaxonomyDescriptor> result;
    result.reserve(dimensions().size());
    for (const auto &dim : dimensions()) {
        result.append(TaxonomyDescriptor{dim.taxonomyId, dim.displayName, /*translatable=*/true});
    }
    return result;
}

void PageBlocFashionTaxonomyLinks::syncTaxonomy(const QString &taxonomyId,
                                                 const QString &sourceDbPath,
                                                 const QDir    &workingDir) const
{
    const Dimension *dim = findDimension(taxonomyId);
    if (!dim) {
        return;
    }

    QStringList names;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("fashionsync_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(sourceDbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("SELECT ") + dim->sourceColumn
                       + QStringLiteral(" FROM records ORDER BY ") + dim->sourceColumn
                       + QStringLiteral(" COLLATE NOCASE"));
                while (q.next()) {
                    const QString name = q.value(0).toString().trimmed();
                    if (!name.isEmpty()) {
                        names.append(name);
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    TaxonomyDb(workingDir).sync(taxonomyId, names);
}

QStringList PageBlocFashionTaxonomyLinks::loadDimension(const QString &taxonomyId,
                                                        const QDir    &workingDir) const
{
    return TaxonomyDb(workingDir).load(taxonomyId);
}
