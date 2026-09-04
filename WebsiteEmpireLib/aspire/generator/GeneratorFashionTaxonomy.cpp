#include "GeneratorFashionTaxonomy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>

#include "ExceptionWithTitleText.h"
#include "aspire/attributes/fashion/PageAttributesFashionProductType.h"
#include "aspire/attributes/fashion/PageAttributesFashionColorFamily.h"
#include "aspire/attributes/fashion/PageAttributesFashionColor.h"
#include "aspire/attributes/fashion/PageAttributesFashionSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionStyleAesthetic.h"
#include "aspire/attributes/fashion/PageAttributesFashionFitSilhouette.h"
#include "aspire/attributes/fashion/PageAttributesFashionMaterial.h"
#include "aspire/attributes/fashion/PageAttributesFashionPattern.h"
#include "aspire/attributes/fashion/PageAttributesFashionDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionCulture.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboBase.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboSeasonEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboFitProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboMaterialProductSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboFitProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorColor.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductPattern.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboMaterialProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductDemographicEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboPatternProductSeason.h"
#include "aspire/downloader/DownloadedPagesTable.h"

DECLARE_GENERATOR(GeneratorFashionTaxonomy)

const int GeneratorFashionTaxonomy::MAX_CANDIDATES_PER_JOB = 40;
const int GeneratorFashionTaxonomy::VOCAB_ROUND = 2;

// =============================================================================
// File-local: combination table metadata + seed vocabulary + shared helpers
// =============================================================================

