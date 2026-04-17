#pragma once

#include <QDir>
#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocText.h"

// ---------------------------------------------------------------------------
// Common DB fixture
// ---------------------------------------------------------------------------
struct DbFixture {
    QTemporaryDir    dir;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;

    DbFixture()
        : categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
    {}
};

// ---------------------------------------------------------------------------
// Create a source page with text content in the 1_text slot
// ---------------------------------------------------------------------------
inline int addPageWithText(PageRepositoryDb &repo,
                           const QString    &typeId,
                           const QString    &permalink,
                           const QString    &lang,
                           const QString    &text = QStringLiteral("Source text for translation"))
{
    const int id = repo.create(typeId, permalink, lang);
    QHash<QString, QString> data;
    data.insert(QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), text);
    repo.saveData(id, data);
    return id;
}

// ---------------------------------------------------------------------------
// Store a translation for a page's 1_text field
// ---------------------------------------------------------------------------
inline void addTranslation(PageRepositoryDb &repo,
                           CategoryTable    &categoryTable,
                           int               pageId,
                           const QString    &authorLang,
                           const QString    &targetLang,
                           const QString    &translatedText)
{
    const auto &rec = repo.findById(pageId);
    QHash<QString, QString> data = repo.loadData(pageId);
    auto type = AbstractPageType::createForTypeId(rec->typeId, categoryTable);
    type->load(data);
    type->setAuthorLang(authorLang);
    const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    type->applyTranslation(QStringView{}, fieldId, targetLang, translatedText);
    type->save(data);
    repo.saveData(pageId, data);
}
