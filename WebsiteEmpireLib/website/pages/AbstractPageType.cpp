#include "AbstractPageType.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QHash>
#include <QString>
#include <QUrl>

// =============================================================================
// Registry internals
// =============================================================================

namespace {

struct PageTypeEntry {
    QString                        displayName;
    AbstractPageType::Factory      factory;
};

QHash<QString, PageTypeEntry> &registry()
{
    static QHash<QString, PageTypeEntry> s_registry;
    return s_registry;
}

// Insertion-order list of type ids (QHash does not preserve order).
QList<QString> &registryOrder()
{
    static QList<QString> s_order;
    return s_order;
}

} // namespace

// =============================================================================
// Recorder
// =============================================================================

AbstractPageType::Recorder::Recorder(const QString &typeId,
                                      const QString &displayName,
                                      Factory        factory)
{
    Q_ASSERT_X(!registry().contains(typeId),
               "AbstractPageType::Recorder",
               qPrintable(QStringLiteral("Duplicate page type id: ") + typeId));
    registry().insert(typeId, {displayName, std::move(factory)});
    registryOrder().append(typeId);
}

// =============================================================================
// Static factory / query
// =============================================================================

std::unique_ptr<AbstractPageType> AbstractPageType::createForTypeId(const QString &typeId,
                                                                      CategoryTable &table)
{
    const auto it = registry().find(typeId);
    if (it == registry().end()) {
        return nullptr;
    }
    return it->factory(table);
}

QList<QString> AbstractPageType::allTypeIds()
{
    return registryOrder();
}

// =============================================================================
// setAuthorLang
// =============================================================================

void AbstractPageType::setAuthorLang(const QString &lang)
{
    m_authorLang = lang;
}

// =============================================================================
// collectAiKeyClues
// =============================================================================

QHash<QString, QString> AbstractPageType::collectAiKeyClues() const
{
    QHash<QString, QString> result;
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        const auto clues = blocs.at(i)->getAiKeyClues();
        for (auto it = clues.cbegin(); it != clues.cend(); ++it) {
            result.insert(prefix + it.key(), it.value());
        }
    }
    return result;
}

// =============================================================================
// aiUpdateTargets
// =============================================================================

QList<AbstractPageType::AiUpdateTarget> AbstractPageType::aiUpdateTargets() const
{
    QList<AiUpdateTarget> result;
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const auto spec = blocs.at(i)->getAiUpdateSpec();
        if (!spec) {
            continue;
        }
        const QString prefix = QString::number(i) + QStringLiteral("_");
        const auto clues = blocs.at(i)->getAiKeyClues();
        AiUpdateTarget t;
        t.displayName  = spec->displayName;
        t.prefixedKey  = prefix + spec->dataKey;
        t.formatPrompt = spec->formatPrompt;
        t.aiKeyClue    = clues.value(spec->dataKey);
        t.validator    = spec->validator;
        result.append(t);
    }
    return result;
}

// =============================================================================
// bindGenerationContext
// =============================================================================

void AbstractPageType::bindGenerationContext(IPageRepository & /*repo*/,
                                              const QDir      & /*workingDir*/)
{
    // Default: no-op. Override in page types that need repo/stats access during
    // addCode() (e.g. PageTypeCategory).
}

// =============================================================================
// setWebsiteContext
// =============================================================================

void AbstractPageType::setWebsiteContext(const QString &websiteName, const QString &author)
{
    m_websiteName   = websiteName;
    m_websiteAuthor = author;
}

// =============================================================================
// prepareJsonLdImage
// =============================================================================

void AbstractPageType::prepareJsonLdImage(const QDir & /*workingDir*/, const QString & /*domain*/) {}

// =============================================================================
// setGenerationContext
// =============================================================================

void AbstractPageType::setGenerationContext(const QString              &permalink,
                                             const QString              &sourceLang,
                                             const QStringList          &targetLangs,
                                             const QHash<QString, QString> &publishedByLang,
                                             const QHash<QString, QString> &updatedByLang)
{
    m_permalink       = permalink;
    m_sourceLang      = sourceLang;
    m_targetLangs     = targetLangs;
    m_publishedByLang = publishedByLang;
    m_updatedByLang   = updatedByLang;
}

// =============================================================================
// _buildHreflangTags
// =============================================================================

