#ifndef PAGEBLOCSOCIALMEDIA_H
#define PAGEBLOCSOCIALMEDIA_H

#include "website/pages/blocs/AbstractSecondaryPageBloc.h"

class PageRecord;

/**
 * Stores social media text metadata (first pass) and orchestrates the
 * generation of social image variants (second pass).
 *
 * First pass (metadata JSON — same session as article text):
 *   Per-platform title and description fields, populated by Claude alongside
 *   other metadata blocs.  Keys follow the pattern <platform>_title /
 *   <platform>_desc so the AI schema prompt can carry per-field constraints.
 *
 * Second pass (AbstractSecondaryPageBloc):
 *   After the article and its primary [IMGFIX] SVG are saved, LauncherGeneration
 *   calls buildSecondPassPrompts() to obtain one prompt per image variant.
 *   Claude returns raw SVG; validateSecondPassResult() checks basic structure.
 *   The launcher rasterises each validated SVG to WebP via QSvgRenderer and
 *   stores both blob types in images.db.
 *
 * Stored keys
 * -----------
 * First-pass text:
 *   facebook_title, facebook_desc
 *   twitter_title,  twitter_desc
 *   google_title    (Google does not use a title meta; reserved for future use)
 *   pinterest_title, pinterest_desc
 *   linkedin_title,  linkedin_desc
 *
 * Second-pass image filenames (set after generation, read by PageGenerator):
 *   img_og        — landscape WebP filename (e.g. "article-og.webp")
 *   img_wide      — wide WebP filename
 *   img_square    — square WebP filename
 *   img_portrait  — portrait WebP filename
 *
 * addCode() is a no-op: all output is injected into <head> by PageGenerator,
 * which iterates AbstractSocialMedia::all() and reads this bloc's accessors.
 */
class PageBlocSocialMedia : public AbstractSecondaryPageBloc
{
public:
    static constexpr const char *KEY_FB_TITLE  = "facebook_title";
    static constexpr const char *KEY_FB_DESC   = "facebook_desc";
    static constexpr const char *KEY_TW_TITLE  = "twitter_title";
    static constexpr const char *KEY_TW_DESC   = "twitter_desc";
    static constexpr const char *KEY_PT_TITLE  = "pinterest_title";
    static constexpr const char *KEY_PT_DESC   = "pinterest_desc";
    static constexpr const char *KEY_LI_TITLE  = "linkedin_title";
    static constexpr const char *KEY_LI_DESC   = "linkedin_desc";
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
    // Accessors for PageGenerator
    // -------------------------------------------------------------------------
    QString facebookTitle()  const;
    QString facebookDesc()   const;
    QString twitterTitle()   const;
    QString twitterDesc()    const;
    QString pinterestTitle() const;
    QString pinterestDesc()  const;
    QString linkedinTitle()  const;
    QString linkedinDesc()   const;

    /** Filename of the landscape (OG) WebP, empty until second pass completes. */
    QString imgOg()       const;
    /** Filename of the wide (Google) WebP, empty until second pass completes. */
    QString imgWide()     const;
    /** Filename of the square (Twitter summary) WebP, empty until second pass completes. */
    QString imgSquare()   const;
    /** Filename of the portrait (Pinterest) WebP, empty until second pass completes. */
    QString imgPortrait() const;

    /**
     * Sets the filename for a given image size after generation.
     * Called by LauncherGeneration once a variant is written to images.db.
     */
    void setImgFileName(AbstractSocialMedia::ImageSize size, const QString &fileName);

private:
    QString m_fbTitle;
    QString m_fbDesc;
    QString m_twTitle;
    QString m_twDesc;
    QString m_ptTitle;
    QString m_ptDesc;
    QString m_liTitle;
    QString m_liDesc;
    QString m_imgOg;
    QString m_imgWide;
    QString m_imgSquare;
    QString m_imgPortrait;
    QString m_svgBaseFileName;
};

#endif // PAGEBLOCSOCIALMEDIA_H
