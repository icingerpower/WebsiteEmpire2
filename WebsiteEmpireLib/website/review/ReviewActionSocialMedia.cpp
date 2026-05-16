#include "ReviewActionSocialMedia.h"

#include "website/pages/IPageRepository.h"
#include <QDir>
#include "website/pages/PageFlag.h"
#include "website/pages/PageRecord.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/AbstractSecondaryPageBloc.h"
#include "website/pages/attributes/CategoryTable.h"

#include <algorithm>
#include <limits>

QString ReviewActionSocialMedia::getId()          const { return QLatin1String(ID); }
QString ReviewActionSocialMedia::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

bool ReviewActionSocialMedia::run(const PageRecord &page,
                                   int               displayCount,
                                   IPageRepository  &repo)
{
    // Already flagged — nothing to do.
    if (page.flags & static_cast<quint32>(PageFlag::SocialMedia)) {
        return false;
    }

    // Build a throw-away page-type instance to query secondary blocs.
    // CategoryTable is only needed for category-bloc init; threshold queries
    // don't use category data, so an empty-dir dummy is sufficient.
    QDir emptyDir;
    CategoryTable dummyCategories(emptyDir);
    const auto pageType = AbstractPageType::createForTypeId(page.typeId, dummyCategories);
    if (!pageType) {
        return false;
    }

    int minThreshold = std::numeric_limits<int>::max();
    const QList<const AbstractPageBloc *> &blocs = pageType->getPageBlocs();
    for (const AbstractPageBloc *bloc : std::as_const(blocs)) {
        if (!bloc->isSecondTimeGeneration()) {
            continue;
        }
        const auto *secBloc = static_cast<const AbstractSecondaryPageBloc *>(bloc);
        minThreshold = std::min(minThreshold, secBloc->getDisplayThreshold());
    }

    if (minThreshold == std::numeric_limits<int>::max()) {
        // No secondary blocs — page type does not participate in social-media generation.
        return false;
    }

    if (displayCount < minThreshold) {
        return false;
    }

    repo.setFlag(page.id, PageFlag::SocialMedia, true);
    return true;
}

DECLARE_REVIEW_ACTION(ReviewActionSocialMedia)
