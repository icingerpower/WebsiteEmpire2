#include <QObject>

#include "PageAttributesLangIdiom.h"
#include "PageAttributesLangCode.h"
#include "PageAttributesLangTag.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesLangIdiom);

const QString PageAttributesLangIdiom::ID_PHRASE    = QStringLiteral("lang_idiom_phrase");
const QString PageAttributesLangIdiom::ID_LANG_CODE = QStringLiteral("lang_idiom_lang_code");
const QString PageAttributesLangIdiom::ID_TAGS      = QStringLiteral("lang_idiom_tags");

QString PageAttributesLangIdiom::getId() const
{
    return QStringLiteral("PageAttributesLangIdiom");
}

QString PageAttributesLangIdiom::getName() const
{
    return QObject::tr("Language Idiom");
}

QString PageAttributesLangIdiom::getDescription() const
{
    return QObject::tr("Attributes defining a language idiom or fixed phrase used in learning exercises");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesLangIdiom::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_PHRASE
                            , tr("Phrase")
                            , tr("The idiom or fixed phrase text — unique identifier within its language")
                            , tr("It's raining cats and dogs")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The phrase can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_LANG_CODE
                            , tr("Language Code")
                            , tr("BCP-47 code of the language this idiom belongs to")
                            , tr("en")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The language code can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // not optional
                            , ReferenceSpec{PageAttributesLangCode::ID_CODE, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_TAGS
                            , tr("Tags")
                            , tr("One or more tags describing the theme, situation, or grammar category "
                                 "of this idiom")
                            , tr("greetings")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
                            , ReferenceSpec{PageAttributesLangTag::ID_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    return attributes;
}
