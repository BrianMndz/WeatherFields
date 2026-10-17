# Build settings shared by WeatherFields targets.

# Daisy builds without exceptions or RTTI, so real-time code must not use
# them. This option makes the desktop build reject them first. The desktop
# preset turns it off: JUCE compiles with RTTI, and mixing RTTI settings in
# one program can leave polymorphic classes without type information.
option(WF_STRICT_RT "Compile real-time libraries without exceptions and RTTI" ON)

set(WF_GNU_LIKE "$<CXX_COMPILER_ID:GNU,Clang,AppleClang>")

# C++14 on every host, so the Bela board's Clang 3.9 builds the same code.
# -pedantic-errors turns C++17 constructs, which compilers otherwise accept
# as extensions, into errors.
function(wf_cxx14 target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(${target} PRIVATE
        "$<${WF_GNU_LIKE}:-Wall;-Wextra;-pedantic-errors>"
    )
endfunction()

# wf_add_rt_library(<name> [SOURCES <file>...] [LINK <target>...])
#
# Adds wf::<name> (target wf_<name>), a library for code below Platforms,
# with its public headers in ./include. Without SOURCES it is header-only.
function(wf_add_rt_library name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")
    set(target wf_${name})

    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        set(scope PUBLIC)
        wf_cxx14(${target})
        if(WF_STRICT_RT)
            target_compile_options(${target} PRIVATE
                "$<${WF_GNU_LIKE}:-fno-exceptions;-fno-rtti>"
            )
        endif()
    else()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif()

    add_library(wf::${name} ALIAS ${target})
    target_compile_features(${target} ${scope} cxx_std_14)
    target_include_directories(${target} ${scope} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${target} ${scope} ${ARG_LINK})
endfunction()

# wf_add_check(<name> SOURCES <file>... [LINK <target>...])
#
# Adds a check executable and registers it with CTest; it passes by
# returning 0. Checks compile as C++14 too, so they also check that the
# headers they include are C++14.
function(wf_add_check name)
    if(NOT WF_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")

    add_executable(${name} ${ARG_SOURCES})
    wf_cxx14(${name})
    target_link_libraries(${name} PRIVATE ${ARG_LINK})
    add_test(NAME ${name} COMMAND ${name})
endfunction()
