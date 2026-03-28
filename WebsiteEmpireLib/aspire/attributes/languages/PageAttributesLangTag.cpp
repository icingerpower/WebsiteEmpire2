#include <QObject>

#include "PageAttributesLangTag.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesLangTag);

const QString PageAttributesLangTag::ID_NAME       = QStringLiteral("lang_tag_name");
const QString PageAttributesLangTag::ID_PERCENTAGE = QStringLiteral("lang_tag_percentage");

QString PageAttributesLangTag::getId() const
{
    return QStringLiteral("PageAttributesLangTag");
}

QString PageAttributesLangTag::getName() const
{
    return QObject::tr("Language Tag");
}

QString PageAttributesLangTag::getDescription() const
{
    return QObject::tr("Attributes defining a language learning tag: a theme, grammar category, "
                       "situation, or word type (e.g. pronoun) used to group and prioritise content");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesLangTag::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("English unique identifier for this tag (e.g. \"food\", "
                                 "\"greetings\", \"pronoun\")")
                            , tr("greetings")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The tag name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_PERCENTAGE
                            , tr("Estimated Interest Percentage")
                            , tr("Estimated percentage of learners interested in this tag — "
                                 "used to sort tags from most to least important when generating pages")
                            , tr("80.0")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The interest percentage can't be empty");
                                }
                                bool ok;
                                const double percentage = value.toDouble(&ok);
                                if (!ok) {
                                    return tr("The interest percentage must be a number");
                                }
                                if (percentage < 0.0 || percentage > 100.0) {
                                    return tr("The interest percentage must be between 0 and 100");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
