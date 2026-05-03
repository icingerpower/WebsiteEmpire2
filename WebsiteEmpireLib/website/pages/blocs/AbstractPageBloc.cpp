#include "AbstractPageBloc.h"

QHash<QString, QString> AbstractPageBloc::getAiKeyClues() const
{
    return {};
}

const QList<const AbstractAttribute *> &AbstractPageBloc::getAttributes() const
{
    static const QList<const AbstractAttribute *> empty;
    return empty;
}
