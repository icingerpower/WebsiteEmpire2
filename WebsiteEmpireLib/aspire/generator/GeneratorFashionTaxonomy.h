#ifndef GENERATORFASHIONTAXONOMY_H
#define GENERATORFASHIONTAXONOMY_H

#include "AbstractGenerator.h"

// Generator that builds a fashion outfit-combination database for
// programmatic SEO, covering the 8 taxonomy dimensions (product type,
// color, season, event, style/aesthetic, fit/silhouette, material/pattern,
// demographic) and 19 combination tables: 9 derived from the study's 12
// query formulas (several formulas share an identical slot signature and
// are folded into one table, distinguished by their formula id — see
// PageAttributesFashionCombo* headers) plus 10 round-2 expansion tables
// derived from a Google-Ads-volume keyword research pass.
//
// Unlike GeneratorHealth, the 11 vocabulary tables are NOT discovered via
// AI jobs: the study enumerates a small, closed set of values per dimension
// (a few dozen entries at most), so they are seeded deterministically by
// seedStaticVocabulary(). Only the *combinations* — whether a given tuple of
// values makes sense, and in which cultures — require AI judgment, since
// that depends on cultural/contextual knowledge (mourning colors, modesty
// norms, festival associations, ...) that cannot be hard-coded.
//
// Core invariant: a combination row is only ever recorded when the AI
// assessment tags it with at least one applicable culture. Combinations
// judged to make sense nowhere are discarded, never written — this is what
// keeps the database containing "only relevant combos". The invariant is
// double-enforced: processReply() skips empty-culture candidates before
// calling recordResultPage(), and PageAttributesFashionComboBase::ID_CULTURES
// additionally rejects an empty value at the AspiredDb validation layer.
//
// Job IDs: "combo/<comboKey>/<page>", one combo table per key. Pagination
// mirrors GeneratorHealth's difficulty-scoring jobs (always recomputes the
// next unprocessed batch fresh from DB state, rather than tracking an
// offset) — see nextCandidateBatch() in the .cpp. A full batch (size ==
// MAX_CANDIDATES_PER_JOB) triggers discovery of the next page; a partial or
// empty batch means that combo table is exhausted.
class GeneratorFashionTaxonomy : public AbstractGenerator
{
    Q_OBJECT

public:
    // Maximum candidate tuples assessed per job. When a reply is asked to
    // assess exactly this many, a continuation job for the next page is
    // discovered automatically.
    static const int MAX_CANDIDATES_PER_JOB;

    // Vocabulary round. Pages are fixed slices of the cross product and
    // vocabulary values are consumed in alphabetical order, so ANY change to
    // the seed lists re-maps every page of every combo table — an existing
    // .ini's Done pages would then describe the wrong slices. Whenever the
    // seed vocabulary changes after a round has been dispatched, bump this
    // constant: job ids gain a ".r<round>" key suffix (e.g.
    // "combo/color_product.r2/0"), so the expanded space is re-walked
    // completely under fresh ids while the previous round's Done state stays
    // dormant. rowAlreadyRecorded() skips rows recorded in earlier rounds,
    // so a re-walk only costs re-assessment, never duplicates.
    static const int VOCAB_ROUND;

    explicit GeneratorFashionTaxonomy(const QDir &workingDir = QDir(), QObject *parent = nullptr);

    QString            getId()   const override;
    QString            getName() const override;
    AbstractGenerator *createInstance(const QDir &workingDir) const override;
    QMap<QString, AbstractPageAttributes *> createResultPageAttributes() const override;
    GeneratorTables    getTables()                             const override;

    // Populates the 11 vocabulary tables (ProductType, ColorFamily, Color,
    // Season, Event, StyleAesthetic, FitSilhouette, Material, Pattern,
    // Demographic, Culture) from the fixed seed lists derived from the
    // fashion taxonomy study. Idempotent — existing rows (matched by name)
    // are never duplicated, so it is safe to call on every run.
    // Must be called after openResultsTable() (the tables must be open) and
    // before pulling combo jobs, since combination candidate generation
    // reads these tables to build its cross-product.
    void seedStaticVocabulary();

    // ---- Job-ID helpers (public for testability) ---------------------------

    // Everything between "combo/" and the trailing "/<page>" segment,
    // e.g. "combo/color_product/3" -> "color_product".
    static QString comboKeyFromJobId(const QString &jobId);

    // The trailing integer segment, e.g. "combo/color_product/3" -> 3.
    static int pageFromJobId(const QString &jobId);

protected:
    QStringList buildInitialJobIds()                                    const override;
    QJsonObject buildJobPayload(const QString &jobId)                   const override;
    void        processReply(const QString &jobId, const QJsonObject &) override;

private:
    // Lazily calls seedStaticVocabulary() on first use so that running this
    // generator's jobs (GUI "Run" button or --runjobs) is a single
    // self-contained command — the operator never has to remember a separate
    // manual seeding step. Safe no-op once already seeded.
    void ensureVocabularySeeded() const;
};

#endif // GENERATORFASHIONTAXONOMY_H
