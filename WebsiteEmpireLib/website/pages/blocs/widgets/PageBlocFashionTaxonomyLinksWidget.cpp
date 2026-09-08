#include "PageBlocFashionTaxonomyLinksWidget.h"

#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

PageBlocFashionTaxonomyLinksWidget::PageBlocFashionTaxonomyLinksWidget(
    const QHash<QString, QStringList> &vocabByDimension, QWidget *parent)
    : AbstractPageBlockWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
        auto *header = new QLabel(QStringLiteral("<b>") + dim.displayName + QStringLiteral("</b>"), this);
        layout->addWidget(header);

        const QStringList items = vocabByDimension.value(dim.taxonomyId);
        if (items.isEmpty()) {
            auto *label = new QLabel(
                tr("No %1 items in local taxonomy. Use the Taxonomies tab to sync.")
                    .arg(dim.displayName),
                this);
            label->setWordWrap(true);
            layout->addWidget(label);
            continue;
        }

        auto *list = new QListWidget(this);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        for (const QString &name : items) {
            auto *item = new QListWidgetItem(name, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        layout->addWidget(list);
        m_listsByDimension.insert(dim.taxonomyId, list);
    }
}

void PageBlocFashionTaxonomyLinksWidget::load(const QHash<QString, QString> &values)
{
    for (auto it = m_listsByDimension.constBegin(); it != m_listsByDimension.constEnd(); ++it) {
        const QString &taxonomyId = it.key();
        QListWidget   *list       = it.value();

        const QStringList selected = values.value(taxonomyId)
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        QSet<QString> selectedSet;
        selectedSet.reserve(selected.size());
        for (const QString &s : selected) {
            selectedSet.insert(s.trimmed());
        }

        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            item->setCheckState(selectedSet.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void PageBlocFashionTaxonomyLinksWidget::save(QHash<QString, QString> &values) const
{
    for (auto it = m_listsByDimension.constBegin(); it != m_listsByDimension.constEnd(); ++it) {
        const QString &taxonomyId = it.key();
        QListWidget   *list       = it.value();

        QStringList checked;
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem *item = list->item(i);
            if (item->checkState() == Qt::Checked) {
                checked.append(item->text());
            }
        }
        values.insert(taxonomyId, checked.join(QLatin1Char(',')));
    }
}
