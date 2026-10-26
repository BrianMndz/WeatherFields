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
    wf::stratus::Engine engine;
    Expect(wf::IsValidParamTable(engine.Params()), "Stratus parameter table is valid");

    const wf::ParamTable& table = engine.Params();
    Expect(table.count == 5, "Stratus exposes five parameters");
    for (std::size_t i = 0; i < table.count; ++i) {
        Expect(table.specs[i].id == i, "parameters are in ID order");
    }

    const wf::ParamSpec* thru = table.Find(wf::stratus::Engine::kThru);
    Expect(thru != nullptr && thru->def == 1.0f, "Thru defaults to on");
    const wf::ParamSpec* record = table.Find(wf::stratus::Engine::kRecord);
    Expect(record != nullptr && record->def == 0.0f, "Record defaults to off");

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
