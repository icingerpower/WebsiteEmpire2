#include "AbstractTheme.h"

#include "website/AbstractEngine.h"
#include "website/commonblocs/AbstractCommonBloc.h"

#include <QColor>
#include <QPixmap>
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
    case COL_VALUE: {
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return paramValue(param.id);
        }
        const bool isColor = param.id.contains(QStringLiteral("_color"));
        if (role == IsColorRole) {
            return isColor;
        }
        if (role == Qt::DecorationRole && isColor) {
            const QColor color = QColor::fromString(paramValue(param.id).toString());
            if (color.isValid()) {
                QPixmap pix(16, 16);
                pix.fill(color);
                return pix;
            }
        }
        break;
    }
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
    // Load source language code from theme params file.
    AbstractTheme *self = const_cast<AbstractTheme *>(this);
    self->m_sourceLangCode = settings.value(QStringLiteral("source_lang")).toString();
    // Load favicon filenames from the theme-independent website_favicon.ini.
    QSettings faviconSettings(_faviconSettingsPath(), QSettings::IniFormat);
    self->m_faviconSvg        = faviconSettings.value(QStringLiteral("favicon_svg")).toString();
    self->m_faviconIco        = faviconSettings.value(QStringLiteral("favicon_ico")).toString();
    self->m_faviconAppleTouch = faviconSettings.value(QStringLiteral("favicon_apple_touch")).toString();
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

QString AbstractTheme::_faviconSettingsPath() const
{
    return m_workingDir.filePath(QStringLiteral("website_favicon.ini"));
}

QString AbstractTheme::_blocsSettingsPath() const
{
    return m_workingDir.filePath(getId() + QStringLiteral("_blocs.ini"));
}

QList<AbstractCommonBloc *> AbstractTheme::getArticleBlocs()
{
    return {};
}

void AbstractTheme::saveBlocsData()
{
    QSettings settings(_blocsSettingsPath(), QSettings::IniFormat);
    const auto persist = [&settings](const QList<AbstractCommonBloc *> &blocs) {
        for (AbstractCommonBloc *bloc : blocs) {
            settings.beginGroup(bloc->getId());
            const QVariantMap map = bloc->toMap();
            for (auto it = map.cbegin(); it != map.cend(); ++it) {
                settings.setValue(it.key(), it.value());
            }
            bloc->saveTranslations(settings);
            settings.endGroup();
        }
    };
    persist(getTopBlocs());
    persist(getBottomBlocs());
    persist(getArticleBlocs());
}

void AbstractTheme::_loadBlocsData()
{
    QSettings settings(_blocsSettingsPath(), QSettings::IniFormat);
    const auto hydrate = [&settings](const QList<AbstractCommonBloc *> &blocs) {
        for (AbstractCommonBloc *bloc : blocs) {
            settings.beginGroup(bloc->getId());
            QVariantMap map;
            const auto &keys = settings.childKeys();
            for (const auto &key : keys) {
                map.insert(key, settings.value(key));
            }
            if (!map.isEmpty()) {
                bloc->fromMap(map); // sets sources in BlocTranslations
            }
            bloc->loadTranslations(settings); // reads tr_* subgroups
            settings.endGroup();
        }
    };
    hydrate(getTopBlocs());
    hydrate(getBottomBlocs());
    hydrate(getArticleBlocs());
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

QString AbstractTheme::fontUrl() const
{
    const QVariant v = paramValue(QStringLiteral("font_url"));
    return v.isValid() ? v.toString() : QString();
}

QString AbstractTheme::maxContentWidth() const
{
    const QVariant v = paramValue(QStringLiteral("max_content_width"));
    return v.isValid() ? v.toString() : QStringLiteral("860px");
}

QString AbstractTheme::bodyBgColor() const
{
    const QVariant v = paramValue(QStringLiteral("body_bg_color"));
    return v.isValid() ? v.toString() : QStringLiteral("#f4f6fb");
}

QString AbstractTheme::bodyTextColor() const
{
    const QVariant v = paramValue(QStringLiteral("body_text_color"));
    return v.isValid() ? v.toString() : QStringLiteral("#1f2937");
}

void AbstractTheme::setSourceLangCode(const QString &langCode)
{
    m_sourceLangCode = langCode;
    QSettings settings(_settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("source_lang"), langCode);
}

QString AbstractTheme::sourceLangCode() const
{
    _ensureLoaded();
    return m_sourceLangCode;
}

// ---- Favicon accessors -------------------------------------------------------

QString AbstractTheme::faviconSvg() const
{
    _ensureLoaded();
    return m_faviconSvg;
}

void AbstractTheme::setFaviconSvg(const QString &filename)
{
    m_faviconSvg = filename;
    QSettings settings(_faviconSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("favicon_svg"), filename);
}

QString AbstractTheme::faviconIco() const
{
    _ensureLoaded();
    return m_faviconIco;
}

void AbstractTheme::setFaviconIco(const QString &filename)
{
    m_faviconIco = filename;
    QSettings settings(_faviconSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("favicon_ico"), filename);
}

QString AbstractTheme::faviconAppleTouch() const
{
    _ensureLoaded();
    return m_faviconAppleTouch;
}

void AbstractTheme::setFaviconAppleTouch(const QString &filename)
{
    m_faviconAppleTouch = filename;
    QSettings settings(_faviconSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("favicon_apple_touch"), filename);
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

void AbstractTheme::addCodeArticle(AbstractEngine &engine,
                                    int             websiteIndex,
                                    QString        &html,
                                    QString        &css,
                                    QString        &js,
                                    QSet<QString>  &cssDoneIds,
                                    QSet<QString>  &jsDoneIds)
{
    for (AbstractCommonBloc *bloc : getArticleBlocs()) {
        bloc->addCode(QStringView{}, engine, websiteIndex,
                      html, css, js, cssDoneIds, jsDoneIds);
    }
}
