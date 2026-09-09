#include <QtTest>

#include "website/VerticalSyncPolicy.h"
#include "website/AbstractEngine.h"

/**
 * Guards the drift that broke PaneDomains::deployLocally(): the vertical gating
 * rule was hand-written at three call sites and one of them ran BOTH verticals'
 * syncers unconditionally. These tests pin the rule itself, and pin that the
 * engines' getGeneratorId() values still line up with it — a rename on either
 * side would otherwise silently disable all hub syncing.
 */
class Test_Website_VerticalSyncPolicy : public QObject
{
    Q_OBJECT

private slots:
    void test_verticalsync_fashion_true_for_fashion_generator();
    void test_verticalsync_fashion_false_for_health_generator();
    void test_verticalsync_symptom_true_for_health_generator();
    void test_verticalsync_symptom_false_for_fashion_generator();
    void test_verticalsync_both_false_for_empty_generator_id();
    void test_verticalsync_both_false_for_unknown_generator_id();
    void test_verticalsync_never_both_true_for_one_id();
    void test_verticalsync_matches_registered_engine_generator_ids();
};

void Test_Website_VerticalSyncPolicy::test_verticalsync_fashion_true_for_fashion_generator()
{
    QVERIFY(VerticalSyncPolicy::needsFashionHubSync(
        QLatin1String(VerticalSyncPolicy::GENERATOR_FASHION_TAXONOMY)));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_fashion_false_for_health_generator()
{
    // The bug: a Health site ran the Fashion tag-hub syncer.
    QVERIFY(!VerticalSyncPolicy::needsFashionHubSync(
        QLatin1String(VerticalSyncPolicy::GENERATOR_HEALTH)));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_symptom_true_for_health_generator()
{
    QVERIFY(VerticalSyncPolicy::needsSymptomHubSync(
        QLatin1String(VerticalSyncPolicy::GENERATOR_HEALTH)));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_symptom_false_for_fashion_generator()
{
    // The mirror bug: a Fashion site ran the Health symptom-hub syncer, which
    // is how /symptoms pages appeared on a fashion site before the scoping.
    QVERIFY(!VerticalSyncPolicy::needsSymptomHubSync(
        QLatin1String(VerticalSyncPolicy::GENERATOR_FASHION_TAXONOMY)));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_both_false_for_empty_generator_id()
{
    // An engine that declares no generator must never grow another vertical's
    // pages — empty must not be treated as "anything goes".
    QVERIFY(!VerticalSyncPolicy::needsFashionHubSync(QString{}));
    QVERIFY(!VerticalSyncPolicy::needsSymptomHubSync(QString{}));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_both_false_for_unknown_generator_id()
{
    QVERIFY(!VerticalSyncPolicy::needsFashionHubSync(QStringLiteral("languages")));
    QVERIFY(!VerticalSyncPolicy::needsSymptomHubSync(QStringLiteral("languages")));
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_never_both_true_for_one_id()
{
    const QStringList ids = {
        QLatin1String(VerticalSyncPolicy::GENERATOR_HEALTH),
        QLatin1String(VerticalSyncPolicy::GENERATOR_FASHION_TAXONOMY),
        QStringLiteral("languages"),
        QString{},
    };
    for (const QString &id : ids) {
        const bool fashion = VerticalSyncPolicy::needsFashionHubSync(id);
        const bool symptom = VerticalSyncPolicy::needsSymptomHubSync(id);
        QVERIFY2(!(fashion && symptom),
                 qPrintable(QStringLiteral("both syncers enabled for id '%1'").arg(id)));
    }
}

void Test_Website_VerticalSyncPolicy::test_verticalsync_matches_registered_engine_generator_ids()
{
    // Every registered engine's generator id must be recognised by at least one
    // policy function, OR be deliberately vertical-less. This is what catches a
    // rename of GeneratorHealth::getId() / GeneratorFashionTaxonomy::getId()
    // that would otherwise turn all hub syncing into a silent no-op.
    bool sawFashion = false;
    bool sawHealth  = false;
    const auto &engines = AbstractEngine::ALL_ENGINES();
    for (auto it = engines.cbegin(); it != engines.cend(); ++it) {
        const AbstractEngine *engine = it.value();
        QVERIFY(engine != nullptr);
        const QString id = engine->getGeneratorId();
        if (VerticalSyncPolicy::needsFashionHubSync(id)) { sawFashion = true; }
        if (VerticalSyncPolicy::needsSymptomHubSync(id))  { sawHealth  = true; }
    }
    QVERIFY2(sawFashion, "no registered engine maps to the fashion taxonomy generator");
    QVERIFY2(sawHealth,  "no registered engine maps to the health generator");
}

QTEST_MAIN(Test_Website_VerticalSyncPolicy)
#include "test_vertical_sync_policy.moc"
