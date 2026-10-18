#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/Events.h"
#include "wf/Params.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

float FromBits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

constexpr const char* kModeLabels[] = {"Granular", "Stretch", "Looping Delay", "Spectral"};
constexpr const char* kShortLabels[] = {"One", nullptr, "Three"};

constexpr wf::ParamSpec kSpecs[] = {
    {0, "Position", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.5f, "", nullptr},
    {1, "Pitch", wf::ParamKind::Continuous, -24.0f, 24.0f, 0.0f, "st", nullptr},
    {7, "Mode", wf::ParamKind::Choice, 0.0f, 3.0f, 0.0f, "", kModeLabels},
    {3, "Freeze", wf::ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", nullptr},
    {4, "Trig", wf::ParamKind::Trigger, 0.0f, 1.0f, 0.0f, "", nullptr},
};
constexpr wf::ParamTable kTable = wf::MakeParamTable(kSpecs);

// Expects ConstrainValue(spec, in) to accept and give `out`.
void ExpectConstrained(const wf::ParamSpec& spec, float in, float out, const char* description) {
    float value = in;
    const bool accepted = wf::ConstrainValue(spec, value);
    if (!accepted || value != out) {
        std::cerr << "FAIL: " << description << ": " << spec.name << ' ' << in << " gave "
                  << (accepted ? "" : "rejected, ") << value << ", expected " << out << '\n';
        ++failures;
    }
}

void CheckFind() {
    Expect(kTable.count == 5, "MakeParamTable counts the specs");
    Expect(kTable.Find(7) == &kSpecs[2], "Find returns the spec with that ID, not that index");
    Expect(kTable.Find(2) == nullptr, "Find returns null for an unknown ID");
    const wf::ParamTable empty{nullptr, 0};
    Expect(empty.Find(0) == nullptr, "Find on an empty table returns null");
}

void CheckConstrain() {
    const wf::ParamSpec& position = kSpecs[0];
    const wf::ParamSpec& pitch = kSpecs[1];
    const wf::ParamSpec& mode = kSpecs[2];
    const wf::ParamSpec& freeze = kSpecs[3];
    const wf::ParamSpec& trig = kSpecs[4];

    ExpectConstrained(position, 0.25f, 0.25f, "in range is kept");
    ExpectConstrained(position, -0.1f, 0.0f, "below min clamps");
    ExpectConstrained(position, 1.5f, 1.0f, "above max clamps");
    ExpectConstrained(pitch, -30.0f, -24.0f, "negative range clamps low");
    ExpectConstrained(pitch, 7.0f, 7.0f, "negative range keeps values");

    ExpectConstrained(mode, 1.4f, 1.0f, "Choice rounds down");
    ExpectConstrained(mode, 1.5f, 2.0f, "Choice rounds half up");
    ExpectConstrained(mode, 9.0f, 3.0f, "Choice clamps to the last index");
    ExpectConstrained(mode, -2.0f, 0.0f, "Choice clamps to the first index");

    ExpectConstrained(freeze, 0.49f, 0.0f, "Toggle below 0.5 is off");
    ExpectConstrained(freeze, 0.5f, 1.0f, "Toggle at 0.5 is on");
    ExpectConstrained(freeze, 127.0f, 1.0f, "Toggle far above is on");

    ExpectConstrained(trig, 0.0f, 1.0f, "Trigger's value becomes 1");

    const float nonFinite[] = {
        FromBits(0x7f800000u),  // infinity
        FromBits(0xff800000u),  // minus infinity
        FromBits(0x7fc00000u),  // quiet NaN
    };
    for (const float bad : nonFinite) {
        float value = bad;
        Expect(!wf::ConstrainValue(position, value), "non-finite values are rejected");
        float toggle = bad;
        Expect(!wf::ConstrainValue(freeze, toggle), "non-finite values are rejected for any kind");
    }
}

void CheckValidity() {
    Expect(wf::IsValidParamTable(kTable), "the example table is valid");
    Expect(wf::IsValidParamTable(wf::ParamTable{nullptr, 0}), "an empty table is valid");
    Expect(!wf::IsValidParamTable(wf::ParamTable{nullptr, 1}), "a null table with a count is invalid");

    struct Case {
        wf::ParamSpec spec;
        const char* description;
    };
    const Case invalid[] = {
        {{9, "Def", wf::ParamKind::Continuous, 0.0f, 1.0f, 2.0f, "", nullptr}, "def above max"},
        {{9, "Range", wf::ParamKind::Continuous, 1.0f, 0.0f, 0.5f, "", nullptr}, "min above max"},
        {{9, nullptr, wf::ParamKind::Continuous, 0.0f, 1.0f, 0.5f, "", nullptr}, "missing name"},
        {{9, "Unit", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.5f, nullptr, nullptr}, "missing unit"},
        {{9, "Choice", wf::ParamKind::Choice, 1.0f, 3.0f, 1.0f, "", kModeLabels}, "Choice not starting at 0"},
        {{9, "Choice", wf::ParamKind::Choice, 0.0f, 2.5f, 0.0f, "", kModeLabels}, "Choice with a fractional max"},
        {{9, "Choice", wf::ParamKind::Choice, 0.0f, 3.0f, 0.5f, "", kModeLabels}, "Choice with a fractional def"},
        {{9, "Toggle", wf::ParamKind::Toggle, 0.0f, 2.0f, 0.0f, "", nullptr}, "Toggle not in 0..1"},
        {{9, "Toggle", wf::ParamKind::Toggle, 0.0f, 1.0f, 0.5f, "", nullptr}, "Toggle def not 0 or 1"},
        {{9, "NaN", wf::ParamKind::Continuous, 0.0f, FromBits(0x7fc00000u), 0.0f, "", nullptr}, "NaN max"},
        {{9, "Choice", wf::ParamKind::Choice, 0.0f, 3.0f, 0.0f, "", nullptr}, "Choice without labels"},
        {{9, "Choice", wf::ParamKind::Choice, 0.0f, 2.0f, 0.0f, "", kShortLabels}, "Choice missing a label"},
        {{9, "Labels", wf::ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", kModeLabels}, "labels on a Toggle"},
        {{0, "Duplicate", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.5f, "", nullptr}, "duplicate ID"},
    };
    for (const Case& c : invalid) {
        // The example table plus the faulty spec, so the duplicate case
        // collides with Position's ID 0.
        wf::ParamSpec specs[6];
        for (std::size_t i = 0; i < 5; ++i) specs[i] = kSpecs[i];
        specs[5] = c.spec;
        if (wf::IsValidParamTable(wf::ParamTable{specs, 6})) {
            std::cerr << "FAIL: IsValidParamTable accepted " << c.description << '\n';
            ++failures;
        }
    }
}

}  // namespace

int main() {
    CheckFind();
    CheckConstrain();
    CheckValidity();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
