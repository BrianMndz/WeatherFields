# The JUCE desktop host. JUCE is added here rather than at the top level, so
# configurations without the desktop host never configure it.
add_subdirectory(${PROJECT_SOURCE_DIR}/Modules/JUCE ${CMAKE_BINARY_DIR}/Modules/JUCE)

# The desktop app: the host (AudioHost, RigPanel, MainComponent, the emulated
# OLED in OledView) plus every rig in its menu with its own view and OLED
# screen, if it has them (Rigs.cpp, Clouds/, Stratus/, Ui/). The host becomes
# its own library when a second desktop app needs it.
juce_add_gui_app(wf_desktop
    PRODUCT_NAME "WeatherFields"
    COMPANY_NAME "WeatherFields"
    BUNDLE_ID "com.weatherfields.desktop"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "WeatherFields processes the audio interface's input."
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(wf_desktop PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioHost.cpp
    Source/RigPanel.cpp
    Source/OledView.cpp
    Source/Rigs.cpp
    Source/Clouds/PanelControls.cpp
    Source/Clouds/CloudsPanel.cpp
    Source/Stratus/StratusPanel.cpp
)

target_compile_features(wf_desktop PRIVATE cxx_std_17)

# The repo's warnings rather than juce_recommended_warning_flags, which would
# also apply -Wfloat-equal to Core's deliberate exact comparisons.
target_compile_options(wf_desktop PRIVATE "$<${WF_GNU_LIKE}:-Wall;-Wextra>")

target_compile_definitions(wf_desktop PRIVATE
    # juce::Random would clash with stmlib::Random.
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:wf_desktop,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:wf_desktop,JUCE_VERSION>"
)

target_link_libraries(wf_desktop
    PRIVATE
        wf::core
        wf::clouds
        wf::clouds_ui
        wf::stratus
        wf::oled
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
