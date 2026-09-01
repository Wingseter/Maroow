# Verifies that the editor shell's TWO hand-duplicated frame bodies draw the
# same set of windows (MAR-187).
#
# WHY THIS EXISTS AS A SCRIPT AND NOT AS PROSE IN A PLAN
#
# `render_shell_frame` (src/editor/shell_main.cpp) and
# `render_headless_smoke_frames` (src/editor/shell_smoke_frames.cpp) each carry a
# hand-written list of `draw_*_window` calls. Nothing compiler-checks the pair.
# A window added to only one of them is silently broken in a direction that is
# invisible to the whole test suite:
#
#   * missing from the APPLICATION body  -> the window ships in no application,
#     and every headless case still passes;
#   * missing from the SMOKE body        -> the shared smoke frame never renders
#     it, and any future scenario relying on it is blind.
#
# MAR-187 measured BOTH halves by deleting each call in turn: the shell smoke
# stayed green each time, INCLUDING its own actual-frame case. That case draws
# through a scenario-local lambda of its own, so it backs neither body. This
# script is therefore the only mechanism that can catch either omission, which
# is why it runs on every build of `marrow_editor_shell` rather than living in a
# document as a command somebody might run.
#
# It deliberately checks ALL windows, not just the one MAR-187 added: the next
# story to add a window inherits the guard without having to notice it exists.

cmake_minimum_required(VERSION 3.16)

set(_app_file "${CMAKE_CURRENT_LIST_DIR}/../src/editor/shell_main.cpp")
set(_smoke_file "${CMAKE_CURRENT_LIST_DIR}/../src/editor/shell_smoke_frames.cpp")

# Windows the application draws that the headless smoke deliberately does not.
# `draw_agent_window` sits behind `show_agent_panel`, which the smoke never
# enables. Add to this list only with a reason, because every entry is a window
# the smoke can no longer see.
set(_allowed_app_only "draw_agent_window")

# Extracts the run of `draw_*_window(` calls between two anchors.
function(_marrow_extract_draw_calls out_var file_path begin_anchor end_anchor)
    if(NOT EXISTS "${file_path}")
        message(FATAL_ERROR "MAR-187 frame-body check: ${file_path} does not exist.")
    endif()
    file(READ "${file_path}" _text)
    string(FIND "${_text}" "${begin_anchor}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR
            "MAR-187 frame-body check: could not find '${begin_anchor}' in "
            "${file_path}. The anchor moved; fix this script rather than "
            "deleting the check.")
    endif()
    string(SUBSTRING "${_text}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end_anchor}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR
            "MAR-187 frame-body check: could not find '${end_anchor}' after "
            "'${begin_anchor}' in ${file_path}. The anchor moved; fix this "
            "script rather than deleting the check.")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _region)
    string(REGEX MATCHALL "draw_[a-z_]+windows?\\(" _raw "${_region}")
    set(_names "")
    foreach(_call IN LISTS _raw)
        string(REPLACE "(" "" _call "${_call}")
        list(APPEND _names "${_call}")
    endforeach()
    list(REMOVE_DUPLICATES _names)
    list(SORT _names)
    set(${out_var} "${_names}" PARENT_SCOPE)
endfunction()

# The application's draw list: from the frame function to the first
# gesture-finalisation call, which is what follows the list.
_marrow_extract_draw_calls(_app_draws "${_app_file}"
    "ShellFrameOutcome render_shell_frame("
    "finalize_orphaned_inspector_transform_gesture")

# The smoke's SHARED draw list only. The end anchor is the dock-layout
# validation block that immediately follows it -- this deliberately stops long
# before the per-scenario render lambdas further down the same function, which
# must NOT be able to satisfy this check.
_marrow_extract_draw_calls(_smoke_draws "${_smoke_file}"
    "bool render_headless_smoke_frames("
    "if (!validated_dock_layout)")

if(_app_draws STREQUAL "")
    message(FATAL_ERROR
        "MAR-187 frame-body check: found NO draw_*_window calls in the "
        "application frame body. The check cannot pass vacuously.")
endif()
if(_smoke_draws STREQUAL "")
    message(FATAL_ERROR
        "MAR-187 frame-body check: found NO draw_*_window calls in the smoke's "
        "shared draw list. The check cannot pass vacuously.")
endif()

set(_missing_from_smoke "")
foreach(_call IN LISTS _app_draws)
    if(NOT _call IN_LIST _smoke_draws AND NOT _call IN_LIST _allowed_app_only)
        list(APPEND _missing_from_smoke "${_call}")
    endif()
endforeach()

set(_missing_from_app "")
foreach(_call IN LISTS _smoke_draws)
    if(NOT _call IN_LIST _app_draws)
        list(APPEND _missing_from_app "${_call}")
    endif()
endforeach()

if(NOT _missing_from_smoke STREQUAL "" OR NOT _missing_from_app STREQUAL "")
    message("MAR-187 frame-body check FAILED.")
    message("  application body (shell_main.cpp)        : ${_app_draws}")
    message("  smoke shared list (shell_smoke_frames.cpp): ${_smoke_draws}")
    if(NOT _missing_from_app STREQUAL "")
        message("")
        message("  DRAWN ONLY IN THE SMOKE, so it SHIPS IN NO APPLICATION:")
        message("    ${_missing_from_app}")
    endif()
    if(NOT _missing_from_smoke STREQUAL "")
        message("")
        message("  DRAWN ONLY IN THE APPLICATION, so the shared smoke frame")
        message("  never renders it and no headless scenario can see it:")
        message("    ${_missing_from_smoke}")
    endif()
    message("")
    message("  Add the call to BOTH frame bodies. If a window is deliberately")
    message("  application-only, add it to _allowed_app_only in this script")
    message("  WITH A REASON. Note that the shell smoke passes green with")
    message("  either call missing -- including its actual-frame case, which")
    message("  draws through a lambda of its own -- so this script is the only")
    message("  thing that can tell you.")
    message(FATAL_ERROR "MAR-187: the two frame bodies disagree.")
endif()

message(STATUS
    "MAR-187 frame-body check: both frame bodies draw the same windows "
    "(${_app_draws}).")
