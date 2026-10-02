#pragma once

#include <filesystem>

#include "shell_state.hpp"

/**
 * @file
 * @brief MAR-190. The PSD reimport review modal, and nothing else.
 *
 * This file draws widgets and calls model functions. It owns no logic: Task 9
 * greps it for `begin_edit`, `transaction`, `std::sort` and `.erase(` and expects
 * none, and for `BeginChild`, `BeginTable`, `BeginTabItem` and `PushID` and
 * expects none of those either. The second grep is a TESTABILITY constraint
 * promoted to a rule -- `window->GetID(label)` is the wrong seed if anything
 * pushed an id, and a sweep seeded that way reports a widget that is drawn and
 * hoverable the whole time as *absent*, which sends the next person looking in
 * the wrong place.
 */

namespace marrow::editor::shell {

/**
 * @brief Opens a review over @p plan and shows the modal on the next frame.
 *
 * @param state    Shell state whose `psd_reimport` panel is engaged.
 * @param plan     The plan the user is about to review.
 * @param staging  Where @p plan staged; removed when the review ends.
 * @param source   Absolute path of the reviewed PSD.
 */
/**
 * @brief Width reserved to the right of a row label, for its checkbox.
 *
 * Declared here so F2 asserts the located checkbox against THE SAME number the
 * drawing code uses. A literal repeated in the case and the widget is two numbers
 * that agree until somebody changes one, and the case would then be measuring its
 * own copy.
 */
constexpr float kPsdReviewControlColumn = 90.0f;

/**
 * @brief A staging root no other process can collide with.
 *
 * `plan_psd_reimport` REFUSES a non-empty staging root, and the planner's own
 * `unique_staging_directory` numbers from a `static` counter that restarts at 1
 * in every process. Two editors each opening a reimport under one fixed root
 * therefore both pick `<root>/plan-1`, and the second user is refused for a
 * reason they cannot act on. **A "unique" name is unique only across the scope
 * its mechanism spans**, so the pid is part of the name.
 *
 * Declared here rather than duplicated: the opening path and the confirm path
 * both need one, and two spellings of "unique" are two things to keep in
 * agreement.
 *
 * @return An absolute, per-process staging root under the temp directory.
 */
std::filesystem::path psd_reimport_staging_root();

void begin_psd_reimport_review(
    ShellState* state,
    const PsdReimportPlan& plan,
    const std::filesystem::path& staging,
    const std::filesystem::path& source);

/**
 * @brief Discards the review and removes its staging tree.
 *
 * @param state   Shell state whose `psd_reimport` panel is disengaged.
 * @param outcome What to record in the panel's `last_outcome`.
 */
void close_psd_reimport_review(ShellState* state, PsdReviewOutcome outcome);

/**
 * @brief Draws the modal. Called from exactly ONE place.
 *
 * That single call site is `draw_project_window`, which both frame bodies
 * already call unconditionally, so there is no duplicated list for a gate to
 * guard -- and `CheckFrameBodies.cmake` could not guard it anyway, because this
 * function's name does not match its `draw_[a-z_]+windows?\(` regex. Task 9's
 * single-call-site grep is this story's real structural detector.
 *
 * That grep counts CALL SITES, so this comment deliberately does not spell the
 * function's name followed by an open parenthesis -- the same reason S5's
 * comment does not name the widget library. A gate you have to remember to
 * subtract prose from is a gate that drifts.
 *
 * @param state Shell state to read and update.
 */
void draw_psd_reimport_modal(ShellState* state);

} // namespace marrow::editor::shell
