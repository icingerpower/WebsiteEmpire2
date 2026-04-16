#include "GscDataSource.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryFile>
#include <QUrl>

static constexpr const char *TOKEN_URL =
    "https://oauth2.googleapis.com/token";
static constexpr const char *ANALYTICS_URL_TMPL =
    "https://www.googleapis.com/webmasters/v3/sites/%1/searchAnalytics/query";
static constexpr const char *SCOPE =
    "https://www.googleapis.com/auth/webmasters.readonly";
static constexpr const char *CLAUDE_MODEL_UNUSED = ""; // not used here

// ---------------------------------------------------------------------------

GscDataSource::GscDataSource(const GscSettings &settings)
    : m_settings(settings)
    , m_nam(new QNetworkAccessManager(nullptr))
{
}

GscDataSource::~GscDataSource()
{
    delete m_nam;
}

bool GscDataSource::isConfigured() const
{
    return m_settings.isConfigured();
}

// ---------------------------------------------------------------------------
// fetchData
// ---------------------------------------------------------------------------

QList<UrlPerformance> GscDataSource::fetchData(const QString &domain, int nDays)
{
    if (!isConfigured()) {
        return {};
    }

    const QString property = m_settings.propertyForDomain(domain);
    if (property.isEmpty()) {
        qWarning() << "GscDataSource: no GSC property mapped for domain" << domain;
        return {};
    }

    // ---- Load service account JSON ----------------------------------------
    QFile keyFile(m_settings.serviceAccountKeyPath());
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "GscDataSource: cannot open service account key:"
                   << m_settings.serviceAccountKeyPath();
        return {};
    }
    const QJsonObject keyObj = QJsonDocument::fromJson(keyFile.readAll()).object();
    const QString clientEmail  = keyObj.value(QStringLiteral("client_email")).toString();
    const QString privateKeyPem = keyObj.value(QStringLiteral("private_key")).toString();

    if (clientEmail.isEmpty() || privateKeyPem.isEmpty()) {
        qWarning() << "GscDataSource: service account JSON missing client_email or private_key";
        return {};
    }

    // ---- Obtain access token (cached for 1 hour) --------------------------
    if (m_accessToken.isEmpty()
        || QDateTime::currentSecsSinceEpoch() >= m_tokenExpiresAt) {
        const QString jwt = _buildJwt(clientEmail, privateKeyPem);
        if (jwt.isEmpty()) {
            qWarning() << "GscDataSource: JWT signing failed";
            return {};
        }
        m_accessToken = _fetchAccessToken(jwt);
        if (m_accessToken.isEmpty()) {
            qWarning() << "GscDataSource: token exchange failed";
            return {};
        }
        m_tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + 3500;
    }

    // ---- Build date range -------------------------------------------------
    const QDate today = QDate::currentDate();
    const QString endDate   = today.toString(Qt::ISODate);
    const QString startDate = today.addDays(-nDays).toString(Qt::ISODate);

    // ---- Call GSC Search Analytics API ------------------------------------
    const QString apiUrl = QString::fromLatin1(ANALYTICS_URL_TMPL)
                               .arg(QString::fromUtf8(
                                   QUrl::toPercentEncoding(property)));

    QJsonObject reqBody;
    reqBody[QStringLiteral("startDate")]  = startDate;
    reqBody[QStringLiteral("endDate")]    = endDate;
    reqBody[QStringLiteral("dimensions")] = QJsonArray{QStringLiteral("page")};
    reqBody[QStringLiteral("rowLimit")]   = 1000;

    const QByteArray raw = _syncPost(apiUrl,
                                      QJsonDocument(reqBody).toJson(QJsonDocument::Compact),
                                      QByteArrayLiteral("application/json"),
                                      m_accessToken);
    if (raw.isEmpty()) {
        return {};
    }

    // ---- Parse response ---------------------------------------------------
    const QJsonObject resp = QJsonDocument::fromJson(raw).object();
    const QJsonArray rows = resp.value(QStringLiteral("rows")).toArray();

    QList<UrlPerformance> result;
    result.reserve(rows.size());

    for (const QJsonValue &rowVal : rows) {
        const QJsonObject row = rowVal.toObject();
        const QJsonArray  keys = row.value(QStringLiteral("keys")).toArray();
        if (keys.isEmpty()) {
            continue;
        }
        UrlPerformance entry;
        // The GSC "page" dimension returns full URLs; extract the path component.
        const QString fullUrl = keys.at(0).toString();
        const QUrl parsed(fullUrl);
        entry.url         = parsed.path().isEmpty() ? fullUrl : parsed.path();
        entry.impressions = qRound(row.value(QStringLiteral("impressions")).toDouble());
        entry.clicks      = qRound(row.value(QStringLiteral("clicks")).toDouble());
        result.append(entry);
    }

    return result;
}

