#include "shell_weight_paint.hpp"

#include "shell_preview.hpp"

#include <algorithm>
#include <functional>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mesh_weight_model.hpp"

#include "marrow/editor/authoring.hpp"
#include "shell_selection.hpp"
#include "windowing.hpp"

namespace marrow::editor::shell {

constexpr double kWeightEpsilon = 1e-6;

const char* weight_paint_mode_name(WeightPaintMode mode) {
    switch (mode) {
    case WeightPaintMode::Paint:
        return "Paint";
    case WeightPaintMode::Erase:
        return "Erase";
    case WeightPaintMode::Smooth:
        return "Smooth";
    case WeightPaintMode::Replace:
        return "Replace";
    }

    return "Paint";
}

ImVec4 interpolate_color(const ImVec4& start, const ImVec4& end, float alpha) {
    return ImVec4(
        start.x + ((end.x - start.x) * alpha),
        start.y + ((end.y - start.y) * alpha),
        start.z + ((end.z - start.z) * alpha),
        start.w + ((end.w - start.w) * alpha));
}

ImVec4 mesh_weight_heatmap_color(double weight, float alpha) {
    const float t = std::clamp(static_cast<float>(weight), 0.0f, 1.0f);
    const ImVec4 blue(0.12f, 0.33f, 0.95f, alpha);
    const ImVec4 green(0.13f, 0.74f, 0.39f, alpha);
    const ImVec4 yellow(0.96f, 0.83f, 0.24f, alpha);
    const ImVec4 red(0.91f, 0.28f, 0.17f, alpha);

    if (t <= (1.0f / 3.0f)) {
        return interpolate_color(blue, green, t * 3.0f);
    }
    if (t <= (2.0f / 3.0f)) {
        return interpolate_color(green, yellow, (t - (1.0f / 3.0f)) * 3.0f);
    }
    return interpolate_color(yellow, red, (t - (2.0f / 3.0f)) * 3.0f);
}

double weight_for_bone(
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights,
    std::size_t bone_index) {
    for (const auto& influence : vertex_weights.influences) {
        if (influence.bone_index == bone_index) {
            return influence.weight;
        }
    }
    return 0.0;
}

std::vector<std::vector<std::size_t>> build_mesh_vertex_neighbors(
    const std::vector<std::size_t>& triangles,
    std::size_t vertex_count) {
    std::vector<std::vector<std::size_t>> neighbors(vertex_count);
    for (std::size_t index = 0; index + 2U < triangles.size(); index += 3U) {
        const std::size_t a = triangles[index];
        const std::size_t b = triangles[index + 1U];
        const std::size_t c = triangles[index + 2U];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
            continue;
        }

        const auto connect = [&](std::size_t from, std::size_t to) {
            auto& entries = neighbors[from];
            if (std::find(entries.begin(), entries.end(), to) == entries.end()) {
                entries.push_back(to);
            }
        };
        connect(a, b);
        connect(a, c);
        connect(b, a);
        connect(b, c);
        connect(c, a);
        connect(c, b);
    }

    return neighbors;
}

bool mesh_weight_vertex_equal(
    const marrow::editor::MeshWeightVertexEdit& left,
    const marrow::editor::MeshWeightVertexEdit& right,
    double tolerance = 1e-6) {
    if (left.influences.size() != right.influences.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.influences.size(); ++index) {
        const auto& lhs = left.influences[index];
        const auto& rhs = right.influences[index];
        if (lhs.bone_name != rhs.bone_name ||
            std::abs(lhs.x - rhs.x) > tolerance ||
            std::abs(lhs.y - rhs.y) > tolerance ||
            std::abs(lhs.weight - rhs.weight) > tolerance) {
            return false;
        }
    }

    return true;
}

void store_mesh_weight_attachment_edit(
    marrow::editor::ProjectData* project,
    marrow::editor::MeshWeightAttachmentEdit edit) {
    if (project == nullptr) {
        return;
    }

    marrow::editor::MeshWeightAttachmentEdit* existing =
        project->find_mesh_weight_attachment_edit(
            edit.skin_name,
            edit.slot_name,
            edit.attachment_name);
    if (existing != nullptr) {
        *existing = std::move(edit);
    } else {
        project->mesh_weight_attachment_edits.push_back(std::move(edit));
    }
}

