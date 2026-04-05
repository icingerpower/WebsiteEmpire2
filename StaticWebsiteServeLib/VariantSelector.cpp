#include "VariantSelector.h"

#include <random>

namespace VariantSelector {

std::string select(const std::vector<std::string> &activeLabels,
                   const std::string               &abCookie)
{
    if (activeLabels.size() <= 1) {
        return "control";
    }

    if (!abCookie.empty()) {
        for (const auto &label : activeLabels) {
            if (label == abCookie) {
                return label;
            }
        }
    }

    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::size_t> dist(0, activeLabels.size() - 1);
    return activeLabels[dist(rng)];
}

} // namespace VariantSelector
