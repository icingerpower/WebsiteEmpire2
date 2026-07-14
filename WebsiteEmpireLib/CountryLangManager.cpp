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
        QStringLiteral("zh"), QStringLiteral("es"), QStringLiteral("hi"),
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
        // Tier 4 — other languages
        QStringLiteral("su"), QStringLiteral("ku"), QStringLiteral("hr"),
        QStringLiteral("cs"), QStringLiteral("az"), QStringLiteral("sv"),
        QStringLiteral("fi"), QStringLiteral("no"), QStringLiteral("da"),
        QStringLiteral("he"),
    };
}
