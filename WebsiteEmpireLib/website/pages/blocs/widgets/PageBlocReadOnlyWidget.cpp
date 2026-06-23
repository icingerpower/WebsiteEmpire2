#include "PageBlocReadOnlyWidget.h"

#include <QLabel>
#include <QVBoxLayout>

PageBlocReadOnlyWidget::PageBlocReadOnlyWidget(const QString &description, QWidget *parent)
    : AbstractPageBlockWidget(parent)
{
    auto *lbl = new QLabel(description, this);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("color:#666;font-style:italic;padding:8px"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(lbl);
}

void PageBlocReadOnlyWidget::load(const QHash<QString, QString> &) {}
void PageBlocReadOnlyWidget::save(QHash<QString, QString> &) const {}
