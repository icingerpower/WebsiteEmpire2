#include <QObject>

#include "PageAttributesFactory.h"
#include "PageAttributesFactoryCategory.h"
#include "PageAttributesCertifications.h"

const QString PageAttributesFactory::ID_CATEGORY = "category";
const QString PageAttributesFactory::ID_DESCRIPTION = "description";
const QString PageAttributesFactory::ID_INCOME = "income";
const QString PageAttributesFactory::ID_EMPLOYEES = "employees";
const QString PageAttributesFactory::ID_MIN_ORDER_AMOUNT = "min_order_amount";
const QString PageAttributesFactory::ID_CERTIFICATIONS = "certifications";

DECLARE_PAGE_ATTRIBUTES(PageAttributesFactory);

QString PageAttributesFactory::getId() const
{
    return "PageAttributesFactory";
}

QString PageAttributesFactory::getName() const
{
    return QObject::tr("Factory");
}

QString PageAttributesFactory::getDescription() const
{
    return QObject::tr("Attributes required to describe a manufacturing factory");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFactory::getAttributes() const
{
    auto attributes = PageAttributesAddressAndContact::getAttributes();
    *attributes << Attribute{ID_CATEGORY
                            , tr("Category")
                            , tr("The product category they do")
                            , tr("Headquarters")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The category can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt // No relevant schema mapping yet
                            , false
                            , ReferenceSpec{PageAttributesFactoryCategory::ID_FACTORY_CATEGORY, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_DESCRIPTION
                            , tr("Description")
                            , tr("The description of the factory")
                            , tr("We manufacture superior electronics.")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The description can't be empty");
                                }
                                else if (value.size() < 100) {
                                    return tr("The description should be more complete.");
                                }
                                return QString{};
                            }
                            , Schema{"description"}
    };

    *attributes << Attribute{ID_INCOME
                            , tr("Income")
                            , tr("The estimated income of the factory")
                            , tr("1000000")
                            , QString{}
                            , [](const QString &) {
                                return QString{};
                            }
                            , std::nullopt
                            , true // Optional
    };

    *attributes << Attribute{ID_EMPLOYEES
                            , tr("Number of Employees")
                            , tr("The estimated income of the factory") // Requested text
                            , tr("500")
                            , QString{}
                            , [](const QString &) {
                                return QString{};
                            }
                            , std::nullopt
                            , true // Optional
    };

    *attributes << Attribute{ID_MIN_ORDER_AMOUNT
                            , tr("Minimal Order Amount")
                            , tr("The estimated income of the factory") // Requested text
                            , tr("5000")
                            , QString{}
                            , [](const QString &) {
                                return QString{};
                            }
                            , std::nullopt
                            , true // Optional
    };

    *attributes << Attribute{ID_CERTIFICATIONS
                            , tr("Certifications")
                            , tr("The certifications held by the factory")
                            , tr("ISO 9001")
                            , QString{}
                            , [](const QString &) {
                                return QString{};
                            }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesCertifications::ID_CERTIFICATIONS, ReferenceSpec::Cardinality::Multiple}
    };
    
    return attributes;
}

