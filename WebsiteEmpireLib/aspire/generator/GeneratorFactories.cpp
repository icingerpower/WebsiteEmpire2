#include "GeneratorFactories.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>

#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/attributes/PageAttributesFactory.h"
#include "aspire/attributes/PageAttributesFactoryCategory.h"
#include "aspire/downloader/DownloadedPagesTable.h"

DECLARE_GENERATOR(GeneratorFactories)

const int     GeneratorFactories::FACTORY_COUNT_THRESHOLD = 10;
const QString GeneratorFactories::STEP1_TASK = QStringLiteral("step1_city_overview");
const QString GeneratorFactories::STEP2_TASK = QStringLiteral("step2_factory_list");

GeneratorFactories::GeneratorFactories(const QDir &workingDir,
                                       const QString &csvPath,
                                       QObject *parent)
    : AbstractGenerator(workingDir, parent)
    , m_csvPath(csvPath) // Empty → read from "csv_path" param at first cities() call.
{
}

QString GeneratorFactories::getId() const
{
    return QStringLiteral("french-factories");
}

QString GeneratorFactories::getName() const
{
    return tr("French Factories");
}

AbstractGenerator *GeneratorFactories::createInstance(const QDir &workingDir) const
{
    return new GeneratorFactories(workingDir);
}

// ---- Job-ID helpers ------------------------------------------------------------

QString GeneratorFactories::slugify(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() < 128
            && (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('.'))) {
            result += c.toLower();
        } else if (c == QLatin1Char(' ') || c == QLatin1Char('_')) {
            result += QLatin1Char('-');
        }
        // Non-ASCII and other special chars are dropped for URL safety.
    }
    return result;
}

QString GeneratorFactories::cityJobId(const QString &iso2, const QString &cityAscii)
{
    return iso2.toUpper() + QLatin1Char('/') + slugify(cityAscii);
}

QString GeneratorFactories::categoryJobId(const QString &parentCityJobId,
                                          const QString &category)
{
    return parentCityJobId + QStringLiteral("/cat/") + slugify(category);
}

bool GeneratorFactories::isStep1JobId(const QString &jobId)
{
    return jobId.split(QLatin1Char('/')).size() == 2;
}

bool GeneratorFactories::isStep2JobId(const QString &jobId)
{
    const QStringList parts = jobId.split(QLatin1Char('/'));
    return parts.size() == 4 && parts.at(2) == QLatin1String("cat");
}

QString GeneratorFactories::cityKeyFromStep2Id(const QString &jobId)
{
    const QStringList parts = jobId.split(QLatin1Char('/'));
    if (parts.size() < 2) {
        return {};
    }
    return parts.at(0) + QLatin1Char('/') + parts.at(1);
}

QString GeneratorFactories::categoryFromStep2Id(const QString &jobId)
{
    const QStringList parts = jobId.split(QLatin1Char('/'));
    if (parts.size() < 4) {
        return {};
    }
    return parts.at(3);
}

// ---- CSV loading ---------------------------------------------------------------

const QList<GeneratorFactories::CityData> &GeneratorFactories::cities() const
{
    if (!m_citiesLoaded) {
        // m_csvPath is set only when the constructor received an explicit path (tests /
        // CLI).  For GUI instances created by createInstance(), it is empty and we read
        // the "csv_path" param that the user configured in the parameter editor.
        const QString path = m_csvPath.isEmpty()
                                 ? paramValue(QStringLiteral("csv_path")).toString()
                                 : m_csvPath;
        m_cities = loadCitiesFromCsv(path);
        m_citiesLoaded = true;
    }
    return m_cities;
}

QStringList GeneratorFactories::parseCsvLine(const QString &line)
{
    QStringList fields;
    bool inQuotes = false;
    QString current;
    for (const QChar c : line) {
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
        } else if (c == QLatin1Char(',') && !inQuotes) {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

QList<GeneratorFactories::CityData> GeneratorFactories::loadCitiesFromCsv(
    const QString &csvPath)
{
    QList<CityData> result;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "GeneratorFactories: cannot open CSV:" << csvPath;
        return result;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    bool firstLine = true;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (firstLine) {
            firstLine = false;
            continue; // skip header
        }

        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 10) {
            continue;
        }

        // Columns: city, city_ascii, lat, lng, country, iso2, iso3, admin_name,
        //          capital, population[, id]
        if (fields.at(4) != QLatin1String("France")) {
            continue;
        }

        CityData city;
        city.cityName   = fields.at(0);
        city.cityAscii  = fields.at(1);
        city.lat        = fields.at(2).toDouble();
        city.lng        = fields.at(3).toDouble();
        city.country    = fields.at(4);
        city.iso2       = fields.at(5);
        city.region     = fields.at(7);
        city.population = fields.at(9).toLongLong(); // 0 when field is empty
        city.jobId      = cityJobId(city.iso2, city.cityAscii);
        result << city;
    }

    // Sort by population descending; cities with population == 0 sink to the end.
    std::stable_sort(result.begin(), result.end(), [](const CityData &a, const CityData &b) {
        if (a.population == 0 && b.population == 0) {
            return false;
        }
        if (a.population == 0) {
            return false;
        }
        if (b.population == 0) {
            return true;
        }
        return a.population > b.population;
    });

    return result;
}

