# wf::stratocumulus: Stratocumulus, Clouds into Stratus: the looper records
# and plays over Clouds' output (resampling), in place through the output
# buffer, each engine in its part of one host block.
wf_add_rt_library(stratocumulus
    SOURCES
        src/Rig.cpp
    LINK
        wf::clouds
        wf::stratus
)

# What a Bela or Daisy build compiles for Stratocumulus (Tools/Bela/check.sh).
wf_add_manifest(stratocumulus TARGET wf::stratocumulus)

wf_add_check(wf_stratocumulus_rig_check
    SOURCES tests/RigCheck.cpp
    LINK wf::stratocumulus
)
