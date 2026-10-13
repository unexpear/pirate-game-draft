// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the model-spine checks and the Phase 8 session checks. Exit code 0
// iff all pass — so it doubles as a CI gate. No graphics, no SDL.
#include "ship_model.hpp"
#include "session.hpp"

#include <cstdio>

int main() {
    auto results = sea::runSelfTest();
    const auto session = sea::runSessionSelfTest();
    results.insert(results.end(), session.begin(), session.end());

    int passing = 0;
    for (const auto& r : results) {
        if (r.pass) {
            std::printf("PASS: %s\n", r.name.c_str());
            ++passing;
        } else {
            std::printf("FAIL: %s%s%s\n", r.name.c_str(),
                        r.details.empty() ? "" : " — ", r.details.c_str());
        }
    }

    const int total = static_cast<int>(results.size());
    std::printf("\n%d / %d passing\n", passing, total);
    return passing == total ? 0 : 1;
}
