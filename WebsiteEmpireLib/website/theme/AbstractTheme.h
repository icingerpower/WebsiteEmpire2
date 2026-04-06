#ifndef ABSTRACTTHEME_H
#define ABSTRACTTHEME_H

#include "website/theme/Param.h"

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringView>
#include <QVariant>

class AbstractCommonBloc;
class AbstractEngine;

/**
 * Base class for website themes.
 *
 * A theme defines:
 *   - The common blocs rendered above every page body (getTopBlocs), e.g.
 *     CommonBlocHeader then CommonBlocMenuTop.
 *   - The common blocs rendered below every page body (getBottomBlocs), e.g.
 *     CommonBlocMenuBottom then CommonBlocFooter.
 *   - A list of configurable Params (colours, fonts, etc.) exposed as a
 *     QAbstractTableModel so a QTableView can display and edit them directly.
 *     The model has two columns: COL_NAME (read-only) and COL_VALUE (editable).
 *
 * Current param values are persisted to {workingDir}/{getId()}_params.ini via
 * QSettings.  paramValue(id) returns the saved override, or Param::defaultValue
 * when no override exists.
 *
 * The theme owns its common blocs — the raw pointers returned by getTopBlocs()
 * and getBottomBlocs() are valid for the lifetime of the theme object.
 *
 * Lazy loading: param values are loaded from disk on first model access, not in
 * the constructor, to avoid calling the virtual getParams() before the concrete
 * subclass is fully constructed.
 *
 * Registration: subclasses register themselves at static-init time via the
 * DECLARE_THEME() macro.  ALL_THEMES() returns the full registry keyed by
 * getId().  Use create() to instantiate a registered theme with a workingDir.
 */
class AbstractTheme : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_NAME  = 0; ///< Read-only parameter name column
    static constexpr int COL_VALUE = 1; ///< Editable current-value column

    // Normal constructor — binds the theme to a working directory.
    explicit AbstractTheme(const QDir &workingDir, QObject *parent = nullptr);

    /**
     * Prototype constructor — for DECLARE_THEME use only.
     * Sets m_workingDir to QDir().  Prototypes must not be used as
     * QAbstractTableModel instances (their workingDir is empty).
     * Must be public so the file-scope DECLARE_THEME macro can access it.
     */
    AbstractTheme();
    ~AbstractTheme() override = default;

    /** Stable identifier persisted to disk — never change once data exists. */
    virtual QString getId()   const = 0;

    /** Human-readable display name; must be wrapped in tr(). */
    virtual QString getName() const = 0;

    /**
     * Returns a freshly heap-allocated instance bound to workingDir.
     * Called by the application after selecting a theme from ALL_THEMES().
     * The caller owns the returned object.
     */
    virtual AbstractTheme *create(const QDir &workingDir,
                                  QObject    *parent = nullptr) const = 0;

    /**
     * Common blocs rendered above every page body, in render order.
     * Returned pointers are owned by the theme and valid for its lifetime.
     */
    virtual QList<AbstractCommonBloc *> getTopBlocs()    = 0;

    /**
     * Common blocs rendered below every page body, in render order.
     * Returned pointers are owned by the theme and valid for its lifetime.
     */
    virtual QList<AbstractCommonBloc *> getBottomBlocs() = 0;

    /**
     * Full list of configurable parameters for this theme.
     * These become the rows of the QAbstractTableModel.
     * Do not call from subclass constructors — virtual dispatch is not yet
     * active at that point.  Use paramValue() or model access instead.
     */
    virtual QList<Param> getParams() const = 0;

    /**
     * Returns the current value for the param identified by id.
     * Returns the user-saved override if one exists, otherwise Param::defaultValue.
     * Returns an invalid QVariant if id is not found in getParams().
     */
    // -------------------------------------------------------------------------
    // Registry (prototype pattern — mirrors AbstractEngine::ALL_ENGINES)
    // -------------------------------------------------------------------------

    /** QSettings key used to persist the chosen theme id. */
    static QString settingsKey();

    /** Returns all registered theme prototypes keyed by getId(). */
    static const QMap<QString, const AbstractTheme *> &ALL_THEMES();

    /** Used by DECLARE_THEME to register a prototype at static-init time. */
    class Recorder
    {
    public:
        explicit Recorder(AbstractTheme *prototype);
    };

    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // Typed param accessors
    // -------------------------------------------------------------------------
    // Convenience getters for the standard visual params defined by ThemeDefault.
    // All call paramValue() and fall back to a hard-coded default so that common
    // blocs can use them without knowing the concrete theme type.

    QString primaryColor()   const; ///< "primary_color"   default #1a73e8
    QString secondaryColor() const; ///< "secondary_color" default #fbbc04
    QString fontFamily()     const; ///< "font_family"     default sans-serif
    QString fontSizeBase()   const; ///< "font_size_base"  default 16px

    // -------------------------------------------------------------------------
    // Generation helpers
    // -------------------------------------------------------------------------

    /**
     * Calls addCode() on each bloc returned by getTopBlocs(), in order.
     * Intended to be called by AbstractPageType::addCode() before the page
     * blocs, so the header and top menu appear above the page body.
     *
     * origContent is forwarded as an empty QStringView — common blocs do not
     * use it (they render from their own member state).
     */
    void addCodeTop(AbstractEngine &engine,
                    int             websiteIndex,
                    QString        &html,
                    QString        &css,
                    QString        &js,
                    QSet<QString>  &cssDoneIds,
                    QSet<QString>  &jsDoneIds);

    /**
     * Calls addCode() on each bloc returned by getBottomBlocs(), in order.
     * Intended to be called by AbstractPageType::addCode() after the page
     * blocs, so the bottom menu and footer appear below the page body.
     */
    void addCodeBottom(AbstractEngine &engine,
                       int             websiteIndex,
                       QString        &html,
                       QString        &css,
                       QString        &js,
                       QSet<QString>  &cssDoneIds,
                       QSet<QString>  &jsDoneIds);

    // -------------------------------------------------------------------------

    QVariant paramValue(const QString &id) const;

    // QAbstractTableModel
    int           rowCount   (const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool          setData    (const QModelIndex &index, const QVariant &value,
                              int role = Qt::EditRole) override;
    Qt::ItemFlags flags      (const QModelIndex &index) const override;
    QVariant      headerData (int section, Qt::Orientation orientation,
                              int role = Qt::DisplayRole) const override;

protected:
    const QDir &workingDir() const;

private:
    QDir m_workingDir;

    static QMap<QString, const AbstractTheme *> s_themes;

    // Mutable to support lazy initialization from const methods.
    mutable QHash<QString, QVariant> m_values;
    mutable bool                     m_loaded = false;

    /**
     * Loads saved overrides from {workingDir}/{getId()}_params.ini.
     * No-op after the first call.  Safe to call from const methods.
     */
    void _ensureLoaded() const;

    /** Persists all current overrides to {workingDir}/{getId()}_params.ini. */
    void _saveValues() const;

    QString _settingsPath() const;
};

/**
 * Registers a theme prototype at static-initialization time.
 * Place once at file scope in the theme's .cpp file.
 *
 * Example:
 *   DECLARE_THEME(ThemeDefault)
 */
#define DECLARE_THEME(ClassName)                                        \
    static ClassName s_##ClassName##Prototype;                          \
    AbstractTheme::Recorder s_##ClassName##Recorder{&s_##ClassName##Prototype};

#endif // ABSTRACTTHEME_H
