#ifndef PAGEBLOC_META_WIDGET_H
#define PAGEBLOC_META_WIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocMetaWidget; }

/**
 * Edit widget for PageBlocMeta.
 *
 * Provides a Title field (target 50–60 chars) and a Description field
 * (target 120–160 chars).  Live character counters are shown next to each
 * field so the editor can stay within search-engine display limits.
 */
class PageBlocMetaWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocMetaWidget(QWidget *parent = nullptr);
    ~PageBlocMetaWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private slots:
    void _onTitleChanged(const QString &text);
    void _onDescChanged();

private:
    Ui::PageBlocMetaWidget *ui;
};

#endif // PAGEBLOC_META_WIDGET_H
