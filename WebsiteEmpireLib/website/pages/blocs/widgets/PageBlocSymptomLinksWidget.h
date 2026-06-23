#ifndef PAGEBLOCSYMPTOMLINKSWIDGET_H
#define PAGEBLOCSYMPTOMLINKSWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

#include <QStringList>

class QListWidget;
class QLabel;

/**
 * Editor widget for PageBlocSymptomLinks.
 *
 * Displays a scrollable list of checkable symptom names passed in via the
 * constructor (pre-loaded from the local taxonomy store by the caller).
 * The user checks each symptom that applies to the condition article.
 *
 * load() restores previously checked symptoms from the "symptoms" key.
 * save() serialises the checked names as a comma-separated string.
 *
 * When items is empty the list is replaced by a label directing the user
 * to the Taxonomies tab to sync the vocabulary first.
 */
class PageBlocSymptomLinksWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocSymptomLinksWidget(const QStringList &items, QWidget *parent = nullptr);
    ~PageBlocSymptomLinksWidget() override = default;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    QListWidget *m_list  = nullptr;
    QLabel      *m_label = nullptr;
};

#endif // PAGEBLOCSYMPTOMLINKSWIDGET_H
