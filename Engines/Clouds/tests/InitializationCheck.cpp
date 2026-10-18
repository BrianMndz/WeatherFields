#include <cstddef>
#include <cstdint>
#include <iostream>

#include "wf/clouds/Module.h"

namespace {

using wf::clouds::Module;
using wf::clouds::EngineMemory;
using wf::clouds::InitStatus;
using wf::clouds::MemoryRegion;
using wf::clouds::kLargeMemoryBytes;
using wf::clouds::kMemoryAlignment;
using wf::clouds::kSmallMemoryBytes;
using wf::clouds::Quality;

constexpr std::uint32_t kTestSeed = 0x23u;

alignas(kMemoryAlignment) unsigned char validLargeArena[kLargeMemoryBytes];
alignas(kMemoryAlignment) unsigned char validSmallArena[kSmallMemoryBytes];
alignas(kMemoryAlignment) unsigned char misalignedLargeArena[kLargeMemoryBytes + 1];
alignas(kMemoryAlignment) unsigned char overlapArena[kLargeMemoryBytes + kSmallMemoryBytes];

const char* StatusName(InitStatus status) {
    switch (status) {
        case InitStatus::Ok: return "Ok";
        case InitStatus::LargeRegionNull: return "LargeRegionNull";
        case InitStatus::LargeRegionIncorrectSize: return "LargeRegionIncorrectSize";
        case InitStatus::SmallRegionNull: return "SmallRegionNull";
        case InitStatus::SmallRegionIncorrectSize: return "SmallRegionIncorrectSize";
        case InitStatus::RegionsOverlap: return "RegionsOverlap";
        case InitStatus::RegionMisaligned: return "RegionMisaligned";
        case InitStatus::AlreadyInitialized: return "AlreadyInitialized";
        case InitStatus::InvalidQuality: return "InvalidQuality";
    }

    return "Unknown";
}

EngineMemory ValidMemory() {
    return {
        MemoryRegion{validLargeArena, sizeof(validLargeArena)},
        MemoryRegion{validSmallArena, sizeof(validSmallArena)},
    };
}

bool CheckStatus(const char* scenario, InitStatus actual, InitStatus expected) {
    if (actual == expected) return true;

    std::cerr << scenario << ": expected " << StatusName(expected)
              << ", got " << StatusName(actual) << '\n';
    return false;
}

bool CheckInitialized(const char* scenario, const Module& engine, bool expected) {
    const bool actual = engine.IsInitialized();
    if (actual == expected) return true;

    std::cerr << scenario << ": expected initialized=" << std::boolalpha << expected
              << ", got initialized=" << actual << '\n';
    return false;
}

}  // namespace

int main() {
    int failures = 0;

    {
        Module engine;
        const EngineMemory memory{
            MemoryRegion{nullptr, kLargeMemoryBytes},
            MemoryRegion{validSmallArena, sizeof(validSmallArena)},
        };
        failures += !CheckStatus("Null large region", engine.Initialize(memory, Quality::Stereo16Bit32k,
            kTestSeed),
                                 InitStatus::LargeRegionNull);
    }

    {
        Module engine;
        EngineMemory memory = ValidMemory();
        memory.large.sizeBytes = kLargeMemoryBytes - 1;
        failures += !CheckStatus("Incorrect large size", engine.Initialize(memory, Quality::Stereo16Bit32k,
            kTestSeed),
                                 InitStatus::LargeRegionIncorrectSize);
    }

    {
        Module engine;
        const EngineMemory memory{
            MemoryRegion{validLargeArena, sizeof(validLargeArena)},
            MemoryRegion{nullptr, kSmallMemoryBytes},
        };
        failures += !CheckStatus("Valid large, null small", engine.Initialize(memory, Quality::Stereo16Bit32k,
            kTestSeed),
                                 InitStatus::SmallRegionNull);
    }

    {
        Module engine;
        EngineMemory memory = ValidMemory();
        memory.small.sizeBytes = kSmallMemoryBytes - 1;
        failures += !CheckStatus("Incorrect small size", engine.Initialize(memory, Quality::Stereo16Bit32k,
            kTestSeed),
                                 InitStatus::SmallRegionIncorrectSize);
    }

    {
        Module engine;
        const EngineMemory memory{
            MemoryRegion{misalignedLargeArena + 1, kLargeMemoryBytes},
            MemoryRegion{validSmallArena, sizeof(validSmallArena)},
        };
        failures += !CheckStatus("Address offset by one byte",
                                 engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::RegionMisaligned);
    }

    {
        Module engine;
        const EngineMemory memory{
            MemoryRegion{overlapArena, kLargeMemoryBytes},
            MemoryRegion{overlapArena + kMemoryAlignment, kSmallMemoryBytes},
        };
        failures += !CheckStatus("Small region begins inside large region",
                                 engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::RegionsOverlap);
    }

    {
        Module engine;
        const EngineMemory memory = ValidMemory();
        failures += !CheckStatus("Separate valid regions",
                                 engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::Ok);
        failures += !CheckInitialized("Separate valid regions", engine, true);
        failures += !CheckStatus("Second call after success",
                                 engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::AlreadyInitialized);
    }

    {
        Module engine;
        EngineMemory invalidMemory = ValidMemory();
        invalidMemory.small.sizeBytes = kSmallMemoryBytes - 1;

        failures += !CheckStatus("Failure followed by valid retry (failure)",
                                 engine.Initialize(invalidMemory, Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::SmallRegionIncorrectSize);
        failures += !CheckInitialized("Failure followed by valid retry (failure)", engine,
                                      false);
        failures += !CheckStatus("Failure followed by valid retry (retry)",
                                 engine.Initialize(ValidMemory(), Quality::Stereo16Bit32k, kTestSeed),
                                 InitStatus::Ok);
        failures += !CheckInitialized("Failure followed by valid retry (retry)", engine,
                                      true);
    }

    {
        Module engine;
        const Quality invalidQuality = static_cast<Quality>(999);

        failures += !CheckStatus("Invalid quality",
                                 engine.Initialize(ValidMemory(), invalidQuality, kTestSeed),
                                 InitStatus::InvalidQuality);
        failures += !CheckInitialized("Invalid quality", engine, false);
        failures += !CheckStatus("Valid retry after invalid quality",
                                 engine.Initialize(ValidMemory(), Quality::Stereo16Bit32k,
                                                   kTestSeed),
                                 InitStatus::Ok);
        failures += !CheckInitialized("Valid retry after invalid quality", engine, true);
    }

    {
        Module engine;
        failures += !CheckStatus("Mono 16-bit 32 kHz",
                                 engine.Initialize(ValidMemory(), Quality::Mono16Bit32k,
                                                   kTestSeed),
                                 InitStatus::Ok);
    }

    {
        Module engine;
        failures += !CheckStatus("Stereo mu-law 16 kHz",
                                 engine.Initialize(ValidMemory(), Quality::StereoMuLaw16k,
                                                   kTestSeed),
                                 InitStatus::Ok);
    }

    {
        Module engine;
        failures += !CheckStatus("Mono mu-law 16 kHz",
                                 engine.Initialize(ValidMemory(), Quality::MonoMuLaw16k,
                                                   kTestSeed),
                                 InitStatus::Ok);
    }

    return failures == 0 ? 0 : 1;
}
