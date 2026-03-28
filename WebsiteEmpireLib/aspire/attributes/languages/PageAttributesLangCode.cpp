#include <QLocale>
#include <QObject>

#include "PageAttributesLangCode.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesLangCode);

const QString PageAttributesLangCode::ID_CODE        = QStringLiteral("lang_code");
const QString PageAttributesLangCode::ID_NAME        = QStringLiteral("lang_name");
const QString PageAttributesLangCode::ID_NATIVE_NAME = QStringLiteral("lang_native_name");

QString PageAttributesLangCode::getId() const
{
    return QStringLiteral("PageAttributesLangCode");
}

QString PageAttributesLangCode::getName() const
{
    return QObject::tr("Language Code");
}

QString PageAttributesLangCode::getDescription() const
{
    return QObject::tr("Attributes defining a supported language identified by its BCP-47 code");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesLangCode::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_CODE
                            , tr("Code")
                            , tr("BCP-47 language code (e.g. \"fr\", \"en\", \"de\")")
                            , tr("fr")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The language code can't be empty");
                                }
                                const QLocale locale(value);
                                if (locale.language() == QLocale::C) {
                                    return tr("The language code \"%1\" is not a recognised BCP-47 code").arg(value);
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("English display name of the language")
                            , tr("French")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The language name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_NATIVE_NAME
                            , tr("Native Name")
                            , tr("Name of the language as written in the language itself")
                            , tr("Français")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The native name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
