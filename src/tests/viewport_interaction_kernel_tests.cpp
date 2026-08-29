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

bool snap_cases() {
    const auto snapped = [](double value,
                            double step,
                            bool configured_enabled,
                            bool temporarily_enabled,
                            bool bypass) {
        return kernel::snap_scalar({
            value,
            step,
            {configured_enabled, temporarily_enabled, bypass}});
    };

    const auto disabled = snapped(12.4, 10.0, false, false, false);
    const auto configured = snapped(17.6, 10.0, true, false, false);
    const auto temporary = snapped(12.4, 10.0, false, true, false);
    const auto bypassed = snapped(17.6, 10.0, true, true, true);
    if (!check(
            disabled.has_value() && near(*disabled, 12.4),
            "disabled snap changed the scalar") ||
        !check(
            configured.has_value() && near(*configured, 20.0),
            "configured snap did not quantize to the nearest grid line") ||
        !check(
            temporary.has_value() && near(*temporary, 10.0),
            "temporary modifier did not enable snapping") ||
        !check(
            bypassed.has_value() && near(*bypassed, 17.6),
            "Alt bypass did not win over configured and temporary enablement")) {
        return false;
    }

    const auto positive_half = snapped(5.0, 10.0, true, false, false);
    const auto negative_half = snapped(-5.0, 10.0, true, false, false);
    const auto multi_turn = snapped(377.0, 15.0, true, false, false);
    const auto exact_zero = snapped(-0.049, 0.1, true, false, false);
    if (!check(
            positive_half.has_value() && near(*positive_half, 10.0),
            "positive half-step did not round away from zero") ||
        !check(
            negative_half.has_value() && near(*negative_half, -10.0),
            "negative half-step did not round away from zero") ||
        !check(
            multi_turn.has_value() && near(*multi_turn, 375.0),
            "multi-turn absolute angle was normalized before snapping") ||
        !check(
            exact_zero.has_value() && *exact_zero == 0.0 &&
                !std::signbit(*exact_zero),
            "snap did not canonicalize exact zero")) {
        return false;
    }

    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return check(
               !snapped(1.0, 0.0, true, false, false),
               "snap accepted a zero step") &&
        check(
               !snapped(1.0, -1.0, false, false, false),
               "disabled snap accepted a negative step") &&
        check(
               !snapped(infinity, 1.0, true, false, false),
               "snap accepted a non-finite value") &&
        check(
               !snapped(1.0, nan, true, false, false),
               "snap accepted a non-finite step") &&
        check(
               !snapped(1e308, 1e-308, true, false, false),
               "snap accepted an overflowing quotient");
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
    if (!check(
            !kernel::map_scale(mapping, {74.0, 0.0}),
            "uniform mapping accepted a zero-zero starting scale")) {
        return false;
    }

    const kernel::SnapActivation enabled{true, false, false};
    const auto snapped_x = kernel::snap_scale_candidate(
        {-0.04, -3.0}, kernel::ScaleHandle::X, 0.1, enabled);
    const auto snapped_signed_x = kernel::snap_scale_candidate(
        {-0.16, -3.0}, kernel::ScaleHandle::X, 0.1, enabled);
    const auto snapped_uniform = kernel::snap_scale_candidate(
        {-0.26, 0.13}, kernel::ScaleHandle::Uniform, 0.1, enabled);
    const auto snapped_uniform_y = kernel::snap_scale_candidate(
        {0.0, -0.26}, kernel::ScaleHandle::Uniform, 0.1, enabled);
    const auto snapped_uniform_zero = kernel::snap_scale_candidate(
        {0.0, 0.0}, kernel::ScaleHandle::Uniform, 0.1, enabled);
    return check(
               snapped_x.has_value() && snapped_x->scale_x == 0.0 &&
                   !std::signbit(snapped_x->scale_x) &&
                   near(snapped_x->scale_y, -3.0),
               "axis scale snap did not permit canonical exact zero") &&
        check(
               snapped_signed_x.has_value() && near(snapped_signed_x->scale_x, -0.2) &&
                   near(snapped_signed_x->scale_y, -3.0),
               "axis scale snap did not preserve signed absolute scale") &&
        check(
               snapped_uniform.has_value() && near(snapped_uniform->scale_x, -0.3) &&
                   near(snapped_uniform->scale_y, 0.15),
               "uniform scale snap did not preserve the X-driven signed ratio") &&
        check(
               snapped_uniform_y.has_value() && snapped_uniform_y->scale_x == 0.0 &&
                   near(snapped_uniform_y->scale_y, -0.3),
               "uniform scale snap did not fall back to a nonzero Y driver") &&
        check(
               snapped_uniform_zero.has_value() &&
                   snapped_uniform_zero->scale_x == 0.0 &&
                   snapped_uniform_zero->scale_y == 0.0,
               "uniform scale snap rejected the exact-zero result");
}

