#include "GeneratorHealth.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>

#include "aspire/attributes/health/PageAttributesHealthBodyPart.h"
#include "aspire/attributes/health/PageAttributesHealthCondition.h"
#include "aspire/attributes/health/PageAttributesHealthGoal.h"
#include "aspire/attributes/health/PageAttributesHealthInjury.h"
#include "aspire/attributes/health/PageAttributesHealthMentalCondition.h"
#include "aspire/attributes/health/PageAttributesHealthOrgan.h"
#include "aspire/attributes/health/PageAttributesHealthSymptom.h"
#include "aspire/downloader/DownloadedPagesTable.h"

DECLARE_GENERATOR(GeneratorHealth)

// ---- Constants --------------------------------------------------------------

const int     GeneratorHealth::MAX_RESULTS_PER_JOB         = 50;
const QString GeneratorHealth::TASK_BP_GENERAL             = QStringLiteral("body_parts_general");
const QString GeneratorHealth::TASK_BP_BRAIN               = QStringLiteral("body_parts_brain");
const QString GeneratorHealth::TASK_ORGANS                 = QStringLiteral("organs");
const QString GeneratorHealth::TASK_INJURIES               = QStringLiteral("injuries");
const QString GeneratorHealth::TASK_SYMPTOMS_FOR_BP        = QStringLiteral("symptoms_for_body_part");
const QString GeneratorHealth::TASK_ORGANS_FOR_BP          = QStringLiteral("organs_for_body_part");
const QString GeneratorHealth::TASK_CONDITIONS_FOR_SYMPTOM = QStringLiteral("conditions_for_symptom");
const QString GeneratorHealth::TASK_MENTAL_FOR_BRAIN_PART  = QStringLiteral("mental_conditions_for_brain_part");
const QString GeneratorHealth::TASK_MENTAL_COMPLETION          = QStringLiteral("mental_conditions_completion");
const QString GeneratorHealth::TASK_RECENT_CONDITIONS          = QStringLiteral("recent_conditions");
const QString GeneratorHealth::TASK_CONDITION_DIFFICULTY       = QStringLiteral("condition_healing_difficulty");
const QString GeneratorHealth::TASK_MENTAL_CONDITION_DIFFICULTY = QStringLiteral("mental_condition_healing_difficulty");
const QString GeneratorHealth::TASK_GOALS                      = QStringLiteral("health_goals");

// ---- File-local helpers -----------------------------------------------------

namespace {

QJsonArray toJsonArray(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list) {
        arr.append(s);
    }
    return arr;
}

// Joins values from a JSON array, keeping only those present in 'allowed'.
// Pass an empty set to accept all values.
QString toCommaSeparated(const QJsonArray &arr, const QSet<QString> &allowed)
{
    QStringList parts;
    for (const QJsonValue &v : arr) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && (allowed.isEmpty() || allowed.contains(s))) {
            parts << s;
        }
    }
    return parts.join(QLatin1Char(','));
}

// Returns "1", "2", or "3" by extracting the first valid digit from Claude's answer.
// Falls back to "1" (most conservative score) if no valid digit is found.
QString cleanDifficulty(const QString &raw)
{
    bool ok;
    const int v = raw.trimmed().toInt(&ok);
    if (ok && v >= 1 && v <= 3) {
        return QString::number(v);
    }
    for (const QChar c : raw) {
        if (c == QLatin1Char('1') || c == QLatin1Char('2') || c == QLatin1Char('3')) {
            return QString(c);
        }
    }
    return QStringLiteral("1");
}

// Strips non-numeric characters (keeps digits and a single '.') so that
// Claude answers like "6.3%" or "~5" are stored as valid percentage strings.
QString cleanPercentage(const QString &raw)
{
    QString result;
    bool hasDot = false;
    for (const QChar c : raw) {
        if (c.isDigit()) {
            result += c;
        } else if (c == QLatin1Char('.') && !hasDot) {
            result += c;
            hasDot = true;
        }
    }
    return result.isEmpty() ? QStringLiteral("0") : result;
}

} // namespace

// ---- Constructor ------------------------------------------------------------

GeneratorHealth::GeneratorHealth(const QDir &workingDir, QObject *parent)
    : AbstractGenerator(workingDir, parent)
{
}

// ---- AbstractGenerator overrides --------------------------------------------

QString GeneratorHealth::getId() const
{
    return QStringLiteral("health");
}

QString GeneratorHealth::getName() const
{
    return tr("Health Database");
}

AbstractGenerator *GeneratorHealth::createInstance(const QDir &workingDir) const
{
    return new GeneratorHealth(workingDir);
}

QMap<QString, AbstractPageAttributes *> GeneratorHealth::createResultPageAttributes() const
{
    return {
        {tr("Body Parts"),        new PageAttributesHealthBodyPart()},
        {tr("Organs"),            new PageAttributesHealthOrgan()},
        {tr("Symptoms"),          new PageAttributesHealthSymptom()},
        {tr("Injuries"),          new PageAttributesHealthInjury()},
        {tr("Health Conditions"), new PageAttributesHealthCondition()},
        {tr("Mental Conditions"), new PageAttributesHealthMentalCondition()},
        {tr("Health Goals"),      new PageAttributesHealthGoal()},
    };
}

AbstractGenerator::GeneratorTables GeneratorHealth::getTables() const
{
    GeneratorTables tables;

    // Primary: one health condition = one article
    const TableDescriptor condition = _makeDescriptor(
        QStringLiteral("PageAttributesHealthCondition"), tr("Health Conditions"));
    tables.primary.insert(condition.id, condition);

    // Category: controlled-vocabulary tables referenced by conditions
    const TableDescriptor bodyPart = _makeDescriptor(
        QStringLiteral("PageAttributesHealthBodyPart"), tr("Body Parts"));
    tables.category.insert(bodyPart.id, bodyPart);

    const TableDescriptor organ = _makeDescriptor(
        QStringLiteral("PageAttributesHealthOrgan"), tr("Organs"));
    tables.category.insert(organ.id, organ);

    // ReferredTo: child/detail tables associated with conditions
    const TableDescriptor symptom = _makeDescriptor(
        QStringLiteral("PageAttributesHealthSymptom"), tr("Symptoms"));
    tables.referredTo.insert(symptom.id, symptom);

    const TableDescriptor injury = _makeDescriptor(
        QStringLiteral("PageAttributesHealthInjury"), tr("Injuries"));
    tables.referredTo.insert(injury.id, injury);

    const TableDescriptor mental = _makeDescriptor(
        QStringLiteral("PageAttributesHealthMentalCondition"), tr("Mental Conditions"));
    tables.referredTo.insert(mental.id, mental);

    const TableDescriptor goal = _makeDescriptor(
        QStringLiteral("PageAttributesHealthGoal"), tr("Health Goals"));
    tables.referredTo.insert(goal.id, goal);

    Q_ASSERT(tables.primary.size() == 1);
    return tables;
}

// ---- Job-ID helpers ---------------------------------------------------------

QString GeneratorHealth::slugify(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() < 128
            && (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('.'))) {
            result += c.toLower();
        } else if (c == QLatin1Char(' ') || c == QLatin1Char('_')) {
            result += QLatin1Char('-');
        }
        // Non-ASCII and special chars dropped for URL safety.
    }
    return result;
}

int GeneratorHealth::pageFromJobId(const QString &jobId)
{
    const QStringList parts = jobId.split(QLatin1Char('/'));
    return parts.isEmpty() ? 0 : parts.last().toInt();
}

