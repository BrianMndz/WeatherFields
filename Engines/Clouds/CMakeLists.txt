# wf::clouds: the Mutable Instruments Clouds module at its native rate and
# block size (32 kHz, 32 stereo int16 frames) with host-provided memory, the
# block and sample-rate adapters for host callbacks, and wf::clouds::Engine,
# which puts them behind the wf::Engine interface.
wf_add_rt_library(clouds
    SOURCES
        src/Module.cpp
        src/BlockAdapter.cpp
        src/SampleRateAdapter.cpp
        src/Engine.cpp
    LINK
        wf::core
        MutableDSP::clouds
)

# What a Bela or Daisy build compiles for Clouds (Tools/Bela/check.sh).
wf_add_manifest(clouds TARGET wf::clouds)

wf_add_check(wf_clouds_init_check
    SOURCES tests/InitializationCheck.cpp
    LINK wf::clouds
)

wf_add_check(wf_clouds_block_adapter_check
    SOURCES tests/BlockAdapterCheck.cpp
    LINK wf::clouds
)

wf_add_check(wf_clouds_sample_rate_adapter_check
    SOURCES tests/SampleRateAdapterCheck.cpp
    LINK wf::clouds
)

wf_add_check(wf_clouds_quality_check
    SOURCES tests/QualityCheck.cpp
    LINK wf::clouds
)

wf_add_check(wf_clouds_playback_mode_check
    SOURCES tests/PlaybackModeCheck.cpp
    LINK wf::clouds
)

wf_add_check(wf_clouds_engine_check
    SOURCES tests/EngineCheck.cpp
    LINK wf::clouds
)
