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

# wf_add_manifest(<name> TARGET <target>... [SOURCES <file>...]
#                 [INCLUDES <dir>...] [RESOURCES <file>...])
#
# Writes ${CMAKE_BINARY_DIR}/manifests/<name>.txt: every source file, include
# directory and compile definition needed to build the targets and the
# targets they link, one per line ("source <path>", "include <dir>", "define <macro>").
# The builds that do not use CMake, such as Bela's on-board Makefile, read
# their file lists from it, so the CMake files stay the only source lists.
# SOURCES and INCLUDES add files that only that build compiles (a Bela host
# needs Bela's headers), and RESOURCES files it ships unchanged ("resource
# <path>", such as a browser panel); relative paths are from the calling
# directory. It is written once configuration has finished, when every
# target exists.
function(wf_add_manifest name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "TARGET;SOURCES;INCLUDES;RESOURCES")
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "wf_add_manifest(${name}): TARGET is required")
    endif()
    set(extra)
    foreach(kind SOURCES INCLUDES RESOURCES)
        set(paths)
        foreach(path IN LISTS ARG_${kind})
            cmake_path(ABSOLUTE_PATH path BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE)
            list(APPEND paths ${path})
        endforeach()
        list(JOIN paths "|" joined)
        list(APPEND extra "[[${joined}]]")
    endforeach()
    list(JOIN extra " " extra)
    list(JOIN ARG_TARGET "|" targets)
    # A deferred call expands its arguments when it runs, in another scope;
    # EVAL expands them now.
    cmake_language(EVAL CODE "
        cmake_language(DEFER DIRECTORY [[${WeatherFields_SOURCE_DIR}]]
            CALL _wf_write_manifest [[${name}]] [[${targets}]] ${extra})
    ")
endfunction()

# Appends <target> and, depth first, every target it links to the list
# variable named by <out>, each once.
function(_wf_link_closure target out)
    set(closure ${${out}})
    get_target_property(aliased ${target} ALIASED_TARGET)
    if(aliased)
        set(target ${aliased})
    endif()
    if(target IN_LIST closure)
        return()
    endif()
    list(APPEND closure ${target})

    get_target_property(type ${target} TYPE)
    set(dependencies)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(linked ${target} LINK_LIBRARIES)
        if(linked)
            list(APPEND dependencies ${linked})
        endif()
    endif()
    get_target_property(interface ${target} INTERFACE_LINK_LIBRARIES)
    if(interface)
        list(APPEND dependencies ${interface})
    endif()

    foreach(dependency IN LISTS dependencies)
        # A static library's private dependencies appear as $<LINK_ONLY:...>.
        if(dependency MATCHES "^\\$<LINK_ONLY:(.+)>$")
            set(dependency ${CMAKE_MATCH_1})
        endif()
        if(dependency MATCHES "\\$<")
            message(FATAL_ERROR "manifest: unsupported link item ${dependency} in ${target}")
        endif()
        if(TARGET ${dependency})
            _wf_link_closure(${dependency} closure)
        endif()
    endforeach()
    set(${out} ${closure} PARENT_SCOPE)
endfunction()

# The targets and the extra paths arrive joined with "|", as one argument
# per kind.
function(_wf_write_manifest name roots extra_sources extra_includes extra_resources)
    string(REPLACE "|" ";" roots "${roots}")
    set(closure)
    foreach(root IN LISTS roots)
        _wf_link_closure(${root} closure)
    endforeach()

    set(sources)
    set(includes)
    set(defines)
    string(REPLACE "|" ";" extra_sources "${extra_sources}")
    string(REPLACE "|" ";" extra_includes "${extra_includes}")
    string(REPLACE "|" ";" extra_resources "${extra_resources}")
    foreach(source IN LISTS extra_sources)
        list(APPEND sources "source ${source}")
    endforeach()
    set(resources)
    foreach(resource IN LISTS extra_resources)
        list(APPEND resources "resource ${resource}")
    endforeach()
    list(APPEND includes ${extra_includes})
    foreach(target IN LISTS closure)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        get_target_property(dir ${target} SOURCE_DIR)
        get_target_property(target_sources ${target} SOURCES)
        foreach(source IN LISTS target_sources)
            if(source MATCHES "\\.(c|cc|cpp|cxx)$")
                cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${dir} NORMALIZE)
                list(APPEND sources "source ${source}")
            endif()
        endforeach()
        # Evaluated at generation time; they include the usage requirements
        # of everything the target links.
        list(APPEND includes "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
        list(APPEND defines "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    endforeach()

    list(JOIN sources "\n" source_lines)
    list(JOIN resources "\n" resource_lines)
    set(newline "\n")
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/manifests/${name}.txt CONTENT
"# Generated by CMake (wf_add_manifest): ${name}, targets ${roots}.
${source_lines}
include $<JOIN:$<REMOVE_DUPLICATES:${includes}>,${newline}include >
define $<JOIN:$<REMOVE_DUPLICATES:${defines}>,${newline}define >
${resource_lines}
")
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
