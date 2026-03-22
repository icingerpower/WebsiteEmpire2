#ifndef PAGEATTRIBUTESFACTORYCATEGORY_H
#define PAGEATTRIBUTESFACTORYCATEGORY_H

#include "AbstractPageAttributes.h"

// Page attributes for a manufacturing factory category (e.g. "Aerospace",
// "Food Processing").  These records are populated by GeneratorFactories and
// are the target of the ReferenceSpec on PageAttributesFactory::ID_CATEGORY.
class PageAttributesFactoryCategory : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the category name — used as the reference target
    // in PageAttributesFactory::ID_CATEGORY's ReferenceSpec.
    static const QString ID_FACTORY_CATEGORY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFACTORYCATEGORY_H
