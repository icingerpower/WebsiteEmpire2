#ifndef ABSTRACTPAGETYPE_H
#define ABSTRACTPAGETYPE_H

#include "website/WebCodeAdder.h"
#include "website/pages/blocs/AbstractPageBloc.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class AbstractEngine;
class AbstractAttribute;
class CategoryTable;
class IPageRepository;
class QDir;

/**
 * Base class for a page type.
 *
 * A page type is a WebCodeAdder (it contributes HTML/CSS/JS to a generated
 * page) that is composed of one or more page blocs.
 *
 * Self-registration pattern
 * -------------------------
 * Each concrete subclass must define:
 *   static constexpr const char *TYPE_ID;       // stable DB key, never change
 *   static constexpr const char *DISPLAY_NAME;  // human-readable label
 * and place DECLARE_PAGE_TYPE(ClassName) at file scope in its .cpp.
 *
 * The factory receives a CategoryTable reference so that page types whose
 * blocs require category data can use it; page types with no category bloc
 * may ignore the argument.
 *
 * Registry access
 * ---------------
 *   AbstractPageType::createForTypeId("article", categoryTable)
 *       → heap-allocated PageTypeArticle, or nullptr if unknown
 *   AbstractPageType::allTypeIds()
 *       → list of all registered type ids
 */
class AbstractPageType : public WebCodeAdder
{
public:
    using Factory = std::function<std::unique_ptr<AbstractPageType>(CategoryTable &)>;

    virtual ~AbstractPageType() = default;

    /** Stable identifier stored in the database (e.g. "article"). Never change. */
    virtual QString getTypeId() const = 0;

    /** Human-readable label shown in the UI (e.g. "Article"). */
    virtual QString getDisplayName() const = 0;

    /**
     * Returns the ordered list of blocs that compose this page type.
     * Implementations must store the list as a member and return a const ref.
     * Pointers are const: callers may only invoke const methods on the blocs.
     */
    virtual const QList<const AbstractPageBloc *> &getPageBlocs() const = 0;

    /**
     * Records the language the page was authored in.
     * Must be called before isTranslationComplete() or addCode().
     * isTranslationComplete() returns true immediately for lang == authorLang
     * so that source-language pages are always generated without translation checks.
     */
    void setAuthorLang(const QString &lang);

    /**
     * Loads all blocs from a flat key→value map produced by save().
     * Each bloc's keys are namespaced by their position index: "<i>_<key>".
     * Keys with unknown prefixes or unrecognised by a bloc are silently ignored.
     */
    void load(const QHash<QString, QString> &values);

    /**
     * Saves all blocs into a flat key→value map suitable for database storage.
     * Each bloc's keys are prefixed with "<i>_" where i is its position index.
     */
    void save(QHash<QString, QString> &values) const;

    /**
     * Accumulates bloc output in temporary buffers then wraps the result in a
     * full HTML page: <!DOCTYPE html><html><head>…CSS…</head><body>…JS…</body></html>.
     * The css and js out-parameters are intentionally left untouched.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /**
     * Collects all translatable fields across all blocs.
     * Field ids are prefixed "<i>_" matching the load/save namespace.
     */
    void collectTranslatables(QStringView              origContent,
                              QList<TranslatableField> &out) const override;

    /**
     * Applies a translated field value to the correct bloc.
     * fieldId must be in "<i>_<blocFieldId>" form.
     */
    void applyTranslation(QStringView   origContent,
                          const QString &fieldId,
                          const QString &lang,
                          const QString &text) override;

    /**
     * Returns true when all blocs report their translations for lang are complete,
     * or when lang equals the author language (no translation needed).
     */
    bool isTranslationComplete(QStringView   origContent,
                               const QString &lang) const override;

    /**
     * Called by PageGenerator after createForTypeId() + load() + setAuthorLang().
     * Page types that need repository access during addCode() (e.g. PageTypeCategory
     * reading stats for article sorting) override this to receive the repo and dir.
     * Default: no-op.
     */
    virtual void bindGenerationContext(IPageRepository &repo, const QDir &workingDir);

