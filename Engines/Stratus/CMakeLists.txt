# wf::stratus: Stratus, a RAM looper (record, play, overdub, clear) at the
# host's rate with host-provided memory, behind the wf::Engine interface.
wf_add_rt_library(stratus
    SOURCES
        src/Engine.cpp
    LINK
        wf::core
)

# What a Bela or Daisy build compiles for Stratus (Tools/Bela/check.sh).
wf_add_manifest(stratus TARGET wf::stratus)

wf_add_check(wf_stratus_engine_check
    SOURCES tests/EngineCheck.cpp
    LINK wf::stratus
)
