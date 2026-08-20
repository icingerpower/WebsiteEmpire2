#ifndef PAGEATTRIBUTESFASHIONFITSILHOUETTE_H
#define PAGEATTRIBUTESFASHIONFITSILHOUETTE_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion fit/silhouette (e.g. "Oversized", "Wide-Leg",
// "High-Waisted"). Used as a reference target from the combination attribute
// classes (PageAttributesFashionCombo*).
class PageAttributesFashionFitSilhouette : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the fit name — used as the reference target
    // in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONFITSILHOUETTE_H
