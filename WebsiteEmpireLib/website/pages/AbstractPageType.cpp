#include "AbstractPageType.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QHash>
#include <QString>

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
// buildHeadMetaTags / hasSvg
// =============================================================================

QString AbstractPageType::buildHeadMetaTags(const QString & /*baseUrl*/) const
{
    return {};
}

bool AbstractPageType::hasSvg() const
{
    return false;
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
            "max-width:100vw;max-height:100vh}");
        baseCss += QStringLiteral("#img-lightbox::backdrop{"
            "background:rgba(0,0,0,.85);cursor:zoom-out}");
        baseCss += QStringLiteral("#img-lightbox img{"
            "display:block;max-width:min(90vw,1200px);max-height:90vh;"
            "object-fit:contain;cursor:zoom-out;border-radius:4px}");
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

    // <head> extras: viewport meta + optional web font stylesheet
    QString headExtra = QStringLiteral("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    if (theme) {
        const QString fontUrl = theme->fontUrl();
        if (!fontUrl.isEmpty()) {
            headExtra += QStringLiteral("<link rel=\"stylesheet\" href=\"");
            headExtra += fontUrl;
            headExtra += QStringLiteral("\">");
        }
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
        html += buildHeadMetaTags(baseUrl);
    }
    html += QStringLiteral("</head><body>");
    html += bodyHtml;
    if (!innerJs.isEmpty()) {
        html += QStringLiteral("<script>");
        html += innerJs;
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
