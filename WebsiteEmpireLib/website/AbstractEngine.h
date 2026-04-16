#ifndef ABSTRACTENGINE_H
#define ABSTRACTENGINE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QMap>
#include <QScopedPointer>
#include <QString>
#include <QStringList>

class AbstractPageType;
class AbstractTheme;
class CategoryTable;
class HostTable;
class PageTypeArticle;
class PageTypeLegal;

// Base class for website-building engines.
//
// Manages a list of domain configurations (lang code, language, theme, domain,
// host, host folder) persisted to engine_domains.csv in the working directory.
//
// Lifecycle:
//   1. Subclasses self-register via DECLARE_ENGINE() — the default constructor
//      is called at static-init time and the instance is stored in ALL_ENGINES().
//   2. When the user selects an engine, call init() to bind it to a working
//      directory and host table.  init() loads existing data from disk.
//
// All visible mutations go through setData(); _save() is called automatically.
class AbstractEngine : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_LANG_CODE   = 0;
    static constexpr int COL_LANGUAGE    = 1;
    static constexpr int COL_THEME       = 2;
    static constexpr int COL_DOMAIN      = 3;
    static constexpr int COL_HOST        = 4;
    static constexpr int COL_HOST_FOLDER = 5;

    explicit AbstractEngine(QObject *parent = nullptr);
    // Destructor is defined in the .cpp so that QScopedPointer members below can
    // work with forward-declared (incomplete) types.
    ~AbstractEngine() override;

    // Stable id + translatable display name for one engine variation.
    // id   — persisted to disk; must never change once data exists.
    // name — human-readable label wrapped in tr(); safe to rename at any time.
    struct Variation {
        QString id;
        QString name;
    };

    // Subclass identity
    virtual QString            getId()         const = 0;
    virtual QString            getName()       const = 0;
    virtual QList<Variation>   getVariations() const = 0;

    // Returns the stable id of the AbstractGenerator that produces the source
    // data for this engine (must match AbstractGenerator::getId()).
    // Default: empty string — engine is general-purpose / no single generator.
    virtual QString            getGeneratorId() const;

    // Returns a freshly heap-allocated instance of this engine type.
    // The caller owns the returned object.  init() must still be called
    // before the instance is usable.
    virtual AbstractEngine    *create(QObject *parent = nullptr) const = 0;

    // Returns the page types that compose a page in this engine.
    // The default implementation returns Article + Legal, backed by a shared
    // CategoryTable created in _onInit().  The list is empty before init() is
    // called; after init() it contains at least one entry.
    // Overrides must store the list as a member and return a const reference
    // so that callers pay no allocation cost.  If you override _onInit() you
    // must also override getPageTypes() (base _onInit() will not be called).
    virtual const QList<const AbstractPageType *> &getPageTypes() const;

    // Returns all registered engine prototypes keyed by getId().
    static const QMap<QString, const AbstractEngine *> &ALL_ENGINES();

    // Used by DECLARE_ENGINE to register a prototype at startup.
    class Recorder
    {
    public:
        explicit Recorder(AbstractEngine *engine);
    };

    // Returns the lang code for the given website index, or an empty string if
    // the index is out of range (e.g. before init() populates the rows).
    QString getLangCode(int websiteIndex) const;

    /**
     * Sets the active visual theme for this engine.
     * The engine does not take ownership — the caller is responsible for the
     * lifetime of the theme object (typically owned by MainWindow).
     * Must be called before generateAll() / AbstractPageType::addCode().
     */
    void setTheme(AbstractTheme *theme);

    /**
     * Returns the active visual theme, or nullptr if none has been set.
     * Used by AbstractPageType::addCode() to invoke addCodeTop/addCodeBottom.
     */
    AbstractTheme *getActiveTheme() const;

    // Binds this engine to workingDir and hostTable, then loads engine_domains.csv.
    // Must be called before using the model.
    void init(const QDir &workingDir, const HostTable &hostTable);

    // Returns the list of host names from HostTable for the Host column combo box.
    // Returns an empty list if init() has not been called or HostTable has no rows.
    QStringList availableHostNames() const;

    // QAbstractTableModel
    int           rowCount  (const QModelIndex &parent = QModelIndex()) const override;
    int           columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant      data      (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    bool          setData   (const QModelIndex &index, const QVariant &value,
                             int role = Qt::EditRole) override;
    Qt::ItemFlags flags     (const QModelIndex &index) const override;

protected:
    // Called at the end of init() with the resolved working directory.
    // Subclasses override this to (re)create page types that depend on the
    // working directory (e.g. a CategoryTable backed by that directory).
    // The default implementation is a no-op.
    virtual void _onInit(const QDir &workingDir);

private:
    struct DomainRow {
        QString langCode;
        QString language;
        QString theme;
        QString domain;
        QString hostId;      // references HostTable::idForRow() — empty = no host
        QString hostFolder;
        bool    enabled = true;
    };

    void    _load();
    void    _save();
    // Removes rows outside the current (lang × theme) matrix and appends
    // missing (lang, theme) pairs.  Must be called inside beginResetModel/
    // endResetModel.  Returns true if m_rows was modified.
    bool    _reconcileRows();
    QString _hostNameForId(const QString &hostId) const;
    QString _hostIdForName(const QString &name)   const;

    QDir             m_workingDir;
    const HostTable *m_hostTable  = nullptr;
    AbstractTheme   *m_theme      = nullptr; ///< Not owned; set by setTheme()
    QList<DomainRow> m_rows;

    // Backing storage for the default getPageTypes() implementation.
    // Populated by the base _onInit(); unused when a subclass overrides both
    // _onInit() and getPageTypes().
    QScopedPointer<CategoryTable>   m_defaultCategoryTable;
    QScopedPointer<PageTypeArticle> m_defaultArticleType;
    QScopedPointer<PageTypeLegal>   m_defaultLegalType;
    QList<const AbstractPageType *> m_defaultPageTypes;

    static const QMap<QString, const AbstractEngine *> &getEngines();
    static QMap<QString, const AbstractEngine *> s_engines;
};

// Registers an engine prototype at static-initialization time.
#define DECLARE_ENGINE(NEW_CLASS)                                   \
    NEW_CLASS instance##NEW_CLASS;                                  \
    AbstractEngine::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTENGINE_H
