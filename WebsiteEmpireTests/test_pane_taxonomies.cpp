#include <QtTest>

#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include "gui/panes/PaneTaxonomies.h"
#include "website/AbstractEngine.h"
#include "website/EngineArticles.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// True if any QLabel descendant of pane displays exactly text — the
// taxonomy card title is rendered as "<b>DisplayName</b>", so match on the
// substring rather than exact equality.
bool hasLabelContaining(const QWidget *pane, const QString &text)
{
    const auto labels = pane->findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        if (label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

} // namespace

// =============================================================================
// Test_Website_PaneTaxonomies
// =============================================================================

class Test_Website_PaneTaxonomies : public QObject
{
    Q_OBJECT

private slots:
    void test_panetaxonomies_health_engine_shows_symptoms_card();
    void test_panetaxonomies_fashion_engine_shows_no_symptoms_card();
    void test_panetaxonomies_fashion_engine_shows_ten_taxonomy_cards();
    void test_panetaxonomies_null_engine_does_not_crash();
};

void Test_Website_PaneTaxonomies::test_panetaxonomies_health_engine_shows_symptoms_card()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    PaneTaxonomies pane;
    pane.setup(QDir(dir.path()), &engine);
    pane.setVisible(true);

    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Symptoms")));
}

void Test_Website_PaneTaxonomies::test_panetaxonomies_fashion_engine_shows_no_symptoms_card()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    PaneTaxonomies pane;
    pane.setup(QDir(dir.path()), &engine);
    pane.setVisible(true);

    QVERIFY(!hasLabelContaining(&pane, QStringLiteral("Symptoms")));
}

void Test_Website_PaneTaxonomies::test_panetaxonomies_fashion_engine_shows_ten_taxonomy_cards()
{
    // PageTypeArticleFashion's PageBlocFashionTaxonomyLinks declares ten
    // taxonomies via the plural taxonomies() — one card each, none of them
    // "Symptoms" (covered by the sibling test above).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    PaneTaxonomies pane;
    pane.setup(QDir(dir.path()), &engine);
    pane.setVisible(true);

    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Color")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Season")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Occasion")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Material")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Style Aesthetic")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Product Type")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Demographic")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Fit/Silhouette")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Pattern")));
    QVERIFY(hasLabelContaining(&pane, QStringLiteral("Culture")));

    // 10 taxonomies x 2 buttons each ("Browse…" + "Sync") == 20.
    QCOMPARE(pane.findChildren<QPushButton *>().size(), 20);
}

void Test_Website_PaneTaxonomies::test_panetaxonomies_null_engine_does_not_crash()
{
    // Before a working directory/engine is selected, setup() may be called
    // with a null engine — must not crash on show.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PaneTaxonomies pane;
    pane.setup(QDir(dir.path()), nullptr);
    pane.setVisible(true);
    QVERIFY(pane.findChildren<QLabel *>().isEmpty());
}

QTEST_MAIN(Test_Website_PaneTaxonomies)
#include "test_pane_taxonomies.moc"
