#ifndef ABSTRACTENGINE_H
#define ABSTRACTENGINE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class HostTable;

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

    // Returns a freshly heap-allocated instance of this engine type.
    // The caller owns the returned object.  init() must still be called
    // before the instance is usable.
    virtual AbstractEngine    *create(QObject *parent = nullptr) const = 0;

    // Returns all registered engine prototypes keyed by getId().
    static const QMap<QString, const AbstractEngine *> &ALL_ENGINES();

    // Used by DECLARE_ENGINE to register a prototype at startup.
    class Recorder
    {
    public:
        explicit Recorder(AbstractEngine *engine);
    };

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
    const HostTable *m_hostTable = nullptr;
    QList<DomainRow> m_rows;

    static const QMap<QString, const AbstractEngine *> &getEngines();
    static QMap<QString, const AbstractEngine *> s_engines;
};

// Registers an engine prototype at static-initialization time.
#define DECLARE_ENGINE(NEW_CLASS)                                   \
    NEW_CLASS instance##NEW_CLASS;                                  \
    AbstractEngine::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTENGINE_H
