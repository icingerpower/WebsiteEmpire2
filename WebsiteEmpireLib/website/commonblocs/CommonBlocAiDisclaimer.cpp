#include "CommonBlocAiDisclaimer.h"
#include "website/commonblocs/widgets/WidgetCommonBlocAiDisclaimer.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

QString CommonBlocAiDisclaimer::getId() const
{
    return QStringLiteral("ai_disclaimer");
}

QString CommonBlocAiDisclaimer::getName() const
{
    return QCoreApplication::translate("CommonBlocAiDisclaimer", "AI Disclaimer");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocAiDisclaimer::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocAiDisclaimer::addCode(QStringView     origContent,
                                      AbstractEngine &engine,
                                      int             websiteIndex,
                                      QString        &html,
                                      QString        &css,
                                      QString        &js,
                                      QSet<QString>  &cssDoneIds,
                                      QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(origContent)
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_text.isEmpty()) {
        return;
    }

    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();
    const bool useTranslation = !lang.isEmpty() && lang != sourceLang;

    const auto resolve = [&](const char *key, const QString &source) -> const QString & {
        if (!useTranslation) {
            return source;
        }
        const QString &t = m_tr.translation(QLatin1String(key), lang);
        return t.isEmpty() ? source : t;
    };

    const QString &display = resolve(KEY_TEXT, m_text);

    if (!cssDoneIds.contains(QStringLiteral("ai_disclaimer"))) {
        cssDoneIds.insert(QStringLiteral("ai_disclaimer"));
        css += QStringLiteral(
            ".ai-disclaimer{"
              "margin-top:2rem;padding:.75rem 1rem;"
              "border-left:3px solid #ccc;"
              "font-size:.8rem;color:#888;"
              "font-style:italic}");
    }

    html += QStringLiteral("<p class=\"ai-disclaimer\"><strong>");
    html += display.toHtmlEscaped();
    html += QStringLiteral("</strong></p>");
}

AbstractCommonBlocWidget *CommonBlocAiDisclaimer::createEditWidget()
{
    return new WidgetCommonBlocAiDisclaimer();
}

QVariantMap CommonBlocAiDisclaimer::toMap() const
{
    return {{QLatin1String(KEY_TEXT), m_text}};
}

void CommonBlocAiDisclaimer::fromMap(const QVariantMap &map)
{
    m_text = map.value(QLatin1String(KEY_TEXT)).toString();
    m_tr.setSource(QLatin1String(KEY_TEXT), m_text);
}

void CommonBlocAiDisclaimer::setText(const QString &text)
{
    m_text = text;
    m_tr.setSource(QLatin1String(KEY_TEXT), text);
}

const QString &CommonBlocAiDisclaimer::text() const { return m_text; }

QHash<QString, QString> CommonBlocAiDisclaimer::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_text.isEmpty()) {
        result.insert(QLatin1String(KEY_TEXT), m_text);
    }
    return result;
}

void CommonBlocAiDisclaimer::setTranslation(const QString &fieldId,
                                              const QString &langCode,
                                              const QString &translatedText)
{
    m_tr.setTranslation(fieldId, langCode, translatedText);
}

QStringList CommonBlocAiDisclaimer::missingTranslations(const QString &langCode,
                                                          const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    return m_tr.missingFields(langCode);
}

void CommonBlocAiDisclaimer::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocAiDisclaimer::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}

QString CommonBlocAiDisclaimer::translatedText(const QString &fieldId,
                                                 const QString &langCode) const
{
    return m_tr.translation(fieldId, langCode);
}
