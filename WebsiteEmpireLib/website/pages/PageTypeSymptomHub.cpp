#include "PageTypeSymptomHub.h"

#include "website/AbstractEngine.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"   // for SymptomNav::slugify
#include "website/social/AbstractSocialMedia.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>

// =============================================================================
// Helpers — symptom name from permalink
// =============================================================================

static QString symptomNameFromPermalink(const QString &permalink, const QDir &workingDir)
{
    const QString prefix = QStringLiteral("/symptoms/");
    if (!permalink.startsWith(prefix)) {
        return {};
    }
    const QString slug   = permalink.mid(prefix.length());
    const QString dbPath = workingDir.filePath(
        QStringLiteral("results_db/PageAttributesHealthSymptom.db"));
    if (!QFile::exists(dbPath)) {
        return {};
    }

    QString name;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("symhub_sn_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("SELECT health_symptom_name FROM records"));
                while (q.next()) {
                    const QString n = q.value(0).toString().trimmed();
                    if (SymptomNav::slugify(n) == slug) {
                        name = n;
                        break;
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return name;
}

// =============================================================================
// Constructor
// =============================================================================

PageTypeSymptomHub::PageTypeSymptomHub(CategoryTable & /*categoryTable*/)
{
    m_blocs.append(&m_textBloc);          // 0 — AI intro paragraph
    m_blocs.append(&m_conditionListBloc); // 1 — condition list from aspire DB
    m_blocs.append(&m_socialTextBloc);    // 2 — social metadata
    m_blocs.append(&m_metaBloc);          // 3 — SEO title + description
}

// =============================================================================
// Accessors
// =============================================================================

QString PageTypeSymptomHub::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeSymptomHub::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeSymptomHub::getPageBlocs() const
{
    return m_blocs;
}

// =============================================================================
// bindGenerationContext
// =============================================================================

void PageTypeSymptomHub::bindGenerationContext(IPageRepository & /*repo*/,
                                               const QDir       &workingDir)
{
    m_workingDir = workingDir;
}

// =============================================================================
// addCode
// =============================================================================

void PageTypeSymptomHub::addCode(QStringView     origContent,
                                  AbstractEngine &engine,
                                  int             websiteIndex,
                                  QString        &html,
                                  QString        &css,
                                  QString        &js,
                                  QSet<QString>  &cssDoneIds,
                                  QSet<QString>  &jsDoneIds) const
{
    // m_permalink is set by setGenerationContext() before addCode() is called.
    m_conditionListBloc.setRenderContext(m_permalink, m_workingDir);
    AbstractPageType::addCode(origContent, engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

QString PageTypeSymptomHub::buildHeadMetaTags(const QString &baseUrl,
                                               const QString &langCode) const
{
    QString result;

    // ---- Compute the page title ----
    const QString storedTitle = m_metaBloc.seoTitle(langCode);
    QString title;
    if (!storedTitle.isEmpty()) {
        title = storedTitle;
    } else {
        const QString symptomName = symptomNameFromPermalink(m_permalink, m_workingDir);
        if (!symptomName.isEmpty()) {
            const int n = m_conditionListBloc.countConditions();
            if (n > 0) {
                title = symptomName + QStringLiteral(" — ")
                        + QString::number(n) + QLatin1Char(' ')
                        + (n == 1
                           ? QCoreApplication::translate("PageTypeSymptomHub", "possible condition")
                           : QCoreApplication::translate("PageTypeSymptomHub", "possible conditions"));
            } else {
                title = symptomName;
            }
        }
    }

    if (!title.isEmpty()) {
        result += QStringLiteral("<title>");
        result += title;
        result += QStringLiteral("</title>");
    }

    const QString desc = m_metaBloc.seoDescription(langCode);
    if (!desc.isEmpty()) {
        result += QStringLiteral("<meta name=\"description\" content=\"");
        result += desc;
        result += QStringLiteral("\">");
    }

    if (!m_permalink.isEmpty() && !baseUrl.isEmpty()) {
        result += QStringLiteral("<link rel=\"canonical\" href=\"");
        result += baseUrl;
        result += m_permalink;
        result += QStringLiteral("\">");

        result += QStringLiteral("<meta property=\"og:url\" content=\"");
        result += baseUrl;
        result += m_permalink;
        result += QStringLiteral("\">");
    }

    result += QStringLiteral("<meta property=\"og:type\" content=\"website\">");

    // ---- Social meta tags ----
    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();
        QString stitle, sdesc;
        if (id == QLatin1String("opengraph")) {
            stitle = m_socialTextBloc.facebookTitle();
            sdesc  = m_socialTextBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            stitle = m_socialTextBloc.twitterTitle();
            sdesc  = m_socialTextBloc.twitterDesc();
        }
        if (!stitle.isEmpty()) {
            result += platform->titleMetaTagHtml(stitle);
        }
        if (!sdesc.isEmpty()) {
            result += platform->descMetaTagHtml(sdesc);
        }
    }

    // ---- WebPage JSON-LD dateModified ----
    const QString &updated = m_updatedByLang.contains(langCode)
                             ? m_updatedByLang.value(langCode)
                             : m_updatedByLang.value(m_sourceLang);
    if (!updated.isEmpty()) {
        result += QStringLiteral("<script type=\"application/ld+json\">"
                                  "{\"@context\":\"https://schema.org\","
                                  "\"@type\":\"WebPage\","
                                  "\"dateModified\":\"");
        result += updated;
        result += QLatin1Char('"');
        if (!m_permalink.isEmpty() && !baseUrl.isEmpty()) {
            result += QStringLiteral(",\"url\":\"");
            result += baseUrl;
            result += m_permalink;
            result += QLatin1Char('"');
        }
        if (!title.isEmpty()) {
            result += QStringLiteral(",\"name\":\"");
            result += title;
            result += QLatin1Char('"');
        }
        result += QStringLiteral("}</script>\n");
    }

    return result;
}

DECLARE_PAGE_TYPE(PageTypeSymptomHub)
