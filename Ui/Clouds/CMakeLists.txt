# wf::clouds_ui: Clouds' user interfaces, shared by every host: its OLED screen
# (wf::oled::Screen), drawn from wf::clouds::Engine's published values, and its
# hardware front panel (wf::controls::Panel).
wf_add_rt_library(clouds_ui
    SOURCES
        src/FrontPanel.cpp
        src/OledScreen.cpp
    LINK
        wf::controls
        wf::oled
        wf::clouds
)

# What a Bela build compiles for Clouds' interfaces (Tools/Bela/check.sh).
wf_add_manifest(clouds_ui TARGET wf::clouds_ui)

wf_add_check(wf_clouds_oled_screen_check
    SOURCES tests/OledScreenCheck.cpp
    LINK wf::clouds_ui
)

wf_add_check(wf_clouds_front_panel_check
    SOURCES tests/FrontPanelCheck.cpp
    LINK wf::clouds_ui
)
