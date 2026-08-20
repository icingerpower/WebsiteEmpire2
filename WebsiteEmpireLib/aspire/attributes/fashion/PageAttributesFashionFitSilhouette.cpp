#include <QObject>

#include "PageAttributesFashionFitSilhouette.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionFitSilhouette);

const QString PageAttributesFashionFitSilhouette::ID_NAME = QStringLiteral("fashion_fit_silhouette_name");

QString PageAttributesFashionFitSilhouette::getId() const
{
    return QStringLiteral("PageAttributesFashionFitSilhouette");
}

QString PageAttributesFashionFitSilhouette::getName() const
{
    return QObject::tr("Fashion Fit/Silhouette");
}

QString PageAttributesFashionFitSilhouette::getDescription() const
{
    return QObject::tr("Attributes defining a fashion fit/silhouette (e.g. Oversized, Wide-Leg, High-Waisted)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionFitSilhouette::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the fit/silhouette")
                            , tr("Wide-Leg")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The fit/silhouette name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
