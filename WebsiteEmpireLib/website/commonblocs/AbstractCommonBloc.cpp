#include "AbstractCommonBloc.h"

#include "ExceptionWithTitleText.h"

#include <QCoreApplication>
#include <QSettings>

QHash<QString, QString> AbstractCommonBloc::sourceTexts() const
{
    return {};
}

void AbstractCommonBloc::setTranslation(const QString &fieldId,
                                        const QString &langCode,
                                        const QString &translatedText)
{
    Q_UNUSED(fieldId)
    Q_UNUSED(langCode)
    Q_UNUSED(translatedText)
}

QStringList AbstractCommonBloc::missingTranslations(const QString &langCode,
                                                    const QString &sourceLangCode) const
{
    Q_UNUSED(langCode)
    Q_UNUSED(sourceLangCode)
    return {};
}

void AbstractCommonBloc::assertTranslated(const QString &langCode,
                                          const QString &sourceLangCode) const
{
    const QStringList missing = missingTranslations(langCode, sourceLangCode);
    if (!missing.isEmpty()) {
        ExceptionWithTitleText ex(
            QCoreApplication::translate("AbstractCommonBloc", "Missing bloc translation"),
            QCoreApplication::translate("AbstractCommonBloc",
                                        "Bloc \"%1\" is missing translations for language \"%2\": %3")
                .arg(getName(), langCode, missing.join(QStringLiteral(", "))));
        ex.raise();
    }
}

void AbstractCommonBloc::saveTranslations(QSettings &settings)
{
    Q_UNUSED(settings)
}

void AbstractCommonBloc::loadTranslations(QSettings &settings)
{
    Q_UNUSED(settings)
}
