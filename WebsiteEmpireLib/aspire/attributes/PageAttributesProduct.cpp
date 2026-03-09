#include <QObject>

#include "PageAttributesProduct.h"
#include "PageAttributesProductCategory.h"

const QString PageAttributesProduct::ID_CATEGORY = "category";
const QString PageAttributesProduct::ID_SALE_PRICE = "sale_price";
const QString PageAttributesProduct::ID_NAME = "name";
const QString PageAttributesProduct::ID_DESCRIPTION = "description";
const QString PageAttributesProduct::ID_SUPPLIER_PRICE = "supplier_price";

DECLARE_PAGE_ATTRIBUTES(PageAttributesProduct);

QString PageAttributesProduct::getId() const
{
    return "PageAttributesProduct";
}

QString PageAttributesProduct::getName() const
{
    return QObject::tr("Product");
}

QString PageAttributesProduct::getDescription() const
{
    return QObject::tr("Attributes defining a product");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesProduct::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();
    
    *attributes << Attribute{ID_CATEGORY
                            , tr("Category")
                            , tr("The product category")
                            , tr("Electronics")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The category can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt // No relevant schema mapping yet
                            , false
                            , ReferenceSpec{PageAttributesProductCategory::ID_PRODUCT_CATEGORY, ReferenceSpec::Cardinality::Multiple}
    };
    
    *attributes << Attribute{ID_SALE_PRICE
                            , tr("Sale Price")
                            , tr("The selling price of the product")
                            , tr("19.99")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The sale price can't be empty");
                                }
                                bool ok;
                                double price = value.toDouble(&ok);
                                if (!ok || price < 0.0) {
                                    return tr("The sale price must be a valid positive number");
                                }
                                return QString{};
                            }
                            , Schema{"price"}
    };
    
    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the product")
                            , tr("Premium Headphones")
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
                            , tr("The description of the product")
                            , tr("High quality wireless headphones with noise cancellation.")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The description can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"description"}
    };
    
    *attributes << Attribute{ID_SUPPLIER_PRICE
                            , tr("Supplier Price")
                            , tr("The buying price from the supplier")
                            , tr("9.99")
                            , QString{}
                            , [](const QString &value ) {
                                if (!value.isEmpty()) {
                                    bool ok;
                                    double price = value.toDouble(&ok);
                                    if (!ok || price < 0.0) {
                                        return tr("The supplier price must be a valid positive number");
                                    }
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , true // Optional
    };

    return attributes;
}
