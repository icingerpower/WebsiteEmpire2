#ifndef PAGEGENERATOR_H
#define PAGEGENERATOR_H

#include <QByteArray>
#include <QDir>
#include <QString>

class AbstractEngine;
class CategoryTable;
class IPageRepository;

/**
 * Generates static HTML pages from the page repository and writes them into
 * content.db (the database read by the Drogon serving stack).
 *
 * For each page in IPageRepository:
 *  1. Reconstruct the AbstractPageType via the registry (createForTypeId).
 *  2. Load its bloc data from IPageRepository::loadData().
 *  3. Generate the full HTML string via AbstractPageType::addCode().
 *  4. Gzip-compress the HTML with standard gzip format (zlib deflate, wbits=31).
 *  5. Compute an ETag as the MD5 hex of the compressed bytes.
 *  6. UPSERT the row into content.db's pages and page_variants tables.
 *     The variant label is always "control"; is_active = 1.
 *  7. Insert redirect rows for every permalink in the page's permalink_history.
 *
 * content.db is opened via QSqlDatabase (QSQLITE driver).  If it does not
 * exist it is created and the schema is bootstrapped.
 *
 * The QDir workingDir is used only to resolve the path to content.db
 * (workingDir/content.db).  The stats.db and categories.csv files live in the
 * same directory but are not touched by PageGenerator.
 */
class PageGenerator
{
public:
    static constexpr const char *FILENAME = "content.db";

    explicit PageGenerator(IPageRepository &pageRepo,
                           CategoryTable   &categoryTable);

    /**
     * Generates all pages for the given domain and writes them to content.db
     * in workingDir.  Returns the number of pages successfully written.
     *
     * engine and websiteIndex are forwarded to AbstractPageType::addCode() so
     * that page templates can embed domain-specific data (e.g. lang attribute).
     */
    int generateAll(const QDir     &workingDir,
                    const QString  &domain,
                    AbstractEngine &engine,
                    int             websiteIndex);

    // Exposed for testing.
    static QByteArray gzipCompress(const QByteArray &input);
    static QString    computeEtag(const QByteArray &data);

private:
    IPageRepository &m_pageRepo;
    CategoryTable   &m_categoryTable;

    static void ensureSchema(const QString &connectionName);
};

#endif // PAGEGENERATOR_H
