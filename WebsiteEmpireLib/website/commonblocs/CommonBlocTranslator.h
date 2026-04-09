#ifndef COMMONBLOCTRANSLATOR_H
#define COMMONBLOCTRANSLATOR_H

#include <QList>
#include <QString>

class AbstractCommonBloc;
class AbstractEngine;
class AbstractTheme;

/**
 * Translates all missing common-bloc field translations using AI.
 *
 * Workflow:
 *   1. Collect (bloc, fieldId, targetLangCode) triples where translation
 *      is missing AND source text is non-empty AND langCode != sourceLang.
 *   2. For each triple call the AI API stub (_callAi).
 *   3. Store via bloc->setTranslation(fieldId, langCode, text).
 *   4. Call theme.saveBlocsData() once.
 *
 * Returns the number of fields successfully translated.
 */
class CommonBlocTranslator
{
public:
    struct Job {
        AbstractCommonBloc *bloc;
        QString             fieldId;
        QString             sourceText;
        QString             targetLangCode;
    };

    CommonBlocTranslator(AbstractTheme &theme, AbstractEngine &engine);

    QList<Job> pendingJobs() const;
    int        runAll();

private:
    static QString _callAi(const QString &sourceText,
                           const QString &targetLangCode);

    AbstractTheme  &m_theme;
    AbstractEngine &m_engine;
};

#endif // COMMONBLOCTRANSLATOR_H
