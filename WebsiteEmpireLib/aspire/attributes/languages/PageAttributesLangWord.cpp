#include <QObject>

#include "PageAttributesLangWord.h"
#include "PageAttributesLangCode.h"
#include "PageAttributesLangTag.h"
#include "PageAttributesLangIdiom.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesLangWord);

const QString PageAttributesLangWord::ID_WORD      = QStringLiteral("lang_word");
const QString PageAttributesLangWord::ID_RANK      = QStringLiteral("lang_word_rank");
const QString PageAttributesLangWord::ID_LANG_CODE = QStringLiteral("lang_word_lang_code");
const QString PageAttributesLangWord::ID_TAGS      = QStringLiteral("lang_word_tags");
const QString PageAttributesLangWord::ID_IDIOMS    = QStringLiteral("lang_word_idioms");

QString PageAttributesLangWord::getId() const
{
    return QStringLiteral("PageAttributesLangWord");
}

QString PageAttributesLangWord::getName() const
{
    return QObject::tr("Language Word");
}

QString PageAttributesLangWord::getDescription() const
{
    return QObject::tr("Attributes defining a vocabulary word used in language learning pages and exercises");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesLangWord::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_WORD
                            , tr("Word")
                            , tr("The word text — unique identifier within its language")
                            , tr("apple")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The word can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_RANK
                            , tr("Rank")
                            , tr("Corpus frequency rank (1 = most frequent) — used to order words "
                                 "from most to least useful when building lesson pages")
                            , tr("1")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The rank can't be empty");
                                }
                                bool ok;
                                const int rank = value.toInt(&ok);
                                if (!ok || rank < 1) {
                                    return tr("The rank must be a positive integer");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_LANG_CODE
                            , tr("Language Code")
                            , tr("BCP-47 code of the language this word belongs to")
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
                            , tr("One or more tags describing the theme, grammar category, or situation "
                                 "this word belongs to")
                            , tr("food")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
                            , ReferenceSpec{PageAttributesLangTag::ID_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_IDIOMS
                            , tr("Idioms")
                            , tr("Idioms or fixed phrases in which this word appears — allows the "
                                 "website generator to display related phrases alongside a word page")
                            , tr("It's raining cats and dogs")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
                            , ReferenceSpec{PageAttributesLangIdiom::ID_PHRASE, ReferenceSpec::Cardinality::Multiple}
    };

    return attributes;
}
