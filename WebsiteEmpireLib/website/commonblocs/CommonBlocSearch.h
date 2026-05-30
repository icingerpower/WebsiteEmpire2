#ifndef COMMONBLOCSEARCH_H
#define COMMONBLOCSEARCH_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"

#include <QString>

/**
 * Common bloc that renders a search bar above the top menu.
 *
 * Generates a standard HTML <form> that submits a "q" query parameter to a
 * configurable action URL.  Typical actions: a Google Custom Search URL, or a
 * site-local /search page that runs client-side JS search.
 *
 * The placeholder text is fully translatable via BlocTranslations.  The
 * primary colour from the active theme is applied via a CSS custom property
 * (--search-primary) so the static CSS block never needs regeneration when
 * the colour changes — only the inline style on the wrapper <div> does.
 *
 * The bloc is silent (renders nothing) when action is empty.
 */
class CommonBlocSearch : public AbstractCommonBloc
{
public:
    static constexpr const char *ID              = "search";
    static constexpr const char *KEY_ACTION      = "action";
    static constexpr const char *KEY_PLACEHOLDER = "placeholder";

    CommonBlocSearch() = default;
    ~CommonBlocSearch() override = default;

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

    void          setAction(const QString &action);
    const QString &action() const;
    void          setPlaceholder(const QString &placeholder);
    const QString &placeholder() const;

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
    QString          m_action;
    QString          m_placeholder;
    BlocTranslations m_tr;
};

#endif // COMMONBLOCSEARCH_H
