#include "AbstractPageBlockWidget.h"

AbstractPageBlockWidget::AbstractPageBlockWidget(QWidget *parent)
    : QWidget(parent)
{
}

void AbstractPageBlockWidget::setImageContext(ImageWriter *, const QStringList &)
{
}
