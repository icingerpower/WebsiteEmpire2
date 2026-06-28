#include "PageTypeSymptomIndex.h"

#include "website/AbstractEngine.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"   // for SymptomNav::slugify
#include "website/social/AbstractSocialMedia.h"
#include "website/taxonomy/TaxonomyDb.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlQuery>

// =============================================================================
// Constructor
// =============================================================================

PageTypeSymptomIndex::PageTypeSymptomIndex(CategoryTable & /*categoryTable*/)
{
    m_blocs.append(&m_textBloc);       // 0 — optional intro text
    m_blocs.append(&m_socialTextBloc); // 1 — social metadata
    m_blocs.append(&m_metaBloc);       // 2 — SEO title + description
}

// =============================================================================
// Accessors
// =============================================================================

QString PageTypeSymptomIndex::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeSymptomIndex::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeSymptomIndex::getPageBlocs() const
{
    return m_blocs;
}

// =============================================================================
// bindGenerationContext
// =============================================================================

void PageTypeSymptomIndex::bindGenerationContext(IPageRepository & /*repo*/,
                                                  const QDir       &workingDir)
{
    m_workingDir = workingDir;
}

// =============================================================================
// addInnerTopCode — renders the symptom grid
// =============================================================================

void PageTypeSymptomIndex::addInnerTopCode(AbstractEngine &engine,
                                            int             websiteIndex,
                                            QString        &html,
                                            QString        &css,
                                            QString        &js,
                                            QSet<QString>  &cssDoneIds,
                                            QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    const QString condDbPath = m_workingDir.filePath(
        QStringLiteral("results_db/PageAttributesHealthCondition.db"));

    // -------------------------------------------------------------------
    // Build condition-count map: symptomName → conditionCount (optional).
    // -------------------------------------------------------------------
    QHash<QString, int> condCountBySymptom;
    if (QFile::exists(condDbPath)) {
        static int s_seed1 = 0;
        const QString connName = QStringLiteral("symidx_cond_") + QString::number(++s_seed1);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(condDbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("SELECT health_condition_symptoms FROM records"));
                while (q.next()) {
                    const QStringList parts = q.value(0).toString()
                                              .split(QLatin1Char(','), Qt::SkipEmptyParts);
                    for (const QString &part : std::as_const(parts)) {
                        const QString &sym = part.trimmed();
                        if (!sym.isEmpty()) {
                            condCountBySymptom[sym]++;
                        }
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // -------------------------------------------------------------------
    // Build set of symptom slugs that have at least one article assigned
    // directly via page_data (covers AI-assigned symptoms not in the
    // conditions aspire DB).
    // -------------------------------------------------------------------
    QSet<QString> slugsWithDirectArticles;
    const QString pagesDbPath = m_workingDir.filePath(QStringLiteral("pages.db"));
    if (QFile::exists(pagesDbPath)) {
        static int s_seed2 = 0;
        const QString connName2 = QStringLiteral("symidx_art_") + QString::number(++s_seed2);
        {
            QSqlDatabase db2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName2);
            db2.setDatabaseName(pagesDbPath);
            db2.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db2.open()) {
                QSqlQuery q2(db2);
                q2.exec(QStringLiteral(
                    "SELECT pd.value FROM pages p"
                    " JOIN page_data pd ON pd.page_id = p.id"
                    " WHERE p.type_id = 'article'"
                    "   AND pd.key LIKE '%_symptoms'"
                    "   AND pd.value != ''"));
                while (q2.next()) {
                    const QStringList parts = q2.value(0).toString()
                                              .split(QLatin1Char(','), Qt::SkipEmptyParts);
                    for (const QString &part : std::as_const(parts)) {
                        const QString &trimmed = part.trimmed();
                        if (!trimmed.isEmpty()) {
                            slugsWithDirectArticles.insert(SymptomNav::slugify(trimmed));
                        }
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName2);
    }

    // -------------------------------------------------------------------
    // Load symptom names from the taxonomy with translations when available.
    // englishName drives permalink/slug; displayName is rendered in the HTML.
    // -------------------------------------------------------------------
    const QString lang = engine.getLangCode(websiteIndex);
    const QList<QPair<QString, QString>> rawSymptoms =
        TaxonomyDb(m_workingDir).loadTranslated(QStringLiteral("symptoms"), lang);

    struct Symptom { QString englishName; QString displayName; QString slug; };
    QList<Symptom> symptoms;
    symptoms.reserve(rawSymptoms.size());
    for (const auto &[englishName, displayName] : std::as_const(rawSymptoms)) {
        if (!englishName.isEmpty()) {
            symptoms.append({englishName, displayName, SymptomNav::slugify(englishName)});
        }
    }

    if (symptoms.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("symptom-index-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".symptom-index-grid{display:grid;"
            "grid-template-columns:repeat(auto-fill,minmax(200px,1fr));"
            "gap:.5em;margin:1.5em 0;padding:0;list-style:none}"
            ".symptom-index-grid li a{display:flex;justify-content:space-between;"
            "align-items:baseline;padding:.4em .6em;border-radius:4px;"
            "text-decoration:none;color:inherit;transition:background .15s}"
            ".symptom-index-grid li a:hover{background:#e8f0fe}"
            ".symptom-index-grid .sym-count{font-size:.8em;color:#6b7280;"
            "white-space:nowrap;margin-left:.5em}");
    }

    html += QStringLiteral("<ul class=\"symptom-index-grid\">");
    for (const auto &[englishName, displayName, slug] : std::as_const(symptoms)) {
        const QString permalink = QStringLiteral("/symptoms/") + slug;
        const QString resolved  = engine.resolvePermalink(permalink, websiteIndex);
        const int     count     = condCountBySymptom.value(englishName, 0);

        // Skip symptoms with no articles from either source.
        if (count == 0 && !slugsWithDirectArticles.contains(slug)) {
            continue;
        }

        html += QStringLiteral("<li>");
        if (!resolved.isEmpty() && engine.isPageAvailable(permalink, websiteIndex)) {
            html += QStringLiteral("<a href=\"");
            html += resolved.startsWith(QLatin1Char('/')) ? resolved.mid(1) : resolved;
            html += QStringLiteral("\">");
            html += displayName;
            if (count > 0) {
                html += QStringLiteral("<span class=\"sym-count\">");
                html += QString::number(count);
                html += QStringLiteral("</span>");
            }
            html += QStringLiteral("</a>");
        } else {
            html += displayName;
        }
        html += QStringLiteral("</li>");
    }
    html += QStringLiteral("</ul>");
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

QString PageTypeSymptomIndex::buildHeadMetaTags(const QString &baseUrl,
                                                 const QString &langCode) const
{
    QString result;

    // ---- Title ----
    const QString storedTitle = m_metaBloc.seoTitle(langCode);
    QString title;
    if (!storedTitle.isEmpty()) {
        title = storedTitle;
    } else {
        // Count symptoms from the taxonomy vocabulary.
        const int symCount = TaxonomyDb(m_workingDir).load(QStringLiteral("symptoms")).size();
        if (symCount > 0) {
            title = QCoreApplication::translate("PageTypeSymptomIndex",
                        "Browse conditions by symptom — %1 symptoms").arg(symCount);
        } else {
            title = QCoreApplication::translate("PageTypeSymptomIndex",
                        "Browse conditions by symptom");
        }
    }

    result += QStringLiteral("<title>");
    result += title;
    result += QStringLiteral("</title>");

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

    // ---- WebPage JSON-LD ----
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

DECLARE_PAGE_TYPE(PageTypeSymptomIndex)
