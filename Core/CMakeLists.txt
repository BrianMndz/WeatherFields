# wf::core: the shared types and small DSP every engine, rig and host uses.
# Header-only until its first source file arrives.
wf_add_rt_library(core)

wf_add_check(wf_core_dsp_check
    SOURCES tests/DspCheck.cpp
    LINK wf::core
)

# The same check under Bela's optimisation flags, where -ffast-math lets the
# compiler assume no NaN: IsFinite() must still reject one.
wf_add_check(wf_core_dsp_fast_math_check
    SOURCES tests/DspCheck.cpp
    LINK wf::core
)
if(TARGET wf_core_dsp_fast_math_check)
    target_compile_options(wf_core_dsp_fast_math_check PRIVATE
        "$<${WF_GNU_LIKE}:-O3;-ffast-math>"
    )
endif()

wf_add_check(wf_core_params_check
    SOURCES tests/ParamsCheck.cpp
    LINK wf::core
)

find_package(Threads REQUIRED)
wf_add_check(wf_core_spsc_queue_check
    SOURCES tests/SpscQueueCheck.cpp
    LINK wf::core Threads::Threads
)
