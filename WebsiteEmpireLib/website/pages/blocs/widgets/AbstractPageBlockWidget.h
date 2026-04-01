#ifndef ABSTRACTPAGEBLOCKWIDGET_H
#define ABSTRACTPAGEBLOCKWIDGET_H

#include <QWidget>

/**
 * Base widget for editing a page bloc's content.
 *
 * Every concrete bloc type provides its own subclass that knows how to
 * display and edit that specific kind of content.
 *
 * - load()  populates the widget from the stored content string.
 * - save()  serialises the widget state back into contentToUpdate.
 */
class AbstractPageBlockWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AbstractPageBlockWidget(QWidget *parent = nullptr);
    ~AbstractPageBlockWidget() override = default;

    virtual void load(const QString &origContent) = 0;
    virtual void save(QString &contentToUpdate) = 0;
};

#endif // ABSTRACTPAGEBLOCKWIDGET_H
