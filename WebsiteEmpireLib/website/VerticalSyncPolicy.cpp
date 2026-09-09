#include "VerticalSyncPolicy.h"

namespace VerticalSyncPolicy {

bool needsFashionHubSync(const QString &generatorId)
{
    return generatorId == QLatin1String(GENERATOR_FASHION_TAXONOMY);
}

bool needsSymptomHubSync(const QString &generatorId)
{
    return generatorId == QLatin1String(GENERATOR_HEALTH);
}

} // namespace VerticalSyncPolicy
