#ifndef PAGESSTATSWIDGET_H
#define PAGESSTATSWIDGET_H

#include <QDir>
#include <QWidget>

class QSqlQueryModel;
namespace Ui { class PagesStatsWidget; }

/**
 * Widget that shows aggregated display/click statistics per page permalink,
 * read directly from stats.db via QSqlDatabase (QSQLITE, WAL mode).
 *
 * Columns: Permalink | Displays | Clicks | CTR% | Avg Scroll% | Avg Time(s)
 *
 * The widget opens stats.db on construction and closes it on destruction.
 * Call refresh() (or click Refresh) to re-query after new data arrives.
 *
 * workingDir must contain stats.db.  The file is created if missing (empty).
 */
class PagesStatsWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr const char *FILENAME = "stats.db";

    explicit PagesStatsWidget(const QDir &workingDir,
                              QWidget    *parent = nullptr);
    ~PagesStatsWidget() override;

public slots:
    void refresh();

private:
    Ui::PagesStatsWidget *ui;
    QString               m_connectionName;
    QSqlQueryModel       *m_model = nullptr;
};

#endif // PAGESSTATSWIDGET_H
