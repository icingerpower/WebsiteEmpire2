#include "CommonBlocPageLabels.h"

#include "ExceptionWithTitleText.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

CommonBlocPageLabels::CommonBlocPageLabels()
{
    const auto &sources = sourceTexts();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        m_tr.setSource(it.key(), it.value());
    }
}

QHash<QString, QString> CommonBlocPageLabels::sourceTexts() const
{
    // Translation source data must stay English regardless of the editor locale.
    return {{QLatin1String(POSSIBLE_CONDITIONS), QStringLiteral("Possible conditions")},
            {QLatin1String(ALL_SYMPTOMS), QStringLiteral("All symptoms")},
            {QLatin1String(BROWSE_CATEGORIES), QStringLiteral("Browse all categories")},
            {QLatin1String(BROWSE_SYMPTOMS), QStringLiteral("Browse conditions by symptom")},
            {QLatin1String(BROWSE_SYMPTOMS_COUNT),
             QStringLiteral("Browse conditions by symptom — %1 symptoms")}};
}

QString CommonBlocPageLabels::getId() const
{
    return QStringLiteral("page_labels");
}

QString CommonBlocPageLabels::getName() const
{
    return QCoreApplication::translate("CommonBlocPageLabels", "Page labels");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocPageLabels::supportedScopes() const
{
    return {ScopeType::Global};
}

AbstractCommonBlocWidget *CommonBlocPageLabels::createEditWidget()
{
    return nullptr;
}

QVariantMap CommonBlocPageLabels::toMap() const
{
    return {};
}

void CommonBlocPageLabels::fromMap(const QVariantMap &)
{
}

void CommonBlocPageLabels::addCode(QStringView, AbstractEngine &, int, QString &,
                                  QString &, QString &, QSet<QString> &, QSet<QString> &) const
{
}

QString CommonBlocPageLabels::translationSourceLang(const QString &) const
{
    return QStringLiteral("en");
}

void CommonBlocPageLabels::setTranslation(const QString &fieldId, const QString &lang,
                                         const QString &text)
{
    if (fieldId == QLatin1String(BROWSE_SYMPTOMS_COUNT) && !text.isEmpty()
        && text.count(QStringLiteral("%1")) != 1) {
        ExceptionWithTitleText ex(
            QCoreApplication::translate("CommonBlocPageLabels", "Invalid page label translation"),
            QCoreApplication::translate("CommonBlocPageLabels",
                                        "The symptom count translation must preserve %1."));
        ex.raise();
    }
    m_tr.setTranslation(fieldId, lang, text);
}

QString CommonBlocPageLabels::translatedText(const QString &fieldId, const QString &lang) const
{
    return m_tr.translation(fieldId, lang);
}

QStringList CommonBlocPageLabels::missingTranslations(const QString &lang, const QString &) const
{
    if (lang == QStringLiteral("en")) {
        return {};
    }
    QStringList missing;
    const auto &sources = sourceTexts();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        const auto &translated = m_tr.translation(it.key(), lang);
        if (translated.trimmed().isEmpty()
            || (it.key() == QLatin1String(BROWSE_SYMPTOMS_COUNT)
                && translated.count(QStringLiteral("%1")) != 1)) {
            missing.append(it.key());
        }
    }
    return missing;
}

void CommonBlocPageLabels::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocPageLabels::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}

void CommonBlocPageLabels::load(const QDir &workingDir)
{
    m_tr = BlocTranslations();
    const auto &sources = sourceTexts();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        m_tr.setSource(it.key(), it.value());
    }
    QSettings settings(workingDir.filePath(QStringLiteral("page_labels.ini")), QSettings::IniFormat);
    loadTranslations(settings);
}

void CommonBlocPageLabels::save(const QDir &workingDir)
{
    QSettings settings(workingDir.filePath(QStringLiteral("page_labels.ini")), QSettings::IniFormat);
    saveTranslations(settings);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        ExceptionWithTitleText ex(
            QCoreApplication::translate("CommonBlocPageLabels", "Page label save failed"),
            QCoreApplication::translate("CommonBlocPageLabels", "Could not save %1.")
                .arg(settings.fileName()));
        ex.raise();
    }
}

QString CommonBlocPageLabels::text(const QString &fieldId, const QString &lang) const
{
    if (lang == QStringLiteral("en")) {
        return m_tr.source(fieldId);
    }
    if (missingTranslations(lang, QStringLiteral("en")).contains(fieldId)) {
        assertTranslated(lang, QStringLiteral("en"));
    }
    return m_tr.translation(fieldId, lang);
}