QString GeneratorHealth::entitySlug(const QString &jobId)
{
    // Everything after the second '/', e.g. "symptom/bp/knee" → "knee"
    const int first = jobId.indexOf(QLatin1Char('/'));
    if (first < 0) {
        return {};
    }
    const int second = jobId.indexOf(QLatin1Char('/'), first + 1);
    return second >= 0 ? jobId.mid(second + 1) : QString{};
}

bool GeneratorHealth::isBpGeneralJobId(const QString &j)
{
    return j.startsWith(QLatin1String("bp/general/"));
}
bool GeneratorHealth::isBpBrainJobId(const QString &j)
{
    return j.startsWith(QLatin1String("bp/brain/"));
}
bool GeneratorHealth::isOrgansJobId(const QString &j)
{
    // organ/0, organ/1 — but NOT organ/bp/*
    return j.startsWith(QLatin1String("organ/"))
           && !j.startsWith(QLatin1String("organ/bp/"));
}
bool GeneratorHealth::isInjuriesJobId(const QString &j)
{
    return j.startsWith(QLatin1String("injury/"));
}
bool GeneratorHealth::isSymptomsForBpJobId(const QString &j)
{
    return j.startsWith(QLatin1String("symptom/bp/"));
}
bool GeneratorHealth::isOrgansForBpJobId(const QString &j)
{
    return j.startsWith(QLatin1String("organ/bp/"));
}
bool GeneratorHealth::isConditionsForSymptomJobId(const QString &j)
{
    return j.startsWith(QLatin1String("condition/symptom/"));
}
bool GeneratorHealth::isMentalForBrainPartJobId(const QString &j)
{
    return j.startsWith(QLatin1String("mental/bp/"));
}
bool GeneratorHealth::isMentalCompletionJobId(const QString &j)
{
    return j.startsWith(QLatin1String("mental/completion/"));
}
bool GeneratorHealth::isRecentJobId(const QString &j)
{
    return j.startsWith(QLatin1String("recent/"));
}
bool GeneratorHealth::isConditionDifficultyJobId(const QString &j)
{
    return j.startsWith(QLatin1String("difficulty/condition/"));
}
bool GeneratorHealth::isMentalDifficultyJobId(const QString &j)
{
    return j.startsWith(QLatin1String("difficulty/mental/"));
}
bool GeneratorHealth::isGoalsJobId(const QString &j)
{
    return j.startsWith(QLatin1String("goal/"));
}

// ---- Initial job list -------------------------------------------------------

QStringList GeneratorHealth::buildInitialJobIds() const
{
    // Steps 1–3 seeded upfront.  Steps 4–7 discovered from processReply.
    // mental/completion/0 is spawned once organs step completes (body parts + organs
    // are fully populated by then).
    // recent/0 is spawned once body-part steps complete (all reference tables
    // are sufficiently populated to produce meaningful associations).
    return {
        QStringLiteral("bp/general/0"),
        QStringLiteral("bp/brain/0"),
        QStringLiteral("organ/0"),
        QStringLiteral("injury/0"),
        // Backfill jobs: score existing DB rows that predate the healingDifficulty column.
        // On a fresh run these terminate immediately (no unscored rows); new conditions are
        // inserted with the score directly via the updated conditionEntrySchema().
        QStringLiteral("difficulty/condition/0"),
        QStringLiteral("difficulty/mental/0"),
        // Health goals: generated once body parts are available (referenced optionally).
        QStringLiteral("goal/0"),
    };
}

// ---- Step settings ----------------------------------------------------------

QSettings &GeneratorHealth::stepSettings() const
{
    if (!m_stepSettings) {
        m_stepSettings.reset(new QSettings(
            workingDir().filePath(getId() + QStringLiteral("_steps.ini")),
            QSettings::IniFormat));
    }
    return *m_stepSettings;
}

void GeneratorHealth::markPaginatedStepDone(const QString &settingsKey)
{
    stepSettings().setValue(
        QStringLiteral("Steps/") + settingsKey + QStringLiteral("/complete"), true);
    stepSettings().sync();
    qDebug() << "GeneratorHealth: step" << settingsKey << "marked complete";

    // Spawn late-pipeline sweeps once enough reference data is available.
    // mental/completion requires body parts + organs to be fully populated.
    if (settingsKey == QLatin1String("organs")) {
        addDiscoveredJob(QStringLiteral("mental/completion/0"));
        qDebug() << "GeneratorHealth: spawned mental/completion/0 (organs step complete)";
    }
    // recent conditions require body parts, organs, symptoms, and conditions to be
    // substantially populated — spawn once both body-part steps are done.
    if (settingsKey == QLatin1String("bp_brain")) {
        addDiscoveredJob(QStringLiteral("recent/0"));
        qDebug() << "GeneratorHealth: spawned recent/0 (brain body-parts step complete)";
    }
}

bool GeneratorHealth::hasNoPendingJobsWithPrefix(const QString &prefix) const
{
    // Read directly from AbstractGenerator's persisted .ini (always synced after
    // each recordReply, so the data on disk is current).
    const QSettings mainIni(
        workingDir().filePath(getId() + QStringLiteral(".ini")),
        QSettings::IniFormat);
    const QStringList doneList      = mainIni.value(QStringLiteral("Done/ids")).toStringList();
    const QStringList discoveredIds = mainIni.value(QStringLiteral("Discovered/ids")).toStringList();
    const QSet<QString> done(doneList.begin(), doneList.end());

    // Gather all known jobs with this prefix.
    QStringList allWithPrefix;
    for (const QString &id : buildInitialJobIds() + discoveredIds) {
        if (id.startsWith(prefix)) {
            allWithPrefix << id;
        }
    }

    if (allWithPrefix.isEmpty()) {
        return false; // No jobs for this step have been discovered yet.
    }
    for (const QString &id : std::as_const(allWithPrefix)) {
        if (!done.contains(id)) {
            return false;
        }
    }
    return true;
}

bool GeneratorHealth::isStepComplete(Step step) const
{
    stepSettings().sync(); // Reload from disk so we see the latest flags.
    switch (step) {
    case Step::BodyPartsGeneral:
        return stepSettings().value(QStringLiteral("Steps/bp_general/complete")).toBool();
    case Step::BodyPartsBrain:
        return stepSettings().value(QStringLiteral("Steps/bp_brain/complete")).toBool();
    case Step::Organs:
        return stepSettings().value(QStringLiteral("Steps/organs/complete")).toBool();
    case Step::Injuries:
        return stepSettings().value(QStringLiteral("Steps/injuries/complete")).toBool();
    case Step::SymptomsPerBodyPart:
        return isStepComplete(Step::BodyPartsGeneral)
            && isStepComplete(Step::BodyPartsBrain)
            && hasNoPendingJobsWithPrefix(QStringLiteral("symptom/bp/"));
    case Step::OrgansPerBodyPart:
        return isStepComplete(Step::BodyPartsGeneral)
            && isStepComplete(Step::BodyPartsBrain)
            && hasNoPendingJobsWithPrefix(QStringLiteral("organ/bp/"));
    case Step::ConditionsPerSymptom:
        return isStepComplete(Step::SymptomsPerBodyPart)
            && hasNoPendingJobsWithPrefix(QStringLiteral("condition/symptom/"));
    case Step::MentalPerBrainPart:
        return isStepComplete(Step::BodyPartsBrain)
            && hasNoPendingJobsWithPrefix(QStringLiteral("mental/bp/"));
    case Step::MentalCompletion:
        return stepSettings().value(QStringLiteral("Steps/mental_completion/complete")).toBool();
    case Step::RecentConditions:
        return stepSettings().value(QStringLiteral("Steps/recent/complete")).toBool();
    case Step::ConditionDifficulty:
        return stepSettings().value(QStringLiteral("Steps/condition_difficulty/complete")).toBool();
    case Step::MentalConditionDifficulty:
        return stepSettings().value(QStringLiteral("Steps/mental_difficulty/complete")).toBool();
    case Step::Goals:
        return stepSettings().value(QStringLiteral("Steps/goals/complete")).toBool();
    }
    return false;
}

