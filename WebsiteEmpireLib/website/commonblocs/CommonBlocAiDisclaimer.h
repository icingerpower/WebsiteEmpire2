#ifndef COMMONBLOCAIDISCLAIMER_H
#define COMMONBLOCAIDISCLAIMER_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"

#include <QString>

/**
 * Common bloc that renders a short disclaimer at the bottom of article pages.
 *
 * Unlike top/bottom blocs rendered on every page, this bloc is injected only
 * on article pages via AbstractTheme::addCodeArticle(), which is called from
 * PageTypeArticle::addInnerBottomCode() just before </main>.
 *
 * The text field is fully translatable via BlocTranslations (persisted in
 * default_blocs.ini under group "ai_disclaimer").  Re-running common translation
 * is sufficient to update all language variants — no per-article edits needed.
 *
 * The bloc is silent (renders nothing) when text is empty.
 */
class CommonBlocAiDisclaimer : public AbstractCommonBloc
{
public:
    static constexpr const char *ID       = "ai_disclaimer";
    static constexpr const char *KEY_TEXT = "text";

    CommonBlocAiDisclaimer() = default;
    ~CommonBlocAiDisclaimer() override = default;

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

    void          setText(const QString &text);
    const QString &text() const;

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
    QString          m_text;
    BlocTranslations m_tr;
};

#endif // COMMONBLOCAIDISCLAIMER_H
