#include <QObject>

#include "PageAttributesAddressAndContact.h"

const QString PageAttributesAddressAndContact::ID_EMAIL = "email";
const QString PageAttributesAddressAndContact::ID_PHONE = "phone";
const QString PageAttributesAddressAndContact::ID_CONTACT_NAME = "contact_name";

DECLARE_PAGE_ATTRIBUTES(PageAttributesAddressAndContact);

QString PageAttributesAddressAndContact::getId() const
{
    return "PageAttributesAddressAndContact";
}

QString PageAttributesAddressAndContact::getName() const
{
    return QObject::tr("Contact");
}

QString PageAttributesAddressAndContact::getDescription() const
{
    return QObject::tr("Set of attributes to describe an address and contact details");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesAddressAndContact::getAttributes() const
{
    auto attributes = PageAttributesAddress::getAttributes();
    
    *attributes << Attribute{ID_EMAIL
                            , tr("Email")
                            , tr("The contact email address")
                            , tr("contact@example.com")
                            , QString{}
                            , [](const QString &) {
                                return QString{}; // Optional
                            }
                            , Schema{"email"}
                            , true // Optional
    };

    *attributes << Attribute{ID_PHONE
                            , tr("Telephone")
                            , tr("The contact phone number")
                            , tr("+33102030405")
                            , QString{}
                            , [](const QString &) {
                                return QString{}; // Optional
                            }
                            , Schema{"telephone"}
                            , true // Optional
    };

    *attributes << Attribute{ID_CONTACT_NAME
                            , tr("Contact Name")
                            , tr("First and last name of the contact person")
                            , tr("John Doe")
                            , QString{}
                            , [](const QString &) {
                                return QString{}; // Optional
                            }
                            , std::nullopt
                            , true // Optional
    };

    return attributes;
}