// ---- DB value loaders -------------------------------------------------------

QStringList GeneratorHealth::loadFieldValues(const QString &attrId,
                                              const QString &sqlColumn) const
{
    const DownloadedPagesTable *table = resultsTable(attrId);
    if (!table) {
        return {};
    }
    QSqlQuery q(table->database());
    QStringList values;
    if (q.exec(QStringLiteral("SELECT %1 FROM records").arg(sqlColumn))) {
        while (q.next()) {
            const QString v = q.value(0).toString().trimmed();
            if (!v.isEmpty()) {
                values << v;
            }
        }
    }
    return values;
}

QStringList GeneratorHealth::loadBodyParts(bool brainOnly) const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesHealthBodyPart"));
    if (!table) {
        return {};
    }
    const QString filter = brainOnly
        ? QStringLiteral(" WHERE %1 = 'true'")
                .arg(PageAttributesHealthBodyPart::ID_IS_BRAIN_PART)
        : QStringLiteral(" WHERE %1 = 'false'")
                .arg(PageAttributesHealthBodyPart::ID_IS_BRAIN_PART);
    QSqlQuery q(table->database());
    QStringList result;
    if (q.exec(QStringLiteral("SELECT %1 FROM records")
                   .arg(PageAttributesHealthBodyPart::ID_BODY_PART_NAME)
               + filter)) {
        while (q.next()) {
            const QString v = q.value(0).toString().trimmed();
            if (!v.isEmpty()) {
                result << v;
            }
        }
    }
    return result;
}

QStringList GeneratorHealth::loadAllBodyParts() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthBodyPart"),
                           PageAttributesHealthBodyPart::ID_BODY_PART_NAME);
}

QStringList GeneratorHealth::loadOrgans() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthOrgan"),
                           PageAttributesHealthOrgan::ID_ORGAN_NAME);
}

QStringList GeneratorHealth::loadInjuries() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthInjury"),
                           PageAttributesHealthInjury::ID_INJURY_NAME);
}

QStringList GeneratorHealth::loadSymptoms() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthSymptom"),
                           PageAttributesHealthSymptom::ID_SYMPTOM_NAME);
}

QStringList GeneratorHealth::loadConditions() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthCondition"),
                           PageAttributesHealthCondition::ID_NAME);
}

QStringList GeneratorHealth::loadMentalConditions() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthMentalCondition"),
                           PageAttributesHealthMentalCondition::ID_NAME);
}

QStringList GeneratorHealth::loadGoals() const
{
    return loadFieldValues(QStringLiteral("PageAttributesHealthGoal"),
                           PageAttributesHealthGoal::ID_NAME);
}

QStringList GeneratorHealth::loadUnscoredConditionNames(bool isMental) const
{
    const QString attrId  = isMental
        ? QStringLiteral("PageAttributesHealthMentalCondition")
        : QStringLiteral("PageAttributesHealthCondition");
    const QString nameCol = isMental
        ? PageAttributesHealthMentalCondition::ID_NAME
        : PageAttributesHealthCondition::ID_NAME;
    const QString diffCol = isMental
        ? PageAttributesHealthMentalCondition::ID_HEALING_DIFFICULTY
        : PageAttributesHealthCondition::ID_HEALING_DIFFICULTY;
    const DownloadedPagesTable *table = resultsTable(attrId);
    if (!table) {
        return {};
    }
    QSqlQuery q(table->database());
    // Always offset 0: once a batch is UPDATE-d the rows no longer match the WHERE clause,
    // so the next page is always the first batch of still-unscored conditions.
    const QString sql = QStringLiteral(
        "SELECT \"%1\" FROM records WHERE \"%2\" IS NULL OR \"%2\" = '' LIMIT %3")
        .arg(nameCol, diffCol)
        .arg(MAX_RESULTS_PER_JOB);
    QStringList names;
    if (q.exec(sql)) {
        while (q.next()) {
            const QString v = q.value(0).toString().trimmed();
            if (!v.isEmpty()) {
                names << v;
            }
        }
    }
    return names;
}

int GeneratorHealth::dbCount(const QString &attrId) const
{
    const DownloadedPagesTable *table = resultsTable(attrId);
    if (!table) {
        return 0;
    }
    QSqlQuery q(table->database());
    return (q.exec(QStringLiteral("SELECT COUNT(*) FROM records")) && q.next())
               ? q.value(0).toInt()
               : 0;
}

// ---- Name resolution --------------------------------------------------------

QString GeneratorHealth::resolveBodyPartName(const QString &slug) const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesHealthBodyPart"));
    if (!table) {
        return slug;
    }
    QSqlQuery q(table->database());
    if (q.exec(QStringLiteral("SELECT %1 FROM records")
                   .arg(PageAttributesHealthBodyPart::ID_BODY_PART_NAME))) {
        while (q.next()) {
            const QString name = q.value(0).toString();
            if (slugify(name) == slug) {
                return name;
            }
        }
    }
    qDebug() << "GeneratorHealth: could not resolve body-part slug" << slug
             << "— using slug as name";
    return slug;
}

QString GeneratorHealth::resolveSymptomName(const QString &slug) const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesHealthSymptom"));
    if (!table) {
        return slug;
    }
    QSqlQuery q(table->database());
    if (q.exec(QStringLiteral("SELECT %1 FROM records")
                   .arg(PageAttributesHealthSymptom::ID_SYMPTOM_NAME))) {
        while (q.next()) {
            const QString name = q.value(0).toString();
            if (slugify(name) == slug) {
                return name;
            }
        }
    }
    qDebug() << "GeneratorHealth: could not resolve symptom slug" << slug
             << "— using slug as name";
    return slug;
}

// ---- Payload builders -------------------------------------------------------

