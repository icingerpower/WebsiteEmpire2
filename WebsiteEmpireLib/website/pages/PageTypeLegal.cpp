#include "PageTypeLegal.h"

QString PageTypeLegal::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeLegal::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

void PageTypeLegal::addInnerTopCode(AbstractEngine & /*engine*/,
                                        int              /*websiteIndex*/,
                                        QString        & /*html*/,
                                        QString        & /*css*/,
                                        QString        & /*js*/,
                                        QSet<QString>  & /*cssDoneIds*/,
                                        QSet<QString>  & /*jsDoneIds*/) const
{
}

DECLARE_PAGE_TYPE(PageTypeLegal)
