#ifndef GSCDATASOURCE_H
#define GSCDATASOURCE_H

#include "AbstractPerformanceDataSource.h"
#include "GscSettings.h"

#include <QDateTime>
#include <QString>

class QNetworkAccessManager;

/**
 * Performance data source backed by the Google Search Console Search Analytics
 * API.
 *
 * Authentication uses a service account JSON key (RS256 JWT signed via the
 * openssl CLI — no additional library dependency).  The access token is cached
 * for its 1-hour lifetime so the sign+exchange round-trip only happens once per
 * process.
 *
 * fetchData() is synchronous from the caller's perspective: it spins a local
 * QEventLoop until the network reply arrives.  Call it before starting any
 * QCoro coroutines to avoid nested event-loop issues.
 *
 * Requires: openssl in PATH, ANTHROPIC_API_KEY not needed (uses service
 * account), and Qt Network module.
 */
class GscDataSource : public AbstractPerformanceDataSource
{
public:
    explicit GscDataSource(const GscSettings &settings);
    ~GscDataSource() override;

    bool isConfigured() const override;

    /**
     * Fetches per-URL impressions and clicks from the GSC Search Analytics API
     * for the given domain over the last nDays days.
     * Maps domain → GSC property using the GscSettings properties map.
     * Returns empty list if the domain is not mapped, auth fails, or the API
     * returns an error.
     */
    QList<UrlPerformance> fetchData(const QString &domain, int nDays = 30) override;

private:
    // JWT / token helpers
    QString _buildJwt(const QString &clientEmail, const QString &privateKeyPem);
    QString _signWithOpenssl(const QString &signingInput, const QString &privateKeyPem);
    QString _fetchAccessToken(const QString &jwt);

    // Synchronous network POST — spins a local QEventLoop.
    QByteArray _syncPost(const QString &url,
                         const QByteArray &body,
                         const QByteArray &contentType,
                         const QString    &bearerToken = {});

    const GscSettings    &m_settings;
    QNetworkAccessManager *m_nam          = nullptr;
    QString               m_accessToken;
    qint64                m_tokenExpiresAt = 0; // seconds since epoch
};

#endif // GSCDATASOURCE_H