const GeneratorFactories::CityData *GeneratorFactories::findCity(
    const QString &cityKey) const
{
    for (const auto &city : cities()) {
        if (city.jobId == cityKey) {
            return &city;
        }
    }
    return nullptr;
}

// ---- Schema builder ------------------------------------------------------------

QJsonObject GeneratorFactories::buildFactorySchema()
{
    const AbstractPageAttributes *attrs =
        AbstractPageAttributes::ALL_PAGE_ATTRIBUTES().value(
            QStringLiteral("PageAttributesFactory"));
    if (!attrs) {
        qDebug() << "GeneratorFactories: PageAttributesFactory not found in registry";
        return {};
    }

    const auto attrListPtr = attrs->getAttributes(); // keep refcount alive
    const auto &attrList = *attrListPtr;
    QJsonObject schema;
    for (const auto &attr : attrList) {
        if (attr.isImage) {
            continue; // skip image-only attributes
        }
        const QString req = attr.optional
                                ? QStringLiteral("optional")
                                : QStringLiteral("required");
        QString desc = req + QStringLiteral(" \u2014 ") + attr.description;
        if (!attr.valueExemple.isEmpty()) {
            desc += QStringLiteral(" (e.g. ") + attr.valueExemple + QLatin1Char(')');
        }
        schema.insert(attr.id, desc);
    }
    return schema;
}

// ---- Helpers ---------------------------------------------------------------

QStringList GeneratorFactories::loadExistingCategories() const
{
    // Prefer reading from the already-open table model to avoid opening a second
    // SQLite connection, which causes "database is locked" under concurrent access.
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesFactoryCategory"));
    if (table) {
        const int col = table->fieldIndex(
            PageAttributesFactoryCategory::ID_FACTORY_CATEGORY);
        QStringList cats;
        const int rows = table->rowCount();
        for (int row = 0; row < rows; ++row) {
            const QString cat = table->data(table->index(row, col)).toString();
            if (!cat.isEmpty()) {
                cats << cat;
            }
        }
        cats.sort(Qt::CaseInsensitive);
        qDebug() << "GeneratorFactories: loaded" << cats.size()
                 << "existing category/ies from model";
        return cats;
    }

    // Fall back to a temporary connection when the table has not been opened yet
    // (e.g., previewing jobs before openResultsTable() is called).
    const QString dbPath = workingDir().filePath(
        QStringLiteral("results_db/PageAttributesFactoryCategory.db"));
    if (!QFile::exists(dbPath)) {
        return {};
    }
    const QString connName = getId() + QLatin1Char('_')
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList categories;
    {
        // Inner scope: db and q must be destroyed before removeDatabase() is called,
        // otherwise Qt warns "connection still in use" and SQLite holds a read lock
        // that blocks any concurrent write on the same file.
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral(
                    "SELECT factory_category_name FROM records "
                    "ORDER BY factory_category_name COLLATE NOCASE"))) {
                while (q.next()) {
                    categories << q.value(0).toString();
                }
            }
        }
    } // db and q destroyed here — connection fully released
    QSqlDatabase::removeDatabase(connName);
    qDebug() << "GeneratorFactories: loaded" << categories.size()
             << "existing category/ies from DB";
    return categories;
}

QString GeneratorFactories::resolveKindName(const QString &cityKey,
                                             const QString &slug) const
{
    const QString safeId = QString(cityKey).replace(QLatin1Char('/'), QLatin1Char('_'));
    QFile file(workingDir().filePath(
        QStringLiteral("results/") + safeId + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return slug;
    }
    const QJsonObject step1 = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : step1.value(QStringLiteral("factoryKinds")).toArray()) {
        const QString kind = v.toString().trimmed();
        if (slugify(kind) == slug) {
            return kind;
        }
    }
    return slug;
}

// ---- AbstractGenerator implementation -----------------------------------------

QStringList GeneratorFactories::buildInitialJobIds() const
{
    QStringList ids;
    ids.reserve(cities().size());
    for (const auto &city : cities()) {
        ids << city.jobId;
    }
    return ids;
}

