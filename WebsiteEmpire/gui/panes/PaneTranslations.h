#ifndef PANETRANSLATIONS_H
#define PANETRANSLATIONS_H

#include <QDir>
#include <QWidget>
#include <memory>

namespace Ui { class PaneTranslations; }

class AbstractEngine;
class AbstractTheme;
class CategoryTable;
class PageDb;
class PageRepositoryDb;
class TranslationFieldTable;
class TranslationStatusTable;

/**
 * Pane showing the translation state of the website.
 *
 * Top table  (TranslationStatusTable) — one row per source page, one
 *   checkbox column per target language; checked when the translation is
 *   complete for that page / language pair.
 *
 * Bottom table (TranslationFieldTable) — one row per translatable common-bloc
 *   field (header, footer, cookies, menus…), one text column per target
 *   language showing the stored translated string.
 *
 * The two tables are separated by a vertical QSplitter so the user can
 * resize them independently.
 *
 * DB initialisation is deferred: nothing is opened until setVisible(true) is
 * first called.  setup() must be called from MainWindow::_init() before that.
 *
 * Destruction order: m_statusModel / m_fieldModel are declared after the
 * repos they reference so that reverse-order member destruction destroys
 * the models first, then the repos.
 */
class PaneTranslations : public QWidget
{
    Q_OBJECT

public:
    explicit PaneTranslations(QWidget *parent = nullptr);
    ~PaneTranslations() override;

    /**
     * Binds the pane to a working directory, engine, and theme.
     * engine drives the language columns in the status table: one column per
     *   unique lang code present in the engine's domain rows.  May be nullptr
     *   when no engine has been configured.
     * theme provides the common blocs shown in the fields table; may be
     *   nullptr when no theme is configured.
     * Must be called once from MainWindow::_init() before first show.
     */
    void setup(const QDir &workingDir, AbstractEngine *engine, AbstractTheme *theme);

    /**
     * Updates the active theme pointer and reloads the pane if it has already
     * been initialised.  Call from MainWindow::_reloadTheme() after the theme
     * object is replaced so the old pointer never dangles.
     */
    void setTheme(AbstractTheme *theme);

    void setVisible(bool visible) override;

private:
    /**
     * Creates the DB stack, reads TranslationSettings, and wires both
     * table models to their views.  Called on first setVisible(true) and
     * whenever _reload() is triggered.
     */
    void _initModels();

    /**
     * Tears down all models and repos, then calls _initModels() again.
     * Connected to the Reload button and called automatically when the theme
     * is replaced via setTheme().
     */
    void _reload();

    /**
     * Shows a small dialog containing the two CLI commands the user should run
     * to translate pages and common blocs respectively.
     * Connected to the "View commands" button.
     */
    void _viewCommands();

    /**
     * Prompts for a language via a combobox dialog, then deletes all
     * translation data for the selected page / chosen language.
     * Connected to the "Remove one translation" button.
     */
    void _removeOneTranslation();

    /**
     * Shows a confirmation dialog, then deletes all translation data for the
     * selected page across every language.
     * Connected to the "Remove all translations" button.
     */
    void _removeAllTranslations();

    /**
     * Enables or disables the remove buttons based on whether a row is
     * currently selected in tableViewStatus.  Called on selection changes.
     */
    void _updateRemoveButtons();

    /** Refreshes the progress labels from the current model state. */
    void _updateProgressLabels();

    Ui::PaneTranslations *ui;
    QDir            m_workingDir;
    AbstractEngine *m_engine  = nullptr;
    AbstractTheme  *m_theme   = nullptr;
    bool            m_isSetup = false;

    // Data sources — repo must be declared before models (destroyed after).
    std::unique_ptr<CategoryTable>    m_categoryTable;
    std::unique_ptr<PageDb>           m_pageDb;
    std::unique_ptr<PageRepositoryDb> m_pageRepo;

    // Models — declared after repos; destroyed first (reverse-order rule).
    std::unique_ptr<TranslationStatusTable> m_statusModel;
    std::unique_ptr<TranslationFieldTable>  m_fieldModel;
};

#endif // PANETRANSLATIONS_H
