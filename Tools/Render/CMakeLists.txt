# Offline renders. Tools run on the desktop only, so they may use C++17.

# wf_render_clouds: CloudsEngine's CLI. It renders a fixed six-second
# timeline (a 220/330 Hz tone for two seconds, then silence with freeze on,
# PITCH +7 from four seconds) at the engine's native rate and writes
# clouds_input.wav and clouds_output_<quality>_<mode>.wav to the working
# directory.
add_executable(wf_render_clouds RenderClouds.cpp)
target_compile_features(wf_render_clouds PRIVATE cxx_std_17)
target_link_libraries(wf_render_clouds PRIVATE wf::clouds)

# The 16 quality x mode renders must stay byte-identical to the references:
# CloudsEngine's clouds_cli built from e7738d9 (CloudsDSP f69c1f4), on macOS
# arm64 with Apple Clang, identical in Debug and Release. Another compiler or
# architecture may round differently (for example, by contracting into FMA),
# and would need its own references.
if(WF_BUILD_TESTS)
    add_test(NAME wf_render_clouds_references
        COMMAND ${CMAKE_COMMAND}
            -DRENDER=$<TARGET_FILE:wf_render_clouds>
            -DREFERENCES=${CMAKE_CURRENT_SOURCE_DIR}/clouds_references.sha256
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/clouds_renders
            -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckCloudsRenders.cmake
    )
endif()
