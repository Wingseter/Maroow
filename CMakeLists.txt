cmake_minimum_required(VERSION 3.16)

project(Marrow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(marrow_renderer STATIC
    src/renderer/module.cpp
)

target_include_directories(marrow_renderer
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(marrow_runtime STATIC
    src/runtime/animation.cpp
    src/runtime/module.cpp
    src/runtime/atlas.cpp
    src/runtime/json.cpp
    src/runtime/skeleton.cpp
)

target_include_directories(marrow_runtime
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_renderer
    PUBLIC
        marrow_runtime
)

add_executable(marrow_bootstrap
    src/main.cpp
    src/editor/module.cpp
)

target_include_directories(marrow_bootstrap
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(marrow_bootstrap
    PRIVATE
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
        marrow_runtime
)
