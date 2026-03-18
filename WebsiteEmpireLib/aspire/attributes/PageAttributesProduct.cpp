#include <QImage>
#include <QObject>

#include "PageAttributesProduct.h"
#include "PageAttributesProductCategory.h"

const QString PageAttributesProduct::ID_URL           = "url";
const QString PageAttributesProduct::ID_CATEGORY      = "category";
const QString PageAttributesProduct::ID_SALE_PRICE = "sale_price";
const QString PageAttributesProduct::ID_NAME = "name";
const QString PageAttributesProduct::ID_DESCRIPTION = "description";
const QString PageAttributesProduct::ID_SUPPLIER_PRICE = "supplier_price";
const QString PageAttributesProduct::ID_IMAGES = "images";

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

    *attributes << Attribute{ID_URL
                            , tr("URL")
                            , tr("The URL of the product page")
                            , tr("https://example.com/product/premium-headphones/")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The URL can't be empty");
                                }
                                if (!value.startsWith(QLatin1String("http"))) {
                                    return tr("The URL must start with http");
                                }
                                return QString{};
                            }
                            , std::nullopt // schema
                            , false        // not optional
                            , std::nullopt // no reference
                            , false        // not image
                            , std::nullopt // no image validation
                            , true         // isUrl
    };

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

    *attributes << Attribute{ID_IMAGES
                            , tr("Images")
                            , tr("The product images (1–10). Each image's smallest side must be at least %1 px.").arg(MIN_IMAGE_SIDE_PX)
                            , QString{}
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt // schema
                            , false        // not optional
                            , std::nullopt // no reference
                            , true         // is image
                            , [](const QList<QSharedPointer<QImage>> &images) -> QString {
                                if (images.isEmpty() || images.size() > 10) {
                                    return tr("The product must have between 1 and 10 images");
                                }
                                for (const auto &img : std::as_const(images)) {
                                    const int minSide = qMin(img->width(), img->height());
                                    if (minSide < MIN_IMAGE_SIDE_PX) {
                                        return tr("Each image's smallest side must be at least %1 px").arg(MIN_IMAGE_SIDE_PX);
                                    }
                                }
                                return QString{};
                            }
    };

    return attributes;
}