QJsonObject GeneratorHealth::buildBpPayload(bool brain, int page) const
{
    const QStringList existing = loadBodyParts(brain);
    const QString kind = brain
        ? tr("brain structures (e.g. \"Hippocampus\", \"Amygdala\", \"Cerebellum\", "
             "\"Prefrontal Cortex\")")
        : tr("human anatomical structures that are NOT brain structures and NOT internal "
             "organs — focus on bones, joints, muscles, tendons, ligaments, and external "
             "body regions (e.g. \"Knee\", \"Shoulder\", \"Achilles Tendon\", \"Lumbar Spine\"). "
             "Do NOT include Heart, Liver, Kidney, Lung, Stomach, or any other internal organ");

    QJsonObject payload;
    payload[QStringLiteral("task")]           = brain ? TASK_BP_BRAIN : TASK_BP_GENERAL;
    payload[QStringLiteral("page")]           = page;
    payload[QStringLiteral("maxResults")]     = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]  = existing.size();
    payload[QStringLiteral("existingValues")] = toJsonArray(existing);
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct %2. "
        "Do NOT include any name already present in 'existingValues'. "
        "If fewer than %1 new values exist, return only those. "
        "Return an empty array if none remain.")
        .arg(MAX_RESULTS_PER_JOB)
        .arg(kind);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("values")]  = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildOrgansPayload(int page) const
{
    const QStringList existing = loadOrgans();

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_ORGANS;
    payload[QStringLiteral("page")]           = page;
    payload[QStringLiteral("maxResults")]     = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]  = existing.size();
    payload[QStringLiteral("existingValues")] = toJsonArray(existing);
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct human organs (e.g. \"Heart\", \"Liver\", \"Kidney\"). "
        "Do NOT include any name already in 'existingValues'. "
        "Return fewer entries if fewer non-duplicate organs remain.")
        .arg(MAX_RESULTS_PER_JOB);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("values")]  = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildInjuriesPayload(int page) const
{
    const QStringList existing = loadInjuries();

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_INJURIES;
    payload[QStringLiteral("page")]           = page;
    payload[QStringLiteral("maxResults")]     = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]  = existing.size();
    payload[QStringLiteral("existingValues")] = toJsonArray(existing);
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct human injury types (e.g. \"Sprain\", \"Fracture\", "
        "\"Concussion\", \"Tendon Rupture\", \"Dislocation\"). "
        "Focus on common medical injury categories, not specific accident descriptions. "
        "Do NOT include any name already in 'existingValues'. "
        "Return fewer entries if fewer non-duplicate injuries remain.")
        .arg(MAX_RESULTS_PER_JOB);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("values")]  = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildSymptomsForBpPayload(const QString &bpSlug) const
{
    const QString name         = resolveBodyPartName(bpSlug);
    const QStringList existing = loadSymptoms();

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_SYMPTOMS_FOR_BP;
    payload[QStringLiteral("bodyPart")]       = name;
    payload[QStringLiteral("maxResults")]     = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]  = existing.size();
    payload[QStringLiteral("existingValues")] = toJsonArray(existing);
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "Return up to %1 symptoms that can affect or originate from the '%2'. "
        "STRONGLY PREFER reusing symptom names already in 'existingValues' — use the EXACT "
        "same string from that list whenever a symptom applies to '%2'. "
        "Only include a symptom NOT in 'existingValues' if it genuinely applies to '%2' "
        "and has no close equivalent already in the list. "
        "Return fewer entries if fewer symptoms apply.")
        .arg(MAX_RESULTS_PER_JOB)
        .arg(name);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("values")]  = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildOrgansForBpPayload(const QString &bpSlug) const
{
    const QString name         = resolveBodyPartName(bpSlug);
    const QStringList existing = loadOrgans();

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_ORGANS_FOR_BP;
    payload[QStringLiteral("bodyPart")]       = name;
    payload[QStringLiteral("maxResults")]     = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]  = existing.size();
    payload[QStringLiteral("existingValues")] = toJsonArray(existing);
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct traditional human organs that are directly associated "
        "with the '%2'. An organ is a self-contained biological structure with a dedicated "
        "physiological function (e.g. Heart, Liver, Kidney, Lung, Stomach, Brain, Pancreas, "
        "Spleen, Bladder). "
        "Do NOT include tendons, ligaments, cartilage, bones, muscles, nerves, blood vessels, "
        "synovial membranes, periosteum, bursae, or other non-organ anatomical sub-structures. "
        "Do NOT include any organ already in 'existingValues'. "
        "If '%2' has no directly associated major organs, return an empty array.")
        .arg(MAX_RESULTS_PER_JOB)
        .arg(name);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]   = QString{};
    replyFormat[QStringLiteral("values")]  = QJsonArray{};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

// Shared condition schema fragment embedded in the replyFormat of condition jobs.
static QJsonObject conditionEntrySchema()
{
    QJsonObject s;
    s[QStringLiteral("name")]                 = QStringLiteral("string — condition name");
    s[QStringLiteral("populationPercentage")] = QStringLiteral("number string 0–100");
    s[QStringLiteral("healingDifficulty")]    = QStringLiteral("integer 1, 2, or 3");
    s[QStringLiteral("bodyParts")]            = QJsonArray{};
    s[QStringLiteral("symptoms")]             = QJsonArray{};
    s[QStringLiteral("organs")]               = QJsonArray{};
    s[QStringLiteral("injuries")]             = QJsonArray{};
    return s;
}

