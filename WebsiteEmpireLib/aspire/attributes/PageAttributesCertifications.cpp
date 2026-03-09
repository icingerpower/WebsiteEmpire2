#include <QObject>

#include "PageAttributesCertifications.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesCertifications);

const QString PageAttributesCertifications::ID_CERTIFICATIONS = "certification_name";
const QString PageAttributesCertifications::ID_DESCRIPTION = "description";

QString PageAttributesCertifications::getId() const
{
    return "PageAttributesCertifications";
}

QString PageAttributesCertifications::getName() const
{
    return QObject::tr("Certifications");
}

QString PageAttributesCertifications::getDescription() const
{
    return QObject::tr("Attributes for a certification");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesCertifications::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();
    
    *attributes << Attribute{ID_CERTIFICATIONS
                            , tr("Name")
                            , tr("The name of the certification")
                            , tr("ISO 9001")
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
                            , tr("The description of the certification")
                            , tr("Quality management system certification")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The description can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"description"}
    };
    return attributes;
}
