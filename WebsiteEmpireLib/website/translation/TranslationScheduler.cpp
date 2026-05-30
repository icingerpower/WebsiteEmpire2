#include "TranslationScheduler.h"

#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"

#include <algorithm>

QList<PageTranslator::TranslationJob>
TranslationScheduler::buildJobs(IPageRepository           &repo,
                                CategoryTable             &categoryTable,
                                const TranslationSettings &settings,
                                const QString             &editingLang)
{
    QList<PageTranslator::TranslationJob> priorityJobs;
    QList<PageTranslator::TranslationJob> restJobs;

    const QList<PageRecord> &pages = repo.findSourcePages(editingLang);

    for (const PageRecord &page : std::as_const(pages)) {
        if (page.langCodesToTranslate.isEmpty()) {
            continue; // not yet assessed — skip
        }

        for (const QString &targetLang : std::as_const(page.langCodesToTranslate)) {
            // Load and check completeness before queuing to avoid redundant API calls.
            auto type = AbstractPageType::createForTypeId(page.typeId, categoryTable);
            if (!type) {
                continue;
            }

            const QHash<QString, QString> &data = repo.loadData(page.id);
            if (data.isEmpty()) {
                continue;
            }

            type->load(data);
            type->setAuthorLang(editingLang);

            if (type->isTranslationComplete(QStringView{}, targetLang)) {
                // Content is complete, but also verify the permalink slug is set
                // when the page type uses per-language URL slugs (endPermalink != "").
                // The slug is injected dynamically by PageTranslator and is invisible
                // to isTranslationComplete(), so we must check it explicitly here.
                if (page.endPermalink.isEmpty()) {
                    continue; // fully done — no slug needed
                }
                const QString slugKey = QStringLiteral("tr:") + targetLang
                                        + QStringLiteral(":_permalink_slug");
                if (!data.value(slugKey).isEmpty()) {
                    continue; // slug already translated
                }
                // Fall through: re-queue to get the missing slug translated.
            }

            PageTranslator::TranslationJob job;
            job.pageId     = page.id;
            job.typeId     = page.typeId;
            job.sourceLang = editingLang;
            job.targetLang = targetLang;

            if (settings.priorityPageTypes.contains(page.typeId)) {
                priorityJobs.append(job);
            } else {
                restJobs.append(job);
            }
        }
    }

    // Within the priority tier, legal pages come first.
    std::stable_sort(priorityJobs.begin(), priorityJobs.end(),
                     [](const PageTranslator::TranslationJob &a,
                        const PageTranslator::TranslationJob &b) {
                         return (a.typeId == QLatin1String(PageTypeLegal::TYPE_ID))
                             && (b.typeId != QLatin1String(PageTypeLegal::TYPE_ID));
                     });

    priorityJobs.append(restJobs);

    if (settings.limitPerRun > 0 && priorityJobs.size() > settings.limitPerRun) {
        priorityJobs.resize(settings.limitPerRun);
    }

    return priorityJobs;
}
