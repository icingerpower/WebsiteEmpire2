#include "AbstractEngine.h"
#include "HostTable.h"
#include "CountryLangManager.h"
#include "website/pages/PageTypeArticle.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QFile>
#include <QLocale>
#include <QSet>
#include <QTextStream>

static constexpr int COLUMN_COUNT = 6;
static const QString CSV_FILE   = QStringLiteral("engine_domains.csv");
static const QString CSV_HEADER = QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder");

// ---- Constructor / Destructor -----------------------------------------------

AbstractEngine::AbstractEngine(QObject *parent)
    : QAbstractTableModel(parent)
{
}

AbstractEngine::~AbstractEngine() = default;

// ---- Subclass identity defaults ---------------------------------------------

QString AbstractEngine::getGeneratorId() const
{
    return {};
}

// ---- Registry ---------------------------------------------------------------

QMap<QString, const AbstractEngine *> AbstractEngine::s_engines;

AbstractEngine::Recorder::Recorder(AbstractEngine *engine)
{
    s_engines.insert(engine->getId(), engine);
}

const QMap<QString, const AbstractEngine *> &AbstractEngine::ALL_ENGINES()
{
    return getEngines();
}

const QMap<QString, const AbstractEngine *> &AbstractEngine::getEngines()
{
    return s_engines;
}

// ---- Public API — getLangCode / theme ---------------------------------------

QString AbstractEngine::getLangCode(int websiteIndex) const
{
    if (websiteIndex < 0 || websiteIndex >= m_rows.size()) {
        return {};
    }
    return m_rows.at(websiteIndex).langCode;
}

void AbstractEngine::setTheme(AbstractTheme *theme)
{
    m_theme = theme;
}

AbstractTheme *AbstractEngine::getActiveTheme() const
{
    return m_theme;
}

// ---- Init -------------------------------------------------------------------

void AbstractEngine::init(const QDir &workingDir, const HostTable &hostTable)
{
    m_workingDir = workingDir;
    m_hostTable  = &hostTable;
    _load();
    _onInit(workingDir);
}

void AbstractEngine::_onInit(const QDir &workingDir)
{
    // Default: build the standard Article + Legal composition.
    // Release in reverse dependency order: types hold a ref to the table.
    m_defaultLegalType.reset();
    m_defaultArticleType.reset();
    m_defaultCategoryTable.reset(new CategoryTable(workingDir));
    m_defaultArticleType.reset(new PageTypeArticle(*m_defaultCategoryTable));
    m_defaultLegalType.reset(new PageTypeLegal(*m_defaultCategoryTable));
    m_defaultPageTypes.clear();
    m_defaultPageTypes.append(m_defaultArticleType.data());
    m_defaultPageTypes.append(m_defaultLegalType.data());
}

const QList<const AbstractPageType *> &AbstractEngine::getPageTypes() const
{
    return m_defaultPageTypes;
}

// ---- Public API -------------------------------------------------------------

QStringList AbstractEngine::availableHostNames() const
{
    if (!m_hostTable) {
        return {};
    }
    QStringList names;
    const int count = m_hostTable->rowCount();
    for (int i = 0; i < count; ++i) {
        names << m_hostTable->data(m_hostTable->index(i, HostTable::COL_NAME)).toString();
    }
    return names;
}

// ---- QAbstractTableModel ----------------------------------------------------

int AbstractEngine::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int AbstractEngine::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COLUMN_COUNT;
}

QVariant AbstractEngine::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto &row = m_rows.at(index.row());

    if (index.column() == COL_LANG_CODE && role == Qt::CheckStateRole) {
        return row.enabled ? Qt::Checked : Qt::Unchecked;
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_LANG_CODE:   return row.langCode;
        case COL_LANGUAGE:    return row.language;
        case COL_THEME:       return row.theme;
        case COL_DOMAIN:      return row.domain;
        case COL_HOST:        return _hostNameForId(row.hostId);
        case COL_HOST_FOLDER: return row.hostFolder;
        default:              break;
        }
    }

    return {};
}

QVariant AbstractEngine::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
            case COL_LANG_CODE:   return tr("Lang code");
            case COL_LANGUAGE:    return tr("Language");
            case COL_THEME:       return tr("Theme");
            case COL_DOMAIN:      return tr("Domain");
            case COL_HOST:        return tr("Host");
            case COL_HOST_FOLDER: return tr("Host folder");
            default:              return {};
            }
        } else {
            return QString::number(section + 1);

        }
    }
    return QVariant{};
}

bool AbstractEngine::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return false;
    }

    auto &row = m_rows[index.row()];

    if (index.column() == COL_LANG_CODE && role == Qt::CheckStateRole) {
        const bool checked = (value.toInt() == Qt::Checked);
        if (row.enabled == checked) {
            return false;
        }
        row.enabled = checked;
        _save();
        emit dataChanged(index, index, {role});
        return true;
    }

    if (role != Qt::EditRole) {
        return false;
    }

    const QString str = value.toString();

    switch (index.column()) {
    case COL_LANG_CODE:
        if (row.langCode   == str) { return false; } row.langCode   = str; break;
    case COL_LANGUAGE:
        if (row.language   == str) { return false; } row.language   = str; break;
    case COL_THEME:
        if (row.theme      == str) { return false; } row.theme      = str; break;
    case COL_DOMAIN:
        if (row.domain     == str) { return false; } row.domain     = str; break;
    case COL_HOST: {
        const QString id = _hostIdForName(str);
        if (row.hostId     == id)  { return false; } row.hostId     = id;  break;
    }
    case COL_HOST_FOLDER:
        if (row.hostFolder == str) { return false; } row.hostFolder = str; break;
    default:
        return false;
    }

    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags AbstractEngine::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == COL_LANG_CODE) {
        f |= Qt::ItemIsUserCheckable;
    }
    if (index.column() >= COL_DOMAIN) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

