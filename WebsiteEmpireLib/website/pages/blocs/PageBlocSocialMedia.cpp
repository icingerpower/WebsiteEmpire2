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
