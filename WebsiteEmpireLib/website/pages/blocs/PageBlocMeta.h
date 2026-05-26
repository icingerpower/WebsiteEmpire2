#ifndef PAGEBLOC_META_H
#define PAGEBLOC_META_H

#include "website/commonblocs/BlocTranslations.h"
#include "website/pages/blocs/AbstractPageBloc.h"

/**
 * Stores SEO metadata for a page: the HTML <title> and <meta name="description">.
 *
 * addCode() is a no-op — this bloc contributes nothing to the body.
 * PageTypeArticle::buildHeadMetaTags() calls seoTitle() / seoDescription()
 * to emit the <title> and meta description tags into <head>.
 *
 * Both fields are translatable so each language version carries its own
 * title and description rather than serving the source-language copy.
 *
 * AI clues steer content generation toward correct search-engine lengths:
 *   seo_title       50–60 chars, must include the main keyword
 *   seo_description 120–160 chars, summarises page content
 */
class PageBlocMeta : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_SEO_TITLE = "seo_title";
    static constexpr const char *KEY_SEO_DESC  = "seo_description";

    PageBlocMeta() = default;
    ~PageBlocMeta() override = default;

    QString getName() const override;
    QHash<QString, QString> getAiKeyClues() const override;
    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

    /** No body output — meta tags are injected into <head> by buildHeadMetaTags(). */
    void addCode(QStringView origContent, AbstractEngine &engine, int websiteIndex,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    // ---- Translation protocol ------------------------------------------------

    void collectTranslatables(QStringView origContent,
                              QList<TranslatableField> &out) const override;
    void applyTranslation(QStringView origContent, const QString &fieldId,
                          const QString &lang, const QString &text) override;
    bool isTranslationComplete(QStringView origContent,
                               const QString &lang) const override;

    // ---- Accessors used by PageTypeArticle::buildHeadMetaTags() --------------

    /** Returns the title for lang, falling back to the source value. */
    QString seoTitle(const QString &lang) const;

    /** Returns the description for lang, falling back to the source value. */
    QString seoDescription(const QString &lang) const;

private:
    QString          m_title;
    QString          m_description;
    BlocTranslations m_translations;
};

#endif // PAGEBLOC_META_H