    /**
     * Called by PageGenerator before addCode() to supply website-level metadata
     * (website name and author name) used by JSON-LD structured data.
     * Both strings may be empty if the caller does not have settings available.
     */
    void setWebsiteContext(const QString &websiteName, const QString &author);

    /**
     * Called by PageGenerator before addCode() to precompute the JSON-LD image
     * fallback when no second-pass social-media images have been generated yet.
     *
     * domain is the bare hostname used to look up the source SVG in images.db
     * (e.g. "biomarky.com").  The lookup tries domain="" (global fallback) first,
     * then the supplied domain, to handle both storage conventions.
     *
     * Default: no-op.  PageTypeArticle overrides this to rasterize the article's
     * primary SVG illustration to a 1200×630 WebP and cache it in images.db under
     * domain="" (global fallback served to every language/domain variant).
     *
     * The result is stored in m_jsonLdFallbackImage (filename only, no domain or
     * leading slash) and consumed by buildHeadMetaTags().
     */
    virtual void prepareJsonLdImage(const QDir &workingDir, const QString &domain);

    /**
     * Called by PageGenerator before addCode() to supply the page's permalink,
     * its source language, the list of target languages for hreflang generation,
     * and per-language creation / update timestamps.
     *
     * publishedByLang maps each BCP-47 language code to the ISO 8601 UTC
     * creation timestamp of that language's page record (i.e. when that
     * translation was first created).  updatedByLang maps the same codes to the
     * last-modified timestamp.  Both maps are keyed by the language they
     * describe; the source language is always present.
     *
     * buildHeadMetaTags() implementations use these to emit
     * article:published_time / article:modified_time Open Graph tags and the
     * JSON-LD datePublished / dateModified properties, keyed by the langCode
     * argument they receive.
     */
    void setGenerationContext(const QString              &permalink,
                              const QString              &sourceLang,
                              const QStringList          &targetLangs,
                              const QHash<QString, QString> &publishedByLang,
                              const QHash<QString, QString> &updatedByLang);

    /**
     * Returns <title>, canonical, Open Graph and hreflang <meta>/<link> tags
     * to inject into the page <head>.  addCode() calls this between the <style>
     * block and </head>.
     *
     * baseUrl is "https://<domain>" (no trailing slash).
     * langCode is the language being rendered (e.g. "fr").
     *
     * Default: returns an empty string.  PageTypeArticle overrides this.
     */
    virtual QString buildHeadMetaTags(const QString &baseUrl,
                                      const QString &langCode) const;

    /**
     * Returns true when the page type requires an SVG image to be generated as
     * part of its first-pass AI pipeline (e.g. PageTypeArticle).
     *
     * LauncherGeneration uses this to decide whether a page that completed the
     * content step without a valid SVG should be marked Complete or left in
     * ContentReady so that the SVG generation is retried on the next run.
     * Page types where SVG is optional or absent (e.g. PageTypeCategory) keep
     * the default false so that missing SVG does not block completion.
     */
    virtual bool hasSvg() const;

    /**
     * Returns true when this page type should be indexed by search engines.
     * Default: true.  Override and return false for page types that must never
     * appear in search results (e.g. PageTypeLegal — privacy policy, ToS).
     * addCode() emits <meta name="robots" content="noindex,follow"> when false.
     */
    virtual bool shouldIndex() const;

    /**
     * Aggregates each bloc's getAiKeyClues() into a single flat map, prefixing
     * every key with "<blocIndex>_" to match the save()/load() namespace.
     * Used by GenPageQueue to replace empty-string placeholders in the JSON schema
     * prompt with per-field hints so Claude knows how to fill each metadata field.
     */
    QHash<QString, QString> collectAiKeyClues() const;

    /**
     * Returns one entry for every bloc that exposes an AiUpdateSpec.
     * Each entry carries the prefixed key (e.g. "0_categories"), the format
     * prompt, the vocabulary hint from getAiKeyClues(), and the validator.
     * Used by PaneUpdate to populate the target dropdown and by LauncherUpdate
     * to drive the two-call AI flow.
     */
    struct AiUpdateTarget {
        QString displayName;  ///< e.g. "Categories"
        QString prefixedKey;  ///< e.g. "0_categories"
        QString formatPrompt; ///< second-call instructions
        QString aiKeyClue;    ///< vocabulary hint (from getAiKeyClues())
        AbstractPageBloc::AiUpdateSpec::Validator validator;
    };
    QList<AiUpdateTarget> aiUpdateTargets() const;

