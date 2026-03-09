#include <QObject>
#include <QLocale>

#include "PageAttributesAddress.h"

const QString PageAttributesAddress::ID_COMPANY_NAME = "company_name";
const QString PageAttributesAddress::ID_STREET = "street";
const QString PageAttributesAddress::ID_POSTAL_CODE = "postal_code";
const QString PageAttributesAddress::ID_CITY = "city";
const QString PageAttributesAddress::ID_STATE = "state";
const QString PageAttributesAddress::ID_COUNTRY = "country";

QString PageAttributesAddress::getDescription() const
{
    return QObject::tr("Set of attributes to describe an address");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesAddress::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();
    *attributes << Attribute{ID_COMPANY_NAME
                            , tr("Company name")
                            , tr("The name of the company")
                            , tr("Carpet Ultimatate Ltd")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The company name can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"name"}
    };
    *attributes << Attribute{ID_STREET
                            , tr("Street")
                            , tr("The street address")
                            , tr("123 Main St")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The street address can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"streetAddress"}
    };

    *attributes << Attribute{ID_POSTAL_CODE
                            , tr("Postal code")
                            , tr("The postal code")
                            , tr("75001")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The postal code can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"postalCode"}
    };

    *attributes << Attribute{ID_CITY
                            , tr("City")
                            , tr("The city")
                            , tr("Paris")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The city can't be empty");
                                }
                                return QString{};
                            }
                            , Schema{"addressLocality"}
    };

    *attributes << Attribute{ID_STATE
                            , tr("State / Province")
                            , tr("The state or province")
                            , tr("Ile-de-France")
                            , QString{}
                            , [](const QString &) {
                                return QString{};
                            }
                            , Schema{"addressRegion"}
    };

    *attributes << Attribute{ID_COUNTRY
                            , tr("Country Code")
                            , tr("The 2-letter ISO 3166-1 country code")
                            , tr("FR")
                            , QString{}
                            , [](const QString &value ) {
                                if (value.isEmpty()) {
                                    return tr("The country code can't be empty");
                                }
                                const QList<QLocale> locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
                                for (const QLocale &locale : locales) {
                                    if (QLocale::territoryToCode(locale.territory()) == value) {
                                        return QString{};
                                    }
                                }
                                return tr("Invalid country code");
                            }
                            , Schema{"addressCountry"}
    };
    return attributes;
}
