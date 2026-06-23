#include "PageBlocSymptomLinksWidget.h"

#include "website/pages/blocs/PageBlocSymptomLinks.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

PageBlocSymptomLinksWidget::PageBlocSymptomLinksWidget(const QStringList &items,
                                                       QWidget *parent)
    : AbstractPageBlockWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (items.isEmpty()) {
        m_label = new QLabel(
            tr("No symptoms in local taxonomy. Use the Taxonomies tab to sync."),
            this);
        m_label->setWordWrap(true);
        layout->addWidget(m_label);
    } else {
        m_list = new QListWidget(this);
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        for (const QString &name : std::as_const(items)) {
            auto *item = new QListWidgetItem(name, m_list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        layout->addWidget(m_list);
    }
}

void PageBlocSymptomLinksWidget::load(const QHash<QString, QString> &values)
{
    if (!m_list) {
        return;
    }
    const QStringList selected = values.value(
        QLatin1String(PageBlocSymptomLinks::KEY_SYMPTOMS))
        .split(QLatin1Char(','), Qt::SkipEmptyParts);

    QSet<QString> selectedSet;
    selectedSet.reserve(selected.size());
    for (const QString &s : selected) {
        selectedSet.insert(s.trimmed());
    }

    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        item->setCheckState(selectedSet.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
    }
}

void PageBlocSymptomLinksWidget::save(QHash<QString, QString> &values) const
{
    if (!m_list) {
        return;
    }
    QStringList checked;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (item->checkState() == Qt::Checked) {
            checked.append(item->text());
        }
    }
    values.insert(QLatin1String(PageBlocSymptomLinks::KEY_SYMPTOMS),
                  checked.join(QLatin1Char(',')));
}
