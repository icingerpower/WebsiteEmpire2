#ifndef ABSTRACTCOMMONBLOCWIDGET_H
#define ABSTRACTCOMMONBLOCWIDGET_H

#include <QWidget>

class AbstractCommonBloc;

/**
 * Base editor widget for a common bloc (header, footer, menus).
 *
 * Unlike AbstractPageBlockWidget, data transfer uses typed references to the
 * concrete AbstractCommonBloc rather than a flat QHash.  Common blocs hold
 * structured data (HTML strings, menu item lists) that cannot be expressed as
 * a flat key→value map without losing type information.
 *
 * Concrete widget subclasses downcast the AbstractCommonBloc reference to
 * their specific bloc type in loadFrom() and saveTo().
 */
class AbstractCommonBlocWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AbstractCommonBlocWidget(QWidget *parent = nullptr);
    ~AbstractCommonBlocWidget() override = default;

    /**
     * Populate the widget from the bloc's current state.
     * The bloc must outlive this call.
     */
    virtual void loadFrom(const AbstractCommonBloc &bloc) = 0;

    /**
     * Write the widget's edited state back into the bloc.
     */
    virtual void saveTo(AbstractCommonBloc &bloc) const = 0;

signals:
    /**
     * Emitted whenever the user modifies any field in the widget.
     * WidgetEditCommonBlocs connects to this to auto-save immediately.
     */
    void changed();
};

#endif // ABSTRACTCOMMONBLOCWIDGET_H
