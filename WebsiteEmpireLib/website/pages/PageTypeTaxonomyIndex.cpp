#include "PageTypeTaxonomyIndex.h"

#include "website/AbstractEngine.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeCategory.h"
#include "website/pages/PageTypeSymptomHub.h"
#include "website/social/AbstractSocialMedia.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QSet>

#include <algorithm>

// =============================================================================
// aggregatedTypeIds
// =============================================================================

QStringList PageTypeTaxonomyIndex::aggregatedTypeIds()
{
    return {
        QLatin1String(PageTypeCategory::TYPE_ID),
        QLatin1String(PageTypeSymptomHub::TYPE_ID),
    };
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageTypeTaxonomyIndex::PageTypeTaxonomyIndex(CategoryTable & /*categoryTable*/)
{
    m_blocs.append(&m_textBloc);       // 0 — optional intro text
    m_blocs.append(&m_socialTextBloc); // 1 — social metadata
    m_blocs.append(&m_metaBloc);       // 2 — SEO title + description
}

PageTypeTaxonomyIndex::~PageTypeTaxonomyIndex() = default;

// =============================================================================
// Accessors
// =============================================================================

QString PageTypeTaxonomyIndex::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeTaxonomyIndex::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeTaxonomyIndex::getPageBlocs() const
{
    return m_blocs;
}

// =============================================================================
// bindGenerationContext
// =============================================================================

void PageTypeTaxonomyIndex::bindGenerationContext(IPageRepository &repo,
                                                   const QDir       & /*workingDir*/)
{
    m_repo = &repo;
}

// =============================================================================
// _activeLetter
// =============================================================================

QChar PageTypeTaxonomyIndex::_activeLetter() const
{
    const QString seg = m_permalink.section(QLatin1Char('/'), -1, -1);
    if (seg.length() == 1 && seg.at(0).isLetter()) {
        return seg.at(0).toLower();
    }
    return QChar(); // null → default (no explicit letter suffix)
}

// =============================================================================
// _slugToDisplayName
// =============================================================================

QString PageTypeTaxonomyIndex::_slugToDisplayName(const QString &slug)
{
    QString name = slug;
    if (name.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        name.chop(5);
    }
    name.replace(QLatin1Char('-'), QLatin1Char(' '));

    // Title-case: capitalise the first character of each word
    bool capitalizeNext = true;
    for (int i = 0; i < name.size(); ++i) {
        if (name.at(i) == QLatin1Char(' ')) {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            name[i] = name.at(i).toUpper();
            capitalizeNext = false;
        }
    }
    return name;
}

// =============================================================================
// addInnerTopCode — grid + optional A–Z nav
// =============================================================================

void PageTypeTaxonomyIndex::addInnerTopCode(AbstractEngine &engine,
                                             int             websiteIndex,
                                             QString        &html,
                                             QString        &css,
                                             QString        &js,
                                             QSet<QString>  &cssDoneIds,
                                             QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (!m_repo) {
        return;
    }

    // -----------------------------------------------------------------------
    // 1. Collect all generated source pages and build display entries
    // -----------------------------------------------------------------------
    struct Entry {
        QString displayName;
        QString permalink;
        QChar   letter;
    };

    QList<Entry> entries;
    for (const QString &typeId : aggregatedTypeIds()) {
        const QList<PageRecord> pages = m_repo->findGeneratedByTypeId(typeId);
        entries.reserve(entries.size() + pages.size());
        for (const PageRecord &p : std::as_const(pages)) {
            const QString slug = p.permalink.section(QLatin1Char('/'), -1, -1);
            if (slug.isEmpty()) {
                continue;
            }
            const QString name = _slugToDisplayName(slug);
            if (name.isEmpty()) {
                continue;
            }
            const QChar letter = name.at(0).toLower();
            if (!letter.isLetter()) {
                continue;
            }
            entries.append({name, p.permalink, letter});
        }
    }

    if (entries.isEmpty()) {
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return QString::localeAwareCompare(a.displayName, b.displayName) < 0;
    });

    // -----------------------------------------------------------------------
    // 2. Determine paginated mode and effective letter to display
    // -----------------------------------------------------------------------
    const bool  isPaginated  = (m_repo->countByTypeId(QStringLiteral("taxonomy_index")) > 1);
    const QChar activeLetter = _activeLetter();

    // When paginated:
    //   default page (null activeLetter) → show letter 'a'
    //   letter page                      → show that letter
    // When not paginated:
    //   showLetter is null → show everything
    const QChar showLetter = isPaginated
                             ? (activeLetter.isNull() ? QChar(QLatin1Char('a')) : activeLetter)
                             : QChar();

    // -----------------------------------------------------------------------
    // 3. Filter entries for this page
    // -----------------------------------------------------------------------
    QList<const Entry *> filtered;
    for (const Entry &e : std::as_const(entries)) {
        if (showLetter.isNull() || e.letter == showLetter) {
            filtered.append(&e);
        }
    }

    // -----------------------------------------------------------------------
    // 4. Inject CSS (once per page)
    // -----------------------------------------------------------------------
    static const QString CSS_ID = QStringLiteral("taxidx-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".taxidx-az-nav{margin:1em 0}"
            ".taxidx-az-nav ul{display:flex;flex-wrap:wrap;gap:.3em;"
            "padding:0;margin:0;list-style:none}"
            ".taxidx-az-nav li a{display:inline-block;padding:.25em .6em;"
            "border-radius:4px;text-decoration:none;font-weight:600;color:inherit;"
            "transition:background .15s}"
            ".taxidx-az-nav li a:hover,"
            ".taxidx-az-nav li a[aria-current=page]{background:#e8f0fe}"
            ".taxidx-grid{display:grid;"
            "grid-template-columns:repeat(auto-fill,minmax(200px,1fr));"
            "gap:.5em;margin:1.5em 0;padding:0;list-style:none}"
            ".taxidx-grid li a{display:block;padding:.4em .6em;border-radius:4px;"
            "text-decoration:none;color:inherit;transition:background .15s}"
            ".taxidx-grid li a:hover{background:#e8f0fe}");
    }

    // -----------------------------------------------------------------------
    // 5. A–Z navigation bar (paginated mode only)
    // -----------------------------------------------------------------------
    if (isPaginated) {
        // Build letter → permalink map from all taxonomy_index pages
        QHash<QChar, QString> letterPermalinks;
        // The default /categories page serves letter 'a'
        letterPermalinks.insert(QChar(QLatin1Char('a')), QLatin1String(BASE_PERMALINK));

        QList<PageRecord> indexPages = m_repo->findGeneratedByTypeId(
            QStringLiteral("taxonomy_index"));
        indexPages += m_repo->findPendingByTypeId(QStringLiteral("taxonomy_index"));

        for (const PageRecord &ip : std::as_const(indexPages)) {
            const QString seg = ip.permalink.section(QLatin1Char('/'), -1, -1);
            if (seg.length() == 1 && seg.at(0).isLetter()) {
                letterPermalinks.insert(seg.at(0).toLower(), ip.permalink);
            }
        }

        // Which letters actually have content
        QSet<QChar> lettersWithContent;
        for (const Entry &e : std::as_const(entries)) {
            lettersWithContent.insert(e.letter);
        }

        html += QStringLiteral(
            "<nav class=\"taxidx-az-nav\" aria-label=\"Browse by letter\"><ul>");
        for (int c = 'a'; c <= 'z'; ++c) {
            const QChar letter(c);
            if (!lettersWithContent.contains(letter)) {
                continue;
            }
            const QString &lperm    = letterPermalinks.value(letter);
            const bool isCurrent    = (showLetter == letter);
            const bool hasPage      = !lperm.isEmpty()
                                      && engine.isPageAvailable(lperm, websiteIndex);

            html += QStringLiteral("<li>");
            if (hasPage) {
                const QString resolved = engine.resolvePermalink(lperm, websiteIndex);
                html += QStringLiteral("<a href=\"");
                html += resolved.startsWith(QLatin1Char('/')) ? resolved.mid(1) : resolved;
                html += QLatin1Char('"');
                if (isCurrent) {
                    html += QStringLiteral(" aria-current=\"page\"");
                }
                html += QLatin1Char('>');
            }
            html += letter.toUpper();
            if (hasPage) {
                html += QStringLiteral("</a>");
            }
            html += QStringLiteral("</li>");
        }
        html += QStringLiteral("</ul></nav>");
    }

    // -----------------------------------------------------------------------
    // 6. Entry grid
    // -----------------------------------------------------------------------
    if (filtered.isEmpty()) {
        return;
    }

    html += QStringLiteral("<ul class=\"taxidx-grid\">");
    for (const Entry *e : std::as_const(filtered)) {
        const bool available = engine.isPageAvailable(e->permalink, websiteIndex);
        html += QStringLiteral("<li>");
        if (available) {
            const QString resolved = engine.resolvePermalink(e->permalink, websiteIndex);
            html += QStringLiteral("<a href=\"");
            html += resolved.startsWith(QLatin1Char('/')) ? resolved.mid(1) : resolved;
            html += QStringLiteral("\">");
        }
        html += e->displayName;
        if (available) {
            html += QStringLiteral("</a>");
        }
        html += QStringLiteral("</li>");
    }
    html += QStringLiteral("</ul>");
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

QString PageTypeTaxonomyIndex::buildHeadMetaTags(const QString &baseUrl,
                                                  const QString &langCode) const
{
    QString result;

    const QString storedTitle = m_metaBloc.seoTitle(langCode);
    const QString title = storedTitle.isEmpty()
        ? QCoreApplication::translate("PageTypeTaxonomyIndex",
                                      "Browse all categories")
        : storedTitle;

    result += QStringLiteral("<title>");
    result += title;
    result += QStringLiteral("</title>");

    const QString &desc = m_metaBloc.seoDescription(langCode);
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

    const QString &updated = m_updatedByLang.contains(langCode)
                             ? m_updatedByLang.value(langCode)
                             : m_updatedByLang.value(m_sourceLang);
    if (!updated.isEmpty()) {
        result += QStringLiteral(
            "<script type=\"application/ld+json\">"
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

DECLARE_PAGE_TYPE(PageTypeTaxonomyIndex)
