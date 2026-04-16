#ifndef GSCSETTINGS_H
#define GSCSETTINGS_H

#include <QDir>
#include <QMap>
#include <QString>

/**
 * Loads GSC (Google Search Console) configuration from gsc_settings.json in
 * the working directory.
 *
 * Expected JSON format:
 * {
 *   "serviceAccountKeyPath": "/path/to/service-account.json",
 *   "properties": {
 *     "example.com":     "sc-domain:example.com",
 *     "otherdomain.com": "sc-domain:otherdomain.com"
 *   }
 * }
 *
 * serviceAccountKeyPath — absolute path to the Google Cloud service account
 *   JSON key file.  The service account must have "Restricted" read access
 *   granted to each GSC property listed under "properties".
 *
 * properties — map from bare domain (as it appears in AbstractEngine) to the
 *   GSC property URL.  Use "sc-domain:<domain>" for domain-level properties or
 *   "https://www.<domain>/" for URL-prefix properties.
 *
 * isConfigured() returns false when the file is absent or the key path is
 * empty; GscDataSource will not attempt any API calls in that case.
 */
class GscSettings
{
public:
    static constexpr const char *FILENAME = "gsc_settings.json";

    explicit GscSettings(const QDir &workingDir);

    bool           isConfigured()                      const;
    const QString &serviceAccountKeyPath()             const;
    // Returns the GSC property URL for domain, or empty string if not mapped.
    QString        propertyForDomain(const QString &domain) const;

private:
    QString              m_serviceAccountKeyPath;
    QMap<QString, QString> m_properties; // domain → GSC property URL
};

#endif // GSCSETTINGS_H