WeightPaintSelectionContext resolve_weight_paint_selection_context(
    const ShellState& state) {
    WeightPaintSelectionContext context;
    if (!state.load_result) {
        return context;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const ResolvedSelection active = resolve_shell_selection(state);
    if (active.active_attachment.has_value()) {
        context.target_slot_index = active.active_attachment->slot_index;
        context.target_attachment = active.active_attachment;
    } else if (active.active_slot_index.has_value()) {
        context.target_slot_index = active.active_slot_index;
    } else {
        for (auto iterator = state.selection.items().rbegin();
             iterator != state.selection.items().rend();
             ++iterator) {
            const auto* attachment =
                std::get_if<marrow::editor::AttachmentSelection>(&*iterator);
            if (attachment == nullptr) {
                continue;
            }
            const auto slot_index = skeleton.find_slot_index(attachment->slot_name);
            const auto skin_index = skeleton.find_skin_index(attachment->skin_name);
            if (!slot_index.has_value() || !skin_index.has_value() ||
                skeleton.find_attachment(
                    *skin_index,
                    *slot_index,
                    attachment->attachment_name) == nullptr) {
                continue;
            }
            context.target_slot_index = slot_index;
            context.target_attachment = PreviewAttachmentSelection{
                *slot_index,
                *skin_index,
                attachment->attachment_name};
            break;
        }
        if (!context.target_slot_index.has_value()) {
            for (auto iterator = state.selection.items().rbegin();
                 iterator != state.selection.items().rend();
                 ++iterator) {
                const auto* slot = std::get_if<marrow::editor::SlotSelection>(&*iterator);
                if (slot == nullptr) {
                    continue;
                }
                const auto slot_index = skeleton.find_slot_index(slot->slot_name);
                if (slot_index.has_value()) {
                    context.target_slot_index = slot_index;
                    break;
                }
            }
        }
    }

    if (active.active_bone_index.has_value()) {
        context.influence_bone_index = active.active_bone_index;
    } else if (active.active_slot_index.has_value()) {
        // Paint readiness must not depend on click order: a selected bone
        // supplies the influence even while the slot/attachment is the
        // active item. Only a sole slot/attachment selection falls back to
        // its owning bone.
        for (auto iterator = state.selection.items().rbegin();
             iterator != state.selection.items().rend();
             ++iterator) {
            const auto* bone =
                std::get_if<marrow::editor::BoneSelection>(&*iterator);
            if (bone == nullptr) {
                continue;
            }
            const auto bone_index = skeleton.find_bone_index(bone->bone_name);
            if (bone_index.has_value()) {
                context.influence_bone_index = bone_index;
                break;
            }
        }
        if (!context.influence_bone_index.has_value() &&
            state.selection.items().size() == 1U) {
            context.influence_bone_index = active.context_bone_index;
        }
    }
    return context;
}

std::optional<MeshWeightPaintTarget> current_mesh_weight_paint_target(const ShellState& state) {
    const WeightPaintSelectionContext context =
        resolve_weight_paint_selection_context(state);
    if (!state.load_result || !state.preview_skeleton ||
        !context.target_slot_index.has_value()) {
        return std::nullopt;
    }

    const std::size_t slot_index = *context.target_slot_index;
    const auto& skeleton = *state.load_result.skeleton_data;
    if (slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    const marrow::runtime::AttachmentData* display_attachment = nullptr;
    if (context.target_attachment.has_value()) {
        if (!context.target_attachment->skin_index.has_value()) {
            return std::nullopt;
        }
        const auto current_selection = current_attachment_selection(state, slot_index);
        if (!current_selection.has_value() ||
            current_selection->skin_index != context.target_attachment->skin_index ||
            current_selection->attachment_name !=
                context.target_attachment->attachment_name) {
            return std::nullopt;
        }
        display_attachment = skeleton.find_attachment(
            *context.target_attachment->skin_index,
            slot_index,
            context.target_attachment->attachment_name);
    } else {
        display_attachment = state.preview_skeleton->current_attachment(slot_index);
    }
    if (display_attachment == nullptr || display_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    MeshWeightPaintTarget target;
    target.slot_index = slot_index;
    target.slot_name = skeleton.slots()[slot_index].name;
    target.display_attachment_name = display_attachment->name;
    target.display_attachment = display_attachment;

    if (display_attachment->kind == marrow::runtime::AttachmentKind::LinkedMesh &&
        display_attachment->linked_mesh.has_value()) {
        target.source_skin_index =
            display_attachment->linked_mesh->parent_skin_index.has_value()
                ? display_attachment->linked_mesh->parent_skin_index
                : skeleton.default_skin_index();
        target.source_attachment_name =
            display_attachment->linked_mesh->parent_attachment;
        target.source_attachment =
            target.source_skin_index.has_value()
                ? skeleton.find_attachment(
                      *target.source_skin_index,
                      slot_index,
                      target.source_attachment_name)
                : nullptr;
    } else {
        std::optional<std::size_t> source_skin_index;
        target.source_attachment =
            skeleton.find_attachment_source(slot_index, display_attachment->name, &source_skin_index);
        target.source_skin_index = source_skin_index;
        target.source_attachment_name = display_attachment->name;
    }

    if (target.source_attachment == nullptr || target.source_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    target.source_skin_name = source_skin_name(skeleton, target.source_skin_index);
    return target;
}

std::optional<MeshWeightOverlay> build_mesh_weight_overlay(
    const ShellState& state,
    const ViewportLayout& layout) {
    const std::optional<MeshWeightPaintTarget> target = current_mesh_weight_paint_target(state);
    if (!target.has_value() || !state.preview_skeleton) {
        return std::nullopt;
    }

    const std::optional<marrow::runtime::MeshAttachmentPose> pose =
        state.preview_skeleton->evaluate_current_mesh_attachment(target->slot_index);
    if (!pose.has_value() ||
        target->display_attachment == nullptr ||
        target->display_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    const auto& geometry = *target->display_attachment->mesh_geometry;
    if (pose->vertices.size() != geometry.weights.size()) {
        return std::nullopt;
    }

    MeshWeightOverlay overlay;
    overlay.target = *target;
    overlay.triangles = geometry.triangles;
    overlay.neighbors = build_mesh_vertex_neighbors(geometry.triangles, pose->vertices.size());
    const std::vector<double>* vertex_offsets =
        state.preview_skeleton->current_mesh_vertex_offsets(target->slot_index);
    if (vertex_offsets != nullptr) {
        overlay.vertex_offsets = *vertex_offsets;
    } else {
        overlay.vertex_offsets.assign(pose->vertices.size() * 2U, 0.0);
    }

    overlay.vertices.reserve(pose->vertices.size());
    const WeightPaintSelectionContext context =
        resolve_weight_paint_selection_context(state);
    for (std::size_t vertex_index = 0; vertex_index < pose->vertices.size(); ++vertex_index) {
        const double selected_weight =
            context.influence_bone_index.has_value()
                ? weight_for_bone(
                      geometry.weights[vertex_index],
                      *context.influence_bone_index)
                : 0.0;
        overlay.vertices.push_back(MeshWeightOverlayVertex{
            screen_from_world(
                layout,
                pose->vertices[vertex_index].x,
                pose->vertices[vertex_index].y),
            pose->vertices[vertex_index],
            selected_weight});
    }

    return overlay;
}

bool apply_weight_to_vertex_impl(
    const ShellState& state,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    bool assign_stamp,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    const WeightPaintSelectionContext context =
        resolve_weight_paint_selection_context(state);
    if (!state.load_result || !context.influence_bone_index.has_value() ||
        vertex == nullptr ||
        vertex_index >= overlay.vertices.size() ||
        *context.influence_bone_index >= state.load_result.skeleton_data->bones().size() ||
        *context.influence_bone_index >=
            state.preview_skeleton->bone_world_transforms().size()) {
        return false;
    }

    marrow::editor::MeshWeightVertexEdit updated = *vertex;
    const auto& skeleton = *state.load_result.skeleton_data;
    const std::string active_bone_name =
        skeleton.bones()[*context.influence_bone_index].name;
    auto influence_it = std::find_if(
        updated.influences.begin(),
        updated.influences.end(),
        [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
            return influence.bone_name == active_bone_name;
        });

    if (influence_it == updated.influences.end()) {
        // A new influence binds against the SETUP pose, not the pose under the
        // playhead. Inverting the current preview pose baked whatever the
        // animation happened to be doing into a `.marrow`-persisted bind
        // offset, so the same brush stroke produced different geometry
        // depending on where the user had scrubbed. The setup-pose position is
        // derived from the influences the vertex already carries, which are
        // themselves setup-frame offsets, so it also carries no animation FFD
        // by construction -- the FFD subtraction the current-pose path needed
        // is gone with it.
        const std::vector<marrow::runtime::BoneWorldTransform> local_setup =
            state.weight_paint_stroke.setup_transforms.empty()
                ? mesh_weight_model::setup_pose_bone_world_transforms(
                      state.load_result.skeleton_data)
                : std::vector<marrow::runtime::BoneWorldTransform>{};
        const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms =
            state.weight_paint_stroke.setup_transforms.empty()
                ? local_setup
                : state.weight_paint_stroke.setup_transforms;
        if (updated.influences.empty() ||
            *context.influence_bone_index >= setup_transforms.size()) {
            return false;
        }

        double setup_world_x = 0.0;
        double setup_world_y = 0.0;
        double weight_total = 0.0;
        for (const auto& existing : updated.influences) {
            const auto existing_index = skeleton.find_bone_index(existing.bone_name);
            if (!existing_index.has_value() || *existing_index >= setup_transforms.size()) {
                return false;
            }
            const auto& transform = setup_transforms[*existing_index];
            setup_world_x += (((existing.x * static_cast<double>(transform.a)) +
                               (existing.y * static_cast<double>(transform.b)) +
                               static_cast<double>(transform.world_x)) *
                              existing.weight);
            setup_world_y += (((existing.x * static_cast<double>(transform.c)) +
                               (existing.y * static_cast<double>(transform.d)) +
                               static_cast<double>(transform.world_y)) *
                              existing.weight);
            weight_total += existing.weight;
        }
        // Materialized runtime weights need not sum to one, so recover the
        // weighted *average*. A canonical vertex is unaffected bit-for-bit.
        if (std::abs(weight_total - 1.0) >
            mesh_weight_model::kMeshWeightSumTolerance) {
            if (!(weight_total > 0.0)) {
                return false;
            }
            setup_world_x /= weight_total;
            setup_world_y /= weight_total;
        }

        const auto bind_position = mesh_weight_model::inverse_transform_point_safe(
            setup_transforms[*context.influence_bone_index],
            setup_world_x,
            setup_world_y);
        if (!bind_position.has_value()) {
            return false;
        }

        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            active_bone_name,
            static_cast<double>(bind_position->x),
            static_cast<double>(bind_position->y),
            0.0});
        influence_it = updated.influences.end() - 1;
    }

    // Paint is a rate: it accumulates toward the active bone. Replace is a
    // target: the stamp is the weight the active bone should END UP with, and
    // the remaining influences are scaled to fill what is left. Assigning the
    // stamp as a *raw* weight and letting normalization sort it out cannot
    // express that -- at strength 1.0 it yields 1/(1 + others), never the full
    // 1.0 a Replace brush exists to paint in one pass.
    const std::size_t active_index =
        static_cast<std::size_t>(influence_it - updated.influences.begin());
    if (assign_stamp) {
        const double target = std::clamp(stamp_strength, 0.0, 1.0);
        double others_total = 0.0;
        for (std::size_t index = 0; index < updated.influences.size(); ++index) {
            if (index != active_index) {
                others_total += updated.influences[index].weight;
            }
        }
        if (target >= 1.0 - mesh_weight_model::kMeshWeightEpsilon) {
            for (std::size_t index = 0; index < updated.influences.size(); ++index) {
                if (index != active_index) {
                    updated.influences[index].weight = 0.0;
                }
            }
            updated.influences[active_index].weight = 1.0;
        } else {
            if (others_total > 0.0) {
                const double scale = (1.0 - target) / others_total;
                for (std::size_t index = 0; index < updated.influences.size(); ++index) {
                    if (index != active_index) {
                        updated.influences[index].weight *= scale;
                    }
                }
            }
            updated.influences[active_index].weight = target;
        }
    } else {
        updated.influences[active_index].weight += stamp_strength;
    }
    // A vertex whose influence list cannot be made canonical makes this one
    // sample a no-op rather than killing the stroke.
    if (!mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &updated).empty()) {
        return false;
    }
    if (mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

bool apply_paint_weight_to_vertex(
    const ShellState& state,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    return apply_weight_to_vertex_impl(
        state, overlay, vertex_index, stamp_strength, false, vertex);
}

bool apply_replace_weight_to_vertex(
    const ShellState& state,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    return apply_weight_to_vertex_impl(
        state, overlay, vertex_index, stamp_strength, true, vertex);
}

bool apply_erase_weight_to_vertex(
    const ShellState& state,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    const WeightPaintSelectionContext context =
        resolve_weight_paint_selection_context(state);
    if (!state.load_result || !context.influence_bone_index.has_value() ||
        vertex == nullptr) {
        return false;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (*context.influence_bone_index >= skeleton.bones().size()) {
        return false;
    }

    if (vertex->influences.size() <= 1U) {
        return false;
    }

    marrow::editor::MeshWeightVertexEdit updated = *vertex;
    const std::string active_bone_name =
        skeleton.bones()[*context.influence_bone_index].name;
    auto influence_it = std::find_if(
        updated.influences.begin(),
        updated.influences.end(),
        [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
            return influence.bone_name == active_bone_name;
        });
    if (influence_it == updated.influences.end()) {
        return false;
    }

    influence_it->weight = std::max(0.0, influence_it->weight - stamp_strength);
    if (!mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &updated).empty()) {
        return false;
    }
    if (mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

bool apply_smooth_weight_to_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::editor::MeshWeightVertexEdit>& source_vertices,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr || vertex_index >= overlay.neighbors.size()) {
        return false;
    }

    struct AveragedInfluence {
        std::string bone_name;
        double average_weight{0.0};
        double bind_x_sum{0.0};
        double bind_y_sum{0.0};
        double bind_weight_sum{0.0};
    };

    const auto accumulate_vertex = [&](const marrow::editor::MeshWeightVertexEdit& sample_vertex,
                                       std::vector<AveragedInfluence>* averages) {
        for (const auto& influence : sample_vertex.influences) {
            auto averaged_it = std::find_if(
                averages->begin(),
                averages->end(),
                [&](const AveragedInfluence& averaged) {
                    return averaged.bone_name == influence.bone_name;
                });
            if (averaged_it == averages->end()) {
                averages->push_back(AveragedInfluence{influence.bone_name});
                averaged_it = averages->end() - 1;
            }

            averaged_it->average_weight += influence.weight;
            averaged_it->bind_x_sum += influence.x * influence.weight;
            averaged_it->bind_y_sum += influence.y * influence.weight;
            averaged_it->bind_weight_sum += influence.weight;
        }
    };

    std::vector<AveragedInfluence> averages;
    averages.reserve(8U);
    int sample_count = 1;
    accumulate_vertex(*vertex, &averages);

    for (const std::size_t neighbor_index : overlay.neighbors[vertex_index]) {
        if (neighbor_index >= source_vertices.size()) {
            continue;
        }

        accumulate_vertex(source_vertices[neighbor_index], &averages);
        ++sample_count;
    }

    if (sample_count <= 1) {
        return false;
    }

    std::vector<std::string> ordered_bones;
    ordered_bones.reserve(averages.size());
    for (const auto& influence : vertex->influences) {
        ordered_bones.push_back(influence.bone_name);
    }
    for (const AveragedInfluence& averaged : averages) {
        if (std::find(ordered_bones.begin(), ordered_bones.end(), averaged.bone_name) ==
            ordered_bones.end()) {
            ordered_bones.push_back(averaged.bone_name);
        }
    }

    marrow::editor::MeshWeightVertexEdit updated;
    updated.influences.reserve(ordered_bones.size());
    for (const std::string& bone_name : ordered_bones) {
        const auto current_it = std::find_if(
            vertex->influences.begin(),
            vertex->influences.end(),
            [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
                return influence.bone_name == bone_name;
            });
        const auto averaged_it = std::find_if(
            averages.begin(),
            averages.end(),
            [&](const AveragedInfluence& averaged) {
                return averaged.bone_name == bone_name;
            });

        const double current_weight =
            current_it != vertex->influences.end() ? current_it->weight : 0.0;
        const double average_weight =
            averaged_it != averages.end()
                ? averaged_it->average_weight / static_cast<double>(sample_count)
                : 0.0;
        const double blended_weight =
            current_weight + ((average_weight - current_weight) * stamp_strength);
        if (blended_weight <= kWeightEpsilon) {
            continue;
        }

        double bind_x = current_it != vertex->influences.end() ? current_it->x : 0.0;
        double bind_y = current_it != vertex->influences.end() ? current_it->y : 0.0;
        if (averaged_it != averages.end() && averaged_it->bind_weight_sum > kWeightEpsilon) {
            const double average_bind_x = averaged_it->bind_x_sum / averaged_it->bind_weight_sum;
            const double average_bind_y = averaged_it->bind_y_sum / averaged_it->bind_weight_sum;
            if (current_it != vertex->influences.end()) {
                bind_x = current_it->x + ((average_bind_x - current_it->x) * stamp_strength);
                bind_y = current_it->y + ((average_bind_y - current_it->y) * stamp_strength);
            } else {
                bind_x = average_bind_x;
                bind_y = average_bind_y;
            }
        }

        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            bone_name,
            bind_x,
            bind_y,
            blended_weight});
    }

    if (!mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &updated).empty()) {
        return false;
    }
    if (mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

std::string weight_paint_stroke_label(
    const ShellState& state,
    const MeshWeightPaintTarget& target) {
    std::string bone_name = "<bone>";
    const WeightPaintSelectionContext context =
        resolve_weight_paint_selection_context(state);
    if (state.load_result && context.influence_bone_index.has_value() &&
        *context.influence_bone_index < state.load_result.skeleton_data->bones().size()) {
        bone_name =
            state.load_result.skeleton_data->bones()[*context.influence_bone_index].name;
    }

    switch (state.weight_paint.mode) {
    case WeightPaintMode::Paint:
        return "Painted " + bone_name + " weights on " + target.source_attachment_name;
    case WeightPaintMode::Erase:
        return "Erased " + bone_name + " weights on " + target.source_attachment_name;
    case WeightPaintMode::Smooth:
        return "Smoothed weights on " + target.source_attachment_name;
    case WeightPaintMode::Replace:
        return "Replaced " + bone_name + " weights on " + target.source_attachment_name;
    }

    return "Edited mesh weights";
}

void reset_weight_paint_stroke(ShellState* state) {
    if (state == nullptr) {
        return;
    }

    state->weight_paint_stroke.active = false;
    state->weight_paint_stroke.changed = false;
    state->weight_paint_stroke.label.clear();
    state->weight_paint_stroke.group.clear();
    state->weight_paint_stroke.has_last_sample = false;
    state->weight_paint_stroke.setup_transforms.clear();
}

void begin_weight_paint_stroke(
    ShellState* state,
    const MeshWeightPaintTarget& target) {
    if (state == nullptr || state->weight_paint_stroke.active || !state->load_result) {
        return;
    }
    if (state->pending_edit_action.has_value()) {
        state->status_message = "Finish the active edit before painting weights";
        return;
    }

    state->weight_paint_stroke.active = true;
    state->weight_paint_stroke.changed = false;
    state->weight_paint_stroke.before_snapshot = capture_history_snapshot(*state);
    state->weight_paint_stroke.label = weight_paint_stroke_label(*state, target);
    state->weight_paint_stroke.group =
        "mesh-weight:" + target.source_skin_name + ":" + target.slot_name + ":" +
        target.source_attachment_name;
    state->weight_paint_stroke.has_last_sample = false;
    // Resolved once per stroke, not once per sample.
    state->weight_paint_stroke.setup_transforms =
        mesh_weight_model::setup_pose_bone_world_transforms(
            state->load_result.skeleton_data);
}

bool finish_weight_paint_stroke(ShellState* state) {
    if (state == nullptr || !state->weight_paint_stroke.active) {
        return false;
    }

    const bool changed = state->weight_paint_stroke.changed;
    const EditorHistorySnapshot before_snapshot = state->weight_paint_stroke.before_snapshot;
    const std::string label = state->weight_paint_stroke.label;
    const std::string group = state->weight_paint_stroke.group;
    reset_weight_paint_stroke(state);

    if (!changed) {
        return false;
    }

    return record_action_from_snapshots(
        state,
        before_snapshot,
        EditActionKind::EditProperty,
        label,
        group,
        false);
}

bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const WeightPaintSample& sample) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const WeightPaintSelectionContext selection_context =
        resolve_weight_paint_selection_context(*state);
    if ((state->weight_paint.mode == WeightPaintMode::Paint ||
         state->weight_paint.mode == WeightPaintMode::Erase ||
         state->weight_paint.mode == WeightPaintMode::Replace) &&
        (!selection_context.influence_bone_index.has_value() ||
         *selection_context.influence_bone_index >=
             state->load_result.skeleton_data->bones().size())) {
        return false;
    }

    marrow::editor::MeshWeightAttachmentEdit next_edit =
        overlay.target.source_attachment != nullptr
            ? mesh_weight_model::mesh_weight_edit_from_runtime(
                  *state->load_result.skeleton_data,
                  overlay.target.source_skin_name,
                  overlay.target.slot_name,
                  overlay.target.source_attachment_name,
                  *overlay.target.source_attachment)
            : marrow::editor::MeshWeightAttachmentEdit{};
    if (next_edit.vertices.empty()) {
        return false;
    }

    const std::vector<marrow::editor::MeshWeightVertexEdit> smooth_source_vertices =
        next_edit.vertices;
    bool changed = false;
    for (std::size_t vertex_index = 0; vertex_index < overlay.vertices.size(); ++vertex_index) {
        if (vertex_index >= next_edit.vertices.size()) {
            break;
        }

        const float distance = std::sqrt(squared_distance(
            sample.screen_position,
            overlay.vertices[vertex_index].screen_position));
        if (distance > state->weight_paint.radius_pixels) {
            continue;
        }

        const double falloff = 1.0 -
            (static_cast<double>(distance) /
             std::max(static_cast<double>(state->weight_paint.radius_pixels), 1.0));
        const double stamp_strength = pressure_scaled_strength(
            static_cast<double>(state->weight_paint.strength),
            static_cast<double>(sample.pressure),
            falloff);
        if (stamp_strength <= 1e-6) {
            continue;
        }

        bool vertex_changed = false;
        switch (state->weight_paint.mode) {
        case WeightPaintMode::Paint:
            vertex_changed = apply_paint_weight_to_vertex(
                *state,
                overlay,
                vertex_index,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        case WeightPaintMode::Erase:
            vertex_changed = apply_erase_weight_to_vertex(
                *state,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        case WeightPaintMode::Replace:
            vertex_changed = apply_replace_weight_to_vertex(
                *state,
                overlay,
                vertex_index,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        case WeightPaintMode::Smooth:
            vertex_changed = apply_smooth_weight_to_vertex(
                *state->load_result.skeleton_data,
                smooth_source_vertices,
                overlay,
                vertex_index,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        }

        changed = changed || vertex_changed;
    }

    if (!changed) {
        return false;
    }

    // store_mesh_weight_attachment_edit touches exactly one attachment edit,
    // so snapshot that entry (or its absence) as the rollback buffer instead
    // of deep-copying the whole ProjectData at brush-sample rate.
    marrow::editor::ProjectData* project = state->load_result.project.get();
    marrow::editor::MeshWeightAttachmentEdit* existing_edit =
        project->find_mesh_weight_attachment_edit(
            next_edit.skin_name,
            next_edit.slot_name,
            next_edit.attachment_name);
    const bool had_edit = existing_edit != nullptr;
    marrow::editor::MeshWeightAttachmentEdit previous_edit;
    if (had_edit) {
        previous_edit = *existing_edit;
    }
    const auto restore_previous_edit = [&]() {
        if (had_edit) {
            marrow::editor::MeshWeightAttachmentEdit* stored =
                project->find_mesh_weight_attachment_edit(
                    previous_edit.skin_name,
                    previous_edit.slot_name,
                    previous_edit.attachment_name);
            if (stored != nullptr) {
                *stored = previous_edit;
            }
        } else {
            project->mesh_weight_attachment_edits.pop_back();
        }
    };
    store_mesh_weight_attachment_edit(project, std::move(next_edit));
    if (!rebuild_project_runtime(state)) {
        restore_previous_edit();
        state->status_message = "Weight paint stroke failed";
        return false;
    }
    if (!apply_current_animation_state_to_preview(state)) {
        restore_previous_edit();
        state->status_message = "Weight paint stroke failed";
        return false;
    }

    update_project_dirty_state(state);
    state->weight_paint_stroke.changed = true;
    return true;
}

bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const ImVec2& screen_position) {
    return apply_weight_paint_sample(
        state,
        overlay,
        WeightPaintSample{screen_position, 1.0f});
}

namespace {

/// Resolves the current weight target into the primitive's target triple plus
/// the runtime attachment the overlay materializes from.
struct ResolvedWeightCommandTarget {
    marrow::editor::MeshWeightTarget target;
    const marrow::runtime::AttachmentData* attachment{nullptr};
    std::string attachment_label;
};

std::optional<ResolvedWeightCommandTarget> resolve_weight_command_target(
    const ShellState& state) {
    const std::optional<MeshWeightPaintTarget> paint_target =
        current_mesh_weight_paint_target(state);
    if (!paint_target.has_value() || paint_target->source_attachment == nullptr ||
        paint_target->source_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }
    ResolvedWeightCommandTarget resolved;
    resolved.target = marrow::editor::MeshWeightTarget{
        paint_target->source_skin_name,
        paint_target->slot_name,
        paint_target->source_attachment_name};
    resolved.attachment = paint_target->source_attachment;
    resolved.attachment_label = paint_target->source_attachment_name;
    return resolved;
}

/// Runs one weight primitive inside one transaction and records one history
/// entry. A result that changes nothing commits nothing, which is the GUI
/// analogue of the agent operations' `no_change` disposition.
bool run_weight_command(
    ShellState* state,
    std::string_view label_prefix,
    const std::function<marrow::editor::MeshWeightResult(
        marrow::editor::ProjectData*,
        const marrow::editor::MeshWeightTarget&,
        const marrow::runtime::AttachmentData&)>& apply) {
    if (state == nullptr || !state->load_result) {
        return false;
    }
    if (state->pending_edit_action.has_value() || state->weight_paint_stroke.active) {
        state->status_message = "Finish the active edit before editing weights";
        return false;
    }
    const auto resolved = resolve_weight_command_target(*state);
    if (!resolved.has_value()) {
        state->status_message = "Select a mesh attachment to edit weights";
        return false;
    }

    const std::string label =
        std::string(label_prefix) + " weights on " + resolved->attachment_label;
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        label,
        "mesh-weight-command:" + resolved->target.skin_name + ":" +
            resolved->target.slot_name + ":" + resolved->target.attachment_name,
        false,
        marrow::editor::EditImpact::Project | marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    const marrow::editor::MeshWeightResult result =
        apply(transaction.project(), resolved->target, *resolved->attachment);
    if (!result) {
        transaction.cancel();
        state->error_message = result.error;
        state->status_message = result.error;
        return false;
    }
    if (!result.changed) {
        transaction.cancel();
        state->status_message = label + ": nothing to change";
        return true;
    }

    const marrow::editor::SessionResult commit = transaction.commit();
    if (!commit) {
        state->error_message = commit.error->format();
        return false;
    }
    sync_shell_from_editor_session(state);
    state->status_message = label;
    return true;
}

} // namespace

std::vector<std::size_t> weight_command_scope(const ShellState& state) {
    std::vector<std::size_t> scope;
    if (!state.viewport_ffd_selection.has_value() ||
        state.viewport_ffd_selection->vertex_indices.empty()) {
        return scope;
    }
    const std::optional<MeshWeightPaintTarget> target =
        current_mesh_weight_paint_target(state);
    if (!target.has_value() ||
        state.viewport_ffd_selection->scope.slot_index != target->slot_index ||
        state.viewport_ffd_selection->scope.deform_attachment_name !=
            target->source_attachment_name) {
        // A selection that belongs to a different slot or attachment does not
        // narrow this command; falling back to "every vertex" matches the
        // shipped agent behaviour rather than silently editing the wrong thing.
        return scope;
    }
    scope = state.viewport_ffd_selection->vertex_indices;
    return scope;
}

bool normalize_weights_command(ShellState* state) {
    const std::vector<std::size_t> scope =
        state != nullptr ? weight_command_scope(*state) : std::vector<std::size_t>{};
    return run_weight_command(
        state,
        "Normalized",
        [&](marrow::editor::ProjectData* project,
            const marrow::editor::MeshWeightTarget& target,
            const marrow::runtime::AttachmentData& attachment) {
            return marrow::editor::normalize_mesh_weights(
                project, *state->load_result.skeleton_data, attachment, target, scope);
        });
}

bool rebind_weights_command(ShellState* state) {
    const std::vector<std::size_t> scope =
        state != nullptr ? weight_command_scope(*state) : std::vector<std::size_t>{};
    return run_weight_command(
        state,
        "Rebound",
        [&](marrow::editor::ProjectData* project,
            const marrow::editor::MeshWeightTarget& target,
            const marrow::runtime::AttachmentData& attachment) {
            return marrow::editor::rebind_mesh_weights(
                project, *state->load_result.skeleton_data, attachment, target, scope);
        });
}

bool generate_weights_command(ShellState* state) {
    const std::vector<std::size_t> scope =
        state != nullptr ? weight_command_scope(*state) : std::vector<std::size_t>{};
    const std::vector<std::string> candidates =
        state != nullptr ? state->weight_paint.candidate_bone_names
                         : std::vector<std::string>{};
    return run_weight_command(
        state,
        "Generated",
        [&](marrow::editor::ProjectData* project,
            const marrow::editor::MeshWeightTarget& target,
            const marrow::runtime::AttachmentData& attachment) {
            return marrow::editor::generate_mesh_weights(
                project,
                *state->load_result.skeleton_data,
                attachment,
                target,
                candidates,
                scope);
        });
}

bool set_active_vertex_weights_command(
    ShellState* state,
    std::size_t vertex_index,
    const std::vector<marrow::editor::MeshWeightInfluenceEdit>& influences) {
    marrow::editor::MeshWeightVertexEdit requested;
    requested.influences = influences;
    return run_weight_command(
        state,
        "Edited",
        [&](marrow::editor::ProjectData* project,
            const marrow::editor::MeshWeightTarget& target,
            const marrow::runtime::AttachmentData& attachment) {
            return marrow::editor::set_mesh_vertex_weights(
                project,
                *state->load_result.skeleton_data,
                attachment,
                target,
                {{vertex_index, requested}});
        });
}

} // namespace marrow::editor::shell
