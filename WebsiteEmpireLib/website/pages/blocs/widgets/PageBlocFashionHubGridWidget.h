#ifndef PAGEBLOCFASHIONHUBGRIDWIDGET_H
#define PAGEBLOCFASHIONHUBGRIDWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

class QLabel;

/**
 * Read-only editor widget for PageBlocFashionHubGrid.
 *
 * dimension/tag_value are set once by FashionTaxonomyHubSyncer::syncStubs()
 * and never hand-edited — unlike PageBlocHubGridWidget's category checkbox
 * picker, this widget only displays the two values (so an editor can see
 * which tag a hub page covers) and passes them through unchanged on save().
 *
 * Built programmatically — no .ui file.
 */
class PageBlocFashionHubGridWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocFashionHubGridWidget(QWidget *parent = nullptr);
    ~PageBlocFashionHubGridWidget() override = default;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    QLabel *m_dimensionLabel;
    QLabel *m_tagValueLabel;
    QString m_dimension;
    QString m_tagValue;
};

#endif // PAGEBLOCFASHIONHUBGRIDWIDGET_H
