#ifndef GENERATORHEALTH_H
#define GENERATORHEALTH_H

#include "AbstractGenerator.h"

#include <QScopedPointer>
#include <QSettings>

// Generator that builds a comprehensive health reference database in nine
// ordered pipeline steps:
//
//   1  bp/general/PAGE          — general body parts (isBrainPart=false)
//   2  bp/brain/PAGE            — brain structures   (isBrainPart=true)
//   3  organ/PAGE               — human organs
//   4  symptom/bp/<slug>        — symptoms per body part   (discovered in 1+2)
//   5  organ/bp/<slug>          — organs per body part     (discovered in 1+2)
//   6  condition/symptom/<slug> — physical conditions per symptom (discovered in 4)
//   7  mental/bp/<slug>         — mental conditions per brain part (discovered in 2)
//   8  mental/completion/PAGE   — mental condition sweep (static initial job)
//   9  recent/PAGE              — recently documented conditions (static initial job)
//
// Pagination: each job requests up to MAX_RESULTS_PER_JOB values.  When a
// reply contains exactly that many, a continuation job for the next page is
// discovered automatically.  Existing values are passed in every payload so
// Claude can avoid duplicates without exceeding the token limit.
//
// Duplicate prevention: processReply pre-populates a seenValues set from the
// already-open model before inserting anything, so no value is ever inserted
// twice even across multiple processReply calls.
//
// isStepComplete(Step) reports whether a step is fully processed and no more
// jobs will be spawned for it.  Completion state is persisted in a dedicated
// <id>_steps.ini file so it survives restarts.
class GeneratorHealth : public AbstractGenerator
{
    Q_OBJECT

public:
    // Maximum values per job.  When a reply hits this limit a next-page job
    // is spawned automatically.
    static const int MAX_RESULTS_PER_JOB;

    // "task" discriminators embedded in every job payload so the receiver can
    // identify the job type without parsing the job ID.
    static const QString TASK_BP_GENERAL;
    static const QString TASK_BP_BRAIN;
    static const QString TASK_ORGANS;
    static const QString TASK_INJURIES;
    static const QString TASK_SYMPTOMS_FOR_BP;
    static const QString TASK_ORGANS_FOR_BP;
    static const QString TASK_CONDITIONS_FOR_SYMPTOM;
    static const QString TASK_MENTAL_FOR_BRAIN_PART;
    static const QString TASK_MENTAL_COMPLETION;
    static const QString TASK_RECENT_CONDITIONS;
    static const QString TASK_CONDITION_DIFFICULTY;
    static const QString TASK_MENTAL_CONDITION_DIFFICULTY;

    // Logical pipeline steps in execution order.
    enum class Step {
        BodyPartsGeneral,      // bp/general/*
        BodyPartsBrain,        // bp/brain/*
        Organs,                // organ/*  (not organ/bp/*)
        Injuries,              // injury/*
        SymptomsPerBodyPart,   // symptom/bp/*
        OrgansPerBodyPart,     // organ/bp/*
        ConditionsPerSymptom,  // condition/symptom/*
        MentalPerBrainPart,    // mental/bp/*
        MentalCompletion,           // mental/completion/*
        RecentConditions,           // recent/*
        ConditionDifficulty,        // difficulty/condition/*
        MentalConditionDifficulty,  // difficulty/mental/*
    };

    explicit GeneratorHealth(const QDir &workingDir = QDir(), QObject *parent = nullptr);

    QString            getId()   const override;
    QString            getName() const override;
    AbstractGenerator *createInstance(const QDir &workingDir) const override;
    QMap<QString, AbstractPageAttributes *> createResultPageAttributes() const override;

    // Returns true once the step has been fully processed and no further jobs
    // will be spawned for it.  Safe to call at any time.
    bool isStepComplete(Step step) const;

    // ---- Job-ID helpers (public for testability) ---------------------------

    // URL-safe ASCII slug: lower-case, spaces/underscores → '-', others dropped.
    static QString slugify(const QString &text);

    // Extracts the page number from the last '/'-separated segment of a job ID.
    static int pageFromJobId(const QString &jobId);

    // Extracts the entity slug: everything after the second '/' in the job ID.
    // e.g. "symptom/bp/knee" → "knee",  "condition/symptom/fatigue" → "fatigue"
    static QString entitySlug(const QString &jobId);

