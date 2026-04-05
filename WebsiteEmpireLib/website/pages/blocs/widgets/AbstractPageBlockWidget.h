#ifndef ABSTRACTPAGEBLOCKWIDGET_H
#define ABSTRACTPAGEBLOCKWIDGET_H

#include <QHash>
#include <QString>
#include <QWidget>

/**
 * Base widget for editing a page bloc's content.
 *
 * Every concrete bloc type provides its own subclass that knows how to
 * display and edit that specific kind of content.
 *
 * The hash interface mirrors AbstractPageBloc::load / AbstractPageBloc::save
 * so that PageEditorDialog can transfer state between the bloc and its widget
 * with a single call — no per-key translation layer needed.
 *
 * - load()  populates the widget from the flat key→value map.
 * - save()  serialises the widget state back into the map.
 */
class AbstractPageBlockWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AbstractPageBlockWidget(QWidget *parent = nullptr);
    ~AbstractPageBlockWidget() override = default;

    virtual void load(const QHash<QString, QString> &values) = 0;
    virtual void save(QHash<QString, QString> &values) const = 0;
};

#endif // ABSTRACTPAGEBLOCKWIDGET_H
