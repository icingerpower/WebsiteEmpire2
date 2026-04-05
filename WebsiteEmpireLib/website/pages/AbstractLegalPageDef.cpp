#include "AbstractLegalPageDef.h"

static QList<AbstractLegalPageDef *> &_legalDefRegistry()
{
    static QList<AbstractLegalPageDef *> s_defs;
    return s_defs;
}

const QList<AbstractLegalPageDef *> &AbstractLegalPageDef::allDefs()
{
    return _legalDefRegistry();
}

void AbstractLegalPageDef::registerDef(AbstractLegalPageDef *def)
{
    _legalDefRegistry().append(def);
}
