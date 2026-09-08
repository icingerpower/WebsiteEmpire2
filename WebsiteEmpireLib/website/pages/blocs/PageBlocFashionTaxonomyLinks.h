#ifndef PAGEBLOCFASHIONTAXONOMYLINKS_H
#define PAGEBLOCFASHIONTAXONOMYLINKS_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QDir>
#include <QStringList>

/**
 * Page bloc for fashion-vertical article pages.
 *
 * Generic, data-driven equivalent of PageBlocSymptomLinks: instead of one
 * hardcoded taxonomy ("symptoms"), this bloc covers independent dimensions —
 * Color, Season, Occasion, Material, Style Aesthetic, Product Type,
 * Demographic, Fit/Silhouette, Pattern, Culture — each with its own synced
 * vocabulary, its own selected-tags storage key, and its own hub URL prefix.
 * An editor picks tags per dimension; addCode() renders one pill link per
 * selected tag pointing at that dimension's hub page.
 *
 * Product Type is the single most cross-cutting dimension of all (an axis
 * in 14 of the 19 combo tables, vs. 2-7 for the others) — a
 * "/product-types/<slug>" hub aggregates far more content than any other
 * dimension's hub. Color Family is deliberately NOT a dimension here: unlike
 * every other one, no combo table carries it directly as a column — a
 * combo row only ever records a specific Color, and Color Family is a
 * coarser grouping *of* Color (see PageAttributesFashionColor::ID_FAMILY),
 * so a Color Family hub would need to join through Color rather than sync
 * one column directly. Add it later if that join is built.
 *
 * Unlike Symptoms, every dimension here is translatable
 * (TaxonomyDescriptor::translatable == true): --translateCommon populates
 * TaxonomyDb translations for these ids, and addCode() displays the
 * translated name via TaxonomyDb::translationFor() — same as Symptoms
 * already does for display text, but for Symptoms nothing is ever queued
 * for translation (see PageBlocSymptomLinks::taxonomy()).
 *
 * Call setWorkingDir() from PageTypeArticleFashion's generation-context
 * binding so createEditWidget() can load each dimension's vocabulary from
 * the local taxonomy store.
 *
 * Hub pages (/colors/<slug>, /occasions/<slug>, etc.) are not built by this
 * bloc or anywhere else yet — addCode() relies on the same
 * engine.isPageAvailable() graceful no-op PageBlocSymptomLinks uses, so
 * until those hub pages exist this bloc simply renders nothing (no dead
 * links), and tags stored today become active links automatically once
 * hub-page generation is added later.
 */
class PageBlocFashionTaxonomyLinks : public AbstractPageBloc
{
public:
    /** One independently-synced, independently-translated taxonomy dimension. */
    struct Dimension {
        QString taxonomyId;   // stable key, e.g. "fashion_color" — TaxonomyDb type + storage key suffix
        QString displayName;  // human-readable, e.g. "Color" — shown as the PaneTaxonomies card title
        QString hubPrefix;    // permalink prefix, e.g. "/colors/"
        QString sourceColumn; // aspire "records" table column read by syncTaxonomy(), e.g. "fashion_color_name"
    };

    /** The fashion taxonomy dimensions, in declaration/render order. */
    static const QList<Dimension> &dimensions();

    /**
     * Slugifies a tag name for use in a hub permalink (e.g. "Navy Blue" ->
     * "navy-blue"). NFD-normalizes to strip accents so translated names
     * ("Bordeaux") slug correctly. The single canonical implementation for
     * this vertical — used here by addCode() to build a tag's link href, and
     * by FashionTaxonomyHubSyncer/PageGenerator to build/match that same
     * hub's permalink, so link and hub can never disagree on the slug.
     * Deliberately a separate implementation from PageGenerator::categoryHubSlug()
     * / SymptomNav::slugify() — each vertical's slug rule evolves independently.
     */
    static QString slugify(const QString &name);

    ~PageBlocFashionTaxonomyLinks() override = default;

    /**
     * Stores the working directory so createEditWidget() can load each
     * dimension's vocabulary from the local taxonomy store.
     * Call from PageTypeArticleFashion::bindGenerationContext().
     */
    void setWorkingDir(const QDir &workingDir);

    QString getName() const override;

    /** Reads each dimension's storage key and parses it into the internal tag lists. */
    void load(const QHash<QString, QString> &values) override;

    /** Writes each dimension's internal tag list back under its storage key. */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Renders one pill-style link per selected tag, grouped by dimension.
     * A tag's link is emitted only when engine.isPageAvailable() confirms
     * that dimension's hub page exists for it; otherwise it is skipped
     * (never rendered as plain text — a Fashion tag with no hub page yet is
     * not useful information to a reader).
     * No-op when no tags are stored in any dimension.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    /**
     * One hint per dimension, mirroring PageBlocSymptomLinks::getAiKeyClues():
     * constrains the AI to 0-3 values that are most central to THIS article,
     * not every value mentioned across every section — without this, an
     * article covering several outfit types (e.g. workplace + evening +
     * weekend) tends to get tagged with every color/material/etc. mentioned
     * anywhere in it, producing an unusably long tag list per dimension.
     * Empty for a dimension whose vocabulary hasn't been synced yet.
     */
    QHash<QString, QString> getAiKeyClues() const override;

    /** Returns one TaxonomyDescriptor per dimension(), each translatable == true. */
    QList<TaxonomyDescriptor> taxonomies() const override;

    /**
     * taxonomyId selects which dimension() this sync call is for. Reads that
     * dimension's sourceColumn from sourceDbPath's "records" table and writes
     * the result into TaxonomyDb under taxonomyId. No-op if taxonomyId does
     * not match any known dimension.
     */
    void syncTaxonomy(const QString &taxonomyId, const QString &sourceDbPath,
                      const QDir &workingDir) const override;

    /** Returns the synced vocabulary for one dimension (by taxonomyId), for use in the edit widget. */
    QStringList loadDimension(const QString &taxonomyId, const QDir &workingDir) const;

private:
    QDir                       m_workingDir;
    QHash<QString, QStringList> m_selectedByDimension; // taxonomyId -> selected tag names
};

#endif // PAGEBLOCFASHIONTAXONOMYLINKS_H
