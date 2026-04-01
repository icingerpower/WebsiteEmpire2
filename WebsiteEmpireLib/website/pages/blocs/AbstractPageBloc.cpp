#include "AbstractPageBloc.h"

const QList<const AbstractAttribute *> &AbstractPageBloc::getAttributes() const
{
    static const QList<const AbstractAttribute *> empty;
    return empty;
}
