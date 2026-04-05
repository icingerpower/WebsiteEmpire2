#include "PageTypeLegal.h"

QString PageTypeLegal::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeLegal::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

DECLARE_PAGE_TYPE(PageTypeLegal)
