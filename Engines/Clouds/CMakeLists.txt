# wf::clouds: the Mutable Instruments Clouds engine at the module's native
# rate and block size (32 kHz, 32 stereo int16 frames), with host-provided
# memory, plus the block and sample-rate adapters for host callbacks.
wf_add_rt_library(clouds
    SOURCES
        src/Module.cpp
        src/BlockAdapter.cpp
        src/SampleRateAdapter.cpp
    LINK
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