    static bool isBpGeneralJobId            (const QString &jobId);
    static bool isBpBrainJobId              (const QString &jobId);
    static bool isOrgansJobId               (const QString &jobId); // organ/N, not organ/bp/*
    static bool isInjuriesJobId             (const QString &jobId); // injury/N
    static bool isSymptomsForBpJobId        (const QString &jobId);
    static bool isOrgansForBpJobId          (const QString &jobId);
    static bool isConditionsForSymptomJobId (const QString &jobId);
    static bool isMentalForBrainPartJobId   (const QString &jobId);
    static bool isMentalCompletionJobId       (const QString &jobId);
    static bool isRecentJobId                 (const QString &jobId);
    static bool isConditionDifficultyJobId    (const QString &jobId);
    static bool isMentalDifficultyJobId       (const QString &jobId);

protected:
    QStringList buildInitialJobIds()                                    const override;
    QJsonObject buildJobPayload(const QString &jobId)                   const override;
    void        processReply(const QString &jobId, const QJsonObject &) override;

private:
    // ---- DB value loaders --------------------------------------------------
    // Always reads via direct SQL (not rowCount) to bypass QSqlTableModel's
    // 256-row lazy-load cap.
    QStringList loadFieldValues(const QString &attrId, const QString &sqlColumn) const;
    QStringList loadBodyParts(bool brainOnly) const;  // filtered by isBrainPart
    QStringList loadAllBodyParts()     const;
    QStringList loadOrgans()           const;
    QStringList loadInjuries()         const;
    QStringList loadSymptoms()         const;
    QStringList loadConditions()       const;
    QStringList loadMentalConditions() const;

    // Resolves a slug back to the original name by scanning the body-part or
    // symptom table.  Falls back to the slug itself if not found.
    QString resolveBodyPartName(const QString &slug) const;
    QString resolveSymptomName (const QString &slug) const;

    // ---- Payload builders --------------------------------------------------
    QJsonObject buildBpPayload            (bool brain, int page)       const;
    QJsonObject buildOrgansPayload        (int page)                   const;
    QJsonObject buildInjuriesPayload      (int page)                   const;
    QJsonObject buildSymptomsForBpPayload (const QString &bpSlug)      const;
    QJsonObject buildOrgansForBpPayload   (const QString &bpSlug)      const;
    QJsonObject buildCondForSymptomPayload(const QString &symptomSlug) const;
    QJsonObject buildMentalForBpPayload   (const QString &brainSlug)   const;
    QJsonObject buildMentalCompPayload      (int page)                   const;
    QJsonObject buildRecentPayload          (int page)                   const;
    // Builds a payload that asks Claude to assign healingDifficulty to a batch of
    // conditions (physical or mental) that currently have no score in the DB.
    QJsonObject buildDifficultyPayload      (bool isMental, int page)    const;

    // Records condition objects from a reply, skipping names already in seen.
    // Updates seen with newly inserted names.
    void recordConditions(const QJsonArray &conditions, bool isMental,
                          QSet<QString> &seen);

    // Returns up to MAX_RESULTS_PER_JOB condition names (physical or mental) whose
    // healingDifficulty column is NULL or empty.  Always queries at offset 0: once
    // a batch has been scored via UPDATE the rows no longer match the WHERE clause.
    QStringList loadUnscoredConditionNames(bool isMental) const;

    // ---- Step completion ---------------------------------------------------
    // Reads AbstractGenerator's .ini to find all job IDs with the given prefix
    // and returns true iff every one of them is in Done/ids.
    // Returns false if no jobs with that prefix exist yet.
    bool hasNoPendingJobsWithPrefix(const QString &prefix) const;

    // Sets Steps/<key>/complete = true in the step-state .ini and syncs.
    void markPaginatedStepDone(const QString &settingsKey);

    // Returns the total row count in a results table via COUNT(*) SQL.
    // Uses COUNT(*) rather than rowCount() because QSqlTableModel lazily loads
    // rows in batches of 256 and would cap the count on large tables.
    int dbCount(const QString &attrId) const;

    // Returns (lazily created) QSettings for the <id>_steps.ini file.
    // Separate from AbstractGenerator's .ini to avoid cross-write conflicts.
    QSettings &stepSettings() const;

    mutable QScopedPointer<QSettings> m_stepSettings;
};

#endif // GENERATORHEALTH_H
