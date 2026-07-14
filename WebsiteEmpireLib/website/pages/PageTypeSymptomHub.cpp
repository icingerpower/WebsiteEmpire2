#include "PageTypeSymptomHub.h"

#include "website/AbstractEngine.h"
#include "website/social/AbstractSocialMedia.h"

#include <QDir>
#include <QMap>

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

void PageTypeSymptomHub::bindGenerationContext(IPageRepository &repo,
                                               const QDir      &workingDir)
{
    AbstractPageType::bindGenerationContext(repo, workingDir);
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
// seoTemplateStrings / autoSeoTitle / autoSeoDescription
// =============================================================================

QMap<QString, QString> PageTypeSymptomHub::seoTemplateStrings() const
{
    return {
        { QStringLiteral("title"), QStringLiteral("What Causes %1? %2 Conditions To Know") },
        { QStringLiteral("desc"),  QStringLiteral("Find out which %1 conditions cause %2 and what biomarkers help identify the root cause.") },
    };
}

QString PageTypeSymptomHub::autoSeoTitle(const QString &langCode) const
{
    const QString &stored = m_metaBloc.seoTitle(langCode);
    if (!stored.isEmpty()) {
        return stored;
    }
    // m_conditionListBloc.lastRenderedDisplayName() is populated by addCode()
    // before buildHeadMetaTags() is called — uses the same fallback chain as
    // the h1 (aspire DB → TaxonomyDb → permalink title-case), so it works
    // even when results_db/PageAttributesHealthSymptom.db is absent.
    const QString symptomName = m_conditionListBloc.lastRenderedDisplayName();
    if (symptomName.isEmpty()) {
        return {};
    }
    const int n = m_conditionListBloc.countConditions();
    if (n <= 0) {
        return symptomName;
    }
    const QString tmpl = seoTemplate(QStringLiteral("title"), langCode);
    if (tmpl.isEmpty()) {
        return {};
    }
    return tmpl.arg(symptomName, QString::number(n));
}

QString PageTypeSymptomHub::autoSeoDescription(const QString &langCode) const
{
    const QString &stored = m_metaBloc.seoDescription(langCode);
    if (!stored.isEmpty()) {
        return stored;
    }
    const QString symptomName = m_conditionListBloc.lastRenderedDisplayName();
    if (symptomName.isEmpty()) {
        return {};
    }
    const int n = m_conditionListBloc.countConditions();
    if (n <= 0) {
        return {};
    }
    const QString tmpl = seoTemplate(QStringLiteral("desc"), langCode);
    if (tmpl.isEmpty()) {
        return {};
    }
    return tmpl.arg(QString::number(n), symptomName);
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

QString PageTypeSymptomHub::buildHeadMetaTags(const QString &baseUrl,
                                               const QString &langCode) const
{
    // Base emits: <title>, <meta name="description">, canonical, og:url.
    QString result = AbstractPageType::buildHeadMetaTags(baseUrl, langCode);

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
        const QString &jsonLdTitle = autoSeoTitle(langCode);
        if (!jsonLdTitle.isEmpty()) {
            result += QStringLiteral(",\"name\":\"");
            result += jsonLdTitle.toHtmlEscaped();
            result += QLatin1Char('"');
        }
        result += QStringLiteral("}</script>\n");
    }

    return result;
}

DECLARE_PAGE_TYPE(PageTypeSymptomHub)
