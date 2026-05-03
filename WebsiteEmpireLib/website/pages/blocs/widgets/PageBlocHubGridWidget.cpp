#include "PageBlocHubGridWidget.h"

#include "website/pages/blocs/PageBlocHubGrid.h"
#include "website/pages/blocs/widgets/CategoryEditorWidget.h"

#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

PageBlocHubGridWidget::PageBlocHubGridWidget(CategoryTable &table, QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , m_categoryWidget(new CategoryEditorWidget(table, true, this))
    , m_maxArticlesSpin(new QSpinBox(this))
{
    m_maxArticlesSpin->setRange(1, 100);
    m_maxArticlesSpin->setValue(12);

    auto *form = new QFormLayout;
    form->addRow(tr("Max articles:"), m_maxArticlesSpin);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_categoryWidget);
    layout->addLayout(form);
}

void PageBlocHubGridWidget::load(const QHash<QString, QString> &values)
{
    m_categoryWidget->load(values);
    const int maxArticles = values.value(QLatin1String(PageBlocHubGrid::KEY_MAX_ARTICLES),
                                          QStringLiteral("12")).toInt();
    m_maxArticlesSpin->setValue(maxArticles > 0 ? maxArticles : 12);
}

void PageBlocHubGridWidget::save(QHash<QString, QString> &values) const
{
    m_categoryWidget->save(values);
    values.insert(QLatin1String(PageBlocHubGrid::KEY_MAX_ARTICLES),
                  QString::number(m_maxArticlesSpin->value()));
}
