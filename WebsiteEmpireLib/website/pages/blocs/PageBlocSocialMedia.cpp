#include "PageBlocSocialMedia.h"

#include "website/pages/PageRecord.h"
#include "website/pages/blocs/widgets/PageBlocSocialWidget.h"
#include "website/social/AbstractSocialMedia.h"

#include "ExceptionWithTitleText.h"

#include <QCoreApplication>

// =============================================================================
// getName
// =============================================================================

QString PageBlocSocialMedia::getName() const
{
    return QCoreApplication::translate("PageBlocSocialMedia", "Social Media");
}

// =============================================================================
// getAiKeyClues
// =============================================================================

QHash<QString, QString> PageBlocSocialMedia::getAiKeyClues() const
{
    return {
        {QLatin1String(KEY_FB_TITLE),
         QCoreApplication::translate("PageBlocSocialMedia", "Facebook / Open Graph title, max 60 chars")},
        {QLatin1String(KEY_FB_DESC),
         QCoreApplication::translate("PageBlocSocialMedia", "Facebook / Open Graph description, max 160 chars")},
        {QLatin1String(KEY_TW_TITLE),
         QCoreApplication::translate("PageBlocSocialMedia", "Twitter/X title, max 70 chars")},
        {QLatin1String(KEY_TW_DESC),
         QCoreApplication::translate("PageBlocSocialMedia", "Twitter/X description, max 200 chars")},
        {QLatin1String(KEY_PT_TITLE),
         QCoreApplication::translate("PageBlocSocialMedia", "Pinterest title, max 100 chars")},
        {QLatin1String(KEY_PT_DESC),
         QCoreApplication::translate("PageBlocSocialMedia", "Pinterest description, max 500 chars")},
        {QLatin1String(KEY_LI_TITLE),
         QCoreApplication::translate("PageBlocSocialMedia", "LinkedIn title, max 150 chars")},
        {QLatin1String(KEY_LI_DESC),
         QCoreApplication::translate("PageBlocSocialMedia", "LinkedIn description, max 300 chars")},
    };
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocSocialMedia::load(const QHash<QString, QString> &values)
{
    m_fbTitle      = values.value(QLatin1String(KEY_FB_TITLE));
    m_fbDesc       = values.value(QLatin1String(KEY_FB_DESC));
    m_twTitle      = values.value(QLatin1String(KEY_TW_TITLE));
    m_twDesc       = values.value(QLatin1String(KEY_TW_DESC));
    m_ptTitle      = values.value(QLatin1String(KEY_PT_TITLE));
    m_ptDesc       = values.value(QLatin1String(KEY_PT_DESC));
    m_liTitle      = values.value(QLatin1String(KEY_LI_TITLE));
    m_liDesc       = values.value(QLatin1String(KEY_LI_DESC));
    m_imgOg        = values.value(QLatin1String(KEY_IMG_OG));
    m_imgWide      = values.value(QLatin1String(KEY_IMG_WIDE));
    m_imgSquare    = values.value(QLatin1String(KEY_IMG_SQUARE));
    m_imgPortrait  = values.value(QLatin1String(KEY_IMG_PORTRAIT));
}

void PageBlocSocialMedia::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_FB_TITLE),      m_fbTitle);
    values.insert(QLatin1String(KEY_FB_DESC),       m_fbDesc);
    values.insert(QLatin1String(KEY_TW_TITLE),      m_twTitle);
    values.insert(QLatin1String(KEY_TW_DESC),       m_twDesc);
    values.insert(QLatin1String(KEY_PT_TITLE),      m_ptTitle);
    values.insert(QLatin1String(KEY_PT_DESC),       m_ptDesc);
    values.insert(QLatin1String(KEY_LI_TITLE),      m_liTitle);
    values.insert(QLatin1String(KEY_LI_DESC),       m_liDesc);
    values.insert(QLatin1String(KEY_IMG_OG),        m_imgOg);
    values.insert(QLatin1String(KEY_IMG_WIDE),      m_imgWide);
    values.insert(QLatin1String(KEY_IMG_SQUARE),    m_imgSquare);
    values.insert(QLatin1String(KEY_IMG_PORTRAIT),  m_imgPortrait);
}

// =============================================================================
// addCode — no-op; PageGenerator injects social tags into <head>
// =============================================================================

void PageBlocSocialMedia::addCode(QStringView      /*origContent*/,
                                   AbstractEngine  & /*engine*/,
                                   int              /*websiteIndex*/,
                                   QString         & /*html*/,
                                   QString         & /*css*/,
                                   QString         & /*js*/,
                                   QSet<QString>   & /*cssDoneIds*/,
                                   QSet<QString>   & /*jsDoneIds*/) const
{
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocSocialMedia::createEditWidget()
{
    return new PageBlocSocialWidget;
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

QString PageBlocSocialMedia::facebookTitle()  const { return m_fbTitle; }
QString PageBlocSocialMedia::facebookDesc()   const { return m_fbDesc;  }
QString PageBlocSocialMedia::twitterTitle()   const { return m_twTitle; }
QString PageBlocSocialMedia::twitterDesc()    const { return m_twDesc;  }
QString PageBlocSocialMedia::pinterestTitle() const { return m_ptTitle; }
QString PageBlocSocialMedia::pinterestDesc()  const { return m_ptDesc;  }
QString PageBlocSocialMedia::linkedinTitle()  const { return m_liTitle; }
QString PageBlocSocialMedia::linkedinDesc()   const { return m_liDesc;  }
QString PageBlocSocialMedia::imgOg()          const { return m_imgOg;      }
QString PageBlocSocialMedia::imgWide()        const { return m_imgWide;    }
QString PageBlocSocialMedia::imgSquare()      const { return m_imgSquare;  }
QString PageBlocSocialMedia::imgPortrait()    const { return m_imgPortrait;}

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
