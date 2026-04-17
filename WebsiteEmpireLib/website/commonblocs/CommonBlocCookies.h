#ifndef COMMONBLOCCOOKIES_H
#define COMMONBLOCCOOKIES_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"

#include <QString>

/**
 * Common bloc that renders a fixed bottom cookies-consent bar.
 *
 * The bar is hidden by default (CSS display:none) and shown by JavaScript if
 * the browser cookie "cookies_accepted" is not set.  When the user clicks
 * Accept, the cookie is stored for 365 days and the bar is hidden.
 *
 * Both the consent message and the button label are configurable and fully
 * translatable.  The bloc does not render when both fields are empty.
 *
 * Placed last in ThemeDefault::getBottomBlocs() so it overlays the rest of
 * the page content.
 */
class CommonBlocCookies : public AbstractCommonBloc
{
public:
    static constexpr const char *ID              = "cookies";
    static constexpr const char *KEY_MESSAGE     = "message";
    static constexpr const char *KEY_BUTTON_TEXT = "buttonText";

    CommonBlocCookies() = default;
    ~CommonBlocCookies() override = default;

    QString          getId()           const override;
    QString          getName()         const override;
    QList<ScopeType> supportedScopes() const override;

    /**
     * Emits a fixed-bottom <div id="cookies-bar"> with a message paragraph
     * and an Accept button.  CSS is injected once per page via cssDoneIds.
     * JS (show/hide logic + cookie write) is injected once per page via
     * jsDoneIds.  origContent is unused.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractCommonBlocWidget *createEditWidget() override;

    QVariantMap toMap()              const override;
    void        fromMap(const QVariantMap &map) override;

    void          setMessage(const QString &message);
    const QString &message() const;

    void          setButtonText(const QString &text);
    const QString &buttonText() const;

    // ---- Translation overrides ----
    QHash<QString, QString> sourceTexts()                          const override;
    void        setTranslation(const QString &fieldId,
                               const QString &langCode,
                               const QString &translatedText)           override;
    QStringList missingTranslations(const QString &langCode,
                                    const QString &sourceLangCode) const override;
    void        saveTranslations(QSettings &settings)                    override;
    void        loadTranslations(QSettings &settings)                    override;
    QString     translatedText(const QString &fieldId,
                               const QString &langCode)            const override;

private:
    QString          m_message;
    QString          m_buttonText;
    BlocTranslations m_tr;
};

#endif // COMMONBLOCCOOKIES_H