namespace {

// One dimension slot within a combination table: attrId is the column on the
// combo table itself (used both to write the row and to read back already-
// recorded keys); vocabAttrId/vocabNameId identify the vocabulary table and
// its name column that this slot draws its values from; jsonKey is the
// human-readable key used in the AI job payload.
struct ComboSlot {
    QString attrId;
    QString vocabAttrId;
    QString vocabNameId;
    QString jsonKey;
};

struct ComboSpec {
    QString key;      // short slug used in job ids: "combo/<key>/<page>"
    QString attrId;    // AbstractPageAttributes::getId() of the combo table
    QList<ComboSlot> dimensionSlots;
    QStringList formulaIds;
};

const QList<ComboSpec> &comboSpecs()
{
    static const QList<ComboSpec> specs = {
        { QStringLiteral("color_product")
        , QStringLiteral("PageAttributesFashionComboColorProduct")
        , { {PageAttributesFashionComboColorProduct::ID_COLOR,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("color")}
          , {PageAttributesFashionComboColorProduct::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")} }
        , {PageAttributesFashionComboColorProduct::FORMULA_STYLING_PAIRING,
           PageAttributesFashionComboColorProduct::FORMULA_FOOTWEAR_MATCHING,
           PageAttributesFashionComboColorProduct::FORMULA_HOW_TO_STYLE} },

        { QStringLiteral("season_event")
        , QStringLiteral("PageAttributesFashionComboSeasonEvent")
        , { {PageAttributesFashionComboSeasonEvent::ID_SEASON,
             QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME,
             QStringLiteral("season")}
          , {PageAttributesFashionComboSeasonEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboSeasonEvent::FORMULA_OCCASION_SEASONALITY} },

        { QStringLiteral("fit_product_demographic")
        , QStringLiteral("PageAttributesFashionComboFitProductDemographic")
        , { {PageAttributesFashionComboFitProductDemographic::ID_FIT,
             QStringLiteral("PageAttributesFashionFitSilhouette"), PageAttributesFashionFitSilhouette::ID_NAME,
             QStringLiteral("fit")}
          , {PageAttributesFashionComboFitProductDemographic::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboFitProductDemographic::ID_DEMOGRAPHIC,
             QStringLiteral("PageAttributesFashionDemographic"), PageAttributesFashionDemographic::ID_NAME,
             QStringLiteral("demographic")} }
        , {PageAttributesFashionComboFitProductDemographic::FORMULA_FIT_RECOMMENDATION} },

        { QStringLiteral("color_product_event")
        , QStringLiteral("PageAttributesFashionComboColorProductEvent")
        , { {PageAttributesFashionComboColorProductEvent::ID_COLOR,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("color")}
          , {PageAttributesFashionComboColorProductEvent::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboColorProductEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboColorProductEvent::FORMULA_DIRECT_TRANSACTIONAL} },

        { QStringLiteral("material_product_season")
        , QStringLiteral("PageAttributesFashionComboMaterialProductSeason")
        , { {PageAttributesFashionComboMaterialProductSeason::ID_MATERIAL,
             QStringLiteral("PageAttributesFashionMaterial"), PageAttributesFashionMaterial::ID_NAME,
             QStringLiteral("material")}
          , {PageAttributesFashionComboMaterialProductSeason::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboMaterialProductSeason::ID_SEASON,
             QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME,
             QStringLiteral("season")} }
        , {PageAttributesFashionComboMaterialProductSeason::FORMULA_FABRIC_WEATHER} },

        { QStringLiteral("fit_product_event")
        , QStringLiteral("PageAttributesFashionComboFitProductEvent")
        , { {PageAttributesFashionComboFitProductEvent::ID_FIT,
             QStringLiteral("PageAttributesFashionFitSilhouette"), PageAttributesFashionFitSilhouette::ID_NAME,
             QStringLiteral("fit")}
          , {PageAttributesFashionComboFitProductEvent::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboFitProductEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboFitProductEvent::FORMULA_SILHOUETTE_OCCASION} },

        { QStringLiteral("style_season")
        , QStringLiteral("PageAttributesFashionComboStyleSeason")
        , { {PageAttributesFashionComboStyleSeason::ID_STYLE,
             QStringLiteral("PageAttributesFashionStyleAesthetic"), PageAttributesFashionStyleAesthetic::ID_NAME,
             QStringLiteral("style")}
          , {PageAttributesFashionComboStyleSeason::ID_SEASON,
             QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME,
             QStringLiteral("season")} }
        , {PageAttributesFashionComboStyleSeason::FORMULA_MICROTREND_LIFESTYLE,
           PageAttributesFashionComboStyleSeason::FORMULA_CAPSULE_CURATION} },

        { QStringLiteral("color_color")
        , QStringLiteral("PageAttributesFashionComboColorColor")
        , { {PageAttributesFashionComboColorColor::ID_COLOR_A,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("colorA")}
          , {PageAttributesFashionComboColorColor::ID_COLOR_B,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("colorB")} }
        , {PageAttributesFashionComboColorColor::FORMULA_COLOR_PAIRING} },

        { QStringLiteral("product_pattern")
        , QStringLiteral("PageAttributesFashionComboProductPattern")
        , { {PageAttributesFashionComboProductPattern::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboProductPattern::ID_PATTERN,
             QStringLiteral("PageAttributesFashionPattern"), PageAttributesFashionPattern::ID_NAME,
             QStringLiteral("pattern")} }
        , {PageAttributesFashionComboProductPattern::FORMULA_PRINT_PATTERN_STYLING} },

        // ---- Round-2 expansion tables (Google-Ads-volume keyword research) --

        { QStringLiteral("style_product")
        , QStringLiteral("PageAttributesFashionComboStyleProduct")
        , { {PageAttributesFashionComboStyleProduct::ID_STYLE,
             QStringLiteral("PageAttributesFashionStyleAesthetic"), PageAttributesFashionStyleAesthetic::ID_NAME,
             QStringLiteral("style")}
          , {PageAttributesFashionComboStyleProduct::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")} }
        , {PageAttributesFashionComboStyleProduct::FORMULA_STYLE_PRODUCT_OUTFITS} },

        { QStringLiteral("product_event")
        , QStringLiteral("PageAttributesFashionComboProductEvent")
        , { {PageAttributesFashionComboProductEvent::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboProductEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboProductEvent::FORMULA_WHAT_PRODUCT_TO_WEAR} },

        { QStringLiteral("color_season")
        , QStringLiteral("PageAttributesFashionComboColorSeason")
        , { {PageAttributesFashionComboColorSeason::ID_COLOR,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("color")}
          , {PageAttributesFashionComboColorSeason::ID_SEASON,
             QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME,
             QStringLiteral("season")} }
        , {PageAttributesFashionComboColorSeason::FORMULA_COLOR_SEASON_FASHION} },

        { QStringLiteral("product_demographic")
        , QStringLiteral("PageAttributesFashionComboProductDemographic")
        , { {PageAttributesFashionComboProductDemographic::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboProductDemographic::ID_DEMOGRAPHIC,
             QStringLiteral("PageAttributesFashionDemographic"), PageAttributesFashionDemographic::ID_NAME,
             QStringLiteral("demographic")} }
        , {PageAttributesFashionComboProductDemographic::FORMULA_BEST_PRODUCT_FOR_DEMOGRAPHIC} },

        { QStringLiteral("style_event")
        , QStringLiteral("PageAttributesFashionComboStyleEvent")
        , { {PageAttributesFashionComboStyleEvent::ID_STYLE,
             QStringLiteral("PageAttributesFashionStyleAesthetic"), PageAttributesFashionStyleAesthetic::ID_NAME,
             QStringLiteral("style")}
          , {PageAttributesFashionComboStyleEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboStyleEvent::FORMULA_STYLE_EVENT_OUTFITS} },

        { QStringLiteral("material_product")
        , QStringLiteral("PageAttributesFashionComboMaterialProduct")
        , { {PageAttributesFashionComboMaterialProduct::ID_MATERIAL,
             QStringLiteral("PageAttributesFashionMaterial"), PageAttributesFashionMaterial::ID_NAME,
             QStringLiteral("material")}
          , {PageAttributesFashionComboMaterialProduct::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")} }
        , {PageAttributesFashionComboMaterialProduct::FORMULA_HOW_TO_STYLE_MATERIAL_PRODUCT} },

        { QStringLiteral("style_product_event")
        , QStringLiteral("PageAttributesFashionComboStyleProductEvent")
        , { {PageAttributesFashionComboStyleProductEvent::ID_STYLE,
             QStringLiteral("PageAttributesFashionStyleAesthetic"), PageAttributesFashionStyleAesthetic::ID_NAME,
             QStringLiteral("style")}
          , {PageAttributesFashionComboStyleProductEvent::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboStyleProductEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboStyleProductEvent::FORMULA_STYLE_PRODUCT_FOR_EVENT} },

        { QStringLiteral("color_product_demographic")
        , QStringLiteral("PageAttributesFashionComboColorProductDemographic")
        , { {PageAttributesFashionComboColorProductDemographic::ID_COLOR,
             QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME,
             QStringLiteral("color")}
          , {PageAttributesFashionComboColorProductDemographic::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboColorProductDemographic::ID_DEMOGRAPHIC,
             QStringLiteral("PageAttributesFashionDemographic"), PageAttributesFashionDemographic::ID_NAME,
             QStringLiteral("demographic")} }
        , {PageAttributesFashionComboColorProductDemographic::FORMULA_COLOR_PRODUCT_FOR_DEMOGRAPHIC} },

        { QStringLiteral("product_demographic_event")
        , QStringLiteral("PageAttributesFashionComboProductDemographicEvent")
        , { {PageAttributesFashionComboProductDemographicEvent::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboProductDemographicEvent::ID_DEMOGRAPHIC,
             QStringLiteral("PageAttributesFashionDemographic"), PageAttributesFashionDemographic::ID_NAME,
             QStringLiteral("demographic")}
          , {PageAttributesFashionComboProductDemographicEvent::ID_EVENT,
             QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME,
             QStringLiteral("event")} }
        , {PageAttributesFashionComboProductDemographicEvent::FORMULA_PRODUCT_DEMOGRAPHIC_FOR_EVENT} },

        { QStringLiteral("pattern_product_season")
        , QStringLiteral("PageAttributesFashionComboPatternProductSeason")
        , { {PageAttributesFashionComboPatternProductSeason::ID_PATTERN,
             QStringLiteral("PageAttributesFashionPattern"), PageAttributesFashionPattern::ID_NAME,
             QStringLiteral("pattern")}
          , {PageAttributesFashionComboPatternProductSeason::ID_PRODUCT_TYPE,
             QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME,
             QStringLiteral("productType")}
          , {PageAttributesFashionComboPatternProductSeason::ID_SEASON,
             QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME,
             QStringLiteral("season")} }
        , {PageAttributesFashionComboPatternProductSeason::FORMULA_PATTERN_PRODUCT_FOR_SEASON} },
    };
    return specs;
}

// Suffix appended to every combo key in job ids for the current vocabulary
// round, e.g. "combo/color_product.r2/0". Rationale: pages are fixed slices
// of the cross product and vocabulary values are consumed in alphabetical
// order (loadVocabValues ORDER BY), so ANY seed-list growth re-maps every
// page of every combo table. Instead of surgically editing the .ini, bumping
// GeneratorFashionTaxonomy::VOCAB_ROUND makes buildInitialJobIds() emit
// fresh "<key>.r<round>/0" ids: the expanded space is re-walked completely
// under new job ids while the old round's Done pages stay dormant.
// Already-recorded rows are skipped by rowAlreadyRecorded(), so a re-walk
// only re-asks the AI about previously rejected or brand-new tuples.
QString roundSuffix()
{
    if (GeneratorFashionTaxonomy::VOCAB_ROUND < 2) {
        return {};
    }
    return QStringLiteral(".r") + QString::number(GeneratorFashionTaxonomy::VOCAB_ROUND);
}

const ComboSpec *findSpec(const QString &key)
{
    // A job id's key may carry a round suffix (".r2", ".r3", ...) from any
    // past or current round — strip it so stale in-flight replies from an
    // older round still resolve to their spec.
    QString baseKey = key;
    const int dot = baseKey.indexOf(QStringLiteral(".r"));
    if (dot >= 0) {
        baseKey.truncate(dot);
    }
    for (const ComboSpec &spec : comboSpecs()) {
        if (spec.key == baseKey) {
            return &spec;
        }
    }
    return nullptr;
}

// Reads all values of a vocabulary table's name column, ordered for
// determinism regardless of insertion order.
QStringList loadVocabValues(const AbstractGenerator *gen, const QString &attrId, const QString &nameColumnId)
{
    const DownloadedPagesTable *table = gen->resultsTable(attrId);
    if (!table) {
        return {};
    }
    QSqlQuery q(table->database());
    QStringList values;
    if (q.exec(QStringLiteral("SELECT \"%1\" FROM records ORDER BY \"%1\"").arg(nameColumnId))) {
        while (q.next()) {
            const QString v = q.value(0).toString().trimmed();
            if (!v.isEmpty()) {
                values << v;
            }
        }
    }
    return values;
}

// Total number of distinct (slot-tuple, formulaId) combinations for spec,
// given each slot's vocabulary size.
qint64 totalCandidateCount(const QList<QStringList> &slotValues, int formulaCount)
{
    qint64 total = formulaCount;
    for (const QStringList &values : slotValues) {
        total *= values.size();
    }
    return total;
}

// Decodes linear index `n` (0-based, into the full cross product ordered
// exactly as the old nested-loop enumeration was: formulaId fastest, then
// dimensionSlots.last() next-fastest, up to dimensionSlots.first() slowest)
// into a candidate tuple. Pure function of (spec, slotValues, n) — no DB
// scan, no dependency on what has or hasn't been recorded yet.
QHash<QString, QString> decodeCandidate(const ComboSpec &spec, const QList<QStringList> &slotValues, qint64 n)
{
    QHash<QString, QString> candidate;
    const int formulaCount = spec.formulaIds.size();
    const int formulaIdx = int(n % formulaCount);
    n /= formulaCount;

    const int nSlots = spec.dimensionSlots.size();
    QList<int> slotIdx(nSlots, 0);
    for (int i = nSlots - 1; i >= 0; --i) {
        const int size = slotValues[i].size();
        slotIdx[i] = int(n % size);
        n /= size;
    }
    for (int i = 0; i < nSlots; ++i) {
        candidate.insert(spec.dimensionSlots[i].attrId, slotValues[i][slotIdx[i]]);
    }
    candidate.insert(QStringLiteral("__formulaId"), spec.formulaIds[formulaIdx]);
    return candidate;
}

// Returns up to MAX_CANDIDATES_PER_JOB candidate tuples starting at
// page*MAX_CANDIDATES_PER_JOB in the fixed deterministic ordering over the
// cross product of spec's dimension slots and formula ids.
//
// Deliberately NOT "skip whatever is already recorded in the DB": a
// rejected (empty-culture) candidate is never written to the combo table,
// so a recorded-based scheme can never tell it apart from "not yet
// assessed" — every subsequent page would re-walk from the start and
// re-send the same rejected candidates forever, for any table with a
// less-than-100% acceptance rate. The page number alone is enough to name a
// fixed slice of the space, so every tuple gets visited EXACTLY once across
// all pages regardless of the AI's accept/reject outcome.
QList<QHash<QString, QString>> nextCandidateBatch(const AbstractGenerator *gen, const ComboSpec &spec, int page)
{
    QList<QStringList> slotValues;
    for (const ComboSlot &slot : spec.dimensionSlots) {
        slotValues << loadVocabValues(gen, slot.vocabAttrId, slot.vocabNameId);
    }
    for (const QStringList &values : std::as_const(slotValues)) {
        if (values.isEmpty()) {
            return {}; // vocabulary not seeded yet — no candidates possible
        }
    }

    const qint64 total = totalCandidateCount(slotValues, spec.formulaIds.size());
    const qint64 start = qint64(page) * GeneratorFashionTaxonomy::MAX_CANDIDATES_PER_JOB;
    if (start >= total) {
        return {}; // this table's whole space was already covered by an earlier page
    }
    const qint64 end = qMin(start + GeneratorFashionTaxonomy::MAX_CANDIDATES_PER_JOB, total);

    QList<QHash<QString, QString>> batch;
    for (qint64 i = start; i < end; ++i) {
        batch << decodeCandidate(spec, slotValues, i);
    }
    return batch;
}

// Defensive duplicate guard: true if a row with these exact slot values +
// formulaId already exists in spec's table. Only meant to protect against
// the same job id being dispatched twice by two overlapping generator
// processes sharing one working dir (observed in practice) — NOT part of
// the normal per-page dedup logic above, which no longer needs one.
bool rowAlreadyRecorded(const AbstractGenerator *gen, const ComboSpec &spec, const QHash<QString, QString> &candidate)
{
    const DownloadedPagesTable *table = gen->resultsTable(spec.attrId);
    if (!table) {
        return false;
    }
    QStringList conditions;
    for (const ComboSlot &slot : spec.dimensionSlots) {
        conditions << QStringLiteral("\"%1\" = ?").arg(slot.attrId);
    }
    conditions << QStringLiteral("\"%1\" = ?").arg(PageAttributesFashionComboBase::ID_FORMULA_ID);

    QSqlQuery q(table->database());
    q.prepare(QStringLiteral("SELECT 1 FROM records WHERE %1 LIMIT 1").arg(conditions.join(QStringLiteral(" AND "))));
    for (const ComboSlot &slot : spec.dimensionSlots) {
        q.addBindValue(candidate.value(slot.attrId));
    }
    q.addBindValue(candidate.value(QStringLiteral("__formulaId")));
    return q.exec() && q.next();
}

QJsonArray toJsonArray(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list) {
        arr.append(s);
    }
    return arr;
}

// Keeps only entries that appear verbatim in 'allowed', de-duplicated,
// order-preserved — guards against Claude inventing culture names that
// don't exist in the vocabulary table.
QStringList filterToAvailable(const QJsonArray &arr, const QSet<QString> &allowed)
{
    QStringList result;
    for (const QJsonValue &v : arr) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && allowed.contains(s) && !result.contains(s)) {
            result << s;
        }
    }
    return result;
}

QJsonObject buildComboPayload(const AbstractGenerator *gen, const ComboSpec &spec, int page)
{
    const QList<QHash<QString, QString>> batch = nextCandidateBatch(gen, spec, page);
    const QStringList cultures = loadVocabValues(
        gen, QStringLiteral("PageAttributesFashionCulture"), PageAttributesFashionCulture::ID_NAME);

    QJsonArray candidatesJson;
    for (int i = 0; i < batch.size(); ++i) {
        QJsonObject c;
        c[QStringLiteral("index")] = i;
        for (const ComboSlot &slot : spec.dimensionSlots) {
            c[slot.jsonKey] = batch[i].value(slot.attrId);
        }
        c[QStringLiteral("formulaId")] = batch[i].value(QStringLiteral("__formulaId"));
        candidatesJson.append(c);
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]              = QStringLiteral("fashion_combo_assessment");
    payload[QStringLiteral("comboKey")]          = spec.key;
    payload[QStringLiteral("page")]              = page;
    payload[QStringLiteral("candidates")]        = candidatesJson;
    payload[QStringLiteral("availableCultures")] = toJsonArray(cultures);
    payload[QStringLiteral("instructions")]      = QObject::tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "For each candidate, decide which of 'availableCultures' this specific outfit "
        "combination is contextually and/or aesthetically appropriate for — consider "
        "mourning-color conventions, modesty norms, festival/ceremony associations, and "
        "climate/seasonal fit, which differ by culture. Return ONLY culture names copied "
        "EXACTLY from 'availableCultures'. If the combination does not make sense in ANY "
        "culture, return an empty 'cultures' array for that candidate — it will be discarded "
        "and NOT saved. Also estimate 'msv' (approximate monthly Google search volume as a "
        "non-negative integer; omit the field entirely if you have no basis for an estimate) "
        "and, if there is a clear seasonal peak, 'peakSeasonStart'/'peakSeasonEnd' as month "
        "numbers 1-12 (omit both if there is no clear seasonal peak). "
        "Return exactly one result per candidate, matching 'index'. If 'candidates' is "
        "empty, return an empty 'results' array.");

    QJsonObject resultSchema;
    resultSchema[QStringLiteral("index")]           = QStringLiteral("integer — matches candidate index");
    resultSchema[QStringLiteral("cultures")]        = QJsonArray{};
    resultSchema[QStringLiteral("msv")]             = QStringLiteral("integer (optional)");
    resultSchema[QStringLiteral("peakSeasonStart")] = QStringLiteral("integer 1-12 (optional)");
    resultSchema[QStringLiteral("peakSeasonEnd")]   = QStringLiteral("integer 1-12 (optional)");

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("results")] = QJsonArray{resultSchema};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

// ---- Static vocabulary seed data --------------------------------------------

struct NameFamily {
    QString name;
    QString family;
};

QStringList seedProductTypes()
{
    return {
        // ---- Round-1 seeds (fashion taxonomy study) -------------------------
        QStringLiteral("Heels"), QStringLiteral("Boots"), QStringLiteral("Midi Dress"),
        QStringLiteral("Cargo Pants"), QStringLiteral("Trench Coat"), QStringLiteral("Slip Dress"),
        QStringLiteral("Leather Jacket"), QStringLiteral("Tote Bag"), QStringLiteral("Wide-Leg Trousers"),
        QStringLiteral("Oversized Sweater"), QStringLiteral("Dress"), QStringLiteral("Pants"),
        QStringLiteral("Jeans"), QStringLiteral("Blazer"), QStringLiteral("Skirt"),
        QStringLiteral("Jumpsuit"), QStringLiteral("Suit"), QStringLiteral("Gown"),
        QStringLiteral("Cardigan"), QStringLiteral("Shirt"), QStringLiteral("Leggings"),
        QStringLiteral("Bra"), QStringLiteral("Swimsuit"), QStringLiteral("Sweater"), QStringLiteral("Coat"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        // Footwear
        QStringLiteral("Sneakers"), QStringLiteral("Loafers"), QStringLiteral("Sandals"),
        QStringLiteral("Mules"), QStringLiteral("Ankle Boots"), QStringLiteral("Cowboy Boots"),
        QStringLiteral("Knee-High Boots"), QStringLiteral("Ballet Flats"), QStringLiteral("Mary Janes"),
        QStringLiteral("Chelsea Boots"), QStringLiteral("Espadrilles"), QStringLiteral("Clogs"),
        QStringLiteral("Oxford Shoes"), QStringLiteral("Platform Shoes"), QStringLiteral("Wedges"),
        QStringLiteral("Slide Sandals"), QStringLiteral("Stilettos"),
        // Outerwear
        QStringLiteral("Puffer Jacket"), QStringLiteral("Denim Jacket"), QStringLiteral("Bomber Jacket"),
        QStringLiteral("Shacket"), QStringLiteral("Vest"), QStringLiteral("Duster Coat"),
        QStringLiteral("Parka"), QStringLiteral("Pea Coat"), QStringLiteral("Kimono"),
        QStringLiteral("Poncho"), QStringLiteral("Windbreaker"), QStringLiteral("Teddy Coat"),
        QStringLiteral("Fleece Jacket"),
        // Tops
        QStringLiteral("Corset Top"), QStringLiteral("Bodysuit"), QStringLiteral("Crop Top"),
        QStringLiteral("Blouse"), QStringLiteral("Button-Down Shirt"), QStringLiteral("Polo Shirt"),
        QStringLiteral("Turtleneck"), QStringLiteral("Tube Top"), QStringLiteral("Halter Top"),
        QStringLiteral("Camisole"), QStringLiteral("Peplum Top"), QStringLiteral("Tank Top"),
        QStringLiteral("Hoodie"), QStringLiteral("Sweatshirt"),
        // Bottoms
        QStringLiteral("Joggers"), QStringLiteral("Sweatpants"), QStringLiteral("Cargo Shorts"),
        QStringLiteral("Biker Shorts"), QStringLiteral("Denim Shorts"), QStringLiteral("Bermuda Shorts"),
        QStringLiteral("Skort"), QStringLiteral("Palazzo Pants"), QStringLiteral("Culottes"),
        QStringLiteral("Flare Pants"), QStringLiteral("Straight-Leg Jeans"), QStringLiteral("Mom Jeans"),
        QStringLiteral("Boyfriend Jeans"), QStringLiteral("Skinny Jeans"),
        // Skirts
        QStringLiteral("Pencil Skirt"), QStringLiteral("Pleated Skirt"), QStringLiteral("Maxi Skirt"),
        QStringLiteral("Mini Skirt"), QStringLiteral("Wrap Skirt"), QStringLiteral("Tennis Skirt"),
        QStringLiteral("Denim Skirt"), QStringLiteral("Slip Skirt"), QStringLiteral("A-Line Skirt"),
        // Dresses & one-pieces
        QStringLiteral("Blazer Dress"), QStringLiteral("Maxi Dress"), QStringLiteral("Mini Dress"),
        QStringLiteral("Sundress"), QStringLiteral("Wrap Dress"), QStringLiteral("Cocktail Dress"),
        QStringLiteral("Shirt Dress"), QStringLiteral("Sweater Dress"), QStringLiteral("Bodycon Dress"),
        QStringLiteral("T-Shirt Dress"), QStringLiteral("Romper"), QStringLiteral("Overalls"),
        // Cultural garments (feed the per-culture SEO territories)
        QStringLiteral("Saree"), QStringLiteral("Lehenga"), QStringLiteral("Abaya"),
        QStringLiteral("Hijab"), QStringLiteral("Kaftan"), QStringLiteral("Qipao"),
        QStringLiteral("Hanbok"), QStringLiteral("Kurti"), QStringLiteral("Salwar Kameez"),
        QStringLiteral("Ankara Dress"),
        // Bags
        QStringLiteral("Crossbody Bag"), QStringLiteral("Clutch"), QStringLiteral("Shoulder Bag"),
        QStringLiteral("Backpack"), QStringLiteral("Belt Bag"), QStringLiteral("Bucket Bag"),
        // Swim & intimates
        QStringLiteral("Bralette"), QStringLiteral("Bikini"), QStringLiteral("One-Piece Swimsuit"),
        QStringLiteral("Swim Cover-Up"),
    };
}

QStringList seedColorFamilies()
{
    return {
        QStringLiteral("Neutrals"), QStringLiteral("Pastels"), QStringLiteral("Jewel Tones"),
        QStringLiteral("Vibrant/Brights"), QStringLiteral("Earth Tones"), QStringLiteral("Metallics"),
    };
}

QList<NameFamily> seedColors()
{
    return {
        // Neutrals
        {QStringLiteral("Off-White"), QStringLiteral("Neutrals")},
        {QStringLiteral("Ivory"), QStringLiteral("Neutrals")},
        {QStringLiteral("Charcoal"), QStringLiteral("Neutrals")},
        {QStringLiteral("Oat"), QStringLiteral("Neutrals")},
        {QStringLiteral("Camel"), QStringLiteral("Neutrals")},
        {QStringLiteral("Espresso"), QStringLiteral("Neutrals")},
        {QStringLiteral("Taupe"), QStringLiteral("Neutrals")},
        {QStringLiteral("Sand"), QStringLiteral("Neutrals")},
        {QStringLiteral("Black"), QStringLiteral("Neutrals")},
        {QStringLiteral("White"), QStringLiteral("Neutrals")},
        {QStringLiteral("Beige"), QStringLiteral("Neutrals")},
        {QStringLiteral("Brown"), QStringLiteral("Neutrals")},
        {QStringLiteral("Gray"), QStringLiteral("Neutrals")},
        {QStringLiteral("Navy"), QStringLiteral("Neutrals")},
        // Pastels
        {QStringLiteral("Baby Pink"), QStringLiteral("Pastels")},
        {QStringLiteral("Azure Blue"), QStringLiteral("Pastels")},
        {QStringLiteral("Pastel Green"), QStringLiteral("Pastels")},
        {QStringLiteral("Powder Blue"), QStringLiteral("Pastels")},
        {QStringLiteral("Mint"), QStringLiteral("Pastels")},
        {QStringLiteral("Lavender"), QStringLiteral("Pastels")},
        // Jewel Tones
        {QStringLiteral("Emerald Green"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Sapphire Blue"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Burgundy"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Plum"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Teal"), QStringLiteral("Jewel Tones")},
        // Vibrant/Brights
        {QStringLiteral("Red"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Crimson"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Coral"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Electric Blue"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Hot Pink"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Sunflower Yellow"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Pink"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Yellow"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Orange"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Purple"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Green"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Blue"), QStringLiteral("Vibrant/Brights")},
        // Earth Tones
        {QStringLiteral("Sage Green"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Olive Green"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Rust Orange"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Ochre"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Khaki"), QStringLiteral("Earth Tones")},
        // Metallics
        {QStringLiteral("Champagne Gold"), QStringLiteral("Metallics")},
        {QStringLiteral("Antique Silver"), QStringLiteral("Metallics")},
        {QStringLiteral("Metallic Bronze"), QStringLiteral("Metallics")},
        {QStringLiteral("Shimmering Champagne"), QStringLiteral("Metallics")},
        {QStringLiteral("Gold"), QStringLiteral("Metallics")},
        {QStringLiteral("Silver"), QStringLiteral("Metallics")},
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        {QStringLiteral("Cream"), QStringLiteral("Neutrals")},
        {QStringLiteral("Nude"), QStringLiteral("Neutrals")},
        {QStringLiteral("Slate"), QStringLiteral("Neutrals")},
        {QStringLiteral("Mocha"), QStringLiteral("Neutrals")},
        {QStringLiteral("Caramel"), QStringLiteral("Neutrals")},
        {QStringLiteral("Dusty Rose"), QStringLiteral("Pastels")},
        {QStringLiteral("Baby Blue"), QStringLiteral("Pastels")},
        {QStringLiteral("Lilac"), QStringLiteral("Pastels")},
        {QStringLiteral("Blush Pink"), QStringLiteral("Pastels")},
        {QStringLiteral("Peach"), QStringLiteral("Pastels")},
        {QStringLiteral("Pale Yellow"), QStringLiteral("Pastels")},
        {QStringLiteral("Sky Blue"), QStringLiteral("Pastels")},
        {QStringLiteral("Mauve"), QStringLiteral("Pastels")},
        {QStringLiteral("Periwinkle"), QStringLiteral("Pastels")},
        {QStringLiteral("Apricot"), QStringLiteral("Pastels")},
        {QStringLiteral("Ruby Red"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Amethyst"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Maroon"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Wine"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Indigo"), QStringLiteral("Jewel Tones")},
        {QStringLiteral("Mustard Yellow"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Cobalt Blue"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Magenta"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Neon Green"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Fuchsia"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Turquoise"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Royal Blue"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Lime Green"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Violet"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Salmon"), QStringLiteral("Vibrant/Brights")},
        {QStringLiteral("Terracotta"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Chocolate Brown"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Forest Green"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Hunter Green"), QStringLiteral("Earth Tones")},
        {QStringLiteral("Rose Gold"), QStringLiteral("Metallics")},
        {QStringLiteral("Copper"), QStringLiteral("Metallics")},
    };
}

QStringList seedSeasons()
{
    return {
        QStringLiteral("Spring"), QStringLiteral("Summer"), QStringLiteral("Autumn/Fall"),
        QStringLiteral("Winter"), QStringLiteral("Transition/Early Spring"), QStringLiteral("Resort/Cruise"),
        QStringLiteral("Heatwave"), QStringLiteral("Rainy Season"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Holiday Season"), QStringLiteral("Festival Season"),
        QStringLiteral("Wedding Season"), QStringLiteral("Back to School"),
    };
}

QStringList seedEvents()
{
    return {
        QStringLiteral("Wedding Guest"), QStringLiteral("Bride/Bridal Shower"), QStringLiteral("Cocktail Party"),
        QStringLiteral("Gala/Black Tie"), QStringLiteral("Job Interview"), QStringLiteral("Date Night"),
        QStringLiteral("Beach Vacation"), QStringLiteral("Music Festival"), QStringLiteral("Graduation"),
        QStringLiteral("Funeral"), QStringLiteral("New Year's Eve"), QStringLiteral("Baby Shower"),
        QStringLiteral("Holiday Party"), QStringLiteral("Birthday Party"), QStringLiteral("Office Work"),
        QStringLiteral("Rehearsal Dinner"), QStringLiteral("Engagement Photos"), QStringLiteral("Country Concert"),
        QStringLiteral("Brunch"), QStringLiteral("Wine Tasting"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Prom"), QStringLiteral("Homecoming"), QStringLiteral("Quinceanera"),
        QStringLiteral("Eid"), QStringLiteral("Diwali"), QStringLiteral("Lunar New Year"),
        QStringLiteral("Bachelorette Party"), QStringLiteral("Pool Party"), QStringLiteral("Rooftop Party"),
        QStringLiteral("Dinner Party"), QStringLiteral("Yacht Party"), QStringLiteral("Nightclub"),
        QStringLiteral("Winery Tour"), QStringLiteral("Safari Vacation"), QStringLiteral("Ski Trip"),
        QStringLiteral("European Summer Vacation"), QStringLiteral("Cruise Vacation"),
        QStringLiteral("Business Casual Event"), QStringLiteral("Corporate Gala"), QStringLiteral("High Tea"),
        QStringLiteral("Baptism"), QStringLiteral("Bar Mitzvah"), QStringLiteral("Gender Reveal Party"),
        QStringLiteral("Welcome Dinner"), QStringLiteral("Farewell Dinner"), QStringLiteral("Horse Race"),
        QStringLiteral("Art Gallery Opening"), QStringLiteral("Fashion Show"), QStringLiteral("College Class"),
        QStringLiteral("Court Appearance"), QStringLiteral("Airport Travel"), QStringLiteral("Spa Day"),
        QStringLiteral("Casino Night"), QStringLiteral("Halloween Party"), QStringLiteral("Tailgate Party"),
    };
}

QStringList seedStyles()
{
    return {
        QStringLiteral("Old Money/Quiet Luxury"), QStringLiteral("Y2K Retro"), QStringLiteral("Boho Chic"),
        QStringLiteral("Clean Girl Minimalist"), QStringLiteral("Streetwear"), QStringLiteral("Preppy"),
        QStringLiteral("Dark Academia"), QStringLiteral("Cottagecore"), QStringLiteral("Glam"),
        QStringLiteral("Coastal Grandmother"), QStringLiteral("Goth"), QStringLiteral("Minimalist"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Casual Chic"), QStringLiteral("Smart Casual"), QStringLiteral("Business Casual"),
        QStringLiteral("Grunge"), QStringLiteral("Indie Sleaze"), QStringLiteral("Light Academia"),
        QStringLiteral("Vintage"), QStringLiteral("Techwear"), QStringLiteral("Rocker Chic"),
        QStringLiteral("Tomboy"), QStringLiteral("Coquette"), QStringLiteral("Equestrian Chic"),
        QStringLiteral("Utilitarian"), QStringLiteral("Modest Fashion"), QStringLiteral("Athleisure"),
    };
}

QStringList seedFits()
{
    return {
        QStringLiteral("Mini"), QStringLiteral("Midi"), QStringLiteral("Maxi"), QStringLiteral("Oversized"),
        QStringLiteral("Bodycon"), QStringLiteral("High-Waisted"), QStringLiteral("Cropped"),
        QStringLiteral("A-Line"), QStringLiteral("Wide-Leg"), QStringLiteral("Tailored"),
        QStringLiteral("Fit and Flare"), QStringLiteral("Off-the-Shoulder"), QStringLiteral("Strapless"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Slim Fit"), QStringLiteral("Relaxed Fit"), QStringLiteral("Corseted"),
        QStringLiteral("Asymmetrical"), QStringLiteral("Empire Waist"), QStringLiteral("Wrap"),
        QStringLiteral("Tiered"), QStringLiteral("Halter"), QStringLiteral("Ruched"),
        QStringLiteral("Backless"),
    };
}

QStringList seedMaterials()
{
    return {
        QStringLiteral("Silk"), QStringLiteral("Satin"), QStringLiteral("Leather"), QStringLiteral("Linen"),
        QStringLiteral("Cashmere"), QStringLiteral("Velvet"), QStringLiteral("Denim"), QStringLiteral("Wool"),
        QStringLiteral("Cotton"), QStringLiteral("Tweed"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Chiffon"), QStringLiteral("Tulle"), QStringLiteral("Corduroy"),
        QStringLiteral("Organza"), QStringLiteral("Mesh"), QStringLiteral("Lace"),
        QStringLiteral("Suede"), QStringLiteral("Fleece"), QStringLiteral("Crochet"),
        QStringLiteral("Faux Fur"), QStringLiteral("Nylon"), QStringLiteral("Mohair"),
    };
}

QStringList seedPatterns()
{
    return {
        QStringLiteral("Floral"), QStringLiteral("Plaid/Tartan"), QStringLiteral("Houndstooth"),
        QStringLiteral("Leopard Print"), QStringLiteral("Polka Dot"), QStringLiteral("Camo"),
        QStringLiteral("Striped"), QStringLiteral("Gingham"), QStringLiteral("Snake Print"),
        QStringLiteral("Zebra Print"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Pinstripe"), QStringLiteral("Argyle"), QStringLiteral("Paisley"),
        QStringLiteral("Tie-Dye"), QStringLiteral("Geometric Print"), QStringLiteral("Abstract Print"),
        QStringLiteral("Cow Print"), QStringLiteral("Tiger Print"), QStringLiteral("Toile de Jouy"),
        QStringLiteral("Chevron"), QStringLiteral("Marble Print"), QStringLiteral("Herringbone"),
    };
}

QStringList seedDemographics()
{
    return {
        QStringLiteral("Petite"), QStringLiteral("Plus Size"), QStringLiteral("Tall"),
        QStringLiteral("Hourglass"), QStringLiteral("Pear Shape"), QStringLiteral("Apple Shape"),
        QStringLiteral("Rectangle"), QStringLiteral("Maternity"),
        // ---- Round-2 seeds (Google-Ads-volume keyword research) -------------
        QStringLiteral("Over 50"), QStringLiteral("Over 40"), QStringLiteral("Mid-Size"),
        QStringLiteral("Broad Shoulders"), QStringLiteral("Inverted Triangle"),
        QStringLiteral("Long Torso"), QStringLiteral("Short Torso"),
    };
}

QStringList seedCultures()
{
    // Selected to maximize distinct, non-cannibalizing SEO territory —
    // see conversation notes: each segment has its own garment/occasion
    // vocabulary (saree/lehenga, hijab/abaya, ankara/kente, k-fashion terms)
    // so per-culture sites rank on different query clusters.
    return {
        QStringLiteral("Western/Mainstream"), QStringLiteral("South Asian/Indian"),
        QStringLiteral("Muslim/Modest"), QStringLiteral("East Asian"), QStringLiteral("African/Diaspora"),
    };
}

} // namespace

// =============================================================================
// GeneratorFashionTaxonomy
// =============================================================================

GeneratorFashionTaxonomy::GeneratorFashionTaxonomy(const QDir &workingDir, QObject *parent)
    : AbstractGenerator(workingDir, parent)
{
}

QString GeneratorFashionTaxonomy::getId() const
{
    return QStringLiteral("fashion_taxonomy");
}

QString GeneratorFashionTaxonomy::getName() const
{
    return tr("Fashion Taxonomy Database");
}

AbstractGenerator *GeneratorFashionTaxonomy::createInstance(const QDir &workingDir) const
{
    return new GeneratorFashionTaxonomy(workingDir);
}

QMap<QString, AbstractPageAttributes *> GeneratorFashionTaxonomy::createResultPageAttributes() const
{
    return {
        {tr("Product Types"),       new PageAttributesFashionProductType()},
        {tr("Color Families"),      new PageAttributesFashionColorFamily()},
        {tr("Colors"),               new PageAttributesFashionColor()},
        {tr("Seasons"),              new PageAttributesFashionSeason()},
        {tr("Events"),               new PageAttributesFashionEvent()},
        {tr("Styles/Aesthetics"),   new PageAttributesFashionStyleAesthetic()},
        {tr("Fits/Silhouettes"),    new PageAttributesFashionFitSilhouette()},
        {tr("Materials"),            new PageAttributesFashionMaterial()},
        {tr("Patterns"),             new PageAttributesFashionPattern()},
        {tr("Demographics"),        new PageAttributesFashionDemographic()},
        {tr("Cultures"),             new PageAttributesFashionCulture()},
        {tr("Combo: Color + Product"),               new PageAttributesFashionComboColorProduct()},
        {tr("Combo: Season + Event"),                 new PageAttributesFashionComboSeasonEvent()},
        {tr("Combo: Fit + Product + Demographic"),    new PageAttributesFashionComboFitProductDemographic()},
        {tr("Combo: Color + Product + Event"),        new PageAttributesFashionComboColorProductEvent()},
        {tr("Combo: Material + Product + Season"),    new PageAttributesFashionComboMaterialProductSeason()},
        {tr("Combo: Fit + Product + Event"),          new PageAttributesFashionComboFitProductEvent()},
        {tr("Combo: Style + Season"),                 new PageAttributesFashionComboStyleSeason()},
        {tr("Combo: Color + Color"),                  new PageAttributesFashionComboColorColor()},
        {tr("Combo: Product + Pattern"),              new PageAttributesFashionComboProductPattern()},
        {tr("Combo: Style + Product"),                new PageAttributesFashionComboStyleProduct()},
        {tr("Combo: Product + Event"),                new PageAttributesFashionComboProductEvent()},
        {tr("Combo: Color + Season"),                 new PageAttributesFashionComboColorSeason()},
        {tr("Combo: Product + Demographic"),          new PageAttributesFashionComboProductDemographic()},
        {tr("Combo: Style + Event"),                  new PageAttributesFashionComboStyleEvent()},
        {tr("Combo: Material + Product"),             new PageAttributesFashionComboMaterialProduct()},
        {tr("Combo: Style + Product + Event"),        new PageAttributesFashionComboStyleProductEvent()},
        {tr("Combo: Color + Product + Demographic"),  new PageAttributesFashionComboColorProductDemographic()},
        {tr("Combo: Product + Demographic + Event"),  new PageAttributesFashionComboProductDemographicEvent()},
        {tr("Combo: Pattern + Product + Season"),     new PageAttributesFashionComboPatternProductSeason()},
    };
}

AbstractGenerator::GeneratorTables GeneratorFashionTaxonomy::getTables() const
{
    GeneratorTables tables;

    // Primary: every combination table is independently "one row = one
    // article" — a generation strategy picks which combo it sources from
    // (see DialogAddGeneration's "Source table" picker). The flagship entry
    // (color+product for a specific event — mourning/bridal/festival color
    // conventions vary sharply by culture) is listed first for readability
    // only; QHash does not preserve insertion order.
    const QList<QPair<QString, QString>> primaryCombos = {
        {QStringLiteral("PageAttributesFashionComboColorProductEvent"), tr("Combo: Color + Product + Event")},
        {QStringLiteral("PageAttributesFashionComboColorProduct"), tr("Combo: Color + Product")},
        {QStringLiteral("PageAttributesFashionComboSeasonEvent"), tr("Combo: Season + Event")},
        {QStringLiteral("PageAttributesFashionComboFitProductDemographic"), tr("Combo: Fit + Product + Demographic")},
        {QStringLiteral("PageAttributesFashionComboMaterialProductSeason"), tr("Combo: Material + Product + Season")},
        {QStringLiteral("PageAttributesFashionComboFitProductEvent"), tr("Combo: Fit + Product + Event")},
        {QStringLiteral("PageAttributesFashionComboStyleSeason"), tr("Combo: Style + Season")},
        {QStringLiteral("PageAttributesFashionComboColorColor"), tr("Combo: Color + Color")},
        {QStringLiteral("PageAttributesFashionComboProductPattern"), tr("Combo: Product + Pattern")},
        {QStringLiteral("PageAttributesFashionComboStyleProduct"), tr("Combo: Style + Product")},
        {QStringLiteral("PageAttributesFashionComboProductEvent"), tr("Combo: Product + Event")},
        {QStringLiteral("PageAttributesFashionComboColorSeason"), tr("Combo: Color + Season")},
        {QStringLiteral("PageAttributesFashionComboProductDemographic"), tr("Combo: Product + Demographic")},
        {QStringLiteral("PageAttributesFashionComboStyleEvent"), tr("Combo: Style + Event")},
        {QStringLiteral("PageAttributesFashionComboMaterialProduct"), tr("Combo: Material + Product")},
        {QStringLiteral("PageAttributesFashionComboStyleProductEvent"), tr("Combo: Style + Product + Event")},
        {QStringLiteral("PageAttributesFashionComboColorProductDemographic"), tr("Combo: Color + Product + Demographic")},
        {QStringLiteral("PageAttributesFashionComboProductDemographicEvent"), tr("Combo: Product + Demographic + Event")},
        {QStringLiteral("PageAttributesFashionComboPatternProductSeason"), tr("Combo: Pattern + Product + Season")},
    };
    for (const auto &p : primaryCombos) {
        const TableDescriptor d = _makeDescriptor(p.first, p.second);
        tables.primary.insert(d.id, d);
    }

    // Category: the 11 controlled-vocabulary dimension tables.
    const QList<QPair<QString, QString>> vocab = {
        {QStringLiteral("PageAttributesFashionProductType"), tr("Product Types")},
        {QStringLiteral("PageAttributesFashionColorFamily"), tr("Color Families")},
        {QStringLiteral("PageAttributesFashionColor"), tr("Colors")},
        {QStringLiteral("PageAttributesFashionSeason"), tr("Seasons")},
        {QStringLiteral("PageAttributesFashionEvent"), tr("Events")},
        {QStringLiteral("PageAttributesFashionStyleAesthetic"), tr("Styles/Aesthetics")},
        {QStringLiteral("PageAttributesFashionFitSilhouette"), tr("Fits/Silhouettes")},
        {QStringLiteral("PageAttributesFashionMaterial"), tr("Materials")},
        {QStringLiteral("PageAttributesFashionPattern"), tr("Patterns")},
        {QStringLiteral("PageAttributesFashionDemographic"), tr("Demographics")},
        {QStringLiteral("PageAttributesFashionCulture"), tr("Cultures")},
    };
    for (const auto &v : vocab) {
        const TableDescriptor d = _makeDescriptor(v.first, v.second);
        tables.category.insert(d.id, d);
    }

    // ReferredTo: none — every combo table is a standalone primary source
    // (see above); no table here is a "child of" another combo row.

    Q_ASSERT(tables.primary.size() == primaryCombos.size());
    Q_ASSERT(tables.referredTo.isEmpty());
    return tables;
}

void GeneratorFashionTaxonomy::seedStaticVocabulary()
{
    auto seedSimple = [this](const QString &attrId, const QString &nameId, const QStringList &names) {
        const QStringList existing = loadVocabValues(this, attrId, nameId);
        QSet<QString> seen(existing.begin(), existing.end());
        for (const QString &name : names) {
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            QHash<QString, QString> attrs;
            attrs.insert(nameId, name);
            recordResultPage(attrId, attrs);
        }
    };

    seedSimple(QStringLiteral("PageAttributesFashionProductType"), PageAttributesFashionProductType::ID_NAME, seedProductTypes());
    seedSimple(QStringLiteral("PageAttributesFashionColorFamily"), PageAttributesFashionColorFamily::ID_NAME, seedColorFamilies());

    {
        const QStringList existing = loadVocabValues(
            this, QStringLiteral("PageAttributesFashionColor"), PageAttributesFashionColor::ID_NAME);
        QSet<QString> seen(existing.begin(), existing.end());
        for (const NameFamily &nf : seedColors()) {
            if (nf.name.isEmpty() || seen.contains(nf.name)) {
                continue;
            }
            seen.insert(nf.name);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesFashionColor::ID_NAME, nf.name);
            attrs.insert(PageAttributesFashionColor::ID_FAMILY, nf.family);
            recordResultPage(QStringLiteral("PageAttributesFashionColor"), attrs);
        }
    }

    seedSimple(QStringLiteral("PageAttributesFashionSeason"), PageAttributesFashionSeason::ID_NAME, seedSeasons());
    seedSimple(QStringLiteral("PageAttributesFashionEvent"), PageAttributesFashionEvent::ID_NAME, seedEvents());
    seedSimple(QStringLiteral("PageAttributesFashionStyleAesthetic"), PageAttributesFashionStyleAesthetic::ID_NAME, seedStyles());
    seedSimple(QStringLiteral("PageAttributesFashionFitSilhouette"), PageAttributesFashionFitSilhouette::ID_NAME, seedFits());
    seedSimple(QStringLiteral("PageAttributesFashionMaterial"), PageAttributesFashionMaterial::ID_NAME, seedMaterials());
    seedSimple(QStringLiteral("PageAttributesFashionPattern"), PageAttributesFashionPattern::ID_NAME, seedPatterns());
    seedSimple(QStringLiteral("PageAttributesFashionDemographic"), PageAttributesFashionDemographic::ID_NAME, seedDemographics());
    seedSimple(QStringLiteral("PageAttributesFashionCulture"), PageAttributesFashionCulture::ID_NAME, seedCultures());
}

// ---- Job-ID helpers ---------------------------------------------------------

QString GeneratorFashionTaxonomy::comboKeyFromJobId(const QString &jobId)
{
    // "combo/<key>/<page>" -> "<key>"
    const int first = jobId.indexOf(QLatin1Char('/'));
    if (first < 0) {
        return {};
    }
    const int second = jobId.indexOf(QLatin1Char('/'), first + 1);
    return second >= 0 ? jobId.mid(first + 1, second - first - 1) : QString{};
}

int GeneratorFashionTaxonomy::pageFromJobId(const QString &jobId)
{
    const QStringList parts = jobId.split(QLatin1Char('/'));
    return parts.isEmpty() ? 0 : parts.last().toInt();
}

// ---- AbstractGenerator overrides --------------------------------------------

QStringList GeneratorFashionTaxonomy::buildInitialJobIds() const
{
    QStringList ids;
    for (const ComboSpec &spec : comboSpecs()) {
        ids << QStringLiteral("combo/") + spec.key + roundSuffix() + QStringLiteral("/0");
    }
    return ids;
}

void GeneratorFashionTaxonomy::ensureVocabularySeeded() const
{
    // seedStaticVocabulary() only mutates the SQLite-backed result tables
    // reached through m_resultsTables pointers, never any observable state
    // of this generator instance itself — a classic logically-const lazy
    // initialization, safe to trigger from buildJobPayload() even though
    // that override must stay const (AbstractGenerator's contract).
    const_cast<GeneratorFashionTaxonomy *>(this)->seedStaticVocabulary();
}

QJsonObject GeneratorFashionTaxonomy::buildJobPayload(const QString &jobId) const
{
    ensureVocabularySeeded();
    const ComboSpec *spec = findSpec(comboKeyFromJobId(jobId));
    if (!spec) {
        qDebug() << "GeneratorFashionTaxonomy: unknown job ID:" << jobId;
        return {};
    }
    return buildComboPayload(this, *spec, pageFromJobId(jobId));
}

void GeneratorFashionTaxonomy::processReply(const QString &jobId, const QJsonObject &reply)
{
    ensureVocabularySeeded();
    const ComboSpec *spec = findSpec(comboKeyFromJobId(jobId));
    if (!spec) {
        qDebug() << "GeneratorFashionTaxonomy: unknown job ID:" << jobId;
        return;
    }

    // Recomputed identically to buildJobPayload()'s batch — page number alone
    // determines the slice, so this is safe regardless of what got written in
    // between (see nextCandidateBatch()'s comment).
    const int page = pageFromJobId(jobId);
    const QList<QHash<QString, QString>> batch = nextCandidateBatch(this, *spec, page);
    const QStringList cultureList = loadVocabValues(
        this, QStringLiteral("PageAttributesFashionCulture"), PageAttributesFashionCulture::ID_NAME);
    const QSet<QString> availCultures(cultureList.begin(), cultureList.end());

    const QJsonArray results = reply.value(QStringLiteral("results")).toArray();
    int recordedCount = 0;

    for (const QJsonValue &v : results) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj = v.toObject();
        const int index = obj.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || index >= batch.size()) {
            continue;
        }

        const QStringList cultures = filterToAvailable(obj.value(QStringLiteral("cultures")).toArray(), availCultures);
        if (cultures.isEmpty()) {
            // AI judged this combination to make sense in no culture — discard it.
            // This is the filter that keeps the database containing only relevant combos.
            continue;
        }

        const QHash<QString, QString> &candidate = batch.at(index);
        if (rowAlreadyRecorded(this, *spec, candidate)) {
            // Guards against the same job id being dispatched twice by two
            // overlapping generator processes sharing one working dir.
            continue;
        }

        QHash<QString, QString> attrs;
        for (const ComboSlot &slot : spec->dimensionSlots) {
            attrs.insert(slot.attrId, candidate.value(slot.attrId));
        }
        attrs.insert(PageAttributesFashionComboBase::ID_FORMULA_ID, candidate.value(QStringLiteral("__formulaId")));
        attrs.insert(PageAttributesFashionComboBase::ID_CULTURES, cultures.join(QLatin1Char(',')));

        const QJsonValue msvVal = obj.value(QStringLiteral("msv"));
        if (!msvVal.isUndefined() && !msvVal.isNull()) {
            attrs.insert(PageAttributesFashionComboBase::ID_MSV, QString::number(msvVal.toInt()));
        }
        const QJsonValue startVal = obj.value(QStringLiteral("peakSeasonStart"));
        if (!startVal.isUndefined() && !startVal.isNull()) {
            attrs.insert(PageAttributesFashionComboBase::ID_PEAK_SEASON_START, QString::number(startVal.toInt()));
        }
        const QJsonValue endVal = obj.value(QStringLiteral("peakSeasonEnd"));
        if (!endVal.isUndefined() && !endVal.isNull()) {
            attrs.insert(PageAttributesFashionComboBase::ID_PEAK_SEASON_END, QString::number(endVal.toInt()));
        }

        // A single candidate failing schema-level cross-validation (e.g. the
        // AI tagged cultures for a colorA==colorB self-pair that
        // PageAttributesFashionComboColorColor rejects) must not abort the
        // other up-to-39 already-judged candidates in this batch — that
        // would silently discard real AI work and force a full re-ask on
        // retry. Skip just this one candidate and keep going; nothing is
        // hidden, it's logged explicitly below.
        try {
            recordResultPage(spec->attrId, attrs);
            ++recordedCount;
        } catch (const ExceptionWithTitleText &e) {
            qDebug() << "GeneratorFashionTaxonomy [" << spec->key << "] rejected candidate at index" << index
                     << "-" << e.errorTitle() << ":" << e.errorText();
        }
    }

    qDebug() << "GeneratorFashionTaxonomy [" << spec->key << "] +" << recordedCount
             << "recorded of" << results.size() << "assessed |" << batch.size() << "candidates in batch |"
             << (pendingCount() - 1) << "pending";

    if (batch.size() >= MAX_CANDIDATES_PER_JOB) {
        // Re-derive the key from the incoming job id (NOT spec->key) so the
        // continuation keeps the same round suffix as the job it extends.
        addDiscoveredJob(QStringLiteral("combo/") + comboKeyFromJobId(jobId)
                         + QLatin1Char('/') + QString::number(page + 1));
    }
}
