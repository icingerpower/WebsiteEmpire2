#include "AbstractReviewAction.h"

QList<AbstractReviewAction *> &AbstractReviewAction::getActions()
{
    static QList<AbstractReviewAction *> s_actions;
    return s_actions;
}

AbstractReviewAction::Recorder::Recorder(AbstractReviewAction *action)
{
    getActions().append(action);
}

QList<AbstractReviewAction *> AbstractReviewAction::all()
{
    return getActions();
}
