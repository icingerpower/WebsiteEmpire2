#ifndef COMMONBLOCCOOKIES_H
#define COMMONBLOCCOOKIES_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"

#include <QString>

/**
 * Common bloc that renders a fixed bottom cookies-consent bar.
 *
 * Shows two buttons: Accept (sets cookies_accepted=1 for 365 days) and
 * Reject (sets cookies_accepted=0 for 30 days, then optionally redirects
 * to m_rejectUrl for a cookie management page).  The bar is hidden once
 * either decision is stored in the browser cookie.
 *
 * m_message, m_buttonText (Accept label), and m_rejectText (Reject label)
 * are translatable.  m_rejectUrl is not translatable (URLs don't change
 * per language).  The bloc does not render when m_message is empty.
 */
class CommonBlocCookies : public AbstractCommonBloc
{
public:
    static constexpr const char *ID               = "cookies";
    static constexpr const char *KEY_MESSAGE      = "message";
    static constexpr const char *KEY_BUTTON_TEXT  = "buttonText";
    static constexpr const char *KEY_REJECT_TEXT  = "rejectText";
    static constexpr const char *KEY_REJECT_URL   = "rejectUrl";

    CommonBlocCookies() = default;
    ~CommonBlocCookies() override = default;

    QString          getId()           const override;
    QString          getName()         const override;
    QList<ScopeType> supportedScopes() const override;

    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractCommonBlocWidget *createEditWidget() override;

    QVariantMap toMap()                    const override;
    void        fromMap(const QVariantMap &map)  override;

    void          setMessage(const QString &message);
    const QString &message()    const;

    void          setButtonText(const QString &text);
    const QString &buttonText() const;

    void          setRejectText(const QString &text);
    const QString &rejectText() const;

    void          setRejectUrl(const QString &url);
    const QString &rejectUrl()  const;

    // ---- Translation overrides ----
    QHash<QString, QString> sourceTexts()                                const override;
    void        setTranslation(const QString &fieldId,
                               const QString &langCode,
                               const QString &translatedText)                  override;
    QStringList missingTranslations(const QString &langCode,
                                    const QString &sourceLangCode)       const override;
    void        saveTranslations(QSettings &settings)                          override;
    void        loadTranslations(QSettings &settings)                          override;
    QString     translatedText(const QString &fieldId,
                               const QString &langCode)                  const override;

private:
    QString          m_message;
    QString          m_buttonText;
    QString          m_rejectText;
    QString          m_rejectUrl;
    BlocTranslations m_tr;
};

#endif // COMMONBLOCCOOKIES_H
