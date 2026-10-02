# Phase 2: both full-shell hosts delegate to the same production coordinator.
# Keep the historical target/script name, but do NOT keep a second window list.
# This is a narrow source-wiring guard, not a C++ semantic/order proof. Real
# ImGui smoke cases verify behavior; mutation tests prove this guard is live.
cmake_minimum_required(VERSION 3.16)

function(_marrow_read_code out_var file_path)
    if(NOT EXISTS "${file_path}")
        message(FATAL_ERROR "shared frame wiring: missing ${file_path}")
    endif()
    file(READ "${file_path}" _text)
    # Ignore ordinary C++ comments and quoted strings, so examples cannot satisfy
    # required calls. These source regions deliberately contain no raw strings.
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" _text "${_text}")
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"\"" _text "${_text}")
    set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

function(_marrow_host_region out_var file_path begin_anchor end_anchor)
    _marrow_read_code(_text "${file_path}")
    string(FIND "${_text}" "${begin_anchor}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "shared frame wiring: missing host anchor ${begin_anchor}")
    endif()
    string(SUBSTRING "${_text}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end_anchor}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "shared frame wiring: missing host end anchor ${end_anchor}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _region)
    set(${out_var} "${_region}" PARENT_SCOPE)
endfunction()

function(_marrow_check_host code host_name frame_begin)
    string(REGEX MATCHALL "draw_shell_frame[ \t\r\n]*\\(" _calls "${code}")
    list(LENGTH _calls _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR
            "shared frame wiring: ${host_name} requires exactly one draw_shell_frame call; found ${_count}")
    endif()
    if("${code}" MATCHES "draw_(menu_bar|[a-z_]+windows?)[ \t\r\n]*\\(")
        message(FATAL_ERROR "shared frame wiring: ${host_name} contains direct composition")
    endif()
    if("${code}" MATCHES "(poll_runtime_asset_changes|advance_timeline_playback|advance_parameter_state|finalize_orphaned_[a-z_]+|apply_pending_file_action)[ \t\r\n]*\\(")
        message(FATAL_ERROR "shared frame wiring: ${host_name} duplicates coordinator state updates")
    endif()
    string(FIND "${code}" "${frame_begin}" _begin)
    string(FIND "${code}" "draw_shell_frame" _draw)
    if(_begin EQUAL -1 OR _begin GREATER _draw)
        message(FATAL_ERROR "shared frame wiring: ${host_name} must begin ImGui before delegation")
    endif()
endfunction()

set(_editor "${CMAKE_CURRENT_LIST_DIR}/../src/editor")
_marrow_host_region(_app "${_editor}/shell_main.cpp"
    "ShellFrameOutcome render_shell_frame(" "sg_pass main_pass")
# Stop BEFORE assertions and specialized single-panel scenarios: a later lambda
# must not be able to supply a missing shared-frame call.
_marrow_host_region(_smoke "${_editor}/shell_smoke_frames.cpp"
    "bool render_headless_smoke_frames(" "if (!validated_dock_layout)")
_marrow_check_host("${_app}" "application" "simgui_new_frame")
_marrow_check_host("${_smoke}" "shared smoke" "ImGui::NewFrame")

_marrow_read_code(_frame "${_editor}/shell_frame.cpp")
if(NOT _frame MATCHES "void[ \t\r\n]+draw_shell_frame[ \t\r\n]*\\(" OR
   NOT _frame MATCHES "draw_[a-z_]+windows?[ \t\r\n]*\\(")
    message(FATAL_ERROR "shared frame wiring: coordinator has no window composition")
endif()
if(_frame MATCHES "(ImGui::(NewFrame|EndFrame|Render)|simgui_[a-z_]+|sg_(begin_pass|end_pass|commit)|SDL_[a-zA-Z_]+|drain_commands|acquire_frame_surface|present)[ \t\r\n]*\\(")
    message(FATAL_ERROR "shared frame wiring: coordinator owns host lifecycle work")
endif()

message(STATUS "shared frame wiring: both hosts delegate once; composition belongs to the coordinator")
