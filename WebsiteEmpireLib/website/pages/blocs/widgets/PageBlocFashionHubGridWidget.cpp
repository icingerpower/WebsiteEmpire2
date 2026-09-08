#include "PageBlocFashionHubGridWidget.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"

#include <QLabel>
#include <QVBoxLayout>

PageBlocFashionHubGridWidget::PageBlocFashionHubGridWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , m_dimensionLabel(new QLabel(this))
    , m_tagValueLabel(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_dimensionLabel);
    layout->addWidget(m_tagValueLabel);
    layout->addStretch();
}

void PageBlocFashionHubGridWidget::load(const QHash<QString, QString> &values)
{
    m_dimension = values.value(QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION));
    m_tagValue  = values.value(QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE));
    m_dimensionLabel->setText(tr("Dimension: %1").arg(m_dimension));
    m_tagValueLabel->setText(tr("Tag: %1").arg(m_tagValue));
}

void PageBlocFashionHubGridWidget::save(QHash<QString, QString> &values) const
{
    // Passthrough — this widget has no editable fields; dimension/tag_value
    // are auto-managed by FashionTaxonomyHubSyncer, never hand-edited.
    values.insert(QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION), m_dimension);
    values.insert(QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE), m_tagValue);
}
