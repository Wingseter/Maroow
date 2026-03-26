cmake_minimum_required(VERSION 3.16)

project(Marrow LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

find_package(ZLIB REQUIRED)

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

add_library(marrow_renderer STATIC
    src/renderer/module.cpp
)

target_include_directories(marrow_renderer
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(marrow_runtime STATIC
    src/runtime/animation.cpp
    src/runtime/animation_state.cpp
    src/runtime/binary.cpp
    src/runtime/module.cpp
    src/runtime/atlas.cpp
    src/runtime/json.cpp
    src/runtime/skeleton.cpp
    src/runtime/skeleton_animation.cpp
    src/runtime/skeleton_constraints.cpp
    src/runtime/skeleton_parse.cpp
    src/runtime/skeleton_physics.cpp
    src/runtime/skeleton_skin.cpp
)

target_include_directories(marrow_runtime
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_renderer
    PUBLIC
        marrow_runtime
        glfw
        ${MARROW_OPENGL_LIBRARIES}
        ZLIB::ZLIB
)

if(APPLE)
    target_compile_definitions(marrow_renderer PRIVATE GL_SILENCE_DEPRECATION)
endif()

add_library(marrow_editor STATIC
    src/editor/module.cpp
    src/editor/project.cpp
)

target_include_directories(marrow_editor
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_editor
    PUBLIC
        marrow_runtime
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

add_executable(marrow_inspect
    src/samples/runtime_inspect.cpp
)

target_link_libraries(marrow_inspect
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

add_executable(marrow_editor_shell
    src/editor/shell_main.cpp
)

target_link_libraries(marrow_editor_shell
    PRIVATE
        marrow_editor
        marrow_imgui
)

if(APPLE)
    target_compile_definitions(marrow_editor_shell PRIVATE GL_SILENCE_DEPRECATION)
endif()
