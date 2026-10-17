# The JUCE desktop host. JUCE is added here rather than at the top level, so
# configurations without the desktop host never configure it.
add_subdirectory(${PROJECT_SOURCE_DIR}/Modules/JUCE ${CMAKE_BINARY_DIR}/Modules/JUCE)
