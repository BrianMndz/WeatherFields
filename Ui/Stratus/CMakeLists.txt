# wf::stratus_ui: Stratus' screens. For now its OLED screen (wf::oled::Screen),
# drawn from wf::stratus::Engine's published values on every host.
wf_add_rt_library(stratus_ui
    SOURCES
        src/OledScreen.cpp
    LINK
        wf::oled
        wf::stratus
)

# What a Bela build compiles for Stratus' screens (Tools/Bela/check.sh).
wf_add_manifest(stratus_ui TARGET wf::stratus_ui)

wf_add_check(wf_stratus_oled_screen_check
    SOURCES tests/OledScreenCheck.cpp
    LINK wf::stratus_ui
)
