# Renders every quality x mode combination with wf_render_clouds and compares
# each WAV with its SHA-256 in REFERENCES ("<sha256>  <file>" lines, as
# shasum writes them; `shasum -a 256 -c` in WORK_DIR checks the same).
#
#   cmake -DRENDER=<exe> -DREFERENCES=<file> -DWORK_DIR=<dir> -P CheckCloudsRenders.cmake

foreach(variable RENDER REFERENCES WORK_DIR)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

foreach(quality stereo16 mono16 stereo-mulaw mono-mulaw)
    foreach(mode granular stretch looping-delay spectral)
        execute_process(
            COMMAND "${RENDER}" ${quality} --mode ${mode}
            WORKING_DIRECTORY "${WORK_DIR}"
            RESULT_VARIABLE result
            OUTPUT_QUIET
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "render ${quality} ${mode} failed: ${result}")
        endif()
    endforeach()
endforeach()

file(STRINGS "${REFERENCES}" lines)
set(checked 0)
set(failures 0)
foreach(line IN LISTS lines)
    if(NOT line MATCHES "^([0-9a-f]+)  (.+)$")
        message(FATAL_ERROR "malformed reference line: ${line}")
    endif()
    set(expected "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_2}")
    if(NOT EXISTS "${WORK_DIR}/${name}")
        message(SEND_ERROR "missing render: ${name}")
        math(EXPR failures "${failures} + 1")
        continue()
    endif()
    file(SHA256 "${WORK_DIR}/${name}" actual)
    if(NOT actual STREQUAL expected)
        message(SEND_ERROR "${name}: ${actual}, expected ${expected}")
        math(EXPR failures "${failures} + 1")
    endif()
    math(EXPR checked "${checked} + 1")
endforeach()

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} render(s) differ from the references")
endif()
message(STATUS "${checked} renders match the references")
