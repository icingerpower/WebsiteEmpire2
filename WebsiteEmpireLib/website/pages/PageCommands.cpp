#include "PageCommands.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QTextStream>
#include <iostream>

// =============================================================================
// handle
// =============================================================================

bool PageCommands::handle(const QStringList &args,
                          const QDir        &workingDir,
                          const QString     &domain,
                          AbstractEngine    &engine,
                          int                websiteIndex)
{
    // Expect: --page <subcommand> [options...]
    const int pageIdx = args.indexOf(QStringLiteral("--page"));
    if (pageIdx < 0 || pageIdx + 1 >= args.size()) {
        return false;
    }
    const QString &subcmd = args.at(pageIdx + 1);

    if (subcmd == QStringLiteral("generate")) {
        const int count = runGenerate(workingDir, domain, engine, websiteIndex);
        std::cout << "Generated " << count << " page(s).\n";
        return true;
    }

    if (subcmd == QStringLiteral("translate")) {
        const int idIdx = args.indexOf(QStringLiteral("--id"));
        const int langIdx = args.indexOf(QStringLiteral("--lang"));
        if (idIdx < 0 || idIdx + 1 >= args.size()
            || langIdx < 0 || langIdx + 1 >= args.size()) {
            std::cerr << "Usage: --page translate --id <pageId> --lang <lang>\n";
            return true;
        }
        const int     pageId     = args.at(idIdx + 1).toInt();
        const QString &targetLang = args.at(langIdx + 1);
        const int result = runTranslate(pageId, targetLang, workingDir);
        if (result > 0) {
            std::cout << "Translation saved as page id " << result << ".\n";
        } else {
            std::cerr << "Translation failed.\n";
        }
        return true;
    }

    return false;
}

// =============================================================================
// runGenerate
// =============================================================================

int PageCommands::runGenerate(const QDir &workingDir, const QString &domain,
                              AbstractEngine &engine, int websiteIndex)
{
    CategoryTable   catTable(workingDir);
    PageDb          db(workingDir);
    PageRepositoryDb repo(db);
    PageGenerator   gen(repo, catTable);
    return gen.generateAll(workingDir, domain, engine, websiteIndex);
}

// =============================================================================
// runTranslate
// =============================================================================

int PageCommands::runTranslate(int pageId, const QString &targetLang,
                               const QDir &workingDir)
{
    CategoryTable    catTable(workingDir);
    PageDb           db(workingDir);
    PageRepositoryDb repo(db);

    const auto &rec = repo.findById(pageId);
    if (!rec) {
        std::cerr << "Page id " << pageId << " not found.\n";
        return -1;
    }

    // Load the page and extract its data.
    auto type = AbstractPageType::createForTypeId(rec->typeId, catTable);
    if (!type) {
        std::cerr << "Unknown page type: "
                  << rec->typeId.toStdString() << '\n';
        return -1;
    }

    QHash<QString, QString> data = repo.loadData(pageId);
    type->load(data);

    // Build translated data: copy all keys, translate text bloc values.
    QHash<QString, QString> translatedData = data;
    const auto &blocs = type->getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        const QString &textKey = prefix + QLatin1String(PageBlocText::KEY_TEXT);
        if (translatedData.contains(textKey)) {
            const QString &translated =
                translateText(translatedData.value(textKey), targetLang);
            if (!translated.isEmpty()) {
                translatedData.insert(textKey, translated);
            }
        }
    }

    // Derive the translated permalink: prepend "/<lang>" if not already present.
    QString newPermalink = rec->permalink;
    const QString &langPrefix = QStringLiteral("/") + targetLang + QStringLiteral("/");
    if (!newPermalink.startsWith(langPrefix)) {
        newPermalink = langPrefix + newPermalink.mid(1); // replace leading "/"
    }

    const int newId = repo.create(rec->typeId, newPermalink, targetLang);
    repo.saveData(newId, translatedData);
    return newId;
}

// =============================================================================
// translateText  (stub — replace with real AI API call)
// =============================================================================

QString PageCommands::translateText(const QString &sourceText,
                                    const QString &targetLang)
{
    Q_UNUSED(targetLang)
    // TODO: call AI translation API (e.g. via QNetworkAccessManager + Anthropic API).
    // Return the translated text, or an empty string on failure.
    return sourceText; // identity stub: returns original text unchanged
}
