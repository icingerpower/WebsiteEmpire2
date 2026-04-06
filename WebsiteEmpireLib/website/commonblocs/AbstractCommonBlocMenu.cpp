#include "AbstractCommonBlocMenu.h"
#include "website/commonblocs/widgets/WidgetCommonBlocMenu.h"

void AbstractCommonBlocMenu::setItems(const QList<MenuItem> &items)
{
    m_items = items;
}

const QList<MenuItem> &AbstractCommonBlocMenu::items() const
{
    return m_items;
}

AbstractCommonBlocWidget *AbstractCommonBlocMenu::createEditWidget()
{
    return new WidgetCommonBlocMenu();
}

QString AbstractCommonBlocMenu::buildRel(const QString &rel, bool newTab)
{
    if (!newTab) {
        return rel;
    }
    QString r = rel.trimmed();
    if (!r.contains(QStringLiteral("noopener"))) {
        if (!r.isEmpty()) {
            r += QLatin1Char(' ');
        }
        r += QStringLiteral("noopener noreferrer");
    }
    return r;
}