bool grid_cases() {
    const auto near_grid = kernel::visible_grid_step(10.0, 2.0, 18.0);
    const auto medium_grid = kernel::visible_grid_step(10.0, 1.0, 18.0);
    const auto far_grid = kernel::visible_grid_step(10.0, 0.26, 18.0);
    return check(
               near_grid.has_value() && near(*near_grid, 10.0),
               "visible grid skipped the base world step unnecessarily") &&
        check(
               medium_grid.has_value() && near(*medium_grid, 20.0),
               "visible grid did not choose an integer multiple") &&
        check(
               far_grid.has_value() && near(*far_grid, 70.0),
               "zoomed-out grid was not an integer multiple of the snap grid") &&
        check(
               !kernel::visible_grid_step(0.0, 1.0, 18.0),
               "visible grid accepted a zero world step") &&
        check(
               !kernel::visible_grid_step(10.0, 0.0, 18.0),
               "visible grid accepted zero pixels per unit");
}

bool ffd_cases() {
    const std::vector<kernel::Point> handles{
        {10.0, 10.0},
        {14.0, 10.0},
        {14.0, 10.0},
        {std::numeric_limits<double>::quiet_NaN(), 10.0}};
    if (!check(
            kernel::nearest_ffd_vertex(handles, {12.0, 10.0}) ==
                std::optional<std::size_t>(0U),
            "FFD hit did not break an equal-distance tie by lower vertex index") ||
        !check(
            kernel::nearest_ffd_vertex(handles, {19.9, 10.0}) ==
                std::optional<std::size_t>(1U),
            "FFD hit did not accept the nearest vertex inside 6px") ||
        !check(
            !kernel::nearest_ffd_vertex(handles, {20.1, 10.0}).has_value(),
            "FFD hit accepted a vertex outside 6px")) {
        return false;
    }

    const std::vector<std::size_t> selected{1U, 3U};
    const auto replaced = kernel::update_ffd_point_selection(
        selected, 4U, 2U, kernel::FfdPointSelectionMode::Replace);
    const auto toggled_in = kernel::update_ffd_point_selection(
        selected, 4U, 2U, kernel::FfdPointSelectionMode::Toggle);
    const auto toggled_out = kernel::update_ffd_point_selection(
        selected, 4U, 1U, kernel::FfdPointSelectionMode::Toggle);
    if (!check(
            replaced == std::optional<std::vector<std::size_t>>(
                std::vector<std::size_t>{2U}),
            "FFD point replace did not produce one stable vertex") ||
        !check(
            toggled_in ==
                std::optional<std::vector<std::size_t>>(
                    std::vector<std::size_t>{1U, 2U, 3U}),
            "FFD point toggle did not insert in sorted order") ||
        !check(
            toggled_out ==
                std::optional<std::vector<std::size_t>>(
                    std::vector<std::size_t>{3U}),
            "FFD point toggle did not remove the selected vertex") ||
        !check(
            !kernel::update_ffd_point_selection(
                {1U, 1U}, 4U, 2U, kernel::FfdPointSelectionMode::Toggle),
            "FFD point selection accepted duplicate input") ||
        !check(
            !kernel::update_ffd_point_selection(
                selected, 4U, 4U, kernel::FfdPointSelectionMode::Replace),
            "FFD point selection accepted an out-of-range vertex")) {
        return false;
    }

    const std::vector<kernel::Point> box_handles{
        {0.0, 0.0}, {10.0, 10.0}, {20.0, 20.0}, {10.0, 20.0}};
    const auto forward_box = kernel::collect_ffd_vertices_in_box(
        box_handles, {10.0, 10.0}, {20.0, 20.0});
    const auto reverse_box = kernel::collect_ffd_vertices_in_box(
        box_handles, {20.0, 20.0}, {10.0, 10.0});
    const auto plain_empty = kernel::update_ffd_box_selection(
        selected, box_handles, {30.0, 30.0}, {40.0, 40.0}, false);
    const auto additive_empty = kernel::update_ffd_box_selection(
        selected, box_handles, {30.0, 30.0}, {40.0, 40.0}, true);
    const auto additive_box = kernel::update_ffd_box_selection(
        {0U, 2U}, box_handles, {10.0, 10.0}, {20.0, 20.0}, true);
    if (!check(
            forward_box ==
                std::optional<std::vector<std::size_t>>(
                    std::vector<std::size_t>{1U, 2U, 3U}) &&
                reverse_box == forward_box,
            "FFD box collection was not forward/reverse inclusive and sorted") ||
        !check(
            plain_empty == std::optional<std::vector<std::size_t>>(
                std::vector<std::size_t>{}),
            "plain empty FFD box did not clear selection") ||
        !check(
            additive_empty ==
                std::optional<std::vector<std::size_t>>(selected),
            "additive empty FFD box changed selection") ||
        !check(
            additive_box ==
                std::optional<std::vector<std::size_t>>(
                    std::vector<std::size_t>{0U, 1U, 2U, 3U}),
            "additive FFD box did not merge into sorted unique order") ||
        !check(
            !kernel::collect_ffd_vertices_in_box(
                {{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 1.0}},
                {},
                {2.0, 2.0}),
            "FFD box collection accepted a non-finite handle")) {
        return false;
    }

    const auto one = kernel::make_ffd_inverse({{{2.0, 0.0, 0.0, 4.0}, 1.0}});
    const auto reflected = kernel::make_ffd_inverse({{{-2.0, 0.0, 0.0, 3.0}, 1.0}});
    const auto weighted = kernel::make_ffd_inverse({
        {{2.0, 0.0, 0.0, 1.0}, 0.25},
        {{4.0, 0.0, 0.0, 3.0}, 0.75}});
    const auto one_delta = one.has_value()
        ? kernel::map_ffd_delta(*one, {4.0, 8.0})
        : std::nullopt;
    const auto reflected_delta = reflected.has_value()
        ? kernel::map_ffd_delta(*reflected, {-4.0, 6.0})
        : std::nullopt;
    const auto weighted_delta = weighted.has_value()
        ? kernel::map_ffd_delta(*weighted, {7.0, 5.0})
        : std::nullopt;
    if (!check(
            one_delta.has_value() && near(one_delta->x, 2.0) && near(one_delta->y, 2.0),
            "single-influence FFD inverse mapping failed") ||
        !check(
            reflected_delta.has_value() && near(reflected_delta->x, 2.0) &&
                near(reflected_delta->y, 2.0),
            "reflected FFD inverse mapping failed") ||
        !check(
            weighted_delta.has_value() && near(weighted_delta->x, 2.0) &&
                near(weighted_delta->y, 2.0),
            "weighted non-uniform FFD inverse mapping failed")) {
        return false;
    }

    const std::vector<double> start{0.0, 1.0, 2.0, 3.0, -4.0, 5.0};
    const auto updated = kernel::update_ffd_vertex_offsets(start, 1U, {7.0, -11.0});
    const auto group_updated = kernel::update_ffd_vertex_offsets(
        start,
        std::vector<kernel::FfdVertexDelta>{
            {2U, {1.0, -2.0}},
            {0U, {3.0, 4.0}}});
    if (!check(
            updated.has_value() && (*updated)[0] == start[0] &&
                (*updated)[1] == start[1] && (*updated)[2] == 9.0 &&
                (*updated)[3] == -8.0 && (*updated)[4] == start[4] &&
                (*updated)[5] == start[5],
            "FFD full-vector update changed an untouched component") ||
        !check(
            group_updated.has_value() &&
                *group_updated == std::vector<double>({3.0, 5.0, 2.0, 3.0, -3.0, 3.0}),
            "FFD group update did not atomically update selected pairs")) {
        return false;
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    return check(
               !kernel::make_ffd_inverse({{{1.0, 2.0, 2.0, 4.0}, 1.0}}),
               "singular FFD matrix was accepted") &&
        check(!kernel::make_ffd_inverse({}), "empty FFD influences were accepted") &&
        check(
               !kernel::make_ffd_inverse({{{}, 0.5}}),
               "non-normalized FFD influences were accepted") &&
        check(
               !kernel::make_ffd_inverse({{{1.0, 0.0, 0.0, nan}, 1.0}}),
               "non-finite FFD influence was accepted") &&
        check(
               !kernel::update_ffd_vertex_offsets({0.0, 0.0, nan, 0.0}, 0U, {}),
               "non-finite untouched FFD component was accepted") &&
        check(
               !kernel::update_ffd_vertex_offsets({0.0, 0.0}, 1U, {}),
               "out-of-range FFD vertex update was accepted") &&
        check(
               !kernel::update_ffd_vertex_offsets(
                   start,
                   std::vector<kernel::FfdVertexDelta>{
                       {0U, {1.0, 1.0}}, {0U, {2.0, 2.0}}}),
               "duplicate FFD group update was accepted") &&
        check(
               !kernel::update_ffd_vertex_offsets(
                   start,
                   std::vector<kernel::FfdVertexDelta>{{3U, {1.0, 1.0}}}),
               "out-of-range FFD group update was accepted") &&
        check(
               !kernel::update_ffd_vertex_offsets(
                   start,
                   std::vector<kernel::FfdVertexDelta>{{0U, {nan, 1.0}}}),
               "non-finite FFD group update was accepted");
}

bool arbitration_and_completion_cases() {
    struct PressCase {
        bool active;
        bool brush;
        bool translate;
        bool rotation;
        bool scale;
        bool ffd_vertex;
        bool entity;
        kernel::PressTarget expected;
    };
    const std::vector<PressCase> press_cases{
        {true, true, true, true, true, true, true, kernel::PressTarget::ActiveGesture},
        {false, true, true, true, true, true, true, kernel::PressTarget::WeightBrush},
        {false, false, true, true, true, true, true, kernel::PressTarget::Translate},
        {false, false, false, true, true, true, true, kernel::PressTarget::Rotation},
        {false, false, false, false, true, true, true, kernel::PressTarget::Scale},
        {false, false, false, false, false, true, true, kernel::PressTarget::FfdVertex},
        {false, false, false, false, false, false, true, kernel::PressTarget::Entity},
        {false, false, false, false, false, false, false, kernel::PressTarget::Box},
    };
    for (const PressCase& test : press_cases) {
        if (!check(
                kernel::resolve_press_target(
                    test.active,
                    test.brush,
                    test.translate,
                    test.rotation,
                    test.scale,
                    test.ffd_vertex,
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
    if (!snap_cases() || !rotation_cases() || !rotation_drag_cases() || !scale_cases() ||
        !grid_cases() ||
        !ffd_cases() || !arbitration_and_completion_cases()) {
        return 1;
    }
    std::cout << "Viewport interaction kernel tests passed.\n";
    return 0;
}
