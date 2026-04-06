#include "AbstractTheme.h"

#include "website/commonblocs/AbstractCommonBloc.h"

#include <QSet>
#include <QSettings>
#include <QString>

// Static registry storage
QMap<QString, const AbstractTheme *> AbstractTheme::s_themes;

AbstractTheme::AbstractTheme()
    : QAbstractTableModel(nullptr)
{}

AbstractTheme::AbstractTheme(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{}

// static
QString AbstractTheme::settingsKey()
{
    return QStringLiteral("themeId");
}

// static
const QMap<QString, const AbstractTheme *> &AbstractTheme::ALL_THEMES()
{
    return s_themes;
}

AbstractTheme::Recorder::Recorder(AbstractTheme *prototype)
{
    AbstractTheme::s_themes.insert(prototype->getId(), prototype);
}

QVariant AbstractTheme::paramValue(const QString &id) const
{
    _ensureLoaded();
    if (m_values.contains(id)) {
        return m_values.value(id);
    }
    const auto &params = getParams();
    for (const auto &param : std::as_const(params)) {
        if (param.id == id) {
            return param.defaultValue;
        }
    }
    return {};
}

int AbstractTheme::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    _ensureLoaded();
    return getParams().size();
}

int AbstractTheme::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return 2;
}

QVariant AbstractTheme::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    _ensureLoaded();
    const auto &params = getParams();
    if (index.row() < 0 || index.row() >= params.size()) {
        return {};
    }
    const auto &param = params.at(index.row());

    switch (index.column()) {
    case COL_NAME:
        if (role == Qt::DisplayRole) { return param.name; }
        if (role == Qt::ToolTipRole) { return param.tooltip; }
        break;
    case COL_VALUE:
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return paramValue(param.id);
        }
        break;
    default:
        break;
    }
    return {};
}

bool AbstractTheme::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != COL_VALUE) {
        return false;
    }
    _ensureLoaded();
    const auto &params = getParams();
    if (index.row() < 0 || index.row() >= params.size()) {
        return false;
    }
    const auto &param = params.at(index.row());
    m_values.insert(param.id, value);
    _saveValues();
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

Qt::ItemFlags AbstractTheme::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == COL_VALUE) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

QVariant AbstractTheme::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case COL_NAME:  return tr("Parameter");
    case COL_VALUE: return tr("Value");
    default:        return {};
    }
}

const QDir &AbstractTheme::workingDir() const
{
    return m_workingDir;
}

void AbstractTheme::_ensureLoaded() const
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    QSettings settings(_settingsPath(), QSettings::IniFormat);
    const auto &params = getParams();
    for (const auto &param : std::as_const(params)) {
        if (settings.contains(param.id)) {
            m_values.insert(param.id, settings.value(param.id));
        }
    }
}

void AbstractTheme::_saveValues() const
{
    QSettings settings(_settingsPath(), QSettings::IniFormat);
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
}

QString AbstractTheme::_settingsPath() const
{
    return m_workingDir.filePath(getId() + QStringLiteral("_params.ini"));
}

QString AbstractTheme::primaryColor() const
{
    const QVariant v = paramValue(QStringLiteral("primary_color"));
    return v.isValid() ? v.toString() : QStringLiteral("#1a73e8");
}

QString AbstractTheme::secondaryColor() const
{
    const QVariant v = paramValue(QStringLiteral("secondary_color"));
    return v.isValid() ? v.toString() : QStringLiteral("#fbbc04");
}

QString AbstractTheme::fontFamily() const
{
    const QVariant v = paramValue(QStringLiteral("font_family"));
    return v.isValid() ? v.toString() : QStringLiteral("sans-serif");
}

QString AbstractTheme::fontSizeBase() const
{
    const QVariant v = paramValue(QStringLiteral("font_size_base"));
    return v.isValid() ? v.toString() : QStringLiteral("16px");
}

void AbstractTheme::addCodeTop(AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds)
{
    for (AbstractCommonBloc *bloc : getTopBlocs()) {
        bloc->addCode(QStringView{}, engine, websiteIndex,
                      html, css, js, cssDoneIds, jsDoneIds);
    }
}

void AbstractTheme::addCodeBottom(AbstractEngine &engine,
                                   int             websiteIndex,
                                   QString        &html,
                                   QString        &css,
                                   QString        &js,
                                   QSet<QString>  &cssDoneIds,
                                   QSet<QString>  &jsDoneIds)
{
    for (AbstractCommonBloc *bloc : getBottomBlocs()) {
        bloc->addCode(QStringView{}, engine, websiteIndex,
                      html, css, js, cssDoneIds, jsDoneIds);
    }
}
