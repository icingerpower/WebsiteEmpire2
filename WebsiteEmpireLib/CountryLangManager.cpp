#include "CountryLangManager.h"

CountryLangManager *CountryLangManager::instance()
{
    static CountryLangManager s_instance;
    return &s_instance;
}

CountryLangManager::CountryLangManager(QObject *parent)
    : QObject(parent)
{
}

QStringList CountryLangManager::defaultLangCodes() const
{
    return {
        // Tier 1 — over 200 million total speakers
        QStringLiteral("en"), QStringLiteral("zh"), QStringLiteral("es"), QStringLiteral("hi"),
        QStringLiteral("ar"), QStringLiteral("bn"), QStringLiteral("pt"),
        QStringLiteral("ru"), QStringLiteral("ur"), QStringLiteral("ms"),
        QStringLiteral("id"),
        // Tier 2 — 60–200 million speakers
        QStringLiteral("ja"), QStringLiteral("de"), QStringLiteral("fr"),
        QStringLiteral("pa"), QStringLiteral("vi"), QStringLiteral("ko"),
        QStringLiteral("tr"), QStringLiteral("mr"), QStringLiteral("te"),
        QStringLiteral("ta"), QStringLiteral("fa"), QStringLiteral("it"),
        QStringLiteral("th"), QStringLiteral("sw"),
        // Tier 3 — 20–60 million speakers
        QStringLiteral("pl"), QStringLiteral("uk"), QStringLiteral("nl"),
        QStringLiteral("ro"), QStringLiteral("el"), QStringLiteral("hu"),
        // Commented out — fewer than 20 million native speakers or covered by a
        // closely-related code already in the list above:
        // QStringLiteral("su"),  // Sundanese  (~34 M, regional)
        // QStringLiteral("ku"),  // Kurdish    (~25 M, fragmented dialects)
        // QStringLiteral("hr"),  // Croatian   (~ 5 M)
        // QStringLiteral("cs"),  // Czech      (~10 M)
        // QStringLiteral("az"),  // Azerbaijani(~10 M)
        // QStringLiteral("sv"),  // Swedish    (~10 M)
        // QStringLiteral("fi"),  // Finnish    (~ 5 M)
        // QStringLiteral("no"),  // Norwegian  (~ 5 M)
        // QStringLiteral("da"),  // Danish     (~ 6 M)
        // QStringLiteral("he"),  // Hebrew     (~ 5 M)
    };
}
