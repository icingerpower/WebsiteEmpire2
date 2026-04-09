#include "CommonBlocTranslator.h"

#include "website/AbstractEngine.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/theme/AbstractTheme.h"

#include <QProcessEnvironment>

CommonBlocTranslator::CommonBlocTranslator(AbstractTheme &theme, AbstractEngine &engine)
    : m_theme(theme)
    , m_engine(engine)
{
}

QList<CommonBlocTranslator::Job> CommonBlocTranslator::pendingJobs() const
{
    QList<Job> jobs;

    const QString sourceLang = m_theme.sourceLangCode();

    QList<AbstractCommonBloc *> allBlocs;
    allBlocs.append(m_theme.getTopBlocs());
    allBlocs.append(m_theme.getBottomBlocs());

    for (AbstractCommonBloc *bloc : std::as_const(allBlocs)) {
        const auto sourceMap = bloc->sourceTexts();

        for (int i = 0; i < m_engine.rowCount(); ++i) {
            const QString langCode = m_engine.getLangCode(i);
            if (langCode.isEmpty() || langCode == sourceLang) {
                continue;
            }

            const QStringList missing = bloc->missingTranslations(langCode, sourceLang);
            for (const auto &fieldId : missing) {
                Job job;
                job.bloc = bloc;
                job.fieldId = fieldId;
                job.sourceText = sourceMap.value(fieldId);
                job.targetLangCode = langCode;
                jobs.append(job);
            }
        }
    }

    return jobs;
}

int CommonBlocTranslator::runAll()
{
    const auto jobs = pendingJobs();
    int count = 0;

    for (const auto &job : std::as_const(jobs)) {
        const QString translated = _callAi(job.sourceText, job.targetLangCode);
        if (!translated.isEmpty()) {
            job.bloc->setTranslation(job.fieldId, job.targetLangCode, translated);
            ++count;
        }
    }

    if (count > 0) {
        m_theme.saveBlocsData();
    }

    return count;
}

// Stub: real AI implementation goes here.
// Reads ANTHROPIC_API_KEY from environment; returns empty string for now.
QString CommonBlocTranslator::_callAi(const QString &sourceText,
                                      const QString &targetLangCode)
{
    Q_UNUSED(sourceText)
    Q_UNUSED(targetLangCode)

    // const QString apiKey = QProcessEnvironment::systemEnvironment()
    //     .value(QStringLiteral("ANTHROPIC_API_KEY"));
    // TODO: implement real AI translation call

    return {};
}