QJsonObject GeneratorHealth::buildCondForSymptomPayload(const QString &symptomSlug) const
{
    const QString name             = resolveSymptomName(symptomSlug);
    const QStringList existing     = loadConditions();
    const QStringList bodyParts    = loadAllBodyParts();
    const QStringList organs       = loadOrgans();
    const QStringList injuries     = loadInjuries();
    const QStringList symptoms     = loadSymptoms();

    QJsonObject payload;
    payload[QStringLiteral("task")]                   = TASK_CONDITIONS_FOR_SYMPTOM;
    payload[QStringLiteral("symptom")]                = name;
    payload[QStringLiteral("maxResults")]             = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingConditionCount")] = existing.size();
    payload[QStringLiteral("existingConditions")]     = toJsonArray(existing);
    payload[QStringLiteral("availableBodyParts")]     = toJsonArray(bodyParts);
    payload[QStringLiteral("availableOrgans")]        = toJsonArray(organs);
    payload[QStringLiteral("availableInjuries")]      = toJsonArray(injuries);
    payload[QStringLiteral("availableSymptoms")]      = toJsonArray(symptoms);
    payload[QStringLiteral("instructions")]           = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct NON-MENTAL health conditions where '%2' is a known symptom. "
        "Do NOT include any condition already in 'existingConditions'. "
        "For each condition provide:\n"
        "  • name — condition name\n"
        "  • populationPercentage — estimated %% of global population affected (0–100)\n"
        "  • healingDifficulty — integer 1, 2, or 3:\n"
        "      1 = can be resolved without too much effort\n"
        "      2 = some people resolve it permanently but requires effort; normal medicine often fails\n"
        "      3 = almost no one resolves it permanently; claimed recoveries are not fully verified\n"
        "  • bodyParts — array of body parts from 'availableBodyParts' most affected "
        "(empty array if none)\n"
        "  • symptoms — array of symptoms from 'availableSymptoms' commonly seen "
        "(MUST include '%2' if it appears in availableSymptoms)\n"
        "  • organs — array of organs from 'availableOrgans' primarily affected "
        "(empty array if none)\n"
        "  • injuries — array of injury types from 'availableInjuries' commonly associated "
        "(empty array if none)\n"
        "Use ONLY names that appear exactly in the available lists.")
        .arg(QString::number(MAX_RESULTS_PER_JOB), name);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]      = QString{};
    replyFormat[QStringLiteral("conditions")] = QJsonArray{conditionEntrySchema()};
    payload[QStringLiteral("replyFormat")]    = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildMentalForBpPayload(const QString &brainSlug) const
{
    const QString name             = resolveBodyPartName(brainSlug);
    const QStringList existing     = loadMentalConditions();
    const QStringList bodyParts    = loadAllBodyParts();
    const QStringList organs       = loadOrgans();
    const QStringList injuries     = loadInjuries();
    const QStringList symptoms     = loadSymptoms();

    QJsonObject payload;
    payload[QStringLiteral("task")]                        = TASK_MENTAL_FOR_BRAIN_PART;
    payload[QStringLiteral("brainPart")]                   = name;
    payload[QStringLiteral("maxResults")]                  = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingConditionCount")]      = existing.size();
    payload[QStringLiteral("existingMentalConditions")]    = toJsonArray(existing);
    payload[QStringLiteral("availableBodyParts")]          = toJsonArray(bodyParts);
    payload[QStringLiteral("availableOrgans")]             = toJsonArray(organs);
    payload[QStringLiteral("availableInjuries")]           = toJsonArray(injuries);
    payload[QStringLiteral("availableSymptoms")]           = toJsonArray(symptoms);
    payload[QStringLiteral("instructions")]                = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct MENTAL health conditions that involve or are primarily "
        "associated with the brain structure '%2'. "
        "Do NOT include any condition already in 'existingMentalConditions'. "
        "For each condition provide:\n"
        "  • name — condition name\n"
        "  • populationPercentage — estimated %% of global population affected (0–100)\n"
        "  • healingDifficulty — integer 1, 2, or 3:\n"
        "      1 = can be resolved without too much effort\n"
        "      2 = some people resolve it permanently but requires effort; normal medicine often fails\n"
        "      3 = almost no one resolves it permanently; claimed recoveries are not fully verified\n"
        "  • bodyParts — array of body parts from 'availableBodyParts' most affected\n"
        "  • symptoms — array of symptoms from 'availableSymptoms' commonly seen "
        "(empty array if none apply)\n"
        "  • organs — array of organs from 'availableOrgans' primarily involved "
        "(empty array if none)\n"
        "  • injuries — array of injury types from 'availableInjuries' commonly associated "
        "(empty array if none)\n"
        "Use ONLY names that appear exactly in the available lists.")
        .arg(QString::number(MAX_RESULTS_PER_JOB), name);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]      = QString{};
    replyFormat[QStringLiteral("conditions")] = QJsonArray{conditionEntrySchema()};
    payload[QStringLiteral("replyFormat")]    = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildMentalCompPayload(int page) const
{
    const QStringList existing  = loadMentalConditions();
    const QStringList bodyParts = loadAllBodyParts();
    const QStringList organs    = loadOrgans();
    const QStringList injuries  = loadInjuries();
    const QStringList symptoms  = loadSymptoms();

    QJsonObject payload;
    payload[QStringLiteral("task")]                     = TASK_MENTAL_COMPLETION;
    payload[QStringLiteral("page")]                     = page;
    payload[QStringLiteral("maxResults")]               = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingConditionCount")]   = existing.size();
    payload[QStringLiteral("existingMentalConditions")] = toJsonArray(existing);
    payload[QStringLiteral("availableBodyParts")]       = toJsonArray(bodyParts);
    payload[QStringLiteral("availableOrgans")]          = toJsonArray(organs);
    payload[QStringLiteral("availableInjuries")]        = toJsonArray(injuries);
    payload[QStringLiteral("availableSymptoms")]        = toJsonArray(symptoms);
    payload[QStringLiteral("instructions")]             = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "This is a completion sweep. List up to %1 significant MENTAL health conditions "
        "NOT already in 'existingMentalConditions'. Include any well-known condition "
        "that may not have been captured through brain-part-specific jobs. "
        "For each condition provide:\n"
        "  • name — condition name\n"
        "  • populationPercentage — estimated %% of global population affected (0–100)\n"
        "  • healingDifficulty — integer 1, 2, or 3:\n"
        "      1 = can be resolved without too much effort\n"
        "      2 = some people resolve it permanently but requires effort; normal medicine often fails\n"
        "      3 = almost no one resolves it permanently; claimed recoveries are not fully verified\n"
        "  • bodyParts — array of body parts from 'availableBodyParts' most affected\n"
        "  • symptoms — array of symptoms from 'availableSymptoms' commonly seen "
        "(empty array if none apply)\n"
        "  • organs — array of organs from 'availableOrgans' primarily involved "
        "(empty array if none)\n"
        "  • injuries — array of injury types from 'availableInjuries' commonly associated "
        "(empty array if none)\n"
        "Use ONLY names that appear exactly in the available lists.")
        .arg(MAX_RESULTS_PER_JOB);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]      = QString{};
    replyFormat[QStringLiteral("conditions")] = QJsonArray{conditionEntrySchema()};
    payload[QStringLiteral("replyFormat")]    = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildRecentPayload(int page) const
{
    const QStringList existing       = loadConditions();
    const QStringList existingMental = loadMentalConditions();
    const QStringList bodyParts      = loadAllBodyParts();
    const QStringList organs         = loadOrgans();
    const QStringList injuries       = loadInjuries();
    const QStringList symptoms       = loadSymptoms();

    QJsonObject payload;
    payload[QStringLiteral("task")]                     = TASK_RECENT_CONDITIONS;
    payload[QStringLiteral("page")]                     = page;
    payload[QStringLiteral("maxResults")]               = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingConditionCount")]   = existing.size();
    payload[QStringLiteral("existingConditions")]       = toJsonArray(existing);
    payload[QStringLiteral("existingMentalCount")]      = existingMental.size();
    payload[QStringLiteral("existingMentalConditions")] = toJsonArray(existingMental);
    payload[QStringLiteral("availableBodyParts")]       = toJsonArray(bodyParts);
    payload[QStringLiteral("availableOrgans")]          = toJsonArray(organs);
    payload[QStringLiteral("availableInjuries")]        = toJsonArray(injuries);
    payload[QStringLiteral("availableSymptoms")]        = toJsonArray(symptoms);
    payload[QStringLiteral("instructions")]             = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 health or mental health conditions newly documented, officially "
        "recognised, or significantly reclassified in the last 3 years (2023–2026). "
        "Exclude any condition already in 'existingConditions' or "
        "'existingMentalConditions'. "
        "For each condition provide:\n"
        "  • name — condition name\n"
        "  • isMental — true if mental health condition, false otherwise\n"
        "  • populationPercentage — estimated %% of global population affected (0–100)\n"
        "  • healingDifficulty — integer 1, 2, or 3:\n"
        "      1 = can be resolved without too much effort\n"
        "      2 = some people resolve it permanently but requires effort; normal medicine often fails\n"
        "      3 = almost no one resolves it permanently; claimed recoveries are not fully verified\n"
        "  • bodyParts — array of body parts from 'availableBodyParts' most affected "
        "(empty array if none)\n"
        "  • symptoms — array of symptoms from 'availableSymptoms' commonly seen "
        "(empty array if none)\n"
        "  • organs — array of organs from 'availableOrgans' primarily involved "
        "(empty array if none)\n"
        "  • injuries — array of injury types from 'availableInjuries' commonly associated "
        "(empty array if none)\n"
        "Use ONLY names that appear exactly in the available lists.")
        .arg(MAX_RESULTS_PER_JOB);

    QJsonObject schema = conditionEntrySchema();
    schema[QStringLiteral("isMental")] = false;

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]      = QString{};
    replyFormat[QStringLiteral("conditions")] = QJsonArray{schema};
    payload[QStringLiteral("replyFormat")]    = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildGoalsPayload(int page) const
{
    const QStringList existing  = loadGoals();
    const QStringList bodyParts = loadAllBodyParts();
    const QStringList organs    = loadOrgans();

    QJsonObject payload;
    payload[QStringLiteral("task")]               = TASK_GOALS;
    payload[QStringLiteral("page")]               = page;
    payload[QStringLiteral("maxResults")]         = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("existingCount")]      = existing.size();
    payload[QStringLiteral("existingGoals")]      = toJsonArray(existing);
    payload[QStringLiteral("availableBodyParts")] = toJsonArray(bodyParts);
    payload[QStringLiteral("availableOrgans")]    = toJsonArray(organs);
    payload[QStringLiteral("instructions")]       = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "List up to %1 distinct health or wellness goals (e.g. \"Lose Weight\", "
        "\"Build Muscle\", \"Reduce Anxiety\", \"Improve Sleep\", \"Quit Smoking\", "
        "\"Lower Blood Pressure\", \"Increase Flexibility\"). "
        "Goals should be broad, aspirational outcomes that many people seek — not "
        "disease treatments or medical procedures. "
        "Do NOT include any goal already in 'existingGoals'. "
        "For each goal provide:\n"
        "  • name — the goal name (concise, title-case)\n"
        "  • difficulty — integer 1, 2, or 3:\n"
        "      1 = achievable without too much effort\n"
        "      2 = some people achieve it permanently but requires significant effort; "
        "standard approaches often fail\n"
        "      3 = almost no one achieves it permanently; claimed successes are not "
        "fully verified\n"
        "  • bodyParts — array of body parts from 'availableBodyParts' primarily "
        "involved (empty array if none apply or availableBodyParts is empty)\n"
        "  • organs — array of organs from 'availableOrgans' whose proper function "
        "is key to achieving this goal (e.g. Liver for detox goals, Heart for "
        "cardiovascular goals; empty array if none apply or availableOrgans is empty)\n"
        "Use ONLY names that appear exactly in the available lists.")
        .arg(MAX_RESULTS_PER_JOB);

    QJsonObject entrySchema;
    entrySchema[QStringLiteral("name")]       = QStringLiteral("string — goal name");
    entrySchema[QStringLiteral("difficulty")] = QStringLiteral("integer 1, 2, or 3");
    entrySchema[QStringLiteral("bodyParts")]  = QJsonArray{};
    entrySchema[QStringLiteral("organs")]     = QJsonArray{};

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")] = QString{};
    replyFormat[QStringLiteral("goals")] = QJsonArray{entrySchema};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

