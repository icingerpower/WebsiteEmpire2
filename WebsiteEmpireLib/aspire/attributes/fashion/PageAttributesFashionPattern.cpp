#include <QObject>

#include "PageAttributesFashionPattern.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionPattern);

const QString PageAttributesFashionPattern::ID_NAME = QStringLiteral("fashion_pattern_name");

QString PageAttributesFashionPattern::getId() const
{
    return QStringLiteral("PageAttributesFashionPattern");
}

QString PageAttributesFashionPattern::getName() const
{
    return QObject::tr("Fashion Pattern");
}

QString PageAttributesFashionPattern::getDescription() const
{
    return QObject::tr("Attributes defining a fashion print/pattern (e.g. Floral, Leopard Print)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionPattern::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the print/pattern")
                            , tr("Floral")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The pattern name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
