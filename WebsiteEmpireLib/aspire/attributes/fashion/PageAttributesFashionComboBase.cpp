#include <QObject>

#include "PageAttributesFashionComboBase.h"
#include "PageAttributesFashionCulture.h"

const QString PageAttributesFashionComboBase::ID_FORMULA_ID       = QStringLiteral("combo_formula_id");
const QString PageAttributesFashionComboBase::ID_CULTURES         = QStringLiteral("combo_cultures");
const QString PageAttributesFashionComboBase::ID_MSV              = QStringLiteral("combo_msv");
const QString PageAttributesFashionComboBase::ID_PEAK_SEASON_START = QStringLiteral("combo_peak_season_start");
const QString PageAttributesFashionComboBase::ID_PEAK_SEASON_END   = QStringLiteral("combo_peak_season_end");
const QString PageAttributesFashionComboBase::ID_SOURCE_QUERY      = QStringLiteral("combo_source_query");

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboBase::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    // Captures `this` to dispatch to the concrete subclass's allowedFormulaIds()
    // — the set of valid ids differs per combination table, so it can't be a
    // stateless lambda like the other validators in this codebase.
    *attributes << Attribute{ID_FORMULA_ID
                            , tr("Query Formula")
                            , tr("Which of the 12 study query formulas this row instantiates")
                            , tr("direct_transactional")
                            , QString{}
                            , [this](const QString &value) {
                                if (!allowedFormulaIds().contains(value)) {
                                    return tr("Formula id must be one of: %1")
                                        .arg(allowedFormulaIds().join(QStringLiteral(", ")));
                                }
                                return QString{};
                            }
    };

    // Mandatory and enforced non-empty: a combination with no applicable
    // culture does not "make sense" and must never be recorded. This is the
    // DB-level backstop for the filter GeneratorFashionTaxonomy applies before
    // ever calling recordResultPage().
    *attributes << Attribute{ID_CULTURES
                            , tr("Applicable Cultures")
                            , tr("Cultures/audience segments in which this combination makes sense")
                            , tr("Western/Mainstream")
                            , QString{}
                            , [](const QString &value) {
                                if (value.trimmed().isEmpty()) {
                                    return tr("A combination must have at least one applicable culture "
                                              "to be recorded — otherwise it does not make sense and "
                                              "should be discarded instead of saved");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionCulture::ID_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_MSV
                            , tr("Estimated MSV")
                            , tr("AI-estimated monthly search volume (optional)")
                            , tr("14800")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return QString{}; // optional
                                }
                                bool ok = false;
                                const int msv = value.toInt(&ok);
                                if (!ok || msv < 0) {
                                    return tr("MSV must be a non-negative integer");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , true // optional
    };

    *attributes << Attribute{ID_PEAK_SEASON_START
                            , tr("Peak Season Start (month)")
                            , tr("First month (1-12) of estimated peak search interest (optional)")
                            , tr("9")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return QString{}; // optional
                                }
                                bool ok = false;
                                const int month = value.toInt(&ok);
                                if (!ok || month < 1 || month > 12) {
                                    return tr("Peak season start must be an integer between 1 and 12");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , true // optional
    };

    *attributes << Attribute{ID_PEAK_SEASON_END
                            , tr("Peak Season End (month)")
                            , tr("Last month (1-12) of estimated peak search interest (optional)")
                            , tr("11")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return QString{}; // optional
                                }
                                bool ok = false;
                                const int month = value.toInt(&ok);
                                if (!ok || month < 1 || month > 12) {
                                    return tr("Peak season end must be an integer between 1 and 12");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , true // optional
    };

    *attributes << Attribute{ID_SOURCE_QUERY
                            , tr("Source Example Query")
                            , tr("Literal example query from the study's researched keyword list (optional)")
                            , tr("black cocktail dress for wedding guest")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
    };

    return attributes;
}
