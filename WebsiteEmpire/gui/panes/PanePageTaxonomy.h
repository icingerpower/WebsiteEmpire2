#ifndef PANEPAGETAXONOMY_H
#define PANEPAGETAXONOMY_H
#include <QDir>
#include <QWidget>
class AbstractEngine;
class GenStrategyTable;
class QProcess;
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
    QStringList generationArgs(bool one) const;
    Ui::PanePageTaxonomy *ui;
    QDir m_workingDir;
    GenStrategyTable *m_strategies = nullptr;
    QProcess *m_process = nullptr;
    QString m_taxonomyId;
    int m_row = -1;
    bool m_loading = false;
};
#endif