    /**
     * Returns the union of attributes from all blocs.
     * Built lazily on the first call; subsequent calls return a cached ref.
     */
    const QList<const AbstractAttribute *> &getAttributes() const;

    // -------------------------------------------------------------------------
    // Registry
    // -------------------------------------------------------------------------

    /**
     * Returns a fresh heap-allocated instance for typeId, or nullptr if unknown.
     * The caller owns the returned object.
     */
    static std::unique_ptr<AbstractPageType> createForTypeId(const QString &typeId,
                                                              CategoryTable &table);

    /** Returns all registered type ids in insertion order. */
    static QList<QString> allTypeIds();

    /**
     * Place at file scope in each concrete subclass's .cpp via DECLARE_PAGE_TYPE.
     * Q_ASSERT fires on duplicate typeId.
     */
    class Recorder
    {
    public:
        explicit Recorder(const QString &typeId,
                          const QString &displayName,
                          Factory        factory);
    };

protected:
    // Set by setGenerationContext(); used by buildHeadMetaTags() overrides.
    QString                m_permalink;
    QString                m_sourceLang;
    QStringList            m_targetLangs;
    QHash<QString, QString> m_publishedByLang; ///< lang → ISO 8601 creation timestamp
    QHash<QString, QString> m_updatedByLang;   ///< lang → ISO 8601 last-modified timestamp
    // Set by setWebsiteContext(); used by JSON-LD author/publisher fields.
    QString                m_websiteName;
    QString                m_websiteAuthor;
    // Set by prepareJsonLdImage(); used by buildHeadMetaTags() as image fallback.
    QString                m_jsonLdFallbackImage;

    /**
     * Hook called by addCode() inside <main>, after all page blocs and before
     * </main>.  Default: no-op.  Override in page types that need extra content
     * injected at the bottom of the content area (e.g. PageTypeArticle renders
     * the AI disclaimer here via AbstractTheme::addCodeArticle()).
     */
    virtual void addInnerTopCode(AbstractEngine &engine,
                                     int             websiteIndex,
                                     QString        &html,
                                     QString        &css,
                                     QString        &js,
                                     QSet<QString>  &cssDoneIds,
                                     QSet<QString>  &jsDoneIds) const;

private:
    QString m_authorLang;  ///< set by setAuthorLang(); compared in isTranslationComplete()
    mutable QList<const AbstractAttribute *> m_cachedAttributes;
    mutable bool m_attributesCached = false;

    /**
     * Builds <link rel="alternate" hreflang="…"> tags for every language row
     * in the engine that has the current page available.
     *
     * Same-domain setup (all rows share one domain — path-based routing):
     *   EN → https://domain/permalink
     *   others → https://domain/{langCode}/resolvedPermalink
     *
     * Different-domain setup:
     *   each → https://{rowDomain}/resolvedPermalink
     *
     * Also emits an x-default tag pointing to the English version (or the
     * first available language when no English row is present).
     * Returns an empty string when the engine has only one language row.
     */
    QString _buildHreflangTags(AbstractEngine &engine, int websiteIndex) const;
};

/**
 * Place at file scope in the .cpp of each concrete page type.
 * Requires the class to define static constexpr TYPE_ID and DISPLAY_NAME.
 */
#define DECLARE_PAGE_TYPE(ClassName)                                                   \
    AbstractPageType::Recorder recorder##ClassName {                                   \
        QLatin1String(ClassName::TYPE_ID),                                             \
        QLatin1String(ClassName::DISPLAY_NAME),                                        \
        [](CategoryTable &t) -> std::unique_ptr<AbstractPageType> {                    \
            return std::make_unique<ClassName>(t);                                     \
        }                                                                              \
    };

#endif // ABSTRACTPAGETYPE_H
