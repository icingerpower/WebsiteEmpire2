#include "StatsDbDataSource.h"

#include <QDate>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

StatsDbDataSource::StatsDbDataSource(const QDir &workingDir)
    : m_dbPath(workingDir.filePath(QStringLiteral("stats.db")))
{
}

bool StatsDbDataSource::isConfigured() const
{
    return QFile::exists(m_dbPath);
}

QList<UrlPerformance> StatsDbDataSource::fetchData(const QString & /*domain*/, int nDays)
{
    if (!isConfigured()) {
        return {};
    }

    // Open a temporary, read-only connection to stats.db.
    const QString connName = QStringLiteral("stats_perf_")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QList<UrlPerformance> result;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

        if (!db.open()) {
            qWarning() << "StatsDbDataSource: cannot open stats.db:" << m_dbPath;
        } else {
            const QString from = QDate::currentDate().addDays(-nDays).toString(Qt::ISODate);

            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT path, COUNT(*) AS visits"
                " FROM stats"
                " WHERE visited_at >= :from"
                " GROUP BY path"
                " ORDER BY visits DESC"));
            q.bindValue(QStringLiteral(":from"), from);
            q.exec();

            while (q.next()) {
                UrlPerformance entry;
                entry.url         = q.value(0).toString();
                entry.clicks      = q.value(1).toInt();
                entry.impressions = entry.clicks; // no impression data in stats.db
                result.append(entry);
            }
            db.close();
        }
    }

    QSqlDatabase::removeDatabase(connName);
    return result;
}