QJsonObject GeneratorHealth::buildDifficultyPayload(bool isMental, int page) const
{
    const QStringList unscored = loadUnscoredConditionNames(isMental);

    QJsonObject payload;
    payload[QStringLiteral("task")]       = isMental ? TASK_MENTAL_CONDITION_DIFFICULTY
                                                     : TASK_CONDITION_DIFFICULTY;
    payload[QStringLiteral("page")]       = page;
    payload[QStringLiteral("maxResults")] = MAX_RESULTS_PER_JOB;
    payload[QStringLiteral("conditions")] = toJsonArray(unscored);
    payload[QStringLiteral("instructions")] = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "For each condition listed in 'conditions', assign a healingDifficulty score:\n"
        "  • 1 — can be resolved without too much effort\n"
        "  • 2 — some people resolve it permanently but requires significant effort; "
        "normal medicine often fails\n"
        "  • 3 — almost no one resolves it permanently; claimed recoveries are not fully verified\n"
        "Return exactly one entry per condition. Do NOT omit any condition from the list. "
        "If 'conditions' is empty, return an empty 'scoredConditions' array.");

    QJsonObject entrySchema;
    entrySchema[QStringLiteral("name")]              = QStringLiteral("string — condition name");
    entrySchema[QStringLiteral("healingDifficulty")] = QStringLiteral("integer 1, 2, or 3");

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]            = QString{};
    replyFormat[QStringLiteral("scoredConditions")] = QJsonArray{entrySchema};
    payload[QStringLiteral("replyFormat")] = replyFormat;
    return payload;
}

// ---- buildJobPayload --------------------------------------------------------

QJsonObject GeneratorHealth::buildJobPayload(const QString &jobId) const
{
    if (isBpGeneralJobId(jobId)) {
        return buildBpPayload(false, pageFromJobId(jobId));
    }
    if (isBpBrainJobId(jobId)) {
        return buildBpPayload(true, pageFromJobId(jobId));
    }
    if (isOrgansJobId(jobId)) {
        return buildOrgansPayload(pageFromJobId(jobId));
    }
    if (isInjuriesJobId(jobId)) {
        return buildInjuriesPayload(pageFromJobId(jobId));
    }
    if (isSymptomsForBpJobId(jobId)) {
        return buildSymptomsForBpPayload(entitySlug(jobId));
    }
    if (isOrgansForBpJobId(jobId)) {
        return buildOrgansForBpPayload(entitySlug(jobId));
    }
    if (isConditionsForSymptomJobId(jobId)) {
        return buildCondForSymptomPayload(entitySlug(jobId));
    }
    if (isMentalForBrainPartJobId(jobId)) {
        return buildMentalForBpPayload(entitySlug(jobId));
    }
    if (isMentalCompletionJobId(jobId)) {
        return buildMentalCompPayload(pageFromJobId(jobId));
    }
    if (isRecentJobId(jobId)) {
        return buildRecentPayload(pageFromJobId(jobId));
    }
    if (isConditionDifficultyJobId(jobId)) {
        return buildDifficultyPayload(/*isMental=*/false, pageFromJobId(jobId));
    }
    if (isMentalDifficultyJobId(jobId)) {
        return buildDifficultyPayload(/*isMental=*/true, pageFromJobId(jobId));
    }
    if (isGoalsJobId(jobId)) {
        return buildGoalsPayload(pageFromJobId(jobId));
    }
    qDebug() << "GeneratorHealth: unknown job ID:" << jobId;
    return {};
}

// ---- Condition recording helper ---------------------------------------------

void GeneratorHealth::recordConditions(const QJsonArray &conditions, bool isMental,
                                        QSet<QString> &seen)
{
    const QString attrId    = isMental
        ? QStringLiteral("PageAttributesHealthMentalCondition")
        : QStringLiteral("PageAttributesHealthCondition");
    const QString &idName   = isMental
        ? PageAttributesHealthMentalCondition::ID_NAME
        : PageAttributesHealthCondition::ID_NAME;
    const QString &idPct    = isMental
        ? PageAttributesHealthMentalCondition::ID_POPULATION_PERCENTAGE
        : PageAttributesHealthCondition::ID_POPULATION_PERCENTAGE;
    const QString &idBps    = isMental
        ? PageAttributesHealthMentalCondition::ID_BODY_PARTS
        : PageAttributesHealthCondition::ID_BODY_PARTS;
    const QString &idSyms   = isMental
        ? PageAttributesHealthMentalCondition::ID_SYMPTOMS
        : PageAttributesHealthCondition::ID_SYMPTOMS;
    const QString &idOrgs   = isMental
        ? PageAttributesHealthMentalCondition::ID_ORGANS
        : PageAttributesHealthCondition::ID_ORGANS;
    const QString &idInjs   = isMental
        ? PageAttributesHealthMentalCondition::ID_INJURIES
        : PageAttributesHealthCondition::ID_INJURIES;
    const QString &idDiff   = isMental
        ? PageAttributesHealthMentalCondition::ID_HEALING_DIFFICULTY
        : PageAttributesHealthCondition::ID_HEALING_DIFFICULTY;

    // Build allowed-value sets to prevent Claude from referencing non-existent entries.
    const QStringList bpList  = loadAllBodyParts();
    const QStringList symList = loadSymptoms();
    const QStringList orgList = loadOrgans();
    const QStringList injList = loadInjuries();
    const QSet<QString> availBp (bpList.begin(),  bpList.end());
    const QSet<QString> availSym(symList.begin(), symList.end());
    const QSet<QString> availOrg(orgList.begin(), orgList.end());
    const QSet<QString> availInj(injList.begin(), injList.end());

    for (const QJsonValue &val : conditions) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj  = val.toObject();
        const QString name     = obj.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);

        const QString pct  = cleanPercentage(
            obj.value(QStringLiteral("populationPercentage")).toVariant().toString());
        const QString diff = cleanDifficulty(
            obj.value(QStringLiteral("healingDifficulty")).toVariant().toString());
        const QString bps  = toCommaSeparated(
            obj.value(QStringLiteral("bodyParts")).toArray(), availBp);
        const QString syms = toCommaSeparated(
            obj.value(QStringLiteral("symptoms")).toArray(), availSym);
        const QString orgs = toCommaSeparated(
            obj.value(QStringLiteral("organs")).toArray(), availOrg);
        const QString injs = toCommaSeparated(
            obj.value(QStringLiteral("injuries")).toArray(), availInj);

        QHash<QString, QString> attrs;
        attrs.insert(idName, name);
        attrs.insert(idPct, pct);
        attrs.insert(idDiff, diff);
        if (!bps.isEmpty()) {
            attrs.insert(idBps, bps);
        }
        if (!syms.isEmpty()) {
            attrs.insert(idSyms, syms);
        }
        if (!orgs.isEmpty()) {
            attrs.insert(idOrgs, orgs);
        }
        if (!injs.isEmpty()) {
            attrs.insert(idInjs, injs);
        }
        recordResultPage(attrId, attrs);
    }
}

