#include "PageBlocSocialMedia.h"

#include "website/AbstractEngine.h"
#include "website/pages/PageRecord.h"
#include "website/pages/blocs/widgets/PageBlocReadOnlyWidget.h"
#include "website/social/AbstractSocialMedia.h"

#include "ExceptionWithTitleText.h"

#include <QCoreApplication>

// =============================================================================
// getName
// =============================================================================

QString PageBlocSocialMedia::getName() const
{
    return QCoreApplication::translate("PageBlocSocialMedia", "Social Media Images");
}

// =============================================================================
// getAiKeyClues — no first-pass AI keys; text metadata lives in PageBlocSocial
// =============================================================================

QHash<QString, QString> PageBlocSocialMedia::getAiKeyClues() const
{
    return {};
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocSocialMedia::load(const QHash<QString, QString> &values)
{
    m_imgOg       = values.value(QLatin1String(KEY_IMG_OG));
    m_imgWide     = values.value(QLatin1String(KEY_IMG_WIDE));
    m_imgSquare   = values.value(QLatin1String(KEY_IMG_SQUARE));
    m_imgPortrait = values.value(QLatin1String(KEY_IMG_PORTRAIT));
}

void PageBlocSocialMedia::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_IMG_OG),       m_imgOg);
    values.insert(QLatin1String(KEY_IMG_WIDE),     m_imgWide);
    values.insert(QLatin1String(KEY_IMG_SQUARE),   m_imgSquare);
    values.insert(QLatin1String(KEY_IMG_PORTRAIT), m_imgPortrait);
}

// =============================================================================
// addCode — no-op; PageGenerator injects image tags into <head>
// =============================================================================

