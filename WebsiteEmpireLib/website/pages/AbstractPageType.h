#ifndef ABSTRACTPAGETYPE_H
#define ABSTRACTPAGETYPE_H

#include "website/WebCodeAdder.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

class AbstractEngine;
class AbstractPageBloc;
class AbstractAttribute;
class CategoryTable;

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

private:
    QString m_authorLang;  ///< set by setAuthorLang(); compared in isTranslationComplete()
    mutable QList<const AbstractAttribute *> m_cachedAttributes;
    mutable bool m_attributesCached = false;
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