// ---- processReply -----------------------------------------------------------

void GeneratorHealth::processReply(const QString &jobId, const QJsonObject &reply)
{
    // --- Steps 1 & 2: body parts (general and brain) -------------------------
    if (isBpGeneralJobId(jobId) || isBpBrainJobId(jobId)) {
        const bool isBrain     = isBpBrainJobId(jobId);
        const int  page        = pageFromJobId(jobId);
        const QJsonArray names = reply.value(QStringLiteral("values")).toArray();

        // Pre-populate from DB to guard against duplicates across jobs.
        const QStringList existingList = loadBodyParts(isBrain);
        QSet<QString> seen(existingList.begin(), existingList.end());

        for (const QJsonValue &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);

            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthBodyPart::ID_BODY_PART_NAME, name);
            attrs.insert(PageAttributesHealthBodyPart::ID_IS_BRAIN_PART,
                         isBrain ? QStringLiteral("true") : QStringLiteral("false"));
            recordResultPage(QStringLiteral("PageAttributesHealthBodyPart"), attrs);

            // Discover per-body-part symptom and organ jobs.
            const QString slug = slugify(name);
            addDiscoveredJob(QStringLiteral("symptom/bp/") + slug);
            addDiscoveredJob(QStringLiteral("organ/bp/")   + slug);

            // Brain parts additionally drive mental-condition jobs.
            if (isBrain) {
                addDiscoveredJob(QStringLiteral("mental/bp/") + slug);
            }
        }

        const int newCount = seen.size() - existingList.size();
        qDebug() << "GeneratorHealth: body parts" << (isBrain ? "(brain)" : "(general)")
                 << "page" << page << "—" << names.size() << "received,"
                 << newCount << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthBodyPart")) << "total in DB";

        if (names.size() < MAX_RESULTS_PER_JOB) {
            // Last page: mark step complete.
            markPaginatedStepDone(isBrain ? QStringLiteral("bp_brain")
                                          : QStringLiteral("bp_general"));
        } else {
            // Full page: spawn next.
            const QString prefix = isBrain ? QStringLiteral("bp/brain/")
                                           : QStringLiteral("bp/general/");
            addDiscoveredJob(prefix + QString::number(page + 1));
        }
        return;
    }

    // --- Step 3: organs -------------------------------------------------------
    if (isOrgansJobId(jobId)) {
        const int       page  = pageFromJobId(jobId);
        const QJsonArray names = reply.value(QStringLiteral("values")).toArray();

        const QStringList existingList = loadOrgans();
        QSet<QString> seen(existingList.begin(), existingList.end());

        for (const QJsonValue &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthOrgan::ID_ORGAN_NAME, name);
            recordResultPage(QStringLiteral("PageAttributesHealthOrgan"), attrs);
        }

        const int newCount = seen.size() - existingList.size();
        qDebug() << "GeneratorHealth: organs page" << page
                 << "—" << names.size() << "received,"
                 << newCount << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthOrgan")) << "total in DB";

        if (names.size() < MAX_RESULTS_PER_JOB) {
            markPaginatedStepDone(QStringLiteral("organs"));
        } else {
            addDiscoveredJob(QStringLiteral("organ/") + QString::number(page + 1));
        }
        return;
    }

    // --- Step 3b: injuries ----------------------------------------------------
    if (isInjuriesJobId(jobId)) {
        const int        page  = pageFromJobId(jobId);
        const QJsonArray names = reply.value(QStringLiteral("values")).toArray();

        const QStringList existingList = loadInjuries();
        QSet<QString> seen(existingList.begin(), existingList.end());

        for (const QJsonValue &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthInjury::ID_INJURY_NAME, name);
            recordResultPage(QStringLiteral("PageAttributesHealthInjury"), attrs);
        }

        const int newCount = seen.size() - existingList.size();
        qDebug() << "GeneratorHealth: injuries page" << page
                 << "—" << names.size() << "received,"
                 << newCount << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthInjury")) << "total in DB";

        if (names.size() < MAX_RESULTS_PER_JOB) {
            markPaginatedStepDone(QStringLiteral("injuries"));
        } else {
            addDiscoveredJob(QStringLiteral("injury/") + QString::number(page + 1));
        }
        return;
    }

    // --- Step 4: symptoms per body part ---------------------------------------
    if (isSymptomsForBpJobId(jobId)) {
        const QJsonArray names     = reply.value(QStringLiteral("values")).toArray();
        const QStringList existing = loadSymptoms();
        QSet<QString> seen(existing.begin(), existing.end());

        for (const QJsonValue &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthSymptom::ID_SYMPTOM_NAME, name);
            recordResultPage(QStringLiteral("PageAttributesHealthSymptom"), attrs);

            // Spawn a physical-condition discovery job for each new symptom.
            addDiscoveredJob(QStringLiteral("condition/symptom/") + slugify(name));
        }

        const int newCount = seen.size() - existing.size();
        qDebug() << "GeneratorHealth: symptoms for body part" << entitySlug(jobId)
                 << "—" << names.size() << "received,"
                 << newCount << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthSymptom")) << "total in DB";
        return;
    }

    // --- Step 5: additional organs per body part ------------------------------
    if (isOrgansForBpJobId(jobId)) {
        const QJsonArray names     = reply.value(QStringLiteral("values")).toArray();
        const QStringList existing = loadOrgans();
        QSet<QString> seen(existing.begin(), existing.end());

        for (const QJsonValue &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthOrgan::ID_ORGAN_NAME, name);
            recordResultPage(QStringLiteral("PageAttributesHealthOrgan"), attrs);
        }

        const int newCount = seen.size() - existing.size();
        qDebug() << "GeneratorHealth: organs for body part" << entitySlug(jobId)
                 << "—" << names.size() << "received,"
                 << newCount << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthOrgan")) << "total in DB";
        return;
    }

    // --- Step 6: physical conditions per symptom ------------------------------
    if (isConditionsForSymptomJobId(jobId)) {
        const QJsonArray conds     = reply.value(QStringLiteral("conditions")).toArray();
        const QStringList existing = loadConditions();
        QSet<QString> seen(existing.begin(), existing.end());
        const int seenBefore = seen.size();
        recordConditions(conds, /*isMental=*/false, seen);
        qDebug() << "GeneratorHealth: physical conditions for symptom" << entitySlug(jobId)
                 << "—" << conds.size() << "received,"
                 << (seen.size() - seenBefore) << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthCondition")) << "total in DB";
        return;
    }

    // --- Step 7: mental conditions per brain part ----------------------------
    if (isMentalForBrainPartJobId(jobId)) {
        const QJsonArray conds     = reply.value(QStringLiteral("conditions")).toArray();
        const QStringList existing = loadMentalConditions();
        QSet<QString> seen(existing.begin(), existing.end());
        const int seenBefore = seen.size();
        recordConditions(conds, /*isMental=*/true, seen);
        qDebug() << "GeneratorHealth: mental conditions for brain part" << entitySlug(jobId)
                 << "—" << conds.size() << "received,"
                 << (seen.size() - seenBefore) << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthMentalCondition")) << "total in DB";
        return;
    }

    // --- Step 8: mental condition completion sweep ----------------------------
    if (isMentalCompletionJobId(jobId)) {
        const int        page  = pageFromJobId(jobId);
        const QJsonArray conds = reply.value(QStringLiteral("conditions")).toArray();
        const QStringList existing = loadMentalConditions();
        QSet<QString> seen(existing.begin(), existing.end());
        const int seenBefore = seen.size();
        recordConditions(conds, /*isMental=*/true, seen);
        qDebug() << "GeneratorHealth: mental completion page" << page
                 << "—" << conds.size() << "received,"
                 << (seen.size() - seenBefore) << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthMentalCondition")) << "total in DB";

        if (conds.size() < MAX_RESULTS_PER_JOB) {
            markPaginatedStepDone(QStringLiteral("mental_completion"));
        } else {
            addDiscoveredJob(QStringLiteral("mental/completion/") + QString::number(page + 1));
        }
        return;
    }

    // --- Step 9: recent conditions -------------------------------------------
    if (isRecentJobId(jobId)) {
        const int        page  = pageFromJobId(jobId);
        const QJsonArray conds = reply.value(QStringLiteral("conditions")).toArray();

        // Pre-populate both seen sets to avoid duplicates across both tables.
        const QStringList existingPhys   = loadConditions();
        const QStringList existingMental = loadMentalConditions();
        QSet<QString> seenPhys  (existingPhys.begin(),   existingPhys.end());
        QSet<QString> seenMental(existingMental.begin(), existingMental.end());

        for (const QJsonValue &val : conds) {
            if (!val.isObject()) {
                continue;
            }
            const QJsonObject obj = val.toObject();
            const bool isMental   = obj.value(QStringLiteral("isMental")).toBool();
            QSet<QString> &seen   = isMental ? seenMental : seenPhys;
            const QJsonArray wrap{val}; // recordConditions expects an array
            recordConditions(wrap, isMental, seen);
        }

        const int newPhys   = seenPhys.size()   - existingPhys.size();
        const int newMental = seenMental.size() - existingMental.size();
        qDebug() << "GeneratorHealth: recent conditions page" << page
                 << "—" << conds.size() << "received,"
                 << newPhys << "new physical (total:"
                 << dbCount(QStringLiteral("PageAttributesHealthCondition")) << "),"
                 << newMental << "new mental (total:"
                 << dbCount(QStringLiteral("PageAttributesHealthMentalCondition")) << ")";

        if (conds.size() < MAX_RESULTS_PER_JOB) {
            markPaginatedStepDone(QStringLiteral("recent"));
        } else {
            addDiscoveredJob(QStringLiteral("recent/") + QString::number(page + 1));
        }
        return;
    }

    // --- Backfill: healingDifficulty for existing conditions -------------------
    if (isConditionDifficultyJobId(jobId) || isMentalDifficultyJobId(jobId)) {
        const bool isMental = isMentalDifficultyJobId(jobId);
        const int  page     = pageFromJobId(jobId);
        const QJsonArray scored = reply.value(QStringLiteral("scoredConditions")).toArray();

        const QString attrId  = isMental
            ? QStringLiteral("PageAttributesHealthMentalCondition")
            : QStringLiteral("PageAttributesHealthCondition");
        const QString nameCol = isMental
            ? PageAttributesHealthMentalCondition::ID_NAME
            : PageAttributesHealthCondition::ID_NAME;
        const QString diffCol = isMental
            ? PageAttributesHealthMentalCondition::ID_HEALING_DIFFICULTY
            : PageAttributesHealthCondition::ID_HEALING_DIFFICULTY;

        int updated = 0;
        const DownloadedPagesTable *table = resultsTable(attrId);
        if (table && !scored.isEmpty()) {
            QSqlQuery q(table->database());
            q.prepare(QStringLiteral("UPDATE records SET \"%1\" = :val WHERE \"%2\" = :name")
                          .arg(diffCol, nameCol));
            for (const QJsonValue &val : scored) {
                if (!val.isObject()) {
                    continue;
                }
                const QJsonObject obj = val.toObject();
                const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
                const QString diff = cleanDifficulty(
                    obj.value(QStringLiteral("healingDifficulty")).toVariant().toString());
                if (name.isEmpty()) {
                    continue;
                }
                q.bindValue(QStringLiteral(":val"), diff);
                q.bindValue(QStringLiteral(":name"), name);
                if (q.exec()) {
                    ++updated;
                }
            }
        }

        qDebug() << "GeneratorHealth: difficulty backfill" << (isMental ? "mental" : "physical")
                 << "page" << page << "—" << scored.size() << "received," << updated << "updated";

        // After updating, query again at offset 0: scored rows are gone from the WHERE clause.
        const QStringList remaining = loadUnscoredConditionNames(isMental);
        if (remaining.isEmpty()) {
            markPaginatedStepDone(isMental ? QStringLiteral("mental_difficulty")
                                           : QStringLiteral("condition_difficulty"));
        } else {
            const QString prefix = isMental ? QStringLiteral("difficulty/mental/")
                                            : QStringLiteral("difficulty/condition/");
            addDiscoveredJob(prefix + QString::number(page + 1));
        }
        return;
    }

    // --- Health goals ---------------------------------------------------------
    if (isGoalsJobId(jobId)) {
        const int        page  = pageFromJobId(jobId);
        const QJsonArray goals = reply.value(QStringLiteral("goals")).toArray();

        const QStringList existingList = loadGoals();
        QSet<QString> seen(existingList.begin(), existingList.end());

        const QStringList bpList  = loadAllBodyParts();
        const QStringList orgList = loadOrgans();
        const QSet<QString> availBp (bpList.begin(),  bpList.end());
        const QSet<QString> availOrg(orgList.begin(), orgList.end());

        int inserted = 0;
        for (const QJsonValue &val : goals) {
            if (!val.isObject()) {
                continue;
            }
            const QJsonObject obj = val.toObject();
            const QString name    = obj.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty() || seen.contains(name)) {
                continue;
            }
            seen.insert(name);

            const QString diff = cleanDifficulty(
                obj.value(QStringLiteral("difficulty")).toVariant().toString());
            const QString bps  = toCommaSeparated(
                obj.value(QStringLiteral("bodyParts")).toArray(), availBp);
            const QString orgs = toCommaSeparated(
                obj.value(QStringLiteral("organs")).toArray(), availOrg);

            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesHealthGoal::ID_NAME, name);
            attrs.insert(PageAttributesHealthGoal::ID_DIFFICULTY, diff);
            if (!bps.isEmpty()) {
                attrs.insert(PageAttributesHealthGoal::ID_BODY_PARTS, bps);
            }
            if (!orgs.isEmpty()) {
                attrs.insert(PageAttributesHealthGoal::ID_ORGANS, orgs);
            }
            recordResultPage(QStringLiteral("PageAttributesHealthGoal"), attrs);
            ++inserted;
        }

        qDebug() << "GeneratorHealth: health goals page" << page
                 << "—" << goals.size() << "received,"
                 << inserted << "new,"
                 << dbCount(QStringLiteral("PageAttributesHealthGoal")) << "total in DB";

        if (goals.size() < MAX_RESULTS_PER_JOB) {
            markPaginatedStepDone(QStringLiteral("goals"));
        } else {
            addDiscoveredJob(QStringLiteral("goal/") + QString::number(page + 1));
        }
        return;
    }

    qDebug() << "GeneratorHealth: processReply called for unknown job ID:" << jobId;
}
