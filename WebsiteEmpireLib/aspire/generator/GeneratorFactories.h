#ifndef GENERATORFACTORIES_H
#define GENERATORFACTORIES_H

#include "AbstractGenerator.h"

// Generator that builds a database of manufacturing factories for French cities.
//
// City data is loaded from a worldcities.csv file (SimpleMaps format).
// The path is taken from the "csv_path" Param (set via the UI parameter editor)
// when the constructor is called without an explicit csvPath argument.  Passing
// csvPath to the constructor overrides the param (used by tests and the CLI).
// Only rows whose "country" column equals "France" are used; they are sorted by
// population descending (cities with no population data go last).
//
// Two-step job pipeline:
//
//   Step 1 — one job per city, job ID "FR/<city_slug>" (e.g. "FR/paris"):
//     Claude returns:
//       • postalCode            — main postal code for the city
//       • estimatedFactoryCount — total estimated number of manufacturing factories
//     Then either:
//       • factories[]   — full list when estimatedFactoryCount ≤ FACTORY_COUNT_THRESHOLD
//       • factoryKinds[]— list of category labels when the count exceeds the threshold
//     When factoryKinds is non-empty, one step-2 job is spawned per kind via
//     addDiscoveredJob().
//
//   Step 2 — one job per factory kind, ID "FR/<city_slug>/cat/<kind_slug>"
//     (e.g. "FR/paris/cat/aerospace"):
//     Claude returns factories[] with all PageAttributesFactory fields for that kind.
//
// All results are saved as JSON files under <workingDir>/results/.
//
// DECLARE_GENERATOR(GeneratorFactories) registers the prototype (with QDir{}) at startup.
// Use createInstance() for real work; the prototype is only a type descriptor.
class GeneratorFactories : public AbstractGenerator
{
    Q_OBJECT

public:
    // Factories above this count → return kinds list instead of full factory list.
    static const int FACTORY_COUNT_THRESHOLD;

    // "task" field values that distinguish the two steps in every job payload.
    static const QString STEP1_TASK;
    static const QString STEP2_TASK;

    explicit GeneratorFactories(const QDir    &workingDir,
                                const QString &csvPath = QString(),
                                QObject       *parent  = nullptr);

    QString getId()   const override;
    QString getName() const override;
    AbstractGenerator *createInstance(const QDir &workingDir) const override;

    QList<Param>         getParams()                        const override;
    QString              checkParams(const QList<Param> &)  const override;
    QMap<QString, AbstractPageAttributes *> createResultPageAttributes() const override;

    // ---- Job-ID helpers (public for testability) --------------------------------

    // Produces a URL-safe ASCII slug: lower-case, spaces/underscores → '-',
    // non-ASCII and other special characters are dropped.
    static QString slugify(const QString &text);

    // Canonical step-1 job ID: "<ISO2>/<city_ascii_slug>"  e.g. "FR/paris".
    static QString cityJobId(const QString &iso2, const QString &cityAscii);

    // Step-2 job ID: "<city_job_id>/cat/<category_slug>"  e.g. "FR/paris/cat/aerospace".
    static QString categoryJobId(const QString &parentCityJobId, const QString &category);

    // True for "FR/paris" (exactly two '/'-separated segments).
    static bool isStep1JobId(const QString &jobId);

    // True for "FR/paris/cat/aerospace" (four segments, third equals "cat").
    static bool isStep2JobId(const QString &jobId);

    // "FR/paris/cat/aerospace" → "FR/paris"
    static QString cityKeyFromStep2Id(const QString &jobId);

    // "FR/paris/cat/aerospace" → "aerospace"
    static QString categoryFromStep2Id(const QString &jobId);

protected:
    QStringList buildInitialJobIds()                                          const override;
    QJsonObject buildJobPayload(const QString &jobId)                         const override;
    void        processReply(const QString &jobId, const QJsonObject &reply)        override;
    void        onParamChanged(const QString &id)                                   override;

private:
    struct CityData {
        QString jobId;      // "FR/paris"
        QString cityName;   // "Paris" (may include accented characters)
        QString cityAscii;  // "Paris"
        QString region;     // "Île-de-France"
        QString iso2;       // "FR"
        QString country;    // "France"
        qint64  population; // 0 when absent in CSV
        double  lat;
        double  lng;
    };

    const QList<CityData> &cities() const;
    static QList<CityData> loadCitiesFromCsv(const QString &csvPath);
    static QStringList     parseCsvLine(const QString &line);

    // Builds the factorySchema object from the live PageAttributesFactory registry entry.
    static QJsonObject buildFactorySchema();

    // Loads all existing factory category names from the category results DB.
    // Returns an empty list if the DB does not yet exist.
    QStringList loadExistingCategories() const;

    // Given a step-2 city key and the slugified category, returns the original
    // human-readable factoryKind name by loading the step-1 result JSON.
    // Falls back to slug if the JSON is missing or the slug is not found.
    QString resolveKindName(const QString &cityKey, const QString &slug) const;

    QJsonObject buildStep1Payload(const QString &jobId) const;
    QJsonObject buildStep2Payload(const QString &jobId) const;

    void             saveResult(const QString &jobId, const QJsonObject &result);
    const CityData  *findCity(const QString &cityKey) const;

    // Lazily populated on first access to cities().
    mutable QList<CityData> m_cities;
    mutable bool            m_citiesLoaded = false;
    QString                 m_csvPath;
};

#endif // GENERATORFACTORIES_H
