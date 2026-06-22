/**
 * test_website_commonbloc_menu_permalink.cpp
 *
 * Verifies that CommonBlocMenuTop::addCode uses engine.resolvePermalink() so
 * that menu item URLs are rewritten to their language-specific slugs.
 *
 * Covered by commit 2283de676c9e96354659af2e74b847de472d451d:
 *   "Resolve translated permalinks in top and bottom menus"
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/AbstractEngine.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/commonblocs/CommonBlocMenuTop.h"
#include "website/commonblocs/MenuItem.h"

// =============================================================================
// Test_Website_CommonBlocMenuPermalink
// =============================================================================

class Test_Website_CommonBlocMenuPermalink : public QObject
{
    Q_OBJECT

private slots:
    void test_menutop_resolved_permalink_used_for_translated_lang();
    void test_menutop_external_url_not_replaced();
    void test_menutop_untranslated_url_kept_as_is();
};

// Helper: create a temporary dir with an engine_domains.csv that puts langCode
// at index 0, then init the engine with that dir.
static EngineArticles *makeEngine(const QString &dirPath,
                                  const QString &langCode,
                                  QObject       *parent = nullptr)
{
    const QString csvPath =
        QDir(dirPath).absoluteFilePath(QStringLiteral("engine_domains.csv"));
    QFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return nullptr;
    }
    QTextStream out(&csv);
    out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
    out << QStringLiteral("1;") << langCode
        << QStringLiteral(";") << langCode
        << QStringLiteral(";default;") << langCode
        << QStringLiteral(".example.com;;\n");
    csv.close();

    HostTable *hostTable = new HostTable(QDir(dirPath), parent);
    EngineArticles *engine = static_cast<EngineArticles *>(
        EngineArticles().create(parent));
    engine->init(QDir(dirPath), *hostTable);
    return engine;
}

void Test_Website_CommonBlocMenuPermalink::test_menutop_resolved_permalink_used_for_translated_lang()
{
    // Arrange: engine with "fr" at websiteIndex 0.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    EngineArticles *engine = makeEngine(dir.path(), QStringLiteral("fr"), this);
    QVERIFY(engine);
    QCOMPARE(engine->getLangCode(0), QStringLiteral("fr"));

    // Register a translated permalink: fr → /home → /accueil
    QHash<QString, QHash<QString, QString>> map;
    QHash<QString, QString> frMap;
    frMap.insert(QStringLiteral("/home"), QStringLiteral("/accueil"));
    map.insert(QStringLiteral("fr"), frMap);
    engine->setTranslatedPermalinks(map);

    // Set up a menu with one top-level item pointing to /home
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url   = QStringLiteral("/home");

    CommonBlocMenuTop menu;
    menu.setItems({item});

    // Act
    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    menu.addCode(QStringView{}, *engine, 0, html, css, js, cssDone, jsDone);

    // Assert: the translated URL must appear; the source URL must not
    QVERIFY2(html.contains(QStringLiteral("/accueil")),
             "Expected translated permalink /accueil in menu HTML");
    QVERIFY2(!html.contains(QStringLiteral("href=\"/home\"")),
             "Source permalink /home must not appear as href when a translation exists");
}

void Test_Website_CommonBlocMenuPermalink::test_menutop_external_url_not_replaced()
{
    // External URLs (http/https) must be passed through unchanged even when a
    // translated-permalink map is registered.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    EngineArticles *engine = makeEngine(dir.path(), QStringLiteral("fr"), this);
    QVERIFY(engine);

    // Register a translated permalink that would be used if the item URL were internal
    QHash<QString, QHash<QString, QString>> map;
    QHash<QString, QString> frMap;
    frMap.insert(QStringLiteral("https://external.example.com"), QStringLiteral("/should-not-apply"));
    map.insert(QStringLiteral("fr"), frMap);
    engine->setTranslatedPermalinks(map);

    MenuItem item;
    item.label = QStringLiteral("External");
    item.url   = QStringLiteral("https://external.example.com");

    CommonBlocMenuTop menu;
    menu.setItems({item});

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    menu.addCode(QStringView{}, *engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY2(html.contains(QStringLiteral("https://external.example.com")),
             "External URL must be kept as-is");
    QVERIFY2(!html.contains(QStringLiteral("/should-not-apply")),
             "Translated value must not replace an external URL");
}

void Test_Website_CommonBlocMenuPermalink::test_menutop_untranslated_url_kept_as_is()
{
    // When no translation map is set, all URLs must appear unchanged.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    EngineArticles *engine = makeEngine(dir.path(), QStringLiteral("fr"), this);
    QVERIFY(engine);
    // No setTranslatedPermalinks call — empty map

    MenuItem item;
    item.label = QStringLiteral("About");
    item.url   = QStringLiteral("/about");

    CommonBlocMenuTop menu;
    menu.setItems({item});

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    menu.addCode(QStringView{}, *engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY2(html.contains(QStringLiteral("/about")),
             "Untranslated permalink must appear unchanged in menu HTML");
}

// =============================================================================
// Main
// =============================================================================

QTEST_MAIN(Test_Website_CommonBlocMenuPermalink)

#include "test_website_commonbloc_menu_permalink.moc"
