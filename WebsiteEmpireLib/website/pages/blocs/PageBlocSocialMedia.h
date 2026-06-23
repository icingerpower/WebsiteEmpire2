#ifndef PAGEBLOCSOCIALMEDIA_H
#define PAGEBLOCSOCIALMEDIA_H

#include "website/pages/blocs/AbstractSecondaryPageBloc.h"

class PageRecord;

/**
 * Second-pass bloc that generates social-media image variants for an article.
 *
 * This bloc is purely responsible for the opt-in image second pass — text
 * metadata (titles and descriptions) is handled by the sibling PageBlocSocial
 * which always runs in the first pass.
 *
 * Second pass (AbstractSecondaryPageBloc):
 *   LauncherGeneration calls buildSecondPassPrompts() once the SocialMedia flag
 *   is set on the page.  Claude returns one raw SVG per variant;
 *   validateSecondPassResult() checks basic structure.  The launcher rasterises
 *   each validated SVG to WebP via QSvgRenderer and stores both blob types in
 *   images.db.
 *
 * Stored keys (set after generation, read by PageGenerator):
 *   img_og        — landscape WebP filename (e.g. "article-og.webp")
 *   img_wide      — wide WebP filename
 *   img_square    — square WebP filename
 *   img_portrait  — portrait WebP filename
 *
 * addCode() is a no-op: image tags are injected into <head> by PageGenerator.
 */
class PageBlocSocialMedia : public AbstractSecondaryPageBloc
{
public:
    // Image filename keys (set after second-pass generation)
    static constexpr const char *KEY_IMG_OG       = "img_og";
    static constexpr const char *KEY_IMG_WIDE     = "img_wide";
    static constexpr const char *KEY_IMG_SQUARE   = "img_square";
    static constexpr const char *KEY_IMG_PORTRAIT = "img_portrait";

    PageBlocSocialMedia() = default;
    ~PageBlocSocialMedia() override = default;

    // -------------------------------------------------------------------------
    // AbstractPageBloc
    // -------------------------------------------------------------------------
    QString getName() const override;

    /** Returns an empty map — no AI metadata keys in the first pass. */
    QHash<QString, QString> getAiKeyClues() const override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;
    void addCode(QStringView origContent, AbstractEngine &engine, int websiteIndex,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;
    AbstractPageBlockWidget *createEditWidget() override;

    // -------------------------------------------------------------------------
    // AbstractSecondaryPageBloc
    // -------------------------------------------------------------------------
    QList<QString> buildSecondPassPrompts(const QString    &sourceSvgContent,
                                           const PageRecord &page) const override;
    QString validateSecondPassResult(const QString &rawResult,
                                      int            variantIndex) const override;
    QString svgBaseFileName() const override;
    QString variantDataKey(int variantIndex) const override;

    // -------------------------------------------------------------------------
    // Accessors for PageGenerator (image filenames, empty until second pass)
    // -------------------------------------------------------------------------
    QString imgOg()       const;
    QString imgWide()     const;
    QString imgSquare()   const;
    QString imgPortrait() const;

    /**
     * Sets the filename for a given image size after generation.
     * Called by LauncherGeneration once a variant is written to images.db.
     */
    void setImgFileName(AbstractSocialMedia::ImageSize size, const QString &fileName);

private:
    QString m_imgOg;
    QString m_imgWide;
    QString m_imgSquare;
    QString m_imgPortrait;
    QString m_svgBaseFileName;
};

#endif // PAGEBLOCSOCIALMEDIA_H
