#pragma once

#include <string>
#include <vector>

/**
 * Pure variant-selection logic extracted from PageController for testability.
 *
 * Rules:
 *  - 0 or 1 active labels  → always "control"
 *  - abCookie matches a label → return that label
 *  - otherwise → pick uniformly at random from activeLabels
 */
namespace VariantSelector {

std::string select(const std::vector<std::string> &activeLabels,
                   const std::string               &abCookie);

} // namespace VariantSelector
