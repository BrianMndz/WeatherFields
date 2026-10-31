# wf::oled: the 128x64 SH1106 OLED through u8g2, drawn into a buffer the
# display owns, the fonts WeatherFields compiles, the Screen interface rigs'
# screens implement, the widgets they share and the bring-up test pattern. Hosts give it a panel
# (Bela, over I2C) or show its buffer (the desktop's emulated screen), so
# drawing code is the same everywhere.
wf_add_rt_library(oled
    SOURCES
        src/Display.cpp
        src/Fonts.c
        src/TestPattern.cpp
        src/Widgets.cpp
    LINK
        wf::core
        u8g2::u8g2
)

# What a Bela build compiles for the display (Tools/Bela/check.sh).
wf_add_manifest(oled TARGET wf::oled)

wf_add_check(wf_oled_display_check
    SOURCES tests/DisplayCheck.cpp
    LINK wf::oled
)
