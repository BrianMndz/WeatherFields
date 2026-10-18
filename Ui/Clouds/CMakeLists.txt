# wf::clouds_ui: Clouds' screens. For now its OLED screen (wf::oled::Screen),
# drawn from wf::clouds::Engine's published values on every host.
wf_add_rt_library(clouds_ui
    SOURCES
        src/OledScreen.cpp
    LINK
        wf::oled
        wf::clouds
)

# What a Bela build compiles for Clouds' screens (Tools/Bela/check.sh).
wf_add_manifest(clouds_ui TARGET wf::clouds_ui)

wf_add_check(wf_clouds_oled_screen_check
    SOURCES tests/OledScreenCheck.cpp
    LINK wf::clouds_ui
)
