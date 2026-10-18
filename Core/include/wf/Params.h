#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "wf/Dsp.h"

namespace wf {

// Identifies a parameter within one engine. Scenes, MIDI maps and the
// Bela-Seed protocol store it, so it is stable forever: retire an ID, never
// renumber one.
using ParamId = std::uint16_t;

enum class ParamKind : std::uint8_t {
    Continuous,  // any value in [min, max]
    Choice,      // an index in [0, max]: min is 0, max and def are integers
    Toggle,      // 0 or 1, holding until changed: min 0, max 1
    Trigger,     // a one-shot action whose value is unused: min 0, max 1
};

// One control of an engine, in plain units ("st", "dB" ...). A host builds
// its panel, MIDI ranges and scene storage from these; the rig validates
// every incoming value against them.
struct ParamSpec {
    ParamId id;
    const char* name;  // static storage
    ParamKind kind;
    float min;
    float max;
    float def;
    const char* unit;  // static storage; "" when unitless
    // Choice only: max + 1 names in index order, in static storage, so a
    // panel shows "Looping Delay" rather than 2. Null for the other kinds.
    const char* const* labels;
};

// An engine's parameters, in display order. Tables are small, so lookups are
// linear.
struct ParamTable {
    const ParamSpec* specs;
    std::size_t count;

    const ParamSpec* Find(ParamId id) const noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (specs[i].id == id) return &specs[i];
        }
        return nullptr;
    }
};

template <std::size_t N>
constexpr ParamTable MakeParamTable(const ParamSpec (&specs)[N]) noexcept {
    return ParamTable{specs, N};
}

// Makes `value` valid for `spec`: clamps it to the range, rounds a Choice to
// the nearest index, turns a Toggle into 0 or 1 at 0.5, and sets a Trigger's
// unused value to 1. Returns false, leaving `value` alone, for NaN or
// infinity, which the caller drops: clamping NaN would silently move the
// parameter to one end of its range.
inline bool ConstrainValue(const ParamSpec& spec, float& value) noexcept {
    if (!IsFinite(value)) return false;
    switch (spec.kind) {
        case ParamKind::Continuous:
            value = value < spec.min ? spec.min : (value > spec.max ? spec.max : value);
            return true;
        case ParamKind::Choice:
            value = value < 0.0f ? 0.0f : (value > spec.max ? spec.max : value);
            value = std::floor(value + 0.5f);
            return true;
        case ParamKind::Toggle:
            value = value >= 0.5f ? 1.0f : 0.0f;
            return true;
        case ParamKind::Trigger:
            value = 1.0f;
            return true;
    }
    return false;
}

// Whether a table is well-formed: unique IDs, names and units present,
// finite ranges with min <= def <= max, and each kind's rules above, with a
// label for every Choice index and none for other kinds. Engines check their
// own table with it.
inline bool IsValidParamTable(const ParamTable& table) noexcept {
    if (table.specs == nullptr && table.count != 0) return false;
    for (std::size_t i = 0; i < table.count; ++i) {
        const ParamSpec& spec = table.specs[i];
        if (spec.name == nullptr || spec.unit == nullptr) return false;
        if (!IsFinite(spec.min) || !IsFinite(spec.max) || !IsFinite(spec.def)) return false;
        if (!(spec.min <= spec.def && spec.def <= spec.max)) return false;
        if (spec.kind != ParamKind::Choice && spec.labels != nullptr) return false;
        switch (spec.kind) {
            case ParamKind::Continuous:
                break;
            case ParamKind::Choice:
                if (spec.min != 0.0f || spec.max != std::floor(spec.max) ||
                    spec.def != std::floor(spec.def)) {
                    return false;
                }
                if (spec.labels == nullptr) return false;
                for (std::size_t index = 0; index <= static_cast<std::size_t>(spec.max); ++index) {
                    if (spec.labels[index] == nullptr) return false;
                }
                break;
            case ParamKind::Toggle:
            case ParamKind::Trigger:
                if (spec.min != 0.0f || spec.max != 1.0f) return false;
                if (spec.def != 0.0f && spec.def != 1.0f) return false;
                break;
            default:
                return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (table.specs[j].id == spec.id) return false;
        }
    }
    return true;
}

}  // namespace wf
