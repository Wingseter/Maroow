cmake_minimum_required(VERSION 3.21)

project(Marrow LANGUAGES C CXX)

include(CTest)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    MARROW_ENABLE_THREAD_SANITIZER
    "Build the dedicated runtime concurrency stress target with ThreadSanitizer instrumentation"
    OFF)

option(
    MARROW_ENABLE_DISPLAY_TESTS
    "Build tests which require a real display and graphics device"
    OFF)

add_library(marrow_windowing_support STATIC
    src/editor/windowing.cpp
)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
set(SDL_REVISION
    "SDL-3.4.14-147a8ee32dbf9ac02f3794964490687b6bbda1bc"
    CACHE STRING "Pinned vendored SDL revision" FORCE)
if(UNIX AND NOT APPLE)
    set(SDL_X11 ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(external/sdl3 EXCLUDE_FROM_ALL)

add_library(marrow_window_host STATIC
    src/editor/sdl_input.cpp
    src/editor/sdl_window_host.cpp
)

target_include_directories(marrow_window_host
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/editor
        ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(marrow_window_host
    PUBLIC
        marrow_windowing_support
        SDL3::SDL3
)

if(APPLE)
    set_source_files_properties(
        src/editor/sdl_window_host.cpp
        PROPERTIES
            LANGUAGE OBJCXX
            COMPILE_FLAGS "-fobjc-arc"
    )
endif()

target_include_directories(marrow_windowing_support
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/editor
)

if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    set(MARROW_OPENGL_LIBRARIES OpenGL::GL)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
endif()

if(WIN32)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/zlib EXCLUDE_FROM_ALL)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
else()
    find_package(ZLIB REQUIRED)
endif()
find_package(Doxygen QUIET)

set(MARROW_DOXYGEN_CONFIG ${PROJECT_BINARY_DIR}/Doxyfile)
configure_file(
    ${PROJECT_SOURCE_DIR}/docs/root1/Doxyfile.in
    ${MARROW_DOXYGEN_CONFIG}
    @ONLY
)

set(MARROW_IMGUI_DIR ${PROJECT_SOURCE_DIR}/external/imgui)
add_library(marrow_imgui_core STATIC
    ${MARROW_IMGUI_DIR}/imgui.cpp
    ${MARROW_IMGUI_DIR}/imgui_draw.cpp
    ${MARROW_IMGUI_DIR}/imgui_tables.cpp
    ${MARROW_IMGUI_DIR}/imgui_widgets.cpp
)

target_include_directories(marrow_imgui_core
    PUBLIC
        ${MARROW_IMGUI_DIR}
)

add_library(marrow_imgui_sdl3 STATIC
    ${MARROW_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
)

target_include_directories(marrow_imgui_sdl3
    PUBLIC
        ${MARROW_IMGUI_DIR}
        ${MARROW_IMGUI_DIR}/backends
)

target_link_libraries(marrow_imgui_sdl3
    PUBLIC
        marrow_imgui_core
        SDL3::SDL3
)

add_library(marrow_sokol_imgui_compile_probe STATIC EXCLUDE_FROM_ALL
    src/tests/sokol_imgui_compile_probe.cpp
)
target_include_directories(marrow_sokol_imgui_compile_probe PRIVATE
    ${PROJECT_SOURCE_DIR}/external
    ${MARROW_IMGUI_DIR}
)

add_executable(marrow_sokol_imgui_runtime_probe
    src/tests/sokol_imgui_runtime_probe.cpp
)
target_include_directories(marrow_sokol_imgui_runtime_probe PRIVATE
    ${PROJECT_SOURCE_DIR}/external
    ${MARROW_IMGUI_DIR}
)
target_link_libraries(marrow_sokol_imgui_runtime_probe PRIVATE marrow_imgui_core)

add_custom_target(marrow_verify_third_party
    COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/VerifyThirdParty.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Verifying vendored dependency revisions and hashes"
    VERBATIM
)

set(MARROW_RENDERER_SHADER_SOURCE
    ${PROJECT_SOURCE_DIR}/src/renderer/marrow_renderer_shader.glsl)
set(MARROW_RENDERER_SHADER_METAL_HEADER
    ${PROJECT_SOURCE_DIR}/src/renderer/generated/marrow_renderer_shader_metal.h)
set(MARROW_RENDERER_SHADER_GL_HEADER
    ${PROJECT_SOURCE_DIR}/src/renderer/generated/marrow_renderer_shader_gl.h)

set(MARROW_SOKOL_SHDC "")
set(MARROW_RENDERER_SHADER_SLANG "")
if(APPLE)
    set(MARROW_SOKOL_SHDC ${PROJECT_SOURCE_DIR}/external/sokol-tools-bin/bin/osx/sokol-shdc)
elseif(WIN32)
    set(MARROW_SOKOL_SHDC ${PROJECT_SOURCE_DIR}/external/sokol-tools-bin/bin/win32/sokol-shdc.exe)
elseif(UNIX)
    set(MARROW_SOKOL_SHDC ${PROJECT_SOURCE_DIR}/external/sokol-tools-bin/bin/linux/sokol-shdc)
endif()

if(MARROW_SOKOL_SHDC AND EXISTS ${MARROW_SOKOL_SHDC})
    add_custom_command(
        OUTPUT ${MARROW_RENDERER_SHADER_GL_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${PROJECT_SOURCE_DIR}/src/renderer/generated
        COMMAND ${MARROW_SOKOL_SHDC}
            --input ${MARROW_RENDERER_SHADER_SOURCE}
            --output ${MARROW_RENDERER_SHADER_GL_HEADER}
            --slang glsl410
            --format sokol
            --bytecode
            --ifdef
            --no-log-cmdline
        DEPENDS
            ${MARROW_RENDERER_SHADER_SOURCE}
            ${MARROW_SOKOL_SHDC}
        VERBATIM
    )

    if(APPLE)
        add_custom_command(
            OUTPUT ${MARROW_RENDERER_SHADER_METAL_HEADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory
                ${PROJECT_SOURCE_DIR}/src/renderer/generated
            COMMAND ${MARROW_SOKOL_SHDC}
                --input ${MARROW_RENDERER_SHADER_SOURCE}
                --output ${MARROW_RENDERER_SHADER_METAL_HEADER}
                --slang metal_macos
                --format sokol
                --bytecode
                --ifdef
                --no-log-cmdline
            DEPENDS
                ${MARROW_RENDERER_SHADER_SOURCE}
                ${MARROW_SOKOL_SHDC}
            VERBATIM
        )
    elseif(NOT EXISTS ${MARROW_RENDERER_SHADER_METAL_HEADER})
        message(FATAL_ERROR
            "The committed Metal shader header is missing; regenerate it on macOS.")
    endif()

    add_custom_target(marrow_renderer_shaders
        DEPENDS
            ${MARROW_RENDERER_SHADER_GL_HEADER}
            ${MARROW_RENDERER_SHADER_METAL_HEADER}
    )
endif()

add_library(marrow_renderer_core STATIC
    src/renderer/sokol_gfx_impl.cpp
    src/renderer/sokol_backend.cpp
    src/renderer/module.cpp
    ${MARROW_RENDERER_SHADER_GL_HEADER}
    ${MARROW_RENDERER_SHADER_METAL_HEADER}
)

target_compile_definitions(marrow_renderer_core PRIVATE
    MARROW_RENDERER_SCENE_ONLY
)

target_include_directories(marrow_renderer_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/external
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/renderer/generated
)

add_library(marrow_renderer_sapp_host STATIC
    src/renderer/sokol_sapp_impl.cpp
)

target_include_directories(marrow_renderer_sapp_host
    PRIVATE
        ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(marrow_renderer_sapp_host
    PUBLIC
        marrow_renderer_core
)

add_library(marrow_renderer STATIC
    src/renderer/sokol_backend.cpp
    src/renderer/demo_shell.cpp
)

if(APPLE)
    set_source_files_properties(
        src/renderer/sokol_backend.cpp
        src/renderer/sokol_gfx_impl.cpp
        src/renderer/sokol_sapp_impl.cpp
        PROPERTIES
            LANGUAGE OBJCXX
    )
endif()

target_include_directories(marrow_renderer
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/external
        ${PROJECT_SOURCE_DIR}/src/renderer/generated
)

set(MARROW_RUNTIME_SOURCES
    src/runtime/allocator.cpp
    src/runtime/animation.cpp
    src/runtime/animation_state.cpp
    src/runtime/binary.cpp
    src/runtime/profiler.cpp
    src/runtime/module.cpp
    src/runtime/parameter_model.cpp
    src/runtime/parameter_parse.cpp
    src/runtime/parameter_state.cpp
    src/runtime/atlas.cpp
    src/runtime/json.cpp
    src/runtime/skeleton.cpp
    src/runtime/skeleton_animation.cpp
    src/runtime/skeleton_constraints.cpp
    src/runtime/skeleton_parse.cpp
    src/runtime/skeleton_physics.cpp
    src/runtime/skeleton_skin.cpp
    src/runtime/skeleton_world.cpp
    src/runtime/spine_import.cpp
)

add_library(marrow_runtime STATIC
    ${MARROW_RUNTIME_SOURCES}
)

target_include_directories(marrow_runtime
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(marrow_constraint_warning_check STATIC EXCLUDE_FROM_ALL
    src/runtime/skeleton_constraints.cpp
)

target_include_directories(marrow_constraint_warning_check
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(marrow_constraint_warning_check
        PRIVATE
            -Wdouble-promotion
            -Werror
    )
endif()

if(MARROW_ENABLE_THREAD_SANITIZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "MARROW_ENABLE_THREAD_SANITIZER requires Clang or GCC.")
    endif()

    add_library(marrow_thread_sanitizer_flags INTERFACE)
    target_compile_options(marrow_thread_sanitizer_flags INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(marrow_thread_sanitizer_flags INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )

    add_library(marrow_runtime_tsan STATIC
        ${MARROW_RUNTIME_SOURCES}
    )

    target_include_directories(marrow_runtime_tsan
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(marrow_runtime_tsan
        PRIVATE
            marrow_thread_sanitizer_flags
    )
endif()

add_library(marrow_c STATIC
    src/c_api/marrow_c.cpp
)

target_include_directories(marrow_c
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_c
    PUBLIC
        marrow_renderer
        marrow_editor
)

target_link_libraries(marrow_renderer_core
    PUBLIC
        marrow_runtime
        ZLIB::ZLIB
)

target_link_libraries(marrow_renderer
    PUBLIC
        marrow_renderer_core
    PRIVATE
        marrow_renderer_sapp_host
)

if(TARGET marrow_renderer_shaders)
    add_dependencies(marrow_renderer_core marrow_renderer_shaders)
    add_dependencies(marrow_renderer marrow_renderer_shaders)
endif()

if(APPLE)
    find_library(MARROW_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(MARROW_COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(MARROW_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(MARROW_METAL_FRAMEWORK Metal REQUIRED)

    target_link_libraries(marrow_renderer_core
        PUBLIC
            ${MARROW_FOUNDATION_FRAMEWORK}
            ${MARROW_QUARTZCORE_FRAMEWORK}
            ${MARROW_METAL_FRAMEWORK}
    )

    target_link_libraries(marrow_renderer
        PRIVATE
            ${MARROW_FOUNDATION_FRAMEWORK}
            ${MARROW_COCOA_FRAMEWORK}
            ${MARROW_QUARTZCORE_FRAMEWORK}
            ${MARROW_METAL_FRAMEWORK}
    )
elseif(UNIX AND NOT APPLE)
    set(MARROW_SOKOL_PLATFORM_LIBRARIES
        ${MARROW_OPENGL_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )

    if(TARGET X11::X11)
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES X11::X11)
    else()
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES ${X11_LIBRARIES})
    endif()
    if(TARGET X11::Xi)
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES X11::Xi)
    elseif(X11_Xi_LIB)
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES ${X11_Xi_LIB})
    else()
        message(FATAL_ERROR "marrow_renderer requires the XInput Xi library for sokol_app.")
    endif()
    if(TARGET X11::Xcursor)
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES X11::Xcursor)
    elseif(X11_Xcursor_LIB)
        list(APPEND MARROW_SOKOL_PLATFORM_LIBRARIES ${X11_Xcursor_LIB})
    else()
        message(FATAL_ERROR "marrow_renderer requires the Xcursor library for sokol_app.")
    endif()

    target_link_libraries(marrow_renderer_core
        PUBLIC
            ${MARROW_OPENGL_LIBRARIES}
    )

    target_link_libraries(marrow_renderer
        PRIVATE
            ${MARROW_SOKOL_PLATFORM_LIBRARIES}
    )
elseif(WIN32)
    target_link_libraries(marrow_renderer_core PUBLIC opengl32)
    target_link_libraries(marrow_renderer
        PRIVATE
            user32
            shell32
            ole32
            gdi32
            opengl32
    )
endif()

add_library(marrow_editor STATIC
    src/editor/module.cpp
    src/editor/selection.cpp
    src/editor/preferences.cpp
    src/editor/atlas_packer.cpp
    src/editor/psd_import.cpp
    src/editor/project.cpp
    src/editor/parameter_project_model.cpp
    src/editor/authoring.cpp
    src/editor/session.cpp
    src/editor/agent_dispatch.cpp
    src/editor/agent_handlers_inspection.cpp
    src/editor/agent_handlers_editing.cpp
    src/editor/agent_handlers_constraints.cpp
    src/editor/agent_handlers_parameters.cpp
    src/editor/agent_handlers_management.cpp
)

target_include_directories(marrow_editor
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/editor
)

target_link_libraries(marrow_editor
    PUBLIC
        marrow_runtime
    PRIVATE
        ZLIB::ZLIB
)

if(WIN32)
    target_link_libraries(marrow_editor PRIVATE shell32 ole32)
endif()

add_executable(marrow_bootstrap
    src/main.cpp
)

target_include_directories(marrow_bootstrap
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_bootstrap
    PRIVATE
        marrow_editor
        marrow_runtime
)

add_executable(marrow_renderer_sample
    src/samples/renderer_sample.cpp
)

target_link_libraries(marrow_renderer_sample
    PRIVATE
        marrow_renderer
)

add_executable(marrow_validator
    src/samples/marrow_validator.cpp
)

target_link_libraries(marrow_validator
    PRIVATE
        marrow_renderer
)

add_executable(marrow_fixture_smoke
    src/samples/runtime_fixture_smoke.cpp
)

target_link_libraries(marrow_fixture_smoke
    PRIVATE
        marrow_renderer
)

add_executable(marrow_c_smoke
    src/samples/marrow_c_smoke.c
)

set_target_properties(marrow_c_smoke PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    LINKER_LANGUAGE CXX
)

target_link_libraries(marrow_c_smoke
    PRIVATE
        marrow_c
)

add_executable(marrow_unit_tests
    src/tests/runtime_math_tests.cpp
)

target_include_directories(marrow_unit_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime
        ${PROJECT_SOURCE_DIR}/src/renderer
)

target_link_libraries(marrow_unit_tests
    PRIVATE
        marrow_renderer
)

add_executable(marrow_preference_tests
    src/tests/preference_store_tests.cpp
)

target_include_directories(marrow_preference_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/editor
)

target_link_libraries(marrow_preference_tests
    PRIVATE
        marrow_editor
)

add_executable(marrow_selection_tests
    src/tests/selection_set_tests.cpp
)

target_link_libraries(marrow_selection_tests
    PRIVATE
        marrow_editor
)

add_executable(marrow_windowing_tests
    src/tests/windowing_tests.cpp
)

target_link_libraries(marrow_windowing_tests
    PRIVATE
        marrow_windowing_support
)

add_executable(marrow_pen_input_tests
    src/tests/pen_input_tests.cpp
    src/editor/sdl_input.cpp
)

target_include_directories(marrow_pen_input_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/external/sdl3/include
)

add_executable(marrow_window_host_smoke
    src/tests/window_host_smoke.cpp
)

target_link_libraries(marrow_window_host_smoke
    PRIVATE
        marrow_window_host
)

target_link_libraries(marrow_pen_input_tests
    PRIVATE
        marrow_windowing_support
)

add_executable(marrow_agent_socket_tests
    src/tests/agent_socket_tests.cpp
    src/editor/agent_socket.cpp
)

target_include_directories(marrow_agent_socket_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/editor
)

target_link_libraries(marrow_agent_socket_tests PRIVATE marrow_editor)
if(WIN32)
    target_link_libraries(marrow_agent_socket_tests PRIVATE ws2_32)
endif()

add_executable(marrow_benchmark
    src/samples/marrow_benchmark.cpp
)

target_include_directories(marrow_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime
)

target_link_libraries(marrow_benchmark
    PRIVATE
        marrow_renderer
)

add_executable(marrow_thread_stress
    src/tests/runtime_thread_stress.cpp
)

if(MARROW_ENABLE_THREAD_SANITIZER)
    target_link_libraries(marrow_thread_stress
        PRIVATE
            marrow_runtime_tsan
            marrow_thread_sanitizer_flags
    )
else()
    target_link_libraries(marrow_thread_stress
        PRIVATE
            marrow_runtime
    )
endif()

if(TARGET Threads::Threads)
    target_link_libraries(marrow_thread_stress
        PRIVATE
            Threads::Threads
    )
endif()

add_executable(marrow_inspect
    src/samples/runtime_inspect.cpp
)

target_link_libraries(marrow_inspect
    PRIVATE
        marrow_runtime
)

add_executable(spine_to_marrow
    src/samples/spine_to_marrow.cpp
)

target_link_libraries(spine_to_marrow
    PRIVATE
        marrow_runtime
)

add_executable(marrow_spine_import_smoke
    src/samples/spine_import_smoke.cpp
)

target_link_libraries(marrow_spine_import_smoke
    PRIVATE
        marrow_runtime
)

add_executable(marrow_project_smoke
    src/samples/editor_project_smoke.cpp
)

target_link_libraries(marrow_project_smoke
    PRIVATE
        marrow_editor
)

add_executable(marrow_parameter_project_smoke
    src/samples/parameter_project_smoke.cpp
)

target_link_libraries(marrow_parameter_project_smoke
    PRIVATE
        marrow_editor
)

add_executable(marrow_atlas_packer_smoke
    src/samples/atlas_packer_smoke.cpp
)

target_include_directories(marrow_atlas_packer_smoke
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/editor
)

target_link_libraries(marrow_atlas_packer_smoke
    PRIVATE
        marrow_editor
        marrow_renderer
)

add_executable(marrow_psd_import_smoke
    src/samples/psd_import_smoke.cpp
)

target_link_libraries(marrow_psd_import_smoke
    PRIVATE
        marrow_editor
        marrow_renderer
)

add_executable(marrow_agent_dispatch_smoke
    src/samples/agent_dispatch_smoke.cpp
)

target_link_libraries(marrow_agent_dispatch_smoke
    PRIVATE
        marrow_c
)

add_executable(marrow_editor_shell
    src/editor/shell_main.cpp
    src/editor/shell_core.cpp
    src/editor/shell_preview.cpp
    src/editor/shell_asset_watch.cpp
    src/editor/shell_derived_cache.cpp
    src/editor/shell_timeline.cpp
    src/editor/shell_constraints.cpp
    src/editor/shell_selection.cpp
    src/editor/shell_inspector.cpp
    src/editor/shell_weight_paint.cpp
    src/editor/shell_viewport_ui.cpp
    src/editor/shell_project_panels.cpp
    src/editor/shell_parameters.cpp
    src/editor/shell_agent_panel.cpp
    src/editor/shell_viewport.cpp
    src/editor/shell_smoke.cpp
    src/editor/shell_widgets.cpp
    src/editor/icon_registry.cpp
    src/editor/viewport_renderer.cpp
    src/editor/agent_socket.cpp
    src/editor/sokol_imgui_impl.cpp
    src/editor/sokol_graphics_device.cpp
)

target_include_directories(marrow_editor_shell
    PRIVATE
        ${PROJECT_SOURCE_DIR}/external
        ${PROJECT_SOURCE_DIR}/src/renderer/generated
)

target_link_libraries(marrow_editor_shell
    PRIVATE
        marrow_editor
        marrow_renderer_core
        marrow_imgui_sdl3
        marrow_window_host
)

if(WIN32)
    target_link_libraries(marrow_editor_shell PRIVATE ws2_32)
endif()

if(APPLE)
    target_sources(marrow_editor_shell PRIVATE
        src/editor/macos_app_focus.mm
    )
endif()

add_custom_command(TARGET marrow_editor_shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/fonts
        $<TARGET_FILE_DIR:marrow_editor_shell>/fonts
    COMMENT "Copying editor fonts to build directory"
)

file(GLOB MARROW_EDITOR_ICON_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/icons/*.png")

set(MARROW_EDITOR_ICON_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/marrow_editor_icons.stamp")

add_custom_command(
    OUTPUT ${MARROW_EDITOR_ICON_STAMP}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/icons/export/white_48
        ${CMAKE_CURRENT_BINARY_DIR}/icons
    COMMAND ${CMAKE_COMMAND} -E touch ${MARROW_EDITOR_ICON_STAMP}
    DEPENDS ${MARROW_EDITOR_ICON_SOURCES}
    COMMENT "Copying editor icons to build directory"
    VERBATIM
)

add_custom_target(marrow_editor_icons ALL
    DEPENDS ${MARROW_EDITOR_ICON_STAMP})
add_dependencies(marrow_editor_shell marrow_editor_icons)

if(WIN32)
    set(MARROW_PORTABLE_ROOT
        "${PROJECT_BINARY_DIR}/portable/$<CONFIG>")
    set(MARROW_PORTABLE_DIR
        "${MARROW_PORTABLE_ROOT}/Marrow")
    set(MARROW_PORTABLE_ZIP
        "${MARROW_PORTABLE_ROOT}/Marrow-$<CONFIG>-x64.zip")

    add_custom_target(marrow_portable_stage
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${MARROW_PORTABLE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${MARROW_PORTABLE_DIR}
            ${MARROW_PORTABLE_DIR}/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:marrow_editor_shell>
            ${MARROW_PORTABLE_DIR}/marrow_editor_shell.exe
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets/fonts
            ${MARROW_PORTABLE_DIR}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets/icons/export/white_48
            ${MARROW_PORTABLE_DIR}/icons
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets/fixtures
            ${MARROW_PORTABLE_DIR}/assets/fixtures
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/licenses
            ${MARROW_PORTABLE_DIR}/licenses
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md
            ${MARROW_PORTABLE_DIR}/THIRD_PARTY.md
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/packaging/README-Windows.txt
            ${MARROW_PORTABLE_DIR}/README.txt
        COMMAND ${CMAKE_COMMAND} -E copy -t ${MARROW_PORTABLE_DIR}
            $<TARGET_RUNTIME_DLLS:marrow_editor_shell>
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${MARROW_PORTABLE_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/GeneratePortableManifest.cmake
        COMMAND ${CMAKE_COMMAND} -E rm -f ${MARROW_PORTABLE_ZIP}
        COMMAND ${CMAKE_COMMAND} -E chdir ${MARROW_PORTABLE_ROOT}
            ${CMAKE_COMMAND} -E tar cf ${MARROW_PORTABLE_ZIP}
                --format=zip Marrow
        COMMAND ${CMAKE_COMMAND}
            -DARCHIVE=${MARROW_PORTABLE_ZIP}
            -P ${PROJECT_SOURCE_DIR}/cmake/HashPortableArchive.cmake
        COMMAND_EXPAND_LISTS
        DEPENDS marrow_editor_shell
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        COMMENT "Staging the self-contained Marrow portable folder and transport ZIP"
        VERBATIM
    )
endif()

if(MARROW_ENABLE_DISPLAY_TESTS)
    add_executable(marrow_gpu_parity_smoke
        src/tests/gpu_parity_smoke.cpp
        src/editor/sokol_graphics_device.cpp
    )
    if(APPLE)
        target_sources(marrow_gpu_parity_smoke PRIVATE
            src/tests/gpu_readback_metal.mm
        )
        set_source_files_properties(
            src/tests/gpu_readback_metal.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
        target_compile_definitions(marrow_gpu_parity_smoke PRIVATE SOKOL_METAL)
    else()
        target_sources(marrow_gpu_parity_smoke PRIVATE
            src/tests/gpu_readback_gl.cpp
        )
        target_compile_definitions(marrow_gpu_parity_smoke PRIVATE SOKOL_GLCORE)
        if(WIN32)
            target_link_libraries(marrow_gpu_parity_smoke PRIVATE opengl32)
        elseif(UNIX)
            target_link_libraries(marrow_gpu_parity_smoke PRIVATE ${MARROW_OPENGL_LIBRARIES})
        endif()
    endif()
    target_include_directories(marrow_gpu_parity_smoke PRIVATE
        ${PROJECT_SOURCE_DIR}/external
        ${PROJECT_SOURCE_DIR}/src/editor
        ${PROJECT_SOURCE_DIR}/src/tests
    )
    target_link_libraries(marrow_gpu_parity_smoke PRIVATE
        marrow_renderer_core
        marrow_window_host
    )

    add_custom_target(marrow_editor_display_smoke
        COMMAND ${CMAKE_COMMAND} -E env
            MARROW_DISPLAY_SMOKE=1
            $<TARGET_FILE:marrow_editor_shell>
            --project ${PROJECT_SOURCE_DIR}/assets/fixtures/player_idle.marrow
            --auto-close 20
        DEPENDS marrow_editor_shell
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running the SDL/Sokol editor display smoke"
        VERBATIM
    )
endif()

if(DOXYGEN_FOUND)
    add_custom_target(marrow_docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${MARROW_DOXYGEN_CONFIG}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating Marrow documentation with Doxygen"
        VERBATIM
    )
else()
    add_custom_target(marrow_docs
        COMMAND ${CMAKE_COMMAND} -E echo "Doxygen was not found on PATH. Install Doxygen and rerun marrow_docs."
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "Doxygen is required to build Marrow documentation"
        VERBATIM
    )
endif()

if(APPLE)
    target_compile_definitions(marrow_editor_shell PRIVATE SOKOL_METAL)
else()
    target_compile_definitions(marrow_editor_shell PRIVATE SOKOL_GLCORE)
endif()

if(BUILD_TESTING)
    add_test(
        NAME marrow.sokol_imgui_lifecycle
        COMMAND marrow_sokol_imgui_runtime_probe
    )
    set_tests_properties(marrow.sokol_imgui_lifecycle PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "renderer;dependency;unit;noninteractive"
    )

    add_test(
        NAME marrow.windowing_unit
        COMMAND marrow_windowing_tests
    )
    set_tests_properties(marrow.windowing_unit PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "windowing;unit;noninteractive"
    )

    add_test(
        NAME marrow.pen_input_unit
        COMMAND marrow_pen_input_tests
    )
    set_tests_properties(marrow.pen_input_unit PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "windowing;pen;noninteractive"
    )

    add_test(
        NAME marrow.agent_socket_transport
        COMMAND marrow_agent_socket_tests
    )
    set_tests_properties(marrow.agent_socket_transport PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;agent;socket;unit;noninteractive"
    )

    if(MARROW_ENABLE_DISPLAY_TESTS)
        add_test(
            NAME marrow.window_host_smoke
            COMMAND marrow_window_host_smoke
        )
        set_tests_properties(marrow.window_host_smoke PROPERTIES
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            LABELS "windowing;integration;display"
        )

        add_test(
            NAME marrow.gpu_parity_smoke
            COMMAND marrow_gpu_parity_smoke
        )
        set_tests_properties(marrow.gpu_parity_smoke PROPERTIES
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            LABELS "renderer;integration;display"
        )

        add_test(
            NAME marrow.editor_display_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                MARROW_DISPLAY_SMOKE=1
                $<TARGET_FILE:marrow_editor_shell>
                --project ${PROJECT_SOURCE_DIR}/assets/fixtures/player_idle.marrow
                --auto-close 20
        )
        set_tests_properties(marrow.editor_display_smoke PROPERTIES
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            LABELS "editor;integration;display"
        )
    endif()

    add_test(
        NAME marrow.runtime_unit
        COMMAND marrow_unit_tests
    )
    set_tests_properties(marrow.runtime_unit PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "runtime;unit;noninteractive"
    )

    add_test(
        NAME marrow.preference_store
        COMMAND marrow_preference_tests
    )
    set_tests_properties(marrow.preference_store PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;preferences;unit;noninteractive"
    )

    add_test(
        NAME marrow.selection_set
        COMMAND marrow_selection_tests
    )
    set_tests_properties(marrow.selection_set PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;selection;unit;noninteractive"
    )

    add_test(
        NAME marrow.runtime_fixture_smoke
        COMMAND marrow_fixture_smoke
            assets/fixtures/player_idle.mskl
            assets/fixtures/player_idle.matl
    )
    set_tests_properties(marrow.runtime_fixture_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "runtime;fixture;smoke;noninteractive"
    )

    add_test(
        NAME marrow.renderer_skip_render_smoke
        COMMAND marrow_renderer_sample
            --hud
            --skip-render
            assets/fixtures/player_idle.mskl
            assets/fixtures/player_idle.matl
    )
    set_tests_properties(marrow.renderer_skip_render_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "renderer;smoke;noninteractive"
    )

    add_test(
        NAME marrow.project_smoke
        COMMAND marrow_project_smoke
            assets/fixtures/player_idle.marrow
    )
    set_tests_properties(marrow.project_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;project;smoke;noninteractive"
    )

    add_test(
        NAME marrow.parameter_project_smoke
        COMMAND marrow_parameter_project_smoke
            assets/fixtures/parameter_face_basic.marrow
    )
    set_tests_properties(marrow.parameter_project_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "runtime;editor;parameter;project;smoke;noninteractive"
    )

    add_test(
        NAME marrow.parameter_editor_shell_smoke
        COMMAND marrow_editor_shell
            --project assets/fixtures/parameter_face_basic.marrow
            --auto-close 2
    )
    set_tests_properties(marrow.parameter_editor_shell_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;parameter;shell;smoke;noninteractive"
    )

    add_test(
        NAME marrow.art_path_renderer_smoke
        COMMAND marrow_renderer_sample
            --skip-render
            --no-atlas
            assets/fixtures/art_path_stroke.mskl
    )
    set_tests_properties(marrow.art_path_renderer_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "runtime;renderer;parameter;art-path;smoke;noninteractive"
    )

    add_test(
        NAME marrow.renderer_missing_atlas_guard
        COMMAND marrow_renderer_sample
            --skip-render
            --no-atlas
            assets/fixtures/parameter_face_basic.mskl
    )
    set_tests_properties(marrow.renderer_missing_atlas_guard PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "renderer;parameter;guardrail;noninteractive"
        WILL_FAIL TRUE
    )

    add_test(
        NAME marrow.agent_dispatch_smoke
        COMMAND marrow_agent_dispatch_smoke
    )
    set_tests_properties(marrow.agent_dispatch_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;agent;smoke;noninteractive"
    )

    add_test(
        NAME marrow.c_abi_smoke
        COMMAND marrow_c_smoke
    )
    set_tests_properties(marrow.c_abi_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "c-abi;smoke;noninteractive"
    )

    add_test(
        NAME marrow.editor_shell_smoke
        COMMAND marrow_editor_shell
            --project
            assets/fixtures/player_idle.marrow
            --auto-close
            5
    )
    set_tests_properties(marrow.editor_shell_smoke PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "editor;shell;smoke;noninteractive"
    )
endif()
