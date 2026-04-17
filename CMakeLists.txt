cmake_minimum_required(VERSION 3.16)

project(Marrow LANGUAGES C CXX)

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

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

if(APPLE)
    find_library(MARROW_OPENGL_LIBRARY OpenGL REQUIRED)
    set(MARROW_OPENGL_LIBRARIES ${MARROW_OPENGL_LIBRARY})
else()
    find_package(OpenGL REQUIRED)
    set(MARROW_OPENGL_LIBRARIES OpenGL::GL)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
endif()

find_package(ZLIB REQUIRED)
find_package(Doxygen QUIET)

set(MARROW_DOXYGEN_CONFIG ${PROJECT_BINARY_DIR}/Doxyfile)
configure_file(
    ${PROJECT_SOURCE_DIR}/docs/Doxyfile.in
    ${MARROW_DOXYGEN_CONFIG}
    @ONLY
)

set(MARROW_IMGUI_DIR ${PROJECT_SOURCE_DIR}/external/imgui)
add_library(marrow_imgui STATIC
    ${MARROW_IMGUI_DIR}/imgui.cpp
    ${MARROW_IMGUI_DIR}/imgui_draw.cpp
    ${MARROW_IMGUI_DIR}/imgui_tables.cpp
    ${MARROW_IMGUI_DIR}/imgui_widgets.cpp
    ${MARROW_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${MARROW_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(marrow_imgui
    PUBLIC
        ${MARROW_IMGUI_DIR}
        ${MARROW_IMGUI_DIR}/backends
        ${PROJECT_SOURCE_DIR}/external/glfw/include
)

target_link_libraries(marrow_imgui
    PUBLIC
        glfw
        ${MARROW_OPENGL_LIBRARIES}
)

if(APPLE)
    target_compile_definitions(marrow_imgui PRIVATE GL_SILENCE_DEPRECATION)
endif()

set(MARROW_RENDERER_SHADER_SOURCE
    ${PROJECT_SOURCE_DIR}/src/renderer/marrow_renderer_shader.glsl)
set(MARROW_RENDERER_SHADER_HEADER
    ${PROJECT_SOURCE_DIR}/src/renderer/generated/marrow_renderer_shader.h)

set(MARROW_SOKOL_SHDC "")
set(MARROW_RENDERER_SHADER_SLANG "")
if(APPLE)
    set(MARROW_SOKOL_SHDC ${PROJECT_SOURCE_DIR}/external/sokol-tools-bin/bin/osx/sokol-shdc)
    set(MARROW_RENDERER_SHADER_SLANG glsl410:metal_macos)
elseif(UNIX)
    set(MARROW_SOKOL_SHDC ${PROJECT_SOURCE_DIR}/external/sokol-tools-bin/bin/linux/sokol-shdc)
    set(MARROW_RENDERER_SHADER_SLANG glsl410)
endif()

if(MARROW_SOKOL_SHDC AND EXISTS ${MARROW_SOKOL_SHDC})
    add_custom_command(
        OUTPUT ${MARROW_RENDERER_SHADER_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${PROJECT_SOURCE_DIR}/src/renderer/generated
        COMMAND ${MARROW_SOKOL_SHDC}
            --input ${MARROW_RENDERER_SHADER_SOURCE}
            --output ${MARROW_RENDERER_SHADER_HEADER}
            --slang ${MARROW_RENDERER_SHADER_SLANG}
            --format sokol
            --bytecode
            --ifdef
            --no-log-cmdline
        DEPENDS
            ${MARROW_RENDERER_SHADER_SOURCE}
            ${MARROW_SOKOL_SHDC}
        VERBATIM
    )

    add_custom_target(marrow_renderer_shaders
        DEPENDS ${MARROW_RENDERER_SHADER_HEADER}
    )
endif()

add_library(marrow_renderer STATIC
    src/renderer/sokol_backend.cpp
    src/renderer/module.cpp
    ${MARROW_RENDERER_SHADER_HEADER}
)

if(APPLE)
    set_source_files_properties(
        src/renderer/sokol_backend.cpp
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
)

target_link_libraries(marrow_renderer
    PUBLIC
        marrow_runtime
        ZLIB::ZLIB
)

if(TARGET marrow_renderer_shaders)
    add_dependencies(marrow_renderer marrow_renderer_shaders)
endif()

if(APPLE)
    find_library(MARROW_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(MARROW_COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(MARROW_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(MARROW_METAL_FRAMEWORK Metal REQUIRED)

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

    target_link_libraries(marrow_renderer
        PRIVATE
            ${MARROW_SOKOL_PLATFORM_LIBRARIES}
    )
endif()

add_library(marrow_editor STATIC
    src/editor/module.cpp
    src/editor/atlas_packer.cpp
    src/editor/psd_import.cpp
    src/editor/project.cpp
)

target_include_directories(marrow_editor
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_editor
    PUBLIC
        marrow_runtime
    PRIVATE
        ZLIB::ZLIB
)

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

add_executable(marrow_benchmark
    src/samples/marrow_benchmark.cpp
)

target_include_directories(marrow_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime
)

target_link_libraries(marrow_benchmark
    PRIVATE
        marrow_runtime
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

add_executable(marrow_editor_shell
    src/editor/shell_main.cpp
    src/editor/shell_viewport.cpp
    src/editor/shell_smoke.cpp
    src/editor/viewport_renderer.cpp
)

target_link_libraries(marrow_editor_shell
    PRIVATE
        marrow_editor
        marrow_renderer
        marrow_imgui
)

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
    target_compile_definitions(marrow_editor_shell PRIVATE GL_SILENCE_DEPRECATION)
endif()
