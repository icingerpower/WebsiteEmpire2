#include <QtTest>

#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageTypeFashionTagHub.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"

// =============================================================================
// Test_Website_PageTypeFashionTagHub
// =============================================================================

class Test_Website_PageTypeFashionTagHub : public QObject
{
    Q_OBJECT

private slots:
    void test_fashiontaghub_get_type_id();
    void test_fashiontaghub_get_display_name_non_empty();
    void test_fashiontaghub_appends_article_without_moving_existing_blocs();
    void test_fashiontaghub_first_bloc_is_hub_grid();
    void test_fashiontaghub_registered_in_all_type_ids();
    void test_fashiontaghub_create_for_type_id_returns_instance();
    void test_fashiontaghub_create_for_type_id_matches_type_id();

    // --- dimension/tag_value round-trip via page_data ---
    void test_fashiontaghub_dimension_tag_value_round_trip();
};

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_get_type_id()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeFashionTagHub hub{categoryTable};
    QCOMPARE(hub.getTypeId(), QStringLiteral("fashion_tag_hub"));
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_get_display_name_non_empty()
{
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeFashionTagHub hub{categoryTable};
    QVERIFY(!hub.getDisplayName().isEmpty());
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_appends_article_without_moving_existing_blocs()
{
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeFashionTagHub hub{categoryTable};
    QCOMPARE(hub.getPageBlocs().size(), 4);
    QVERIFY(dynamic_cast<const PageBlocText *>(hub.getPageBlocs().at(3)));
    QCOMPARE(hub.getRenderBlocs().first(), hub.getPageBlocs().at(3));
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_first_bloc_is_hub_grid()
{
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeFashionTagHub hub{categoryTable};
    QVERIFY(dynamic_cast<const PageBlocFashionHubGrid *>(hub.getPageBlocs().first()) != nullptr);
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_registered_in_all_type_ids()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(QStringLiteral("fashion_tag_hub")));
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_create_for_type_id_returns_instance()
{
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    const auto instance = AbstractPageType::createForTypeId(
        QStringLiteral("fashion_tag_hub"), categoryTable);
    QVERIFY(instance != nullptr);
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_create_for_type_id_matches_type_id()
{
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    const auto instance = AbstractPageType::createForTypeId(
        QStringLiteral("fashion_tag_hub"), categoryTable);
    QVERIFY(instance != nullptr);
    QCOMPARE(instance->getTypeId(), QStringLiteral("fashion_tag_hub"));
}

void Test_Website_PageTypeFashionTagHub::test_fashiontaghub_dimension_tag_value_round_trip()
{
    // Goes through AbstractPageType::load()/save(), which prefixes every
    // bloc's keys with its index — PageBlocFashionHubGrid is bloc 0, so the
    // raw keys here are "0_dimension"/"0_tag_value", not the bloc-local bare
    // KEY_DIMENSION/KEY_TAG_VALUE (those are only valid when calling the
    // bloc's own load()/save() directly).
    QTemporaryDir dir;
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeFashionTagHub hub{categoryTable};

    const QString dimensionKey = QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION);
    const QString tagValueKey  = QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE);

    QHash<QString, QString> data = {
        {dimensionKey, QStringLiteral("fashion_color")},
        {tagValueKey,  QStringLiteral("Burgundy")},
    };
    hub.load(data);

    QHash<QString, QString> saved;
    hub.save(saved);
    QCOMPARE(saved.value(dimensionKey), QStringLiteral("fashion_color"));
    QCOMPARE(saved.value(tagValueKey),  QStringLiteral("Burgundy"));
}

QTEST_MAIN(Test_Website_PageTypeFashionTagHub)
#include "test_page_type_fashion_tag_hub.moc"