void PageBlocSocialMedia::addCode(QStringView      /*origContent*/,
                                   AbstractEngine  &engine,
                                   int              websiteIndex,
                                   QString         &html,
                                   QString         & /*css*/,
                                   QString         & /*js*/,
                                   QSet<QString>   & /*cssDoneIds*/,
                                   QSet<QString>   & /*jsDoneIds*/) const
{
//    // TEMPORARY — visual debug: show social images inline so they can be
//    // validated in the preview before deploying.
//    if (m_imgOg.isEmpty() && m_imgWide.isEmpty()
//            && m_imgSquare.isEmpty() && m_imgPortrait.isEmpty()) {
//        return;
//    }
//    const QString domain = engine.data(
//        engine.index(websiteIndex, AbstractEngine::COL_DOMAIN)).toString();
//    const QString base = QStringLiteral("/images/") + domain + QLatin1Char('/');
//
//    html += QStringLiteral(
//        "<section style=\"border:3px solid #e00;padding:12px;margin:24px 0;"
//                           "background:#fff5f5;font-family:sans-serif\">"
//        "<p style=\"color:#e00;font-weight:bold;margin:0 0 8px\">"
//        "[DEBUG] Social-media images</p>");
//
//    const auto appendImg = [&](const QString &fn, const char *label) {
//        if (fn.isEmpty()) { return; }
//        html += QStringLiteral("<figure style=\"margin:8px 0\">"
//                               "<img src=\"");
//        html += base;
//        html += fn;
//        html += QStringLiteral("\" style=\"max-width:100%;display:block\">"
//                               "<figcaption style=\"font-size:12px;color:#555\">");
//        html += QLatin1String(label);
//        html += QStringLiteral("</figcaption></figure>");
//    };
//
//    appendImg(m_imgOg,       "og / landscape (1200×630)");
//    appendImg(m_imgWide,     "wide (1200×675)");
//    appendImg(m_imgSquare,   "square (1200×1200)");
//    appendImg(m_imgPortrait, "portrait (1000×1500)");
//
//    html += QStringLiteral("</section>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocSocialMedia::createEditWidget()
{
    return new PageBlocReadOnlyWidget(
        QCoreApplication::translate("PageBlocSocialMedia",
            "Social media image variants (og, wide, square, portrait) are generated "
            "automatically by the Generation pipeline — Second Pass. "
            "Titles and descriptions come from the Social Media bloc above."));
}

// =============================================================================
// AbstractSecondaryPageBloc — second pass
// =============================================================================

QList<QString> PageBlocSocialMedia::buildSecondPassPrompts(const QString    &sourceSvgContent,
                                                             const PageRecord &page) const
{
    const QList<AbstractSocialMedia::ImageSize> &sizes = requiredImageSizes();
    QList<QString> prompts;
    prompts.reserve(sizes.size());

    for (const AbstractSocialMedia::ImageSize size : std::as_const(sizes)) {
        const QSize &dims = AbstractSocialMedia::imageSizeDimensions(size);
        QString prompt;
        prompt += QStringLiteral("You are an SVG graphic designer. Create an SVG social media image"
                                  " for this article.\n\n");
        prompt += QStringLiteral("Article permalink: ");
        prompt += page.permalink;
        prompt += QStringLiteral("\nLanguage: ");
        prompt += page.lang;
        prompt += QStringLiteral("\nTarget dimensions: ");
        prompt += QString::number(dims.width());
        prompt += QStringLiteral("×");
        prompt += QString::number(dims.height());
        prompt += QStringLiteral(" px\n\n");
        prompt += QStringLiteral("Base the visual style and subject on this existing article SVG:\n");
        prompt += sourceSvgContent;
        prompt += QStringLiteral("\n\nRules:\n"
                                  "- Output ONLY the raw SVG (starting with <svg, ending with </svg>).\n"
                                  "- No code fences, no explanations, no extra text.\n"
                                  "- Set viewBox=\"0 0 ");
        prompt += QString::number(dims.width());
        prompt += QStringLiteral(" ");
        prompt += QString::number(dims.height());
        prompt += QStringLiteral("\" and width/height to match.\n"
                                  "- Use only inline styles, web-safe fonts, no external references.\n"
                                  "- Keep under 5000 characters.\n"
                                  "- Text must be in the article's language (");
        prompt += page.lang;
        prompt += QStringLiteral(").");
        prompts.append(prompt);
    }
    return prompts;
}

QString PageBlocSocialMedia::validateSecondPassResult(const QString &rawResult,
                                                       int            variantIndex) const
{
    const QString &trimmed = rawResult.trimmed();
    if (!trimmed.startsWith(QStringLiteral("<svg")) || !trimmed.endsWith(QStringLiteral("</svg>"))) {
        ExceptionWithTitleText ex(
            QCoreApplication::translate("PageBlocSocialMedia", "Social SVG Validation Error"),
            QCoreApplication::translate("PageBlocSocialMedia",
                "Variant %1: response must start with <svg and end with </svg>.")
                .arg(variantIndex));
        ex.raise();
    }
    return trimmed;
}

QString PageBlocSocialMedia::svgBaseFileName() const
{
    return m_svgBaseFileName;
}

QString PageBlocSocialMedia::variantDataKey(int variantIndex) const
{
    switch (requiredImageSizes().at(variantIndex)) {
    case AbstractSocialMedia::ImageSize::Landscape: return QLatin1String(KEY_IMG_OG);
    case AbstractSocialMedia::ImageSize::Wide:      return QLatin1String(KEY_IMG_WIDE);
    case AbstractSocialMedia::ImageSize::Square:    return QLatin1String(KEY_IMG_SQUARE);
    case AbstractSocialMedia::ImageSize::Portrait:  return QLatin1String(KEY_IMG_PORTRAIT);
    }
    return {};
}

// =============================================================================
// Accessors
// =============================================================================

QString PageBlocSocialMedia::imgOg()       const { return m_imgOg;      }
QString PageBlocSocialMedia::imgWide()     const { return m_imgWide;    }
QString PageBlocSocialMedia::imgSquare()   const { return m_imgSquare;  }
QString PageBlocSocialMedia::imgPortrait() const { return m_imgPortrait;}

void PageBlocSocialMedia::setImgFileName(AbstractSocialMedia::ImageSize size,
                                          const QString                  &fileName)
{
    switch (size) {
    case AbstractSocialMedia::ImageSize::Landscape: m_imgOg       = fileName; break;
    case AbstractSocialMedia::ImageSize::Wide:      m_imgWide     = fileName; break;
    case AbstractSocialMedia::ImageSize::Square:    m_imgSquare   = fileName; break;
    case AbstractSocialMedia::ImageSize::Portrait:  m_imgPortrait = fileName; break;
    }
}
