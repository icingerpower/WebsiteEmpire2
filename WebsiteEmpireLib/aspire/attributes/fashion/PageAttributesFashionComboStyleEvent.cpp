#include <QObject>

#include "PageAttributesFashionComboStyleEvent.h"
#include "PageAttributesFashionStyleAesthetic.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboStyleEvent);

const QString PageAttributesFashionComboStyleEvent::ID_STYLE = QStringLiteral("cste_style");
const QString PageAttributesFashionComboStyleEvent::ID_EVENT = QStringLiteral("cste_event");

const QString PageAttributesFashionComboStyleEvent::FORMULA_STYLE_EVENT_OUTFITS = QStringLiteral("style_event_outfits");

QString PageAttributesFashionComboStyleEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboStyleEvent");
}

QString PageAttributesFashionComboStyleEvent::getName() const
{
    return QObject::tr("Combo: Style + Event");
}

QString PageAttributesFashionComboStyleEvent::getDescription() const
{
    return QObject::tr("{style} {event} outfit ideas");
}

QStringList PageAttributesFashionComboStyleEvent::allowedFormulaIds() const
{
    return {FORMULA_STYLE_EVENT_OUTFITS};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboStyleEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_STYLE
                            , tr("Style/Aesthetic")
                            , tr("The style/aesthetic slot of this combination")
                            , tr("Boho Chic")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The style can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionStyleAesthetic::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_EVENT
                            , tr("Event")
                            , tr("The event/occasion slot of this combination")
                            , tr("Date Night")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The event can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionEvent::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    return attributes;
}
