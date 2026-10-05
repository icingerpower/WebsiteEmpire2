#ifndef COMMONBLOCPAGELABELS_H
#define COMMONBLOCPAGELABELS_H

#include "AbstractCommonBloc.h"
#include "BlocTranslations.h"

class QDir;

/** Shared generated-page labels, translated by the common-bloc CLI pipeline.
 * Sources are fixed English strings, independent of the theme's source language.
 * Stored in page_labels.ini in the selected working directory so changing themes
 * does not discard translations. This bloc emits no standalone HTML fragment.
 */
class CommonBlocPageLabels : public AbstractCommonBloc
{
public:
    static constexpr const char *POSSIBLE_CONDITIONS = "possible_conditions";
    static constexpr const char *ALL_SYMPTOMS = "all_symptoms";
    static constexpr const char *BROWSE_CATEGORIES = "browse_categories";
    static constexpr const char *BROWSE_SYMPTOMS = "browse_symptoms";
    static constexpr const char *BROWSE_SYMPTOMS_COUNT = "browse_symptoms_count";

    CommonBlocPageLabels();
    QString getId() const override;
    QString getName() const override;
    QList<ScopeType> supportedScopes() const override;
    AbstractCommonBlocWidget *createEditWidget() override;
    QVariantMap toMap() const override;
    void fromMap(const QVariantMap &map) override;
    void addCode(QStringView, AbstractEngine &, int, QString &, QString &,
                 QString &, QSet<QString> &, QSet<QString> &) const override;
    QHash<QString, QString> sourceTexts() const override;
    QString translationSourceLang(const QString &themeSourceLang) const override;
    void setTranslation(const QString &fieldId, const QString &lang,
                        const QString &text) override;
    QString translatedText(const QString &fieldId, const QString &lang) const override;
    QStringList missingTranslations(const QString &lang,
                                    const QString &sourceLang) const override;
    void saveTranslations(QSettings &settings) override;
    void loadTranslations(QSettings &settings) override;

    void load(const QDir &workingDir);
    void save(const QDir &workingDir);
    /// Returns plain text. Missing/empty translations raise; callers escape for HTML.
    QString text(const QString &fieldId, const QString &lang) const;

private:
    BlocTranslations m_tr;
};

#endif