// ---- Persistence ------------------------------------------------------------

void AbstractEngine::_load()
{
    beginResetModel();
    m_rows.clear();

    QFile file(m_workingDir.absoluteFilePath(CSV_FILE));
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        const QString headerLine = in.readLine();
        const QStringList headers = headerLine.split(';');

        QMap<QString, int> col;
        for (int i = 0; i < headers.size(); ++i) {
            col.insert(headers.at(i).trimmed(), i);
        }

        const int idxEnabled    = col.value(QStringLiteral("Enabled"),    -1);
        const int idxLangCode   = col.value(QStringLiteral("LangCode"),   -1);
        const int idxLanguage   = col.value(QStringLiteral("Language"),   -1);
        const int idxTheme      = col.value(QStringLiteral("Theme"),      -1);
        const int idxDomain     = col.value(QStringLiteral("Domain"),     -1);
        const int idxHostId     = col.value(QStringLiteral("HostId"),     -1);
        const int idxHostFolder = col.value(QStringLiteral("HostFolder"), -1);

        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.trimmed().isEmpty()) {
                continue;
            }
            const QStringList parts = line.split(';');

            DomainRow row;
            if (idxEnabled    >= 0 && idxEnabled    < parts.size()) {
                row.enabled    = parts.at(idxEnabled) != QLatin1String("0");
            }
            if (idxLangCode   >= 0 && idxLangCode   < parts.size()) { row.langCode   = parts.at(idxLangCode); }
            if (idxLanguage   >= 0 && idxLanguage   < parts.size()) { row.language   = parts.at(idxLanguage); }
            if (idxTheme      >= 0 && idxTheme      < parts.size()) { row.theme      = parts.at(idxTheme); }
            if (idxDomain     >= 0 && idxDomain     < parts.size()) { row.domain     = parts.at(idxDomain); }
            if (idxHostId     >= 0 && idxHostId     < parts.size()) { row.hostId     = parts.at(idxHostId); }
            if (idxHostFolder >= 0 && idxHostFolder < parts.size()) { row.hostFolder = parts.at(idxHostFolder); }
            m_rows.append(row);
        }
    }

    const bool changed = _reconcileRows();
    endResetModel();

    if (changed) {
        _save();
    }
}

void AbstractEngine::_save()
{
    QFile file(m_workingDir.absoluteFilePath(CSV_FILE));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AbstractEngine: failed to save" << file.fileName()
                   << ":" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << CSV_HEADER << '\n';

    for (const auto &row : std::as_const(m_rows)) {
        QStringList fields;
        fields << (row.enabled ? QStringLiteral("1") : QStringLiteral("0"))
               << row.langCode
               << row.language
               << row.theme
               << row.domain
               << row.hostId
               << row.hostFolder;
        out << fields.join(';') << '\n';
    }
}

// ---- Reconciliation ---------------------------------------------------------

bool AbstractEngine::_reconcileRows()
{
    const QStringList langCodes = CountryLangManager::instance()->defaultLangCodes();
    const QList<Variation> variations = getVariations();
    if (langCodes.isEmpty() || variations.isEmpty()) {
        return false;
    }

    QSet<QString> validLangs(langCodes.cbegin(), langCodes.cend());
    QSet<QString> validThemes;
    for (const auto &v : variations) {
        validThemes.insert(v.id);
    }

    bool changed = false;

    // Remove rows outside the expected (lang × theme) matrix
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        const auto &row = m_rows.at(i);
        if (!validLangs.contains(row.langCode) || !validThemes.contains(row.theme)) {
            m_rows.removeAt(i);
            changed = true;
        }
    }

    // Build set of already-present (lang|theme) keys
    QSet<QString> existing;
    existing.reserve(m_rows.size());
    for (const auto &row : std::as_const(m_rows)) {
        existing.insert(row.langCode + QLatin1Char('|') + row.theme);
    }

    // Append missing (lang, theme) combinations
    for (const auto &code : langCodes) {
        const QLocale locale(code);
        const QString langName = QLocale::languageToString(locale.language());
        for (const auto &v : variations) {
            const QString key = code + QLatin1Char('|') + v.id;
            if (!existing.contains(key)) {
                DomainRow row;
                row.langCode = code;
                row.language = langName;
                row.theme    = v.id;
                row.enabled  = (code != v.id);
                m_rows.append(row);
                changed = true;
            }
        }
    }

    return changed;
}

// ---- Host helpers -----------------------------------------------------------

QString AbstractEngine::_hostNameForId(const QString &hostId) const
{
    if (!m_hostTable || hostId.isEmpty()) {
        return {};
    }
    const int count = m_hostTable->rowCount();
    for (int i = 0; i < count; ++i) {
        if (m_hostTable->idForRow(i) == hostId) {
            return m_hostTable->data(m_hostTable->index(i, HostTable::COL_NAME)).toString();
        }
    }
    return {};
}

QString AbstractEngine::_hostIdForName(const QString &name) const
{
    if (!m_hostTable || name.isEmpty()) {
        return {};
    }
    const int count = m_hostTable->rowCount();
    for (int i = 0; i < count; ++i) {
        if (m_hostTable->data(m_hostTable->index(i, HostTable::COL_NAME)).toString() == name) {
            return m_hostTable->idForRow(i);
        }
    }
    return {};
}
