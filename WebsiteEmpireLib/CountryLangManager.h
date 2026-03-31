#ifndef COUNTRYLANGMANAGER_H
#define COUNTRYLANGMANAGER_H

#include <QObject>
#include <QStringList>

// Singleton that provides country/language metadata shared across the application.
//
// Access via CountryLangManager::instance() — do NOT construct directly.
class CountryLangManager : public QObject
{
    Q_OBJECT

public:
    static CountryLangManager *instance();

    // BCP-47 codes of the 30 most-spoken languages, excluding English.
    QStringList defaultLangCodes() const;

private:
    explicit CountryLangManager(QObject *parent = nullptr);
};

#endif // COUNTRYLANGMANAGER_H