QJsonObject GeneratorFactories::buildJobPayload(const QString &jobId) const
{
    if (isStep1JobId(jobId)) {
        return buildStep1Payload(jobId);
    }
    return buildStep2Payload(jobId);
}

QJsonObject GeneratorFactories::buildStep1Payload(const QString &jobId) const
{
    const CityData *city = findCity(jobId);
    if (!city) {
        return {};
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]      = STEP1_TASK;
    payload[QStringLiteral("city")]      = city->cityName;
    payload[QStringLiteral("country")]   = city->country;
    payload[QStringLiteral("region")]    = city->region;
    payload[QStringLiteral("iso2")]      = city->iso2;
    payload[QStringLiteral("latitude")]  = city->lat;
    payload[QStringLiteral("longitude")] = city->lng;
    if (city->population > 0) {
        payload[QStringLiteral("population")] = city->population;
    }
    const QStringList existingCats = loadExistingCategories();
    if (!existingCats.isEmpty()) {
        QJsonArray arr;
        for (const auto &c : std::as_const(existingCats)) {
            arr << c;
        }
        payload[QStringLiteral("availableCategories")] = arr;
    }

    payload[QStringLiteral("instructions")] = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' below — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "For this city, fill in:\n"
        "1. 'postalCode': the most common postal code.\n"
        "2. 'estimatedFactoryCount': total estimated number of manufacturing factories.\n"
        "   - If the count is %1 or fewer: populate 'factories' with one entry per factory "
        "using all required fields from the factorySchema.  Set each factory's 'category' "
        "field to the best matching name from 'availableCategories'; only use a new name "
        "if none of the existing ones accurately describes this factory type.\n"
        "   - If the count exceeds %1: leave 'factories' as an empty array and populate "
        "'factoryKinds' with the list of distinct manufacturing category names.  Reuse "
        "names from 'availableCategories' where they apply; add a new name only for a "
        "category that is genuinely absent from the list.")
        .arg(FACTORY_COUNT_THRESHOLD);
    payload[QStringLiteral("factorySchema")] = buildFactorySchema();

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]                 = jobId;
    replyFormat[QStringLiteral("postalCode")]            = QString{};
    replyFormat[QStringLiteral("estimatedFactoryCount")] = 0;
    replyFormat[QStringLiteral("factories")]             = QJsonArray{};
    replyFormat[QStringLiteral("factoryKinds")]          = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

QJsonObject GeneratorFactories::buildStep2Payload(const QString &jobId) const
{
    const QString cityKey  = cityKeyFromStep2Id(jobId);
    const QString slug     = categoryFromStep2Id(jobId);
    // Resolve the URL-safe slug back to the original human-readable factoryKind name.
    const QString category = resolveKindName(cityKey, slug);
    if (category != slug) {
        qDebug() << "GeneratorFactories: resolved category" << slug << "->" << category;
    }
    const CityData *city   = findCity(cityKey);
    if (!city || category.isEmpty()) {
        return {};
    }

    const QStringList existingCats = loadExistingCategories();
    QJsonArray availableCategoriesArr;
    for (const QString &cat : existingCats) {
        availableCategoriesArr.append(cat);
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]                = STEP2_TASK;
    payload[QStringLiteral("city")]                = city->cityName;
    payload[QStringLiteral("country")]             = city->country;
    payload[QStringLiteral("region")]              = city->region;
    payload[QStringLiteral("iso2")]                = city->iso2;
    payload[QStringLiteral("categoryHint")]        = category;
    payload[QStringLiteral("availableCategories")] = availableCategoriesArr;
    payload[QStringLiteral("instructions")] = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' below — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List all manufacturing factories related to '%1' in %2, %3. "
        "Populate 'factories' with one entry per factory using all required fields from the factorySchema. "
        "IMPORTANT: set the 'category' field of every factory to the single best-matching name "
        "from 'availableCategories' — you MUST use one of those exact strings, no variations. "
        "The 'categoryHint' field is a guide; pick whichever available category fits best.")
        .arg(category, city->cityName, city->country);
    payload[QStringLiteral("factorySchema")] = buildFactorySchema();

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]     = jobId;
    replyFormat[QStringLiteral("factories")] = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

