#ifndef PANETHEME_H
#define PANETHEME_H

#include <QWidget>

class AbstractTheme;
class QListWidgetItem;

namespace Ui { class PaneTheme; }

/**
 * Pane for managing the active theme.
 *
 * Left panel (max 150 px): QListWidget listing all registered themes.
 * Selecting a different theme shows a Yes/No confirmation dialog; on
 * confirmation, themeIdSelected() is emitted with the new theme id so
 * that the caller can recreate the theme and call setTheme() again.
 *
 * Right panel (vertical splitter):
 *   - Top:    QTableView bound to the current AbstractTheme model
 *             (editable theme params such as colours and fonts).
 *   - Bottom: QToolBox with one page per common bloc returned by the
 *             theme's getTopBlocs() and getBottomBlocs().  Each page
 *             is a WidgetEditCommonBlocs.
 */
class PaneTheme : public QWidget
{
    Q_OBJECT

public:
    explicit PaneTheme(QWidget *parent = nullptr);
    ~PaneTheme() override;

    /**
     * Bind the pane to a concrete theme instance.
     * Populates the params table and rebuilds the bloc toolbox.
     * May be called again after a theme switch.
     * theme may be nullptr to clear the pane.
     */
    void setTheme(AbstractTheme *theme);

signals:
    /**
     * Emitted when the user confirms a theme switch.
     * The caller should recreate the theme, update the engine, and
     * call setTheme() with the new instance.
     */
    void themeIdSelected(const QString &themeId);

private slots:
    void onCurrentThemeItemChanged(QListWidgetItem *current,
                                   QListWidgetItem *previous);

private:
    void _populateThemeList();
    void _rebuildBlocsToolBox();

    Ui::PaneTheme *ui;
    AbstractTheme *m_theme = nullptr;
};

#endif // PANETHEME_H
