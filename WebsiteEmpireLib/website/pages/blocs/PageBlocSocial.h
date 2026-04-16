#ifndef PAGEBLOC_SOCIAL_H
#define PAGEBLOC_SOCIAL_H

#include "website/pages/blocs/AbstractPageBloc.h"

/**
 * Stores Open Graph / social-media metadata for an article page.
 *
 * Covers four platforms: Facebook (Open Graph), Twitter / X, Pinterest,
 * and LinkedIn.  Each platform carries a title and a description field.
 * Images are out of scope for this initial version.
 *
 * addCode() is a no-op: social meta tags belong in the HTML <head>, not in
 * the body content stream.  The page generator reads these values directly
 * from the bloc's accessors when <head> injection is implemented.
 *
 * Key naming:  <platform>_<field>
 *   facebook_title,  facebook_desc
 *   twitter_title,   twitter_desc
 *   pinterest_title, pinterest_desc
 *   linkedin_title,  linkedin_desc
 */
class PageBlocSocial : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_FB_TITLE = "facebook_title";
    static constexpr const char *KEY_FB_DESC  = "facebook_desc";
    static constexpr const char *KEY_TW_TITLE = "twitter_title";
    static constexpr const char *KEY_TW_DESC  = "twitter_desc";
    static constexpr const char *KEY_PT_TITLE = "pinterest_title";
    static constexpr const char *KEY_PT_DESC  = "pinterest_desc";
    static constexpr const char *KEY_LI_TITLE = "linkedin_title";
    static constexpr const char *KEY_LI_DESC  = "linkedin_desc";

    PageBlocSocial() = default;
    ~PageBlocSocial() override = default;

    QString getName() const override;
    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

    /** No body HTML output — meta tags are injected into <head> by the engine. */
    void addCode(QStringView origContent, AbstractEngine &engine, int websiteIndex,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    // Accessors for the page generator to read when building <head> content.
    QString facebookTitle()  const { return m_fbTitle; }
    QString facebookDesc()   const { return m_fbDesc;  }
    QString twitterTitle()   const { return m_twTitle; }
    QString twitterDesc()    const { return m_twDesc;  }
    QString pinterestTitle() const { return m_ptTitle; }
    QString pinterestDesc()  const { return m_ptDesc;  }
    QString linkedinTitle()  const { return m_liTitle; }
    QString linkedinDesc()   const { return m_liDesc;  }

private:
    QString m_fbTitle;
    QString m_fbDesc;
    QString m_twTitle;
    QString m_twDesc;
    QString m_ptTitle;
    QString m_ptDesc;
    QString m_liTitle;
    QString m_liDesc;
};

#endif // PAGEBLOC_SOCIAL_H
