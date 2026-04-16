#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "aspire/generator/AbstractGenerator.h"
#include "aspire/generator/GeneratorHealth.h"

// ---------------------------------------------------------------------------
// Per-test fixture — each test gets an isolated working directory.
// ---------------------------------------------------------------------------
struct Fixture {
    QTemporaryDir tmpDir;

    GeneratorHealth *makeGen(QObject *parent = nullptr) const
    {
        return new GeneratorHealth(QDir(tmpDir.path()), parent);
    }
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_Generator_Health : public QObject
{
    Q_OBJECT

private slots:

    // ==== Registry + identification =========================================

    void test_health_registered_in_all_generators()
    {
        QVERIFY(AbstractGenerator::ALL_GENERATORS().contains(
            QStringLiteral("health")));
    }

    void test_health_id_correct()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QCOMPARE(gen->getId(), QStringLiteral("health"));
    }

    void test_health_name_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(!gen->getName().isEmpty());
    }

    void test_health_create_instance_non_null()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(QStringLiteral("health"));
        QVERIFY(proto != nullptr);
        QScopedPointer<AbstractGenerator> inst(proto->createInstance(QDir(fx.tmpDir.path())));
        QVERIFY(inst != nullptr);
    }

    void test_health_create_instance_has_correct_id()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(QStringLiteral("health"));
        QVERIFY(proto != nullptr);
        QScopedPointer<AbstractGenerator> inst(proto->createInstance(QDir(fx.tmpDir.path())));
        QCOMPARE(inst->getId(), QStringLiteral("health"));
    }

    // ==== slugify ===========================================================

    void test_health_slugify_lowercase()
    {
        QCOMPARE(GeneratorHealth::slugify(QStringLiteral("Knee")),
                 QStringLiteral("knee"));
    }

    void test_health_slugify_spaces_to_hyphens()
    {
        QCOMPARE(GeneratorHealth::slugify(QStringLiteral("lower back")),
                 QStringLiteral("lower-back"));
    }

    void test_health_slugify_drops_non_ascii()
    {
        QCOMPARE(GeneratorHealth::slugify(QString::fromUtf8("d\xc3\xa9pression")),
                 QStringLiteral("dpression"));
    }

    void test_health_slugify_keeps_hyphens()
    {
        QCOMPARE(GeneratorHealth::slugify(QStringLiteral("anxiety-disorder")),
                 QStringLiteral("anxiety-disorder"));
    }

    // ==== Job-ID helpers ====================================================

    void test_health_page_from_job_id()
    {
        QCOMPARE(GeneratorHealth::pageFromJobId(QStringLiteral("bp/general/0")), 0);
        QCOMPARE(GeneratorHealth::pageFromJobId(QStringLiteral("bp/general/3")), 3);
        QCOMPARE(GeneratorHealth::pageFromJobId(QStringLiteral("recent/7")), 7);
    }

    void test_health_entity_slug_from_symptom_job()
    {
        QCOMPARE(GeneratorHealth::entitySlug(QStringLiteral("symptom/bp/knee")),
                 QStringLiteral("knee"));
    }

    void test_health_entity_slug_from_condition_job()
    {
        QCOMPARE(GeneratorHealth::entitySlug(QStringLiteral("condition/symptom/fatigue")),
                 QStringLiteral("fatigue"));
    }

    void test_health_is_bp_general_job_id_true()
    {
        QVERIFY(GeneratorHealth::isBpGeneralJobId(QStringLiteral("bp/general/0")));
        QVERIFY(GeneratorHealth::isBpGeneralJobId(QStringLiteral("bp/general/5")));
    }

    void test_health_is_bp_general_job_id_false_for_brain()
    {
        QVERIFY(!GeneratorHealth::isBpGeneralJobId(QStringLiteral("bp/brain/0")));
    }

    void test_health_is_bp_brain_job_id_true()
    {
        QVERIFY(GeneratorHealth::isBpBrainJobId(QStringLiteral("bp/brain/0")));
    }

    void test_health_is_bp_brain_job_id_false_for_general()
    {
        QVERIFY(!GeneratorHealth::isBpBrainJobId(QStringLiteral("bp/general/0")));
    }

    void test_health_is_organs_job_id_true()
    {
        QVERIFY(GeneratorHealth::isOrgansJobId(QStringLiteral("organ/0")));
    }

    void test_health_is_organs_job_id_false_for_organs_per_bp()
    {
        // "organ/bp/knee" is OrgansPerBodyPart, not Organs
        QVERIFY(!GeneratorHealth::isOrgansJobId(QStringLiteral("organ/bp/knee")));
    }

    void test_health_is_injuries_job_id_true()
    {
        QVERIFY(GeneratorHealth::isInjuriesJobId(QStringLiteral("injury/0")));
    }

    void test_health_is_injuries_job_id_false_for_other()
    {
        QVERIFY(!GeneratorHealth::isInjuriesJobId(QStringLiteral("organ/0")));
    }

    void test_health_is_symptoms_for_bp_job_id_true()
    {
        QVERIFY(GeneratorHealth::isSymptomsForBpJobId(QStringLiteral("symptom/bp/knee")));
    }

    void test_health_is_symptoms_for_bp_job_id_false_for_condition()
    {
        QVERIFY(!GeneratorHealth::isSymptomsForBpJobId(
            QStringLiteral("condition/symptom/fatigue")));
    }

    void test_health_is_organs_for_bp_job_id_true()
    {
        QVERIFY(GeneratorHealth::isOrgansForBpJobId(QStringLiteral("organ/bp/knee")));
    }

    void test_health_is_organs_for_bp_job_id_false_for_organs()
    {
        QVERIFY(!GeneratorHealth::isOrgansForBpJobId(QStringLiteral("organ/0")));
    }

    void test_health_is_conditions_for_symptom_job_id_true()
    {
        QVERIFY(GeneratorHealth::isConditionsForSymptomJobId(
            QStringLiteral("condition/symptom/fatigue")));
    }

    void test_health_is_mental_for_brain_part_job_id_true()
    {
        QVERIFY(GeneratorHealth::isMentalForBrainPartJobId(
            QStringLiteral("mental/bp/prefrontal-cortex")));
    }

    void test_health_is_mental_completion_job_id_true()
    {
        QVERIFY(GeneratorHealth::isMentalCompletionJobId(
            QStringLiteral("mental/completion/0")));
    }

    void test_health_is_mental_completion_job_id_false_for_bp()
    {
        QVERIFY(!GeneratorHealth::isMentalCompletionJobId(
            QStringLiteral("mental/bp/hippocampus")));
    }

    void test_health_is_recent_job_id_true()
    {
        QVERIFY(GeneratorHealth::isRecentJobId(QStringLiteral("recent/0")));
    }

    void test_health_is_condition_difficulty_job_id_true()
    {
        QVERIFY(GeneratorHealth::isConditionDifficultyJobId(
            QStringLiteral("difficulty/condition/0")));
    }

    void test_health_is_mental_difficulty_job_id_true()
    {
        QVERIFY(GeneratorHealth::isMentalDifficultyJobId(
            QStringLiteral("difficulty/mental/0")));
    }

    void test_health_is_mental_difficulty_job_id_false_for_condition()
    {
        QVERIFY(!GeneratorHealth::isMentalDifficultyJobId(
            QStringLiteral("difficulty/condition/0")));
    }

    void test_health_is_goals_job_id_true()
    {
        QVERIFY(GeneratorHealth::isGoalsJobId(QStringLiteral("goal/0")));
    }

    void test_health_is_goals_job_id_false_for_organ()
    {
        QVERIFY(!GeneratorHealth::isGoalsJobId(QStringLiteral("organ/0")));
    }

    // ==== buildInitialJobIds ================================================

    void test_health_initial_jobs_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(!gen->getAllJobIds().isEmpty());
    }

    void test_health_initial_jobs_contains_bp_general()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("bp/general/0")));
    }

    void test_health_initial_jobs_contains_bp_brain()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("bp/brain/0")));
    }

    void test_health_initial_jobs_contains_organ()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("organ/0")));
    }

    void test_health_initial_jobs_contains_injury()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("injury/0")));
    }

    void test_health_initial_jobs_contains_difficulty_condition()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("difficulty/condition/0")));
    }

    void test_health_initial_jobs_contains_difficulty_mental()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("difficulty/mental/0")));
    }

    void test_health_initial_jobs_contains_goal()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getAllJobIds().contains(QStringLiteral("goal/0")));
    }

    void test_health_initial_jobs_all_unique()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const QSet<QString> idSet(ids.begin(), ids.end());
        QCOMPARE(idSet.size(), ids.size());
    }

    // ==== getTables — primary ===============================================

    void test_health_get_tables_primary_has_one_entry()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QCOMPARE(gen->getTables().primary.size(), 1);
    }

    void test_health_get_tables_primary_id_is_condition()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().primary.contains(
            QStringLiteral("PageAttributesHealthCondition")));
    }

    void test_health_get_tables_primary_name_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(
            QStringLiteral("PageAttributesHealthCondition"));
        QVERIFY(!desc.name.isEmpty());
    }

    void test_health_get_tables_primary_path_ends_with_condition_db()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(
            QStringLiteral("PageAttributesHealthCondition"));
        QVERIFY(desc.tablePath.endsWith(
            QStringLiteral("PageAttributesHealthCondition.db")));
    }

    void test_health_get_tables_primary_path_under_working_dir()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(
            QStringLiteral("PageAttributesHealthCondition"));
        QVERIFY(desc.tablePath.startsWith(fx.tmpDir.path()));
    }

    // ==== getTables — category ==============================================

    void test_health_get_tables_category_has_two_entries()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QCOMPARE(gen->getTables().category.size(), 2);
    }

    void test_health_get_tables_category_contains_body_part()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().category.contains(
            QStringLiteral("PageAttributesHealthBodyPart")));
    }

    void test_health_get_tables_category_contains_organ()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().category.contains(
            QStringLiteral("PageAttributesHealthOrgan")));
    }

    // ==== getTables — referredTo ============================================

    void test_health_get_tables_referred_to_has_four_entries()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QCOMPARE(gen->getTables().referredTo.size(), 4);
    }

    void test_health_get_tables_referred_to_contains_symptom()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().referredTo.contains(
            QStringLiteral("PageAttributesHealthSymptom")));
    }

    void test_health_get_tables_referred_to_contains_injury()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().referredTo.contains(
            QStringLiteral("PageAttributesHealthInjury")));
    }

    void test_health_get_tables_referred_to_contains_mental_condition()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().referredTo.contains(
            QStringLiteral("PageAttributesHealthMentalCondition")));
    }

    void test_health_get_tables_referred_to_contains_goal()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->getTables().referredTo.contains(
            QStringLiteral("PageAttributesHealthGoal")));
    }

    void test_health_get_tables_all_ids_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const AbstractGenerator::GeneratorTables tables = gen->getTables();
        for (const auto &desc : std::as_const(tables.primary)) {
            QVERIFY(!desc.id.isEmpty());
        }
        for (const auto &desc : std::as_const(tables.category)) {
            QVERIFY(!desc.id.isEmpty());
        }
        for (const auto &desc : std::as_const(tables.referredTo)) {
            QVERIFY(!desc.id.isEmpty());
        }
    }

    void test_health_get_tables_all_names_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const AbstractGenerator::GeneratorTables tables = gen->getTables();
        for (const auto &desc : std::as_const(tables.primary)) {
            QVERIFY(!desc.name.isEmpty());
        }
        for (const auto &desc : std::as_const(tables.category)) {
            QVERIFY(!desc.name.isEmpty());
        }
        for (const auto &desc : std::as_const(tables.referredTo)) {
            QVERIFY(!desc.name.isEmpty());
        }
    }

    void test_health_get_tables_all_paths_end_with_db()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        const AbstractGenerator::GeneratorTables tables = gen->getTables();
        for (const auto &desc : std::as_const(tables.primary)) {
            QVERIFY(desc.tablePath.endsWith(QStringLiteral(".db")));
        }
        for (const auto &desc : std::as_const(tables.category)) {
            QVERIFY(desc.tablePath.endsWith(QStringLiteral(".db")));
        }
        for (const auto &desc : std::as_const(tables.referredTo)) {
            QVERIFY(desc.tablePath.endsWith(QStringLiteral(".db")));
        }
    }

    // ==== openResultsTable ==================================================

    void test_health_open_results_table_returns_non_null()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(gen->openResultsTable() != nullptr);
    }

    void test_health_results_table_condition_non_null()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        gen->openResultsTable();
        QVERIFY(gen->resultsTable(QStringLiteral("PageAttributesHealthCondition")) != nullptr);
    }

    void test_health_results_table_body_part_non_null()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        gen->openResultsTable();
        QVERIFY(gen->resultsTable(QStringLiteral("PageAttributesHealthBodyPart")) != nullptr);
    }

    void test_health_results_table_symptom_non_null()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        gen->openResultsTable();
        QVERIFY(gen->resultsTable(QStringLiteral("PageAttributesHealthSymptom")) != nullptr);
    }

    // ==== isStepComplete — fresh generator (no jobs processed) ==============

    void test_health_step_complete_bp_general_false_initially()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(!gen->isStepComplete(GeneratorHealth::Step::BodyPartsGeneral));
    }

    void test_health_step_complete_bp_brain_false_initially()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(!gen->isStepComplete(GeneratorHealth::Step::BodyPartsBrain));
    }

    void test_health_step_complete_conditions_false_initially()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorHealth> gen(fx.makeGen());
        QVERIFY(!gen->isStepComplete(GeneratorHealth::Step::ConditionsPerSymptom));
    }
};

QTEST_MAIN(Test_Generator_Health)
#include "test_generator_health.moc"
