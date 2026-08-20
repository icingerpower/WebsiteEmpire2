#ifndef PAGEATTRIBUTESFASHIONCULTURE_H
#define PAGEATTRIBUTESFASHIONCULTURE_H

#include "../AbstractPageAttributes.h"

// Page attributes for a cultural/audience segment (e.g. "Western/Mainstream",
// "Muslim/Modest", "South Asian/Indian"). This is the controlled vocabulary
// that drives per-culture SEO site segmentation: each combination row
// (PageAttributesFashionCombo*) references the subset of cultures in which
// it is contextually/aesthetically appropriate via ID_CULTURES
// (ReferenceSpec::Cardinality::Multiple). A combination with no applicable
// culture is considered to "not make sense" and is not recorded.
//
// Kept as a flat tag (name + free description) rather than carrying
// structured rules (mourning color, modesty level, etc.) — applicability is
// judged per-combination at generation time, not derived from culture
// metadata. See GeneratorFashionTaxonomy.
class PageAttributesFashionCulture : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the culture name — used as the reference
    // target in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    // Free-text description (optional).
    static const QString ID_DESCRIPTION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONCULTURE_H
