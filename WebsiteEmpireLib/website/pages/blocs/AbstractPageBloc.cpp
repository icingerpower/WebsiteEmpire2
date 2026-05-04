#include "AbstractPageBloc.h"

QHash<QString, QString> AbstractPageBloc::getAiKeyClues() const
{
    return {};
}

std::optional<AbstractPageBloc::AiUpdateSpec> AbstractPageBloc::getAiUpdateSpec() const
{
    return std::nullopt;
}

const QList<const AbstractAttribute *> &AbstractPageBloc::getAttributes() const
{
    static const QList<const AbstractAttribute *> empty;
    return empty;
}