void GeneratorFactories::processReply(const QString &jobId, const QJsonObject &reply)
{
    saveResult(jobId, reply);

    // Store each individual factory entry in the factories results table.
    // Collect unique category names while iterating to populate the category table.
    const QJsonArray factories = reply.value(QStringLiteral("factories")).toArray();
    const QJsonArray kinds     = reply.value(QStringLiteral("factoryKinds")).toArray();
    qDebug() << "GeneratorFactories: processing" << jobId
             << "-" << factories.size() << "factory/ies,"
             << kinds.size() << "kind(s)";

    // Pre-populate with categories already in the DB so we never insert duplicates.
    const QStringList existingCats = loadExistingCategories();
    QSet<QString> seenCategories(existingCats.begin(), existingCats.end());

    for (const QJsonValue &val : std::as_const(factories)) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject factoryObj = val.toObject();
        QHash<QString, QString> attrs;
        for (auto it = factoryObj.constBegin(); it != factoryObj.constEnd(); ++it) {
            attrs.insert(it.key(), it.value().toString());
        }
        recordResultPage(QStringLiteral("PageAttributesFactory"), attrs);

        const QString category = factoryObj.value(QStringLiteral("category")).toString().trimmed();
        if (!category.isEmpty() && !seenCategories.contains(category)) {
            seenCategories.insert(category);
            QHash<QString, QString> catAttrs;
            catAttrs.insert(PageAttributesFactoryCategory::ID_FACTORY_CATEGORY, category);
            recordResultPage(QStringLiteral("PageAttributesFactoryCategory"), catAttrs);
        }
    }

    if (isStep1JobId(jobId)) {
        for (const QJsonValue &val : std::as_const(kinds)) {
            const QString kind = val.toString().trimmed();
            if (!kind.isEmpty()) {
                if (!seenCategories.contains(kind)) {
                    seenCategories.insert(kind);
                    QHash<QString, QString> catAttrs;
                    catAttrs.insert(PageAttributesFactoryCategory::ID_FACTORY_CATEGORY, kind);
                    recordResultPage(QStringLiteral("PageAttributesFactoryCategory"), catAttrs);
                }
                addDiscoveredJob(categoryJobId(jobId, kind));
            }
        }
    }

    // Use COUNT(*) rather than rowCount() — QSqlTableModel loads rows lazily
    // in batches of 256, so rowCount() would be capped at 256 once the table
    // exceeds that size.
    auto dbCount = [](const DownloadedPagesTable *table) -> int {
        if (!table) {
            return 0;
        }
        QSqlQuery q(table->database());
        return (q.exec(QStringLiteral("SELECT COUNT(*) FROM records")) && q.next())
                   ? q.value(0).toInt()
                   : 0;
    };
    qDebug() << "GeneratorFactories: totals —"
             << dbCount(resultsTable(QStringLiteral("PageAttributesFactory"))) << "factories,"
             << dbCount(resultsTable(QStringLiteral("PageAttributesFactoryCategory"))) << "categories";
}

QMap<QString, AbstractPageAttributes *> GeneratorFactories::createResultPageAttributes() const
{
    return {
        {tr("Factories"),          new PageAttributesFactory()},
        {tr("Factory Categories"), new PageAttributesFactoryCategory()},
    };
}

// ---- Parameters ------------------------------------------------------------

QList<AbstractGenerator::Param> GeneratorFactories::getParams() const
{
    Param p;
    p.id           = QStringLiteral("csv_path");
    p.name         = tr("World Cities CSV");
    p.tooltip      = tr("Path to the worldcities.csv file (SimpleMaps format). "
                        "Only rows whose 'country' column equals 'France' are used.");
    p.defaultValue = QString();
    p.isFile       = true;
    return {p};
}

QString GeneratorFactories::checkParams(const QList<Param> &params) const
{
    for (const Param &p : params) {
        if (p.id == QStringLiteral("csv_path")) {
            const QString path = p.defaultValue.toString();
            if (path.isEmpty()) {
                return tr("World Cities CSV path is required.");
            }
            if (!QFile::exists(path)) {
                return tr("World Cities CSV file not found: %1").arg(path);
            }
            return {};
        }
    }
    return tr("Missing 'csv_path' parameter.");
}

void GeneratorFactories::onParamChanged(const QString &id)
{
    if (id == QStringLiteral("csv_path")) {
        // Invalidate the cities cache and reset the job state so the new CSV is
        // picked up on the next getNextJob() / pendingCount() call.
        m_citiesLoaded = false;
        m_cities.clear();
        resetState();
    }
}

// ---- Results ---------------------------------------------------------------

void GeneratorFactories::saveResult(const QString &jobId, const QJsonObject &result)
{
    const QString safeId = QString(jobId).replace(QLatin1Char('/'), QLatin1Char('_'));
    QDir resultsDir(workingDir().filePath(QStringLiteral("results")));
    resultsDir.mkpath(QStringLiteral("."));

    const QString filePath = resultsDir.filePath(safeId + QStringLiteral(".json"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "GeneratorFactories: cannot write result:" << filePath;
        return;
    }
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
}
