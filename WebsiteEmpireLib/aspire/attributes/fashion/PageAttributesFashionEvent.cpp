#include <QObject>

#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionEvent);

const QString PageAttributesFashionEvent::ID_NAME      = QStringLiteral("fashion_event_name");
const QString PageAttributesFashionEvent::ID_FORMALITY = QStringLiteral("fashion_event_formality");

QString PageAttributesFashionEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionEvent");
}

QString PageAttributesFashionEvent::getName() const
{
    return QObject::tr("Fashion Event");
}

QString PageAttributesFashionEvent::getDescription() const
{
    return QObject::tr("Attributes defining a fashion event/occasion (e.g. Wedding Guest, Gala)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionEvent::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the event/occasion")
                            , tr("Wedding Guest")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The event name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_FORMALITY
                            , tr("Formality")
                            , tr("Free-text formality descriptor (optional), e.g. Black Tie, Casual")
                            , tr("Semi-Formal")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
    };

    return attributes;
}
