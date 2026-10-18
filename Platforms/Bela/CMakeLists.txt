# Bela apps: the Bela host (Host/) plus a rig (Apps/<Name>/), built on the
# board by Bela's own Makefile (Tools/Bela/deploy.sh). Nothing here builds on
# this machine, since the host needs Bela's headers; CMake only lists each
# app's files in its manifest, which Tools/Bela/check.sh and deploy.sh read.
set(WF_BELA_HOST_SOURCES
    Host/BelaHost.cpp
    Host/GuiLink.cpp
    Host/I2cPanel.cpp
    Host/OledLink.cpp
)
# What the host itself links, added to every app's targets.
set(WF_BELA_HOST_TARGETS wf::oled)

wf_add_manifest(bela_clouds TARGET wf::clouds wf::clouds_ui ${WF_BELA_HOST_TARGETS}
    SOURCES ${WF_BELA_HOST_SOURCES} Apps/Clouds/CloudsApp.cpp
    INCLUDES Host
    RESOURCES Apps/Clouds/sketch.js
)
