#ifndef PANEPAGETAXONOMY_H
#define PANEPAGETAXONOMY_H
#include <QDir>
#include <QWidget>
class AbstractEngine;
class AvailableCliList;
class GenStrategyTable;
class QProcess;
class QStandardItemModel;
class WebsiteSettingsTable;
namespace Ui { class PanePageTaxonomy; }

// One strategy per taxonomy; targets the existing hub URLs and preserves their
// link-list data. Generation runs in a child process using the shared launcher.
class PanePageTaxonomy : public QWidget
{
    Q_OBJECT
public:
    explicit PanePageTaxonomy(QWidget *parent = nullptr);
    ~PanePageTaxonomy() override;
    void setup(const QDir &workingDir, AbstractEngine *engine, WebsiteSettingsTable *settings);
private:
    void loadTaxonomy();
    void saveEdits();
    void generate(bool one);
    void refreshGeneratedPages();
    void previewPage(int pageId);
    void _restoreTaxonomyCli();
    QStringList generationArgs(bool one) const;
    Ui::PanePageTaxonomy *ui;
    QDir m_workingDir;
    AbstractEngine *m_engine = nullptr;
    GenStrategyTable *m_strategies = nullptr;
    AvailableCliList *m_cliList = nullptr;
    QStandardItemModel *m_generatedModel = nullptr;
    QProcess *m_process = nullptr;
    QString m_taxonomyId;
    int m_row = -1;
    bool m_loading = false;
};
#endif

