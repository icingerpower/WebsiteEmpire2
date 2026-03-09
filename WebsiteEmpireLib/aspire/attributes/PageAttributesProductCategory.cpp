#include <QObject>

#include "PageAttributesProductCategory.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesProductCategory);

const QString PageAttributesProductCategory::ID_PRODUCT_CATEGORY = "category_name_product";
const QString PageAttributesProductCategory::ID_DESCRIPTION = "description";

QString PageAttributesProductCategory::getId() const
{
    return "PageAttributesProductCategory";
}

QString PageAttributesProductCategory::getName() const
{
    return QObject::tr("Product Category");
}

QString PageAttributesProductCategory::getDescription() const
{
    return QObject::tr("Attributes defining a product category");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesProductCategory::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();
    
    *attributes << Attribute{ID_PRODUCT_CATEGORY
                            , tr("Name")
                            , tr("The name of the product category")
                            , tr("Electronics")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The name can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"name"}
    };
    
    *attributes << Attribute{ID_DESCRIPTION
                            , tr("Description")
                            , tr("The description of the product category")
                            , tr("Electronic devices and accessories")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The description can't be empty");
                                }
                                else if (value.size() < 10) {
                                    return tr("The description is too short");
                                }
                                return QString{};
                            }
                            , Schema{"description"}
    };

    return attributes;
}
