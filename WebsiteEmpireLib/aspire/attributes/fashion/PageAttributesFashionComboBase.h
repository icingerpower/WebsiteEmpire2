#ifndef PAGEATTRIBUTESFASHIONCOMBOBASE_H
#define PAGEATTRIBUTESFASHIONCOMBOBASE_H

#include "../AbstractPageAttributes.h"

// Shared attributes for every fashion query-combination table
// (PageAttributesFashionCombo*). Each combination table pairs a small,
// fixed set of dimension-slot references (e.g. Color + ProductType) with
// these common fields:
//   • ID_FORMULA_ID  — which of the 12 study query formulas this row
//                       instantiates (validated against allowedFormulaIds(),
//                       a small fixed set per subclass — not a DB table,
//                       since the 12 formulas are a closed, static list).
//   • ID_CULTURES    — the subset of PageAttributesFashionCulture entries in
//                       which this specific combination is contextually or
//                       aesthetically appropriate. MANDATORY and enforced
//                       non-empty by validate(): a combination with no
//                       applicable culture does not "make sense" and must
//                       never be recorded (GeneratorFashionTaxonomy skips
//                       such candidates instead of calling recordResultPage;
//                       this validator is the DB-level backstop for that
//                       invariant).
//   • ID_MSV / ID_PEAK_SEASON_START / ID_PEAK_SEASON_END — AI-estimated
//                       search-volume tier and peak months; optional since
//                       most of the 1.3M+ combinations have no directly
//                       researched keyword data.
//   • ID_SOURCE_QUERY — the literal example query from the study, when this
//                       row corresponds to one of the ~120 researched
//                       examples (optional).
//
// This class is never registered via DECLARE_PAGE_ATTRIBUTES/instantiated on
// its own — getId()/getName()/getDescription() stay pure virtual, and
// allowedFormulaIds() must be supplied by each concrete subclass.
class PageAttributesFashionComboBase : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_FORMULA_ID;
    static const QString ID_CULTURES;
    static const QString ID_MSV;
    static const QString ID_PEAK_SEASON_START;
    static const QString ID_PEAK_SEASON_END;
    static const QString ID_SOURCE_QUERY;

    // The fixed subset of study query-formula ids (see GeneratorFashionTaxonomy)
    // that this combination table represents. ID_FORMULA_ID's validate() lambda
    // rejects any value not in this set.
    virtual QStringList allowedFormulaIds() const = 0;

    QSharedPointer<QList<Attribute>> getAttributes() const override;

    // Pure virtual (unlike AbstractPageAttributes's opt-in default): every
    // combo table MUST self-describe how to render one of its rows as an
    // article topic, mirroring its own getDescription() template and using
    // its own ID_* slot constants. A missing override is a compile error —
    // deliberately, since the alternative (falling through to the base
    // class's generic "first non-id column" behavior) would silently expose
    // combo_formula_id ("direct_transactional") as the topic instead.
    // rowValues is keyed by DB column name (== Attribute::id); when this
    // table has more than one allowedFormulaIds() entry, branch on
    // rowValues.value(ID_FORMULA_ID) to pick the matching phrasing.
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override = 0;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOBASE_H
