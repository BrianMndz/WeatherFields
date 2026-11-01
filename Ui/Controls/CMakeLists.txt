# wf::controls: a hardware front panel's parts, below every host: knob and
# button conditioning, from the numbers Platforms/Bela/Probe measured, and the
# Panel interface a rig's front panel implements. Hosts only read pins.
wf_add_rt_library(controls LINK wf::core)

wf_add_check(wf_controls_check
    SOURCES tests/ControlsCheck.cpp
    LINK wf::controls
)
