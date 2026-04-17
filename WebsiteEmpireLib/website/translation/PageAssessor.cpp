#include "PageAssessor.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"

int PageAssessor::assess(IPageRepository &repo, const QStringList &targetLangs)
{
    if (targetLangs.isEmpty()) {
        return 0;
    }

    int count = 0;
    const QList<PageRecord> &pages = repo.findAll();
    for (const PageRecord &page : std::as_const(pages)) {
        if (page.sourcePageId != 0) {
            continue; // skip legacy translation pages
        }
        if (!page.langCodesToTranslate.isEmpty()) {
            continue; // already assessed — never overwrite
        }
        repo.setLangCodesToTranslate(page.id, targetLangs);
        ++count;
    }

    return count;
}