// ---------------------------------------------------------------------------
// JWT building
// ---------------------------------------------------------------------------

QString GscDataSource::_buildJwt(const QString &clientEmail,
                                   const QString &privateKeyPem)
{
    // Header
    const QByteArray header = QByteArrayLiteral("{\"alg\":\"RS256\",\"typ\":\"JWT\"}");
    const QString headerB64 = QString::fromLatin1(
        header.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    // Claims
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonObject claims;
    claims[QStringLiteral("iss")]   = clientEmail;
    claims[QStringLiteral("scope")] = QLatin1StringView(SCOPE);
    claims[QStringLiteral("aud")]   = QLatin1StringView(TOKEN_URL);
    claims[QStringLiteral("exp")]   = now + 3600;
    claims[QStringLiteral("iat")]   = now;

    const QByteArray claimsBytes = QJsonDocument(claims).toJson(QJsonDocument::Compact);
    const QString claimsB64 = QString::fromLatin1(
        claimsBytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    const QString signingInput = headerB64 + QLatin1Char('.') + claimsB64;
    const QString signature = _signWithOpenssl(signingInput, privateKeyPem);
    if (signature.isEmpty()) {
        return {};
    }

    return signingInput + QLatin1Char('.') + signature;
}

QString GscDataSource::_signWithOpenssl(const QString &signingInput,
                                         const QString &privateKeyPem)
{
    // Write private key to a temp file.
    QTemporaryFile keyFile;
    keyFile.setAutoRemove(true);
    if (!keyFile.open()) {
        qWarning() << "GscDataSource: cannot create temp key file";
        return {};
    }
    keyFile.write(privateKeyPem.toUtf8());
    keyFile.flush();

    // Write signing input to a temp file.
    QTemporaryFile dataFile;
    dataFile.setAutoRemove(true);
    if (!dataFile.open()) {
        qWarning() << "GscDataSource: cannot create temp data file";
        return {};
    }
    dataFile.write(signingInput.toUtf8());
    dataFile.flush();

    // Signature output temp file — must be closed before openssl writes to it.
    QTemporaryFile sigFile;
    sigFile.setAutoRemove(true);
    if (!sigFile.open()) {
        qWarning() << "GscDataSource: cannot create temp sig file";
        return {};
    }
    const QString sigPath = sigFile.fileName();
    sigFile.close();

    // Run: openssl dgst -sha256 -sign <keyFile> -out <sigFile> <dataFile>
    QProcess proc;
    proc.start(QStringLiteral("openssl"), {
        QStringLiteral("dgst"),
        QStringLiteral("-sha256"),
        QStringLiteral("-sign"),  keyFile.fileName(),
        QStringLiteral("-out"),   sigPath,
        dataFile.fileName()
    });

    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        qWarning() << "GscDataSource: openssl signing failed:"
                   << proc.readAllStandardError();
        return {};
    }

    QFile readSig(sigPath);
    if (!readSig.open(QIODevice::ReadOnly)) {
        qWarning() << "GscDataSource: cannot read signature file";
        return {};
    }
    const QByteArray sig = readSig.readAll();

    return QString::fromLatin1(
        sig.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// ---------------------------------------------------------------------------
// Token exchange
// ---------------------------------------------------------------------------

QString GscDataSource::_fetchAccessToken(const QString &jwt)
{
    const QByteArray body =
        QByteArrayLiteral("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
                          "&assertion=")
        + jwt.toUtf8();

    const QByteArray raw = _syncPost(QLatin1StringView(TOKEN_URL), body,
                                      QByteArrayLiteral("application/x-www-form-urlencoded"));
    if (raw.isEmpty()) {
        return {};
    }

    const QJsonObject obj = QJsonDocument::fromJson(raw).object();
    return obj.value(QStringLiteral("access_token")).toString();
}

// ---------------------------------------------------------------------------
// Synchronous network POST
// ---------------------------------------------------------------------------

QByteArray GscDataSource::_syncPost(const QString    &url,
                                     const QByteArray &body,
                                     const QByteArray &contentType,
                                     const QString    &bearerToken)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (!bearerToken.isEmpty()) {
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + bearerToken.toUtf8());
    }

    QEventLoop loop;
    QNetworkReply *reply = m_nam->post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "GscDataSource: network error for" << url
                   << reply->errorString();
        reply->deleteLater();
        return {};
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}
