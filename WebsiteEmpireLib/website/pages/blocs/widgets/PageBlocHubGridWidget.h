#ifndef PAGEBLOCHUBGRIDWIDGET_H
#define PAGEBLOCHUBGRIDWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

class CategoryTable;
class CategoryEditorWidget;
class QSpinBox;

/**
 * Editor widget for PageBlocHubGrid.
 *
 * Provides a CategoryEditorWidget (check-box mode) for choosing which
 * categories feed the hub, and a QSpinBox for the maximum article count.
 *
 * Built programmatically — no .ui file.  The CategoryTable reference must
 * outlive this widget.
 */
class PageBlocHubGridWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocHubGridWidget(CategoryTable &table, QWidget *parent = nullptr);
    ~PageBlocHubGridWidget() override = default;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    CategoryEditorWidget *m_categoryWidget;
    QSpinBox             *m_maxArticlesSpin;
};

#endif // PAGEBLOCHUBGRIDWIDGET_H
