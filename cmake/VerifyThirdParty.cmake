cmake_minimum_required(VERSION 3.16)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

function(verify_sha relative expected)
    set(path "${ROOT}/${relative}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing vendored dependency file: ${relative}")
    endif()
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "Vendored dependency hash mismatch for ${relative}: ${actual}, expected ${expected}")
    endif()
endfunction()

function(verify_tree_manifest relative expected)
    set(tree "${ROOT}/${relative}")
    if(NOT IS_DIRECTORY "${tree}")
        message(FATAL_ERROR "Missing vendored dependency tree: ${relative}")
    endif()

    file(GLOB_RECURSE tree_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${tree}"
        "${tree}/*")
    list(SORT tree_files)

    set(manifest "")
    foreach(tree_file IN LISTS tree_files)
        file(SHA256 "${tree}/${tree_file}" file_sha)
        string(APPEND manifest "${file_sha}  ./${tree_file}\n")
    endforeach()
    string(SHA256 actual "${manifest}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "Vendored dependency tree mismatch for ${relative}: ${actual}, expected ${expected}")
    endif()
endfunction()

verify_sha("external/sdl3/CMakeLists.txt" "dd47de24dd913f753fe3c87f039ab8d6ca84937aef5122b1b469aa56015c08e3")
verify_sha("external/sdl3/LICENSE.txt" "1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd")
verify_sha("licenses/SDL3.txt" "1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd")
verify_sha("licenses/zlib.txt" "e32ff4e00d9d94930537635291da39e7e612703334bf6fde8c7f1686fe8a45a2")
verify_tree_manifest("external/zlib" "2cfe9a7128fb7ed7726f7fa04eab2237a29b9b832ca710b99f17160f0b8da60c")
verify_sha("licenses/Dear-ImGui.txt" "173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3")
verify_sha("licenses/Sokol.txt" "72f9367756dba68918b8955092c14503934681862eee33cc16495a02f40025ef")
verify_sha("licenses/sokol-tools-bin.txt" "d7bb344eb6e070a617d3b37b3bda6f4b3a08634dd0a413eca1a1b1d247859ee2")
verify_sha("external/imgui/imgui.cpp" "1172357a905ba9ad2cdb5c24a25e725b38cddf268ea6a2a0a58e91601f08dcc1")
verify_sha("external/imgui/imgui_draw.cpp" "a71b4813bcc7695cc01f12bb5bed91d1c2d81b40211a81945505326f424eae99")
verify_sha("external/imgui/imgui_tables.cpp" "f9f709da6010aa65460d290eed85d970c40b66681c38d466d9b0e2e60486c990")
verify_sha("external/imgui/imgui_widgets.cpp" "7ee6e126b9025f57de2f6a109e99ebbdf2e8b4923e98b2b885590de704bb7793")
verify_sha("external/imgui/imgui_internal.h" "fe477721094f897648430bf4a973321456caa9d1111b255b74c6b43a9a9bfd94")
verify_sha("external/imgui/backends/imgui_impl_sdl3.cpp" "560d05b57eee5cc9005e93389ad8ac3b135b9e96bc1a46add440ae0344d71d27")
verify_sha("external/imgui/backends/imgui_impl_sdl3.h" "a7716305e9312d32d7ffb7806555d3ba518c6df6dbf26077d5f6a6e022c6efe5")
verify_sha("external/sokol_gfx.h" "99044f0a719eb98e8e85ce5a8b48b0b41c0cdf08b22c02bffc0545d5915537f6")
verify_sha("external/sokol_app.h" "0e8266a9494ed9aed414ac52fc6e36543cac801afa220766a5dcc4d2c4c2c0eb")
verify_sha("external/sokol_glue.h" "5fe1ab5a9ab0b7dc8761e1da151cacafea338421d3799745067e7421e28af670")
verify_sha("external/sokol_log.h" "b1bf1403c738b8f18b09819d9190ac160298dd590440c9df57cb9d90b77eb29d")
verify_sha("external/sokol_imgui.h" "dcad7d55e3a14a8adaf0400bfea836697abc5e9135e6b17b2806116797b25082")
verify_sha("external/patches/sokol_imgui-59f0433-shutdown-debug-group.patch" "cde046c5d93cb77d7584db51ddf866671be702c67ff0cfadd992dc78d07baaa7")
verify_sha("external/sokol-tools-bin/bin/osx/sokol-shdc" "a284a4fe969458e79ba464ab0abf8c46924b563838c2d288f9141c5f791dc633")
verify_sha("external/sokol-tools-bin/bin/linux/sokol-shdc" "74adb2aa9e20708654b502931be8758cee29b0e39a905fd1165b53f061a29d2b")
verify_sha("external/sokol-tools-bin/bin/win32/sokol-shdc.exe" "c26665c3ddb4d6abc199fadd078689a12f9a7857193e1811ab19e5e467496f77")
verify_sha("src/renderer/generated/marrow_renderer_shader_metal.h" "ebb385888764d038831153e72d1aa21061e762021dc2ca2239ed904cca1e058e")
verify_sha("src/renderer/generated/marrow_renderer_shader_gl.h" "5da815aaf7b3b73299cae81ef966440429d4362e976350f2b28d37d3b277e31e")

foreach(removed_path IN ITEMS
    "external/glfw"
    "external/sdl3/.github/workflows"
    "external/imgui/.github/workflows"
    "external/imgui/backends/imgui_impl_glfw.cpp"
    "external/imgui/backends/imgui_impl_glfw.h"
    "external/imgui/backends/imgui_impl_opengl3.cpp"
    "external/imgui/backends/imgui_impl_opengl3.h"
    "external/imgui/backends/imgui_impl_opengl3_loader.h")
    if(EXISTS "${ROOT}/${removed_path}")
        message(FATAL_ERROR "Removed production backend unexpectedly exists: ${removed_path}")
    endif()
endforeach()

file(READ "${ROOT}/external/sdl3/include/SDL3/SDL_version.h" sdl_version)
if(NOT sdl_version MATCHES "SDL_MAJOR_VERSION[ \t]+3" OR
   NOT sdl_version MATCHES "SDL_MINOR_VERSION[ \t]+4" OR
   NOT sdl_version MATCHES "SDL_MICRO_VERSION[ \t]+14")
    message(FATAL_ERROR "Vendored SDL version is not 3.4.14")
endif()

file(READ "${ROOT}/external/imgui/imgui.h" imgui_version)
if(NOT imgui_version MATCHES "IMGUI_VERSION[ \t]+\"1\\.92\\.6\"")
    message(FATAL_ERROR "Vendored Dear ImGui version is not 1.92.6")
endif()

message(STATUS "Vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui, and sokol-shdc hashes verified")
