#include "AbstractPageAttributes.h"

AbstractPageAttributes::Recorder::Recorder(AbstractPageAttributes *pageAttributes)
{
    getPageAttributes()[pageAttributes->getId()] = pageAttributes;
}

const QMap<QString, const AbstractPageAttributes *> &AbstractPageAttributes::ALL_PAGE_ATTRIBUTES()
{
    return getPageAttributes();
}

QMap<QString, const AbstractPageAttributes *> &AbstractPageAttributes::getPageAttributes()
{
    static QMap<QString, const AbstractPageAttributes *> pageAttributes;
    return pageAttributes;
}