QString AbstractPageType::_buildHreflangTags(AbstractEngine &engine, int /*websiteIndex*/) const
{
    const int rowCount = engine.rowCount();
    if (rowCount <= 1) {
        return {};
    }

    // Detect same-domain setup: all rows share the same non-empty domain.
    QString firstDomain;
    for (int i = 0; i < rowCount; ++i) {
        const QString d = engine.data(engine.index(i, AbstractEngine::COL_DOMAIN)).toString();
        if (d.isEmpty()) {
            continue;
        }
        if (firstDomain.isEmpty()) {
            firstDomain = d;
        } else if (d != firstDomain) {
            firstDomain.clear();
            break;
        }
    }
    const bool sameDomain = !firstDomain.isEmpty();

    QString result;
    QString xDefaultUrl;

    for (int i = 0; i < rowCount; ++i) {
        const QString lang   = engine.getLangCode(i);
        const QString domain = engine.data(engine.index(i, AbstractEngine::COL_DOMAIN)).toString();
        if (lang.isEmpty() || domain.isEmpty()) {
            continue;
        }
        // Only include languages with a published deploy/{lang}/content.db
        // AND where this specific page is available (translated or source lang).
        if (!engine.isLangDeployed(lang)) {
            continue;
        }
        if (!engine.isPageAvailable(m_permalink, i)) {
            continue;
        }

        const QString resolved = engine.resolvePermalink(m_permalink, i);

        QString url = QStringLiteral("https://") + domain;
        if (sameDomain && lang != QStringLiteral("en")) {
            url += QLatin1Char('/') + lang;
        }
        url += resolved;

        result += QStringLiteral("<link rel=\"alternate\" hreflang=\"");
        result += lang;
        result += QStringLiteral("\" href=\"");
        result += url;
        result += QStringLiteral("\">");

        if (xDefaultUrl.isEmpty() || lang == QStringLiteral("en")) {
            xDefaultUrl = url;
        }
    }

    if (!xDefaultUrl.isEmpty()) {
        result += QStringLiteral("<link rel=\"alternate\" hreflang=\"x-default\" href=\"");
        result += xDefaultUrl;
        result += QStringLiteral("\">");
    }

    return result;
}

// =============================================================================
// buildHeadMetaTags / hasSvg
// =============================================================================

QString AbstractPageType::buildHeadMetaTags(const QString & /*baseUrl*/,
                                             const QString & /*langCode*/) const
{
    return {};
}

bool AbstractPageType::hasSvg() const
{
    return false;
}

bool AbstractPageType::shouldIndex() const
{
    return true;
}

void AbstractPageType::addInnerTopCode(AbstractEngine & /*engine*/,
                                        int              /*websiteIndex*/,
                                        QString        & /*html*/,
                                        QString        & /*css*/,
                                        QString        & /*js*/,
                                        QSet<QString>  & /*cssDoneIds*/,
                                        QSet<QString>  & /*jsDoneIds*/) const
{
}

// =============================================================================
// load / save
// =============================================================================

void AbstractPageType::load(const QHash<QString, QString> &values)
{
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        QHash<QString, QString> sub;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                sub.insert(it.key().mid(prefix.size()), it.value());
            }
        }
        // load() mutates bloc state: const_cast is safe because AbstractPageType
        // owns the blocs and load() is the designated mutation path.
        const_cast<AbstractPageBloc *>(blocs.at(i))->load(sub);
    }
}

void AbstractPageType::save(QHash<QString, QString> &values) const
{
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        QHash<QString, QString> sub;
        blocs.at(i)->save(sub);
        for (auto it = sub.cbegin(); it != sub.cend(); ++it) {
            values.insert(prefix + it.key(), it.value());
        }
    }
}

// =============================================================================
// addCode
// =============================================================================

