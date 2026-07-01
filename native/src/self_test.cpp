// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the 11 model-spine checks ported from the Three.js spike and prints a
// PASS/FAIL report. Exit code 0 iff all pass — so it doubles as a CI gate.
#include "ship_model.hpp"

#include <cstdio>

int main() {
    const auto results = sea::runSelfTest();

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
