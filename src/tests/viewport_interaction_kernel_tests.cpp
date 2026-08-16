#include "viewport_interaction_kernel.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace kernel = marrow::editor::viewport_interaction_kernel;

namespace {

bool near(double left, double right, double epsilon = 1e-9) {
    return std::abs(left - right) <= epsilon;
}

bool check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool rotation_cases() {
    struct BasisCase {
        kernel::Matrix2 matrix;
        kernel::Point pointer;
        double expected_angle;
    };
    const std::vector<BasisCase> cases{
        {{1.0, 0.0, 0.0, 1.0}, {1.0, 0.0}, 0.0},
        {{-2.0, 0.0, 0.0, 3.0}, {-2.0, 0.0}, 0.0},
        {{0.0, 2.0, 3.0, 0.0}, {0.0, 3.0}, 0.0},
    };
    for (const BasisCase& test : cases) {
        const auto basis = kernel::make_rotation_basis({0.0, 0.0}, test.matrix);
        if (!check(basis.has_value(), "rotation basis rejected a valid reflection case") ||
            !check(
                near(*kernel::rotation_angle(*basis, test.pointer), test.expected_angle),
                "rotation basis produced the wrong local angle")) {
            return false;
        }
    }

    const auto frozen = kernel::make_rotation_basis(
        {4.0, -3.0}, {2.0, 0.5, -0.25, 3.0});
    if (!check(frozen.has_value(), "rotation basis was not created")) {
        return false;
    }
    const auto frozen_angle = kernel::rotation_angle(*frozen, {6.0, -3.25});
    const auto later_basis = kernel::make_rotation_basis(
        {4.0, -3.0}, {9.0, 8.0, 7.0, 6.0});
    if (!check(
            frozen_angle.has_value() && near(*frozen_angle, 0.0) &&
                later_basis.has_value() &&
                !near(*kernel::rotation_angle(*later_basis, {6.0, -3.25}), 0.0),
            "frozen rotation basis changed with a later parent basis")) {
        return false;
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    return check(
               !kernel::make_rotation_basis({0.0, 0.0}, {1.0, 2.0, 2.0, 4.0}),
               "singular rotation basis was accepted") &&
        check(
               !kernel::make_rotation_basis({nan, 0.0}, {}),
               "non-finite rotation basis was accepted") &&
        check(near(kernel::unwrap_rotation_delta(-180.0), 180.0),
              "negative 180-degree tie did not resolve positive") &&
        check(near(kernel::unwrap_rotation_delta(180.0), 180.0),
              "positive 180-degree tie changed sign");
}

bool rotation_drag_cases() {
    const auto run_turn = [](double sign) {
        kernel::RotationDragState state;
        state.previous_wrapped_angle = 0.0;
        const std::vector<double> samples = sign > 0.0
            ? std::vector<double>{90.0, 180.0, -90.0, 0.0, 90.0}
            : std::vector<double>{-90.0, -180.0, 90.0, 0.0, -90.0};
        for (double sample : samples) {
            const auto update = kernel::update_rotation_drag(&state, 100.0, sample);
            if (update.result != kernel::RotationUpdateResult::Changed) {
                return std::optional<double>{};
            }
        }
        return std::optional<double>{state.accumulated_rotation};
    };
    const auto positive = run_turn(1.0);
    const auto negative = run_turn(-1.0);
    if (!check(positive.has_value() && near(*positive, 450.0),
               "positive multi-turn rotation did not preserve +450 degrees") ||
        !check(negative.has_value() && near(*negative, -450.0),
               "negative multi-turn rotation did not preserve -450 degrees")) {
        return false;
    }

    kernel::RotationDragState rebased;
    rebased.previous_wrapped_angle = 30.0;
    const auto suspended = kernel::update_rotation_drag(&rebased, 4.0, std::nullopt);
    const auto resumed = kernel::update_rotation_drag(&rebased, 4.0001, -140.0);
    const auto advanced = kernel::update_rotation_drag(&rebased, 9.0, -130.0);
    return check(
               suspended.result == kernel::RotationUpdateResult::Suspended,
               "2px pivot did not suspend angular tracking") &&
        check(
               resumed.result == kernel::RotationUpdateResult::Rebased &&
                   near(rebased.accumulated_rotation, 10.0),
               "pivot exit did not rebase without adding an angle") &&
        check(
               advanced.result == kernel::RotationUpdateResult::Changed,
               "rotation did not resume after pivot rebase") &&
        check(
               kernel::update_rotation_drag(
                   &rebased,
                   std::numeric_limits<double>::infinity(),
                   0.0).result == kernel::RotationUpdateResult::Invalid,
               "non-finite rotation input did not fail closed");
}

bool scale_cases() {
    struct BasisCase {
        kernel::Matrix2 matrix;
        bool valid;
    };
    const std::vector<BasisCase> cases{
        {{1.0, 0.0, 0.0, 1.0}, true},
        {{-2.0, 0.0, 0.0, 3.0}, true},
        {{0.0, 2.0, 3.0, 0.0}, true},
        {{1.0, 2.0, 2.0, 4.0}, false},
    };
    for (const BasisCase& test : cases) {
        if (!check(
                kernel::make_scale_basis(
                    {0.0, 0.0}, test.matrix, 17.0, 5.0, -3.0).has_value() ==
                    test.valid,
                "scale basis validity did not match its determinant")) {
            return false;
        }
    }

    kernel::ScaleMapping mapping;
    mapping.pivot_screen = {0.0, 0.0};
    mapping.direction = {1.0, 0.0};
    mapping.start_projection_pixels = 74.0;
    mapping.start_scale_x = 2.0;
    mapping.start_scale_y = -3.0;
    mapping.handle = kernel::ScaleHandle::X;
    const auto sign_cross = kernel::map_scale(mapping, {-37.0, 0.0});
    if (!check(
            sign_cross.has_value() && near(sign_cross->scale_x, -1.0) &&
                near(sign_cross->scale_y, -3.0),
            "signed axis scale did not cross zero")) {
        return false;
    }

    mapping.start_scale_x = 0.0;
    const auto zero_start = kernel::map_scale(mapping, {148.0, 0.0});
    if (!check(
            zero_start.has_value() && near(zero_start->scale_x, 1.0),
            "exact-zero scale did not recover at one unit per 74px")) {
        return false;
    }

    mapping.handle = kernel::ScaleHandle::Uniform;
    mapping.start_scale_x = 2.0;
    mapping.start_scale_y = -3.0;
    const auto uniform = kernel::map_scale(mapping, {-74.0, 0.0});
    if (!check(
            uniform.has_value() && near(uniform->scale_x, -2.0) &&
                near(uniform->scale_y, 3.0),
            "uniform scale did not preserve the signed X:Y ratio")) {
        return false;
    }
    mapping.start_scale_x = 0.0;
    mapping.start_scale_y = 0.0;
    return check(
        !kernel::map_scale(mapping, {74.0, 0.0}),
        "uniform mapping accepted a zero-zero starting scale");
}

bool arbitration_and_completion_cases() {
    struct PressCase {
        bool active;
        bool brush;
        bool translate;
        bool rotation;
        bool scale;
        bool entity;
        kernel::PressTarget expected;
    };
    const std::vector<PressCase> press_cases{
        {true, true, true, true, true, true, kernel::PressTarget::ActiveGesture},
        {false, true, true, true, true, true, kernel::PressTarget::WeightBrush},
        {false, false, true, true, true, true, kernel::PressTarget::Translate},
        {false, false, false, true, true, true, kernel::PressTarget::Rotation},
        {false, false, false, false, true, true, kernel::PressTarget::Scale},
        {false, false, false, false, false, true, kernel::PressTarget::Entity},
        {false, false, false, false, false, false, kernel::PressTarget::Box},
    };
    for (const PressCase& test : press_cases) {
        if (!check(
                kernel::resolve_press_target(
                    test.active,
                    test.brush,
                    test.translate,
                    test.rotation,
                    test.scale,
                    test.entity) == test.expected,
                "viewport press precedence changed")) {
            return false;
        }
    }

    const std::vector<kernel::RankedHit> candidates{
        {0U, kernel::HitPriority::AttachmentSurface, 0.0, 0U},
        {1U, kernel::HitPriority::BoneJoint, 9.0, 9U},
        {2U, kernel::HitPriority::BoneJoint, 4.0, 8U},
        {3U, kernel::HitPriority::BoneJoint, 4.0, 2U},
        {4U, kernel::HitPriority::ConstraintTarget, 100.0, 10U},
    };
    if (!check(
            kernel::resolve_hit_index(candidates) == std::optional<std::size_t>(4U),
            "hit category did not win before distance and stable order")) {
        return false;
    }
    const std::vector<kernel::RankedHit> same_category{
        {0U, kernel::HitPriority::BoneJoint, 4.0, 8U},
        {1U, kernel::HitPriority::BoneJoint, 4.0, 2U},
    };
    if (!check(
            kernel::resolve_hit_index(same_category) ==
                std::optional<std::size_t>(1U),
            "hit stable order did not break an equal-distance tie")) {
        return false;
    }

    struct CompletionCase {
        bool commit;
        bool changed;
        bool context_valid;
        kernel::CompletionAction action;
        bool report_cancelled;
        std::size_t history_entries;
    };
    const std::vector<CompletionCase> completion_cases{
        {true, true, true, kernel::CompletionAction::Commit, false, 1U},
        {true, false, true, kernel::CompletionAction::Cancel, false, 0U},
        {true, true, false, kernel::CompletionAction::Cancel, true, 0U},
        {false, true, true, kernel::CompletionAction::Cancel, true, 0U},
    };
    for (const CompletionCase& test : completion_cases) {
        const auto decision = kernel::completion_decision(
            test.commit, test.changed, test.context_valid);
        if (!check(
                decision.action == test.action &&
                    decision.report_cancelled == test.report_cancelled &&
                    decision.history_entries == test.history_entries,
                "gesture completion did not preserve no-op/one-undo semantics")) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    if (!rotation_cases() || !rotation_drag_cases() || !scale_cases() ||
        !arbitration_and_completion_cases()) {
        return 1;
    }
    std::cout << "Viewport interaction kernel tests passed.\n";
    return 0;
}