void AbstractPageType::addCode(QStringView     origContent,
                                AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds) const
{
    // Accumulate bloc output in temporary buffers so CSS can go into <head>
    // and JS before </body>.
    QString bodyHtml, innerCss, innerJs;

    AbstractTheme *theme = engine.getActiveTheme();

    // Top common blocs: header, top menu (rendered before the page body)
    if (theme) {
        theme->addCodeTop(engine, websiteIndex, bodyHtml, innerCss, innerJs, cssDoneIds, jsDoneIds);
    }

    // Page blocs are wrapped in <main class="page-content"> so they get
    // the centred, max-width container styles without affecting the header/footer.
    bodyHtml += QStringLiteral("<main class=\"page-content\">");
    addInnerTopCode(engine, websiteIndex, bodyHtml, innerCss, innerJs, cssDoneIds, jsDoneIds);
    for (const AbstractPageBloc *bloc : getPageBlocs()) {
        bloc->addCode(origContent, engine, websiteIndex, bodyHtml, innerCss, innerJs, cssDoneIds, jsDoneIds);
    }
    bodyHtml += QStringLiteral("</main>");

    // Bottom common blocs: bottom menu, footer (rendered after the page body)
    if (theme) {
        theme->addCodeBottom(engine, websiteIndex, bodyHtml, innerCss, innerJs, cssDoneIds, jsDoneIds);
    }

    // Base body + page-content styles — prepended so bloc-specific rules override them.
    {
        QString fontFam   = QStringLiteral("sans-serif");
        QString fontSize  = QStringLiteral("16px");
        QString bgColor   = QStringLiteral("#f4f6fb");
        QString textColor = QStringLiteral("#1f2937");
        QString maxWidth  = QStringLiteral("860px");
        QString primary   = QStringLiteral("#1a73e8");

        if (theme) {
            fontFam   = theme->fontFamily();
            fontSize  = theme->fontSizeBase();
            bgColor   = theme->bodyBgColor();
            textColor = theme->bodyTextColor();
            maxWidth  = theme->maxContentWidth();
            primary   = theme->primaryColor();
        }

        QString baseCss;
        baseCss += QStringLiteral("*{box-sizing:border-box}");
        baseCss += QStringLiteral("body{margin:0;padding:0;font-family:");
        baseCss += fontFam;
        baseCss += QStringLiteral(";font-size:");
        baseCss += fontSize;
        baseCss += QStringLiteral(";background:");
        baseCss += bgColor;
        baseCss += QStringLiteral(";color:");
        baseCss += textColor;
        baseCss += QStringLiteral(";line-height:1.7}");
        baseCss += QStringLiteral(".page-content{max-width:");
        baseCss += maxWidth;
        baseCss += QStringLiteral(";margin:0 auto;padding:2rem 1.5rem}");
        baseCss += QStringLiteral(".page-content h1,.page-content h2,.page-content h3"
                                   "{line-height:1.3;margin-top:1.8rem;margin-bottom:0.5rem}");
        baseCss += QStringLiteral(".page-content p{margin-top:0;margin-bottom:1rem}");
        baseCss += QStringLiteral(".page-content a{color:");
        baseCss += primary;
        baseCss += QStringLiteral("}");
        baseCss += QStringLiteral(".page-content img{max-width:100%;height:auto}");

        // Lightbox — cursor hint on images inside page content
        baseCss += QStringLiteral(".page-content img[src]{cursor:zoom-in}");
        // Native <dialog> lightbox overlay
        baseCss += QStringLiteral("#img-lightbox{"
            "padding:0;background:transparent;border:none;"
            "color-scheme:light;max-width:100vw;max-height:100vh}");
        baseCss += QStringLiteral("#img-lightbox::backdrop{"
            "background:rgba(0,0,0,.85);cursor:zoom-out}");
        baseCss += QStringLiteral("#img-lightbox img{"
            "display:block;width:min(90vw,1200px);height:auto;max-height:90vh;"
            "background:#fff;cursor:zoom-out;border-radius:4px}");
        baseCss += QStringLiteral("#img-lightbox__close{"
            "position:fixed;top:1rem;right:1.25rem;"
            "background:none;border:none;color:#fff;"
            "font-size:2rem;line-height:1;cursor:pointer;padding:.25rem .5rem;"
            "opacity:.8;transition:opacity .15s}"
            "#img-lightbox__close:hover{opacity:1}");

        innerCss = baseCss + innerCss;
    }

    // Lightbox HTML + JS — injected once per page at the page-type level.
    bodyHtml += QStringLiteral(
        "<dialog id=\"img-lightbox\" aria-label=\"Image zoom\">"
        "<button id=\"img-lightbox__close\" aria-label=\"Close\">×</button>"
        "<img id=\"img-lightbox__img\" src=\"\" alt=\"\">"
        "</dialog>");
    innerJs += QStringLiteral("(function(){"
        "var dlg=document.getElementById('img-lightbox');"
        "var img=document.getElementById('img-lightbox__img');"
        "var btn=document.getElementById('img-lightbox__close');"
        "if(!dlg||!img)return;"
        "document.querySelectorAll('.page-content img[src]').forEach(function(el){"
            "if(el.closest('.image-link'))return;"
            "el.addEventListener('click',function(){"
                "img.src=el.src;img.alt=el.alt||'';"
                "dlg.showModal()"
            "})"
        "});"
        "function close(){dlg.close();img.src=''}"
        "dlg.addEventListener('click',function(e){if(e.target===dlg)close()});"
        "img.addEventListener('click',close);"
        "if(btn)btn.addEventListener('click',close)"
        "})();");

    // <head> extras: viewport meta, favicons, optional web font stylesheet
    QString headExtra = QStringLiteral("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    if (theme) {
        const QString svgFav = theme->faviconSvg();
        if (!svgFav.isEmpty()) {
            headExtra += QStringLiteral("<link rel=\"icon\" type=\"image/svg+xml\" href=\"/");
            headExtra += svgFav;
            headExtra += QStringLiteral("\">");
        }
        const QString icoFav = theme->faviconIco();
        if (!icoFav.isEmpty()) {
            headExtra += QStringLiteral("<link rel=\"icon\" type=\"image/x-icon\" sizes=\"32x32\" href=\"/");
            headExtra += icoFav;
            headExtra += QStringLiteral("\">");
        }
        const QString appleFav = theme->faviconAppleTouch();
        if (!appleFav.isEmpty()) {
            headExtra += QStringLiteral("<link rel=\"apple-touch-icon\" sizes=\"180x180\" href=\"/");
            headExtra += appleFav;
            headExtra += QStringLiteral("\">");
        }
        const QString fontUrl = theme->fontUrl();
        if (!fontUrl.isEmpty()) {
            // Preconnect to the font origin before the stylesheet so the
            // browser can open the TCP+TLS connection while parsing the link.
            const QUrl parsedUrl(fontUrl);
            const QString fontOrigin = parsedUrl.scheme() + QStringLiteral("://") + parsedUrl.host();
            headExtra += QStringLiteral("<link rel=\"preconnect\" href=\"");
            headExtra += fontOrigin;
            headExtra += QStringLiteral("\">");
            // Google Fonts serves glyph files from a separate CDN origin.
            if (parsedUrl.host().contains(QStringLiteral("googleapis.com"))) {
                headExtra += QStringLiteral("<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>");
            }
            headExtra += QStringLiteral("<link rel=\"stylesheet\" href=\"");
            headExtra += fontUrl;
            headExtra += QStringLiteral("\">");
        }
    }

    if (!shouldIndex()) {
        headExtra += QStringLiteral("<meta name=\"robots\" content=\"noindex,follow\">");
    }

    const QString langCode = engine.getLangCode(websiteIndex);
    html += QStringLiteral("<!DOCTYPE html><html");
    if (!langCode.isEmpty()) {
        html += QStringLiteral(" lang=\"");
        html += langCode;
        html += QStringLiteral("\"");
    }
    const QString domain = engine.data(engine.index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();
    const QString baseUrl = domain.isEmpty()
                            ? QString{}
                            : QStringLiteral("https://") + domain;

    html += QStringLiteral("><head><meta charset=\"utf-8\">");
    html += headExtra;
    if (!innerCss.isEmpty()) {
        html += QStringLiteral("<style>");
        html += innerCss;
        html += QStringLiteral("</style>");
    }
    if (!baseUrl.isEmpty()) {
        html += buildHeadMetaTags(baseUrl, langCode);
    }
    html += _buildHreflangTags(engine, websiteIndex);
    html += QStringLiteral("</head><body>");
    html += bodyHtml;

    // Stats beacon + page JS — one <script> block to minimise round-trips.
    {
        // Beacon: POST /stats/display on load, PATCH /stats/click on link click,
        // POST /stats/session on page hide (uses sendBeacon for reliability).
        QString beaconJs =
            QStringLiteral("(function(){"
                "var pid=") + QStringLiteral("\"") + m_permalink + QStringLiteral("\",")
            + QStringLiteral(
                "did=null,t0=Date.now(),scroll=0,clicked=false;"
                "fetch('/stats/display',{method:'POST',"
                    "headers:{'Content-Type':'application/json'},"
                    "body:JSON.stringify({page_id:pid})})"
                    ".then(function(r){return r.json()})"
                    ".then(function(d){did=d.id})"
                    ".catch(function(){});"
                "document.addEventListener('scroll',function(){"
                    "var p=Math.min(100,Math.round((scrollY+innerHeight)"
                        "/document.documentElement.scrollHeight*100));"
                    "if(p>scroll)scroll=p;"
                "},{passive:true});"
                "document.addEventListener('click',function(e){"
                    "var a=e.target.closest('a[href]');"
                    "if(!a||!did)return;"
                    "var h=a.getAttribute('href');"
                    "if(!h||h.charAt(0)==='#')return;"
                    "clicked=true;"
                    "navigator.sendBeacon('/stats/click/'+did,"
                        "new Blob([JSON.stringify({clicked_at:new Date().toISOString()})],"
                        "{type:'application/json'}));"
                "});"
                "document.addEventListener('visibilitychange',function(){"
                    "if(document.visibilityState!=='hidden')return;"
                    "navigator.sendBeacon('/stats/session',"
                        "new Blob([JSON.stringify({"
                            "page_id:pid,"
                            "scrolling_percentage:scroll,"
                            "time_on_page:Math.round((Date.now()-t0)/1000),"
                            "is_final_page:!clicked"
                        "})],"
                        "{type:'application/json'}));"
                "});"
                "})();");

        html += QStringLiteral("<script>");
        if (!innerJs.isEmpty()) {
            html += innerJs;
            html += QLatin1Char(';');
        }
        html += beaconJs;
        html += QStringLiteral("</script>");
    }

    html += QStringLiteral("</body></html>");

    // css and js are intentionally left untouched — all output is inlined
    // into html. A page type is the top-level generator, not a fragment.
    Q_UNUSED(css)
    Q_UNUSED(js)
}

// =============================================================================
// collectTranslatables / applyTranslation / isTranslationComplete
// =============================================================================

void AbstractPageType::collectTranslatables(QStringView              origContent,
                                             QList<TranslatableField> &out) const
{
    Q_UNUSED(origContent)
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        QList<TranslatableField> blocFields;
        blocs.at(i)->collectTranslatables(QStringView{}, blocFields);
        for (auto &f : blocFields) {
            f.id = prefix + f.id;
        }
        out.append(blocFields);
    }
}

