#ifndef WIDGETEDITCOMMONBLOCS_H
#define WIDGETEDITCOMMONBLOCS_H

#include <QWidget>

class AbstractCommonBloc;
class AbstractCommonBlocWidget;
class AbstractTheme;

namespace Ui { class WidgetEditCommonBlocs; }

/**
 * Editor container for a single AbstractCommonBloc instance.
 *
 * Call setBloc() to bind this widget to a bloc: it will call
 * bloc->createEditWidget(), embed the returned widget, and load
 * the bloc's current data into it.
 *
 * Changes are saved automatically: the container connects to
 * AbstractCommonBlocWidget::changed() and immediately calls
 * saveTo() so the bloc is always up to date.
 *
 * Instances of this class are used as pages inside the QToolBox in
 * PaneTheme, one per common bloc exposed by the current theme.
 */
class WidgetEditCommonBlocs : public QWidget
{
    Q_OBJECT

public:
    explicit WidgetEditCommonBlocs(QWidget *parent = nullptr);
    ~WidgetEditCommonBlocs() override;

    /**
     * Bind this widget to bloc.
     * Creates the editor widget, inserts it into the layout, and
     * populates it with the bloc's current state.  theme is used to
     * persist all blocs to disk whenever the user makes a change.
     * Passing nullptr for bloc clears any existing editor.
     * Both pointers must outlive this widget.
     */
    void setBloc(AbstractCommonBloc *bloc, AbstractTheme *theme);

private slots:
    void onAutoSave();

private:
    Ui::WidgetEditCommonBlocs *ui;
    AbstractCommonBloc        *m_bloc   = nullptr;
    AbstractCommonBlocWidget  *m_editor = nullptr;
    AbstractTheme             *m_theme  = nullptr;
};

#endif // WIDGETEDITCOMMONBLOCS_H
