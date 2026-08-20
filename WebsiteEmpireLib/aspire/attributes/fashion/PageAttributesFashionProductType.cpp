#include <QObject>

#include "PageAttributesFashionProductType.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionProductType);

const QString PageAttributesFashionProductType::ID_NAME     = QStringLiteral("fashion_product_type_name");
const QString PageAttributesFashionProductType::ID_CATEGORY = QStringLiteral("fashion_product_type_category");

QString PageAttributesFashionProductType::getId() const
{
    return QStringLiteral("PageAttributesFashionProductType");
}

QString PageAttributesFashionProductType::getName() const
{
    return QObject::tr("Fashion Product Type");
}

QString PageAttributesFashionProductType::getDescription() const
{
    return QObject::tr("Attributes defining a fashion product type (e.g. Midi Dress, Trench Coat)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionProductType::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the product type")
                            , tr("Midi Dress")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The product type name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_CATEGORY
                            , tr("Category")
                            , tr("Broad merchandising category (optional), e.g. Dresses, Outerwear, Bottoms, Bags, Shoes")
                            , tr("Dresses")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
    };

    return attributes;
}