void AbstractPageType::applyTranslation(QStringView   origContent,
                                         const QString &fieldId,
                                         const QString &lang,
                                         const QString &text)
{
    Q_UNUSED(origContent)
    // fieldId is "<i>_<blocFieldId>" — split on first '_'.
    const int sep = fieldId.indexOf(QLatin1Char('_'));
    if (sep < 0) {
        return;
    }
    bool ok;
    const int idx = fieldId.left(sep).toInt(&ok);
    if (!ok) {
        return;
    }
    const auto &blocs = getPageBlocs();
    if (idx < 0 || idx >= blocs.size()) {
        return;
    }
    const QString blocFieldId = fieldId.mid(sep + 1);
    const_cast<AbstractPageBloc *>(blocs.at(idx))->applyTranslation(
        QStringView{}, blocFieldId, lang, text);
}

bool AbstractPageType::isTranslationComplete(QStringView   origContent,
                                              const QString &lang) const
{
    Q_UNUSED(origContent)
    // Source language is always complete — no translation needed.
    if (!m_authorLang.isEmpty() && lang == m_authorLang) {
        return true;
    }
    for (const AbstractPageBloc *bloc : getPageBlocs()) {
        if (!bloc->isTranslationComplete(QStringView{}, lang)) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// getAttributes
// =============================================================================

const QList<const AbstractAttribute *> &AbstractPageType::getAttributes() const
{
    if (!m_attributesCached) {
        for (const AbstractPageBloc *bloc : getPageBlocs()) {
            const auto &attrs = bloc->getAttributes();
            m_cachedAttributes += attrs;
        }
        m_attributesCached = true;
    }
    return m_cachedAttributes;
}
