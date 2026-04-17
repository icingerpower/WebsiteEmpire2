#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "website/translation/TranslationSettings.h"

namespace {

void writeSettingsFile(const QDir        &dir,
                       const QStringList &targetLangs,
                       int                limitPerRun     = 0,
                       const QStringList &priorityTypes  = {})
{
    QJsonArray langsArr;
    for (const QString &l : std::as_const(targetLangs)) {
        langsArr.append(l);
    }

    QJsonArray priorityArr;
    for (const QString &t : std::as_const(priorityTypes)) {
        priorityArr.append(t);
    }

    QJsonObject root;
    root[QStringLiteral("targetLangs")]       = langsArr;
    root[QStringLiteral("limitPerRun")]       = limitPerRun;
    root[QStringLiteral("priorityPageTypes")] = priorityArr;

    QFile f(dir.filePath(QLatin1String(TranslationSettings::FILENAME)));
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
}

} // namespace

class Test_TranslationSettings : public QObject
{
    Q_OBJECT

private slots:
    void test_settings_not_configured_when_file_absent();
    void test_settings_is_configured_when_target_langs_set();
    void test_settings_not_configured_when_target_langs_empty();
    void test_settings_target_langs_parsed();
    void test_settings_limit_per_run_defaults_to_zero();
    void test_settings_limit_per_run_parsed();
    void test_settings_priority_page_types_defaults_to_empty();
    void test_settings_priority_page_types_parsed();
    void test_settings_malformed_json_not_configured();
    void test_settings_empty_lang_strings_skipped();
    void test_settings_missing_limit_is_zero();
};

void Test_TranslationSettings::test_settings_not_configured_when_file_absent()
{
    QTemporaryDir dir;
    const TranslationSettings s(QDir(dir.path()));
    QVERIFY(!s.isConfigured());
}

void Test_TranslationSettings::test_settings_is_configured_when_target_langs_set()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de"), QStringLiteral("it")});
    const TranslationSettings s(QDir(dir.path()));
    QVERIFY(s.isConfigured());
}

void Test_TranslationSettings::test_settings_not_configured_when_target_langs_empty()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {});
    const TranslationSettings s(QDir(dir.path()));
    QVERIFY(!s.isConfigured());
}

void Test_TranslationSettings::test_settings_target_langs_parsed()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")});
    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.targetLangs, (QStringList{QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")}));
}

void Test_TranslationSettings::test_settings_limit_per_run_defaults_to_zero()
{
    QTemporaryDir dir;
    // Write file without limitPerRun key
    QJsonObject root;
    root[QStringLiteral("targetLangs")] = QJsonArray{QStringLiteral("de")};
    QFile f(dir.filePath(QLatin1String(TranslationSettings::FILENAME)));
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());

    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.limitPerRun, 0);
}

void Test_TranslationSettings::test_settings_limit_per_run_parsed()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de")}, 42);
    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.limitPerRun, 42);
}

void Test_TranslationSettings::test_settings_priority_page_types_defaults_to_empty()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de")});
    const TranslationSettings s(QDir(dir.path()));
    QVERIFY(s.priorityPageTypes.isEmpty());
}

void Test_TranslationSettings::test_settings_priority_page_types_parsed()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de")}, 0,
                      {QStringLiteral("article"), QStringLiteral("legal")});
    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.priorityPageTypes,
             (QStringList{QStringLiteral("article"), QStringLiteral("legal")}));
}

void Test_TranslationSettings::test_settings_malformed_json_not_configured()
{
    QTemporaryDir dir;
    QFile f(dir.filePath(QLatin1String(TranslationSettings::FILENAME)));
    f.open(QIODevice::WriteOnly);
    f.write(QByteArrayLiteral("{ not valid json ]"));

    const TranslationSettings s(QDir(dir.path()));
    QVERIFY(!s.isConfigured());
}

void Test_TranslationSettings::test_settings_empty_lang_strings_skipped()
{
    QTemporaryDir dir;
    // Write raw JSON with an actual empty string in the array.
    // (Using QString() produces JSON null, not ""; use a raw literal instead.)
    QFile f(dir.filePath(QLatin1String(TranslationSettings::FILENAME)));
    f.open(QIODevice::WriteOnly);
    f.write(R"({"targetLangs": ["de", "", "it"], "limitPerRun": 0, "priorityPageTypes": []})");
    f.close();

    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.targetLangs, (QStringList{QStringLiteral("de"), QStringLiteral("it")}));
}

void Test_TranslationSettings::test_settings_missing_limit_is_zero()
{
    QTemporaryDir dir;
    writeSettingsFile(QDir(dir.path()), {QStringLiteral("de")}, 0);
    const TranslationSettings s(QDir(dir.path()));
    QCOMPARE(s.limitPerRun, 0);
}

QTEST_MAIN(Test_TranslationSettings)
#include "test_translation_settings.moc"
