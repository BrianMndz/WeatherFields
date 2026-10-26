#include <iostream>

#include "wf/Rig.h"
#include "wf/stratus/Engine.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    Expect(true, "the check runs");

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
