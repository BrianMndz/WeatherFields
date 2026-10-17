cmake_minimum_required(VERSION 3.24)

project(WeatherFields VERSION 0.1.0 LANGUAGES C CXX)

# No project-wide CMAKE_CXX_STANDARD: code below Platforms compiles as C++14
# through the helpers in cmake/WeatherFields.cmake, and the desktop host gets
# C++17 from JUCE. CMakePresets.json holds the usual configurations.
option(WF_BUILD_TESTS "Build the check executables and register them with CTest" ON)
option(WF_BUILD_DESKTOP "Build the JUCE desktop host" OFF)

include(cmake/WeatherFields.cmake)

if(WF_BUILD_TESTS)
    enable_testing()
endif()

# EXCLUDE_FROM_ALL: only the MutableDSP modules something links get built.
add_subdirectory(Modules/MutableDSP EXCLUDE_FROM_ALL)

add_subdirectory(Core)

if(WF_BUILD_DESKTOP)
    add_subdirectory(Platforms/Desktop)
endif()
