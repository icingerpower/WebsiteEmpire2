#include <drogon/drogon_test.h>

#include <set>

#include "VariantSelector.h"

// ---------------------------------------------------------------------------
// Empty / single label → always "control"
// ---------------------------------------------------------------------------

DROGON_TEST(test_variantsel_empty_labels_returns_control)
{
    CHECK(VariantSelector::select({}, "") == std::string("control"));
}

DROGON_TEST(test_variantsel_single_label_returns_control_ignores_label)
{
    CHECK(VariantSelector::select({"b"}, "") == std::string("control"));
}

DROGON_TEST(test_variantsel_single_label_ignores_matching_cookie)
{
    // Even if cookie matches the only label, return "control".
    CHECK(VariantSelector::select({"b"}, "b") == std::string("control"));
}

// ---------------------------------------------------------------------------
// Cookie match
// ---------------------------------------------------------------------------

DROGON_TEST(test_variantsel_cookie_match_first_label)
{
    CHECK(VariantSelector::select({"control", "a"}, "control") == std::string("control"));
}

DROGON_TEST(test_variantsel_cookie_match_second_label)
{
    CHECK(VariantSelector::select({"control", "a"}, "a") == std::string("a"));
}

DROGON_TEST(test_variantsel_cookie_match_third_of_three)
{
    CHECK(VariantSelector::select({"control", "a", "b"}, "b") == std::string("b"));
}

DROGON_TEST(test_variantsel_cookie_not_in_labels_does_not_return_cookie)
{
    const std::string result =
        VariantSelector::select({"control", "a"}, "unknown");
    // Must be one of the active labels, not "unknown".
    CHECK(result != std::string("unknown"));
    CHECK((result == std::string("control") || result == std::string("a")));
}

DROGON_TEST(test_variantsel_empty_cookie_with_two_labels_returns_active_label)
{
    const std::string result = VariantSelector::select({"control", "a"}, "");
    CHECK((result == std::string("control") || result == std::string("a")));
}

// ---------------------------------------------------------------------------
// Random assignment stays within the active set
// ---------------------------------------------------------------------------

DROGON_TEST(test_variantsel_random_always_in_active_labels)
{
    const std::vector<std::string> labels = {"control", "a", "b"};
    for (int i = 0; i < 200; ++i) {
        const std::string result = VariantSelector::select(labels, "");
        bool found = false;
        for (const auto &l : labels) {
            if (l == result) { found = true; break; }
        }
        CHECK(found == true);
    }
}

DROGON_TEST(test_variantsel_random_hits_multiple_labels_over_many_calls)
{
    // With 200 draws from 3 labels the probability of not seeing all three
    // is astronomically small (< 3 * (2/3)^200).
    const std::vector<std::string> labels = {"control", "a", "b"};
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i) {
        seen.insert(VariantSelector::select(labels, ""));
    }
    CHECK(seen.size() == 3u);
}

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
