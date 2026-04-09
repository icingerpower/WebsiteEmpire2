#ifndef ABSTRACTCOMMONBLOC_H
#define ABSTRACTCOMMONBLOC_H

#include "website/WebCodeAdder.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

class AbstractCommonBlocWidget;
class QSettings;

/**
 * Base class for cross-page HTML fragments: header, footer, top menu,
 * bottom menu.
 *
 * Unlike AbstractPageBloc, common blocs are NOT called during per-page
 * generation.  addCode() is called once per (domain, lang) when the
 * fragment needs to be (re)generated — e.g. after an edit or a menu item
 * reorder.  The result is stored as a fragment in content.db and
 * prepended/appended to page bodies at serve time by Drogon.
 *
 * Because common blocs are not driven by source text, origContent in
 * addCode() is always empty.  All data comes from the bloc's own member
 * variables, loaded by the repository before generation.  Implementations
 * must call Q_UNUSED(origContent).
 *
 * Common blocs are owned by their parent AbstractTheme.
 */
class AbstractCommonBloc : public WebCodeAdder
{
public:
    /**
     * Scope levels at which a common bloc instance can be defined.
     * supportedScopes() returns the subset offered by this bloc type.
     *
     * Resolution order (most specific wins): PerDomain > PerTheme > Global.
     */
    enum class ScopeType {
        Global,    ///< One instance shared across all themes and domains
        PerTheme,  ///< One instance per theme; overrides Global
        PerDomain, ///< One instance per domain; overrides PerTheme and Global
    };

    virtual ~AbstractCommonBloc() = default;

    /**
     * Stable identifier persisted to the DB — never change once data exists.
     */
    virtual QString getId() const = 0;

    /**
     * Human-readable name shown in the UI.
     * Must be wrapped with QCoreApplication::translate() in implementations.
     */
    virtual QString getName() const = 0;

    /**
     * Returns the scope levels the UI should offer for this bloc type.
     * All types except CommonBlocMenuBottom return {Global, PerTheme, PerDomain}.
     * CommonBlocMenuBottom returns {Global} only — it is not customised per
     * theme or domain.
     */
    virtual QList<ScopeType> supportedScopes() const = 0;

    /**
     * Create and return an editor widget for this bloc.
     * Ownership is transferred to the caller.
     *
     * Returns nullptr until a concrete widget is implemented.
     */
    virtual AbstractCommonBlocWidget *createEditWidget() = 0;

    /**
     * Serialize this bloc's editable content to a flat key→value map.
     * Used by AbstractTheme::saveBlocsData() to persist to disk.
     * Keys should use the bloc's own KEY_* constants.
     */
    virtual QVariantMap toMap() const = 0;

    /**
     * Restore this bloc's editable content from a key→value map.
     * Used by AbstractTheme::_loadBlocsData() to hydrate from disk.
     * Unknown keys must be silently ignored.
     */
    virtual void fromMap(const QVariantMap &map) = 0;

    // ---- Translation interface ----

    /**
     * Returns all translatable fields as {fieldId -> sourceText}.
     * Used by CommonBlocTranslator to build AI job payloads.
     * Default: returns empty map (no translatable fields).
     */
    virtual QHash<QString, QString> sourceTexts() const;

    /**
     * Store a translation for fieldId+langCode, produced from the current source.
     * Silently ignored by the default implementation.
     */
    virtual void setTranslation(const QString &fieldId,
                                const QString &langCode,
                                const QString &translatedText);

    /**
     * Returns field IDs (or labels, for menus) that lack a translation for langCode.
     * Returns an empty list when langCode == sourceLangCode (base text is used).
     * Default: returns empty list (no translatable fields).
     */
    virtual QStringList missingTranslations(const QString &langCode,
                                            const QString &sourceLangCode) const;

    /**
     * Raises ExceptionWithTitleText if missingTranslations() is non-empty.
     * Non-virtual -- delegates to missingTranslations().
     */
    void assertTranslated(const QString &langCode,
                          const QString &sourceLangCode) const;

    /**
     * Serialize translation sub-groups.  Called by AbstractTheme::saveBlocsData()
     * after toMap() writes the main keys.  settings is already at this bloc's group.
     * Default: no-op.
     */
    virtual void saveTranslations(QSettings &settings);

    /**
     * Restore translation sub-groups.  Called by AbstractTheme::_loadBlocsData()
     * after fromMap() restores the main keys (so sources are already registered).
     * settings is already at this bloc's group.
     * Default: no-op.
     */
    virtual void loadTranslations(QSettings &settings);
};

#endif // ABSTRACTCOMMONBLOC_H
