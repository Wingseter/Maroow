#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace marrow::runtime {
namespace {

constexpr double kCoordinateEpsilon = 1e-9;
constexpr double kPi = 3.14159265358979323846;

bool nearly_equal(double lhs, double rhs) {
    return std::abs(lhs - rhs) <=
        kCoordinateEpsilon * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

bool finite_vertex(const AttachmentVertex& vertex) {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y);
}

bool valid_color(const SlotColor& color) {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
        std::isfinite(color.b) && std::isfinite(color.a) &&
        color.r >= 0.0 && color.r <= 1.0 && color.g >= 0.0 && color.g <= 1.0 &&
        color.b >= 0.0 && color.b <= 1.0 && color.a >= 0.0 && color.a <= 1.0;
}

bool shape_targets_overlap(
    const std::vector<SkinData>& skins,
    std::size_t slot_index,
    std::string_view lhs,
    std::string_view rhs) {
    for (const SkinData& skin : skins) {
        for (const SkinSlotData& candidate : skin.slot_attachments) {
            if (candidate.slot_index == slot_index &&
                detail::attachment_matches_mesh_deform_source(candidate.attachment, lhs) &&
                detail::attachment_matches_mesh_deform_source(candidate.attachment, rhs)) {
                return true;
            }
        }
    }
    return false;
}

bool slot_has_mesh_attachment(
    const std::vector<SkinData>& skins,
    std::size_t slot_index) {
    for (const SkinData& skin : skins) {
        for (const SkinSlotData& candidate : skin.slot_attachments) {
            if (candidate.slot_index == slot_index &&
                candidate.attachment.mesh_geometry != nullptr) {
                return true;
            }
        }
    }
    return false;
}

void require_non_empty_id(std::string_view id, std::string_view label) {
    if (id.empty()) {
        throw std::invalid_argument(std::string(label) + " id must not be empty");
    }
}

template <typename Map>
void insert_unique_id(
    Map* map,
    const std::string& id,
    std::size_t index,
    std::string_view label) {
    require_non_empty_id(id, label);
    if (!map->emplace(id, index).second) {
        throw std::invalid_argument(
            "duplicate " + std::string(label) + " id '" + id + "'");
    }
}

void append_unique(std::vector<std::size_t>* values, std::size_t value) {
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

bool has_two_distinct_points(const std::vector<AttachmentVertex>& points) {
    if (points.size() < 2U) {
        return false;
    }

    const AttachmentVertex* previous = &points.front();
    for (std::size_t index = 1; index < points.size(); ++index) {
        const AttachmentVertex& point = points[index];
        if (!nearly_equal(point.x, previous->x) || !nearly_equal(point.y, previous->y)) {
            return true;
        }
        previous = &point;
    }
    return false;
}

std::vector<double> sorted_unique_coordinates(
    const std::vector<WarpDeformerKeyform>& keyforms,
    bool x_axis) {
    std::vector<double> values;
    values.reserve(keyforms.size());
    for (const WarpDeformerKeyform& keyform : keyforms) {
        values.push_back(x_axis ? keyform.x : keyform.y);
    }
    std::sort(values.begin(), values.end());
    values.erase(
        std::unique(
            values.begin(),
            values.end(),
            [](double lhs, double rhs) { return nearly_equal(lhs, rhs); }),
        values.end());
    return values;
}

const WarpDeformerKeyform* find_warp_keyform(
    const ParameterDeformerDefinition& deformer,
    double x,
    double y) {
    const auto found = std::find_if(
        deformer.warp_keyforms.begin(),
        deformer.warp_keyforms.end(),
        [&](const WarpDeformerKeyform& keyform) {
            return nearly_equal(keyform.x, x) && nearly_equal(keyform.y, y);
        });
    return found == deformer.warp_keyforms.end() ? nullptr : &*found;
}

struct CoordinateBracket {
    double lower{0.0};
    double upper{0.0};
    double alpha{0.0};
};

CoordinateBracket bracket_coordinate(const std::vector<double>& coordinates, double value) {
    if (value <= coordinates.front()) {
        return {coordinates.front(), coordinates.front(), 0.0};
    }
    if (value >= coordinates.back()) {
        return {coordinates.back(), coordinates.back(), 0.0};
    }

    const auto upper = std::upper_bound(coordinates.begin(), coordinates.end(), value);
    const double upper_value = *upper;
    const double lower_value = *(upper - 1);
    return {
        lower_value,
        upper_value,
        (value - lower_value) / (upper_value - lower_value),
    };
}

AttachmentVertex lerp_vertex(
    const AttachmentVertex& lhs,
    const AttachmentVertex& rhs,
    double alpha) {
    return {
        static_cast<double>(lhs.x) +
            (static_cast<double>(rhs.x) - static_cast<double>(lhs.x)) * alpha,
        static_cast<double>(lhs.y) +
            (static_cast<double>(rhs.y) - static_cast<double>(lhs.y)) * alpha,
    };
}

AttachmentVertex evaluated_warp_control_point(
    const ParameterDeformerDefinition& deformer,
    std::size_t control_point_index,
    double x,
    double y) {
    const std::vector<double> x_coordinates = sorted_unique_coordinates(deformer.warp_keyforms, true);
    const std::vector<double> y_coordinates = sorted_unique_coordinates(deformer.warp_keyforms, false);
    const CoordinateBracket x_bracket = bracket_coordinate(x_coordinates, x);
    const CoordinateBracket y_bracket = bracket_coordinate(y_coordinates, y);

    const WarpDeformerKeyform* lower_lower =
        find_warp_keyform(deformer, x_bracket.lower, y_bracket.lower);
    const WarpDeformerKeyform* upper_lower =
        find_warp_keyform(deformer, x_bracket.upper, y_bracket.lower);
    const WarpDeformerKeyform* lower_upper =
        find_warp_keyform(deformer, x_bracket.lower, y_bracket.upper);
    const WarpDeformerKeyform* upper_upper =
        find_warp_keyform(deformer, x_bracket.upper, y_bracket.upper);
    if (lower_lower == nullptr || upper_lower == nullptr || lower_upper == nullptr ||
        upper_upper == nullptr) {
        return deformer.control_points[control_point_index];
    }

    const AttachmentVertex bottom = lerp_vertex(
        lower_lower->control_points[control_point_index],
        upper_lower->control_points[control_point_index],
        x_bracket.alpha);
    const AttachmentVertex top = lerp_vertex(
        lower_upper->control_points[control_point_index],
        upper_upper->control_points[control_point_index],
        x_bracket.alpha);
    return lerp_vertex(bottom, top, y_bracket.alpha);
}

bool coordinate_between(double value, double first, double second) {
    return value >= std::min(first, second) && value <= std::max(first, second);
}

std::optional<std::size_t> find_lattice_interval(
    const std::vector<double>& coordinates,
    double value) {
    for (std::size_t index = 0; index + 1U < coordinates.size(); ++index) {
        if (coordinate_between(value, coordinates[index], coordinates[index + 1U])) {
            return index;
        }
    }
    return std::nullopt;
}

AttachmentVertex apply_warp_deformer(
    const ParameterDeformerDefinition& deformer,
    const std::vector<double>& parameter_values,
    AttachmentVertex point) {
    std::size_t x_parameter = 0U;
    std::size_t y_parameter = 0U;
    for (const ParameterBindingDefinition& binding : deformer.parameter_bindings) {
        if (!binding.parameter_index.has_value()) {
            return point;
        }
        if (binding.axis == ParameterDeformerAxis::X) {
            x_parameter = *binding.parameter_index;
        } else if (binding.axis == ParameterDeformerAxis::Y) {
            y_parameter = *binding.parameter_index;
        }
    }
    if (x_parameter >= parameter_values.size() || y_parameter >= parameter_values.size()) {
        return point;
    }

    std::vector<double> x_coordinates(deformer.grid_cols);
    std::vector<double> y_coordinates(deformer.grid_rows);
    for (std::size_t column = 0; column < deformer.grid_cols; ++column) {
        x_coordinates[column] = deformer.control_points[column].x;
    }
    for (std::size_t row = 0; row < deformer.grid_rows; ++row) {
        y_coordinates[row] = deformer.control_points[row * deformer.grid_cols].y;
    }

    const std::optional<std::size_t> column = find_lattice_interval(x_coordinates, point.x);
    const std::optional<std::size_t> row = find_lattice_interval(y_coordinates, point.y);
    if (!column.has_value() || !row.has_value()) {
        return point;
    }

    const double x0 = x_coordinates[*column];
    const double x1 = x_coordinates[*column + 1U];
    const double y0 = y_coordinates[*row];
    const double y1 = y_coordinates[*row + 1U];
    const double u = (static_cast<double>(point.x) - x0) / (x1 - x0);
    const double v = (static_cast<double>(point.y) - y0) / (y1 - y0);
    const std::size_t lower_left_index = (*row * deformer.grid_cols) + *column;
    const std::size_t lower_right_index = lower_left_index + 1U;
    const std::size_t upper_left_index = ((*row + 1U) * deformer.grid_cols) + *column;
    const std::size_t upper_right_index = upper_left_index + 1U;

    const double parameter_x = parameter_values[x_parameter];
    const double parameter_y = parameter_values[y_parameter];
    const AttachmentVertex lower_left = evaluated_warp_control_point(
        deformer, lower_left_index, parameter_x, parameter_y);
    const AttachmentVertex lower_right = evaluated_warp_control_point(
        deformer, lower_right_index, parameter_x, parameter_y);
    const AttachmentVertex upper_left = evaluated_warp_control_point(
        deformer, upper_left_index, parameter_x, parameter_y);
    const AttachmentVertex upper_right = evaluated_warp_control_point(
        deformer, upper_right_index, parameter_x, parameter_y);
    return lerp_vertex(
        lerp_vertex(lower_left, lower_right, u),
        lerp_vertex(upper_left, upper_right, u),
        v);
}

double sample_rotation_angle(
    const ParameterDeformerDefinition& deformer,
    double parameter_value) {
    if (parameter_value <= deformer.rotation_keyforms.front().value) {
        return deformer.rotation_keyforms.front().angle;
    }
    if (parameter_value >= deformer.rotation_keyforms.back().value) {
        return deformer.rotation_keyforms.back().angle;
    }

    const auto upper = std::upper_bound(
        deformer.rotation_keyforms.begin(),
        deformer.rotation_keyforms.end(),
        parameter_value,
        [](double value, const RotationDeformerKeyform& keyform) {
            return value < keyform.value;
        });
    const RotationDeformerKeyform& lower_keyform = *(upper - 1);
    const double alpha = (parameter_value - lower_keyform.value) /
        (upper->value - lower_keyform.value);
    return lower_keyform.angle + (upper->angle - lower_keyform.angle) * alpha;
}

AttachmentVertex apply_rotation_deformer(
    const ParameterDeformerDefinition& deformer,
    const std::vector<double>& parameter_values,
    AttachmentVertex point) {
    if (deformer.parameter_bindings.empty() ||
        !deformer.parameter_bindings.front().parameter_index.has_value()) {
        return point;
    }
    const std::size_t parameter_index =
        *deformer.parameter_bindings.front().parameter_index;
    if (parameter_index >= parameter_values.size()) {
        return point;
    }

    const double angle = sample_rotation_angle(deformer, parameter_values[parameter_index]) *
        deformer.influence * kPi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double local_x = static_cast<double>(point.x) - deformer.pivot.x;
    const double local_y = static_cast<double>(point.y) - deformer.pivot.y;
    return {
        deformer.pivot.x + local_x * cosine - local_y * sine,
        deformer.pivot.y + local_x * sine + local_y * cosine,
    };
}

AttachmentVertex apply_deformer(
    const ParameterModelDefinitions& model,
    std::size_t deformer_index,
    const std::vector<double>& parameter_values,
    AttachmentVertex point) {
    if (deformer_index >= model.parameter_deformers.size()) {
        return point;
    }

    const ParameterDeformerDefinition& deformer = model.parameter_deformers[deformer_index];
    if (deformer.kind == ParameterDeformerKind::Warp) {
        point = apply_warp_deformer(deformer, parameter_values, point);
    } else {
        point = apply_rotation_deformer(deformer, parameter_values, point);
    }

    // A parent consumes its child's output. Loader validation limits this to one level.
    if (deformer.parent_index.has_value()) {
        const ParameterDeformerDefinition& parent =
            model.parameter_deformers[*deformer.parent_index];
        if (parent.kind == ParameterDeformerKind::Warp) {
            point = apply_warp_deformer(parent, parameter_values, point);
        } else {
            point = apply_rotation_deformer(parent, parameter_values, point);
        }
    }
    return point;
}

void sample_shape(
    const ParameterShapeDefinition& shape,
    double parameter_value,
    std::vector<double>* vertices_out) {
    if (parameter_value <= shape.keyforms.front().value) {
        *vertices_out = shape.keyforms.front().vertices;
        return;
    }
    if (parameter_value >= shape.keyforms.back().value) {
        *vertices_out = shape.keyforms.back().vertices;
        return;
    }

    const auto upper = std::upper_bound(
        shape.keyforms.begin(),
        shape.keyforms.end(),
        parameter_value,
        [](double value, const ParameterShapeKeyform& keyform) {
            return value < keyform.value;
        });
    const ParameterShapeKeyform& lower_keyform = *(upper - 1);
    const double alpha = (parameter_value - lower_keyform.value) /
        (upper->value - lower_keyform.value);
    vertices_out->resize(lower_keyform.vertices.size());
    for (std::size_t index = 0; index < vertices_out->size(); ++index) {
        (*vertices_out)[index] = lower_keyform.vertices[index] +
            (upper->vertices[index] - lower_keyform.vertices[index]) * alpha;
    }
}

SlotColor lerp_color(const SlotColor& lhs, const SlotColor& rhs, double alpha) {
    return {
        lhs.r + (rhs.r - lhs.r) * alpha,
        lhs.g + (rhs.g - lhs.g) * alpha,
        lhs.b + (rhs.b - lhs.b) * alpha,
        lhs.a + (rhs.a - lhs.a) * alpha,
    };
}

void sample_art_path(
    const ArtPathDefinition& definition,
    const std::vector<double>& parameter_values,
    EvaluatedArtPath* evaluated) {
    evaluated->id = definition.id;
    evaluated->name = definition.name;
    evaluated->points = definition.points;
    evaluated->width = definition.width;
    evaluated->color = definition.color;
    evaluated->cap = definition.cap;
    evaluated->join = definition.join;

    if (!definition.parameter_keyforms.has_value() ||
        !definition.parameter_keyforms->parameter_index.has_value() ||
        definition.parameter_keyforms->keyforms.empty()) {
        return;
    }

    const ArtPathParameterKeyforms& parameter_keyforms = *definition.parameter_keyforms;
    const std::size_t parameter_index = *parameter_keyforms.parameter_index;
    if (parameter_index >= parameter_values.size()) {
        return;
    }
    const double value = parameter_values[parameter_index];
    const std::vector<ArtPathKeyform>& keyforms = parameter_keyforms.keyforms;
    if (value <= keyforms.front().value) {
        evaluated->points = keyforms.front().points;
        evaluated->width = keyforms.front().width;
        evaluated->color = keyforms.front().color;
        return;
    }
    if (value >= keyforms.back().value) {
        evaluated->points = keyforms.back().points;
        evaluated->width = keyforms.back().width;
        evaluated->color = keyforms.back().color;
        return;
    }

    const auto upper = std::upper_bound(
        keyforms.begin(),
        keyforms.end(),
        value,
        [](double sample, const ArtPathKeyform& keyform) {
            return sample < keyform.value;
        });
    const ArtPathKeyform& lower = *(upper - 1);
    const double alpha = (value - lower.value) / (upper->value - lower.value);
    evaluated->points.resize(lower.points.size());
    for (std::size_t index = 0; index < lower.points.size(); ++index) {
        evaluated->points[index] = lerp_vertex(lower.points[index], upper->points[index], alpha);
    }
    evaluated->width = lower.width + (upper->width - lower.width) * alpha;
    evaluated->color = lerp_color(lower.color, upper->color, alpha);
}

bool revisions_match(
    const std::vector<std::uint64_t>& cached,
    const std::vector<std::size_t>& dependencies,
    const std::vector<std::uint64_t>& current) {
    if (cached.size() != dependencies.size()) {
        return false;
    }
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (dependencies[index] >= current.size() ||
            cached[index] != current[dependencies[index]]) {
            return false;
        }
    }
    return true;
}

void capture_revisions(
    const std::vector<std::size_t>& dependencies,
    const std::vector<std::uint64_t>& current,
    std::vector<std::uint64_t>* captured) {
    captured->resize(dependencies.size());
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        (*captured)[index] = current[dependencies[index]];
    }
}

} // namespace

void SkeletonData::initialize_parameter_model() {
    parameter_indices_.clear();
    parameter_group_indices_.clear();
    parameter_shape_indices_.clear();
    parameter_deformer_indices_.clear();
    art_path_indices_.clear();
    expression_indices_.clear();

    for (std::size_t index = 0; index < parameter_model_.parameters.size(); ++index) {
        const ParameterDefinition& parameter = parameter_model_.parameters[index];
        insert_unique_id(&parameter_indices_, parameter.id, index, "parameter");
        if (parameter.name.empty()) {
            throw std::invalid_argument("parameter '" + parameter.id + "' name must not be empty");
        }
        if (!std::isfinite(parameter.min_value) || !std::isfinite(parameter.max_value) ||
            !std::isfinite(parameter.default_value) || parameter.min_value > parameter.max_value) {
            throw std::invalid_argument(
                "parameter '" + parameter.id + "' has an invalid finite range/default");
        }
        if (parameter.clamp &&
            (parameter.default_value < parameter.min_value ||
             parameter.default_value > parameter.max_value)) {
            throw std::invalid_argument(
                "clamped parameter '" + parameter.id + "' default must stay inside its range");
        }
        if (parameter.ui_step.has_value() &&
            (!std::isfinite(*parameter.ui_step) || *parameter.ui_step <= 0.0)) {
            throw std::invalid_argument(
                "parameter '" + parameter.id + "' ui_step must be finite and positive");
        }
    }

    for (std::size_t index = 0; index < parameter_model_.parameter_groups.size(); ++index) {
        const ParameterGroupDefinition& group = parameter_model_.parameter_groups[index];
        insert_unique_id(&parameter_group_indices_, group.id, index, "parameter group");
        if (group.name.empty()) {
            throw std::invalid_argument(
                "parameter group '" + group.id + "' name must not be empty");
        }
        std::unordered_set<std::string> members;
        for (const std::string& parameter_id : group.parameter_ids) {
            if (parameter_indices_.find(parameter_id) == parameter_indices_.end()) {
                throw std::invalid_argument(
                    "parameter group '" + group.id + "' references unknown parameter '" +
                    parameter_id + "'");
            }
            if (!members.insert(parameter_id).second) {
                throw std::invalid_argument(
                    "parameter group '" + group.id + "' repeats parameter '" + parameter_id +
                    "'");
            }
        }
    }

    slot_parameter_shape_indices_.assign(slots_.size(), {});
    for (std::size_t index = 0; index < parameter_model_.parameter_shapes.size(); ++index) {
        ParameterShapeDefinition& shape = parameter_model_.parameter_shapes[index];
        insert_unique_id(&parameter_shape_indices_, shape.id, index, "parameter shape");
        const std::optional<std::size_t> slot_index = find_slot_index(shape.target_slot);
        const std::optional<std::size_t> parameter_index = find_parameter_index(shape.parameter);
        if (!slot_index.has_value()) {
            throw std::invalid_argument(
                "parameter shape '" + shape.id + "' references unknown slot '" +
                shape.target_slot + "'");
        }
        if (!parameter_index.has_value()) {
            throw std::invalid_argument(
                "parameter shape '" + shape.id + "' references unknown parameter '" +
                shape.parameter + "'");
        }
        if (parameters()[*parameter_index].type != ParameterType::Continuous) {
            throw std::invalid_argument(
                "parameter shape '" + shape.id + "' requires a continuous parameter");
        }
        shape.target_slot_index = *slot_index;
        shape.parameter_index = *parameter_index;

        const AttachmentData* attachment =
            find_attachment_source(*slot_index, shape.target_attachment);
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            throw std::invalid_argument(
                "parameter shape '" + shape.id + "' target must resolve to a mesh attachment");
        }
        for (const SkinData& skin : skins_) {
            for (const SkinSlotData& candidate : skin.slot_attachments) {
                if (candidate.slot_index != *slot_index ||
                    !detail::attachment_matches_mesh_deform_source(
                        candidate.attachment,
                        shape.target_attachment)) {
                    continue;
                }
                if (candidate.attachment.mesh_geometry == nullptr ||
                    candidate.attachment.mesh_geometry->vertices.size() !=
                        attachment->mesh_geometry->vertices.size()) {
                    throw std::invalid_argument(
                        "parameter shape '" + shape.id +
                        "' target and deform-inheriting linked meshes must share topology");
                }
            }
        }
        if (shape.keyforms.empty()) {
            throw std::invalid_argument(
                "parameter shape '" + shape.id + "' requires at least one keyform");
        }
        for (std::size_t keyform_index = 0; keyform_index < shape.keyforms.size(); ++keyform_index) {
            const ParameterShapeKeyform& keyform = shape.keyforms[keyform_index];
            if (!std::isfinite(keyform.value) ||
                keyform.vertices.size() != attachment->mesh_geometry->vertices.size() ||
                !std::all_of(keyform.vertices.begin(), keyform.vertices.end(), [](double value) {
                    return std::isfinite(value);
                })) {
                throw std::invalid_argument(
                    "parameter shape '" + shape.id + "' has an invalid keyform payload");
            }
            if (keyform_index > 0U &&
                !(shape.keyforms[keyform_index - 1U].value < keyform.value)) {
                throw std::invalid_argument(
                    "parameter shape '" + shape.id +
                    "' keyform values must be strictly increasing");
            }
        }
        if (shape.blend_mode == ParameterShapeBlendMode::NormalizedOverride) {
            for (std::size_t previous_index = 0; previous_index < index; ++previous_index) {
                const ParameterShapeDefinition& existing =
                    parameter_model_.parameter_shapes[previous_index];
                if (existing.blend_mode == ParameterShapeBlendMode::NormalizedOverride &&
                    existing.target_slot_index == shape.target_slot_index &&
                    shape_targets_overlap(
                        skins_,
                        *slot_index,
                        existing.target_attachment,
                        shape.target_attachment)) {
                    throw std::invalid_argument(
                        "only one normalized_override shape may match a mesh deform source in "
                        "slot '" + shape.target_slot + "'");
                }
            }
        }
        slot_parameter_shape_indices_[*slot_index].push_back(index);
    }

    for (std::size_t index = 0; index < parameter_model_.parameter_deformers.size(); ++index) {
        const ParameterDeformerDefinition& deformer =
            parameter_model_.parameter_deformers[index];
        insert_unique_id(&parameter_deformer_indices_, deformer.id, index, "parameter deformer");
        if (deformer.name.empty()) {
            throw std::invalid_argument(
                "parameter deformer '" + deformer.id + "' name must not be empty");
        }
    }

    for (ParameterDeformerDefinition& deformer : parameter_model_.parameter_deformers) {
        deformer.target_slot_indices.clear();
        std::unordered_set<std::size_t> target_slots;
        for (const std::string& target_slot : deformer.target_slots) {
            const std::optional<std::size_t> slot_index = find_slot_index(target_slot);
            if (!slot_index.has_value()) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' references unknown slot '" +
                    target_slot + "'");
            }
            if (!target_slots.insert(*slot_index).second) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' repeats target slot '" +
                    target_slot + "'");
            }
            if (!slot_has_mesh_attachment(skins_, *slot_index)) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' target slot '" +
                    target_slot + "' must contain a mesh attachment");
            }
            deformer.target_slot_indices.push_back(*slot_index);
        }
        for (ParameterBindingDefinition& binding : deformer.parameter_bindings) {
            binding.parameter_index = find_parameter_index(binding.parameter);
            if (!binding.parameter_index.has_value()) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' references unknown parameter '" +
                    binding.parameter + "'");
            }
            if (parameters()[*binding.parameter_index].type != ParameterType::Continuous) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id +
                    "' requires continuous parameter bindings");
            }
        }

        if (deformer.kind == ParameterDeformerKind::Warp) {
            if (deformer.parameter_bindings.size() != 2U ||
                deformer.parameter_bindings[0].parameter ==
                    deformer.parameter_bindings[1].parameter) {
                throw std::invalid_argument(
                    "warp deformer '" + deformer.id +
                    "' requires distinct x and y parameter bindings");
            }
            bool has_x = false;
            bool has_y = false;
            for (const ParameterBindingDefinition& binding : deformer.parameter_bindings) {
                has_x = has_x || binding.axis == ParameterDeformerAxis::X;
                has_y = has_y || binding.axis == ParameterDeformerAxis::Y;
            }
            if (!has_x || !has_y || deformer.grid_cols < 2U || deformer.grid_rows < 2U ||
                deformer.control_points.size() != deformer.grid_cols * deformer.grid_rows ||
                deformer.warp_keyforms.empty()) {
                throw std::invalid_argument(
                    "warp deformer '" + deformer.id + "' has an invalid lattice or binding set");
            }
            for (std::size_t row = 0; row < deformer.grid_rows; ++row) {
                for (std::size_t column = 0; column < deformer.grid_cols; ++column) {
                    const AttachmentVertex& point =
                        deformer.control_points[row * deformer.grid_cols + column];
                    if (!finite_vertex(point) ||
                        !nearly_equal(point.x, deformer.control_points[column].x) ||
                        !nearly_equal(
                            point.y,
                            deformer.control_points[row * deformer.grid_cols].y)) {
                        throw std::invalid_argument(
                            "warp deformer '" + deformer.id +
                            "' base lattice must be axis aligned and row-major");
                    }
                }
            }
            const auto monotonic = [](double first, double second, double previous, double next) {
                const bool increasing = second > first;
                return increasing ? next > previous : next < previous;
            };
            for (std::size_t column = 1; column < deformer.grid_cols; ++column) {
                const double previous = deformer.control_points[column - 1U].x;
                const double next = deformer.control_points[column].x;
                if (nearly_equal(previous, next) ||
                    (column > 1U && !monotonic(
                        deformer.control_points[0].x,
                        deformer.control_points[1].x,
                        previous,
                        next))) {
                    throw std::invalid_argument(
                        "warp deformer '" + deformer.id +
                        "' lattice x coordinates must be strictly monotonic");
                }
            }
            for (std::size_t row = 1; row < deformer.grid_rows; ++row) {
                const double previous =
                    deformer.control_points[(row - 1U) * deformer.grid_cols].y;
                const double next = deformer.control_points[row * deformer.grid_cols].y;
                if (nearly_equal(previous, next) ||
                    (row > 1U && !monotonic(
                        deformer.control_points[0].y,
                        deformer.control_points[deformer.grid_cols].y,
                        previous,
                        next))) {
                    throw std::invalid_argument(
                        "warp deformer '" + deformer.id +
                        "' lattice y coordinates must be strictly monotonic");
                }
            }
            for (const WarpDeformerKeyform& keyform : deformer.warp_keyforms) {
                if (!std::isfinite(keyform.x) || !std::isfinite(keyform.y) ||
                    keyform.control_points.size() != deformer.control_points.size() ||
                    !std::all_of(
                        keyform.control_points.begin(),
                        keyform.control_points.end(),
                        finite_vertex)) {
                    throw std::invalid_argument(
                        "warp deformer '" + deformer.id + "' has an invalid keyform");
                }
            }
            const std::vector<double> x_values = sorted_unique_coordinates(deformer.warp_keyforms, true);
            const std::vector<double> y_values = sorted_unique_coordinates(deformer.warp_keyforms, false);
            if (x_values.size() < 2U || y_values.size() < 2U ||
                deformer.warp_keyforms.size() != x_values.size() * y_values.size()) {
                throw std::invalid_argument(
                    "warp deformer '" + deformer.id +
                    "' keyforms must contain a complete x/y Cartesian grid");
            }
            for (double x : x_values) {
                for (double y : y_values) {
                    if (find_warp_keyform(deformer, x, y) == nullptr) {
                        throw std::invalid_argument(
                            "warp deformer '" + deformer.id +
                            "' keyforms must contain every x/y Cartesian pair");
                    }
                }
            }
        } else {
            if (deformer.parameter_bindings.size() != 1U ||
                deformer.parameter_bindings.front().axis != ParameterDeformerAxis::Angle ||
                !finite_vertex(deformer.pivot) || !std::isfinite(deformer.influence) ||
                deformer.influence < 0.0 || deformer.influence > 1.0 ||
                deformer.rotation_keyforms.empty()) {
                throw std::invalid_argument(
                    "rotation deformer '" + deformer.id +
                    "' has an invalid angle binding, pivot, influence, or keyform set");
            }
            for (std::size_t index = 0; index < deformer.rotation_keyforms.size(); ++index) {
                const RotationDeformerKeyform& keyform = deformer.rotation_keyforms[index];
                if (!std::isfinite(keyform.value) || !std::isfinite(keyform.angle) ||
                    (index > 0U &&
                     !(deformer.rotation_keyforms[index - 1U].value < keyform.value))) {
                    throw std::invalid_argument(
                        "rotation deformer '" + deformer.id +
                        "' keyform values must be finite and strictly increasing");
                }
            }
        }

        deformer.parent_index.reset();
        if (deformer.parent.has_value()) {
            const auto found = parameter_deformer_indices_.find(*deformer.parent);
            if (found == parameter_deformer_indices_.end()) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' references unknown parent '" +
                    *deformer.parent + "'");
            }
            if (found->second == parameter_deformer_indices_.at(deformer.id)) {
                throw std::invalid_argument(
                    "parameter deformer '" + deformer.id + "' cannot parent itself");
            }
            deformer.parent_index = found->second;
        }
    }
    for (const ParameterDeformerDefinition& deformer : parameter_model_.parameter_deformers) {
        if (deformer.parent_index.has_value() &&
            parameter_model_.parameter_deformers[*deformer.parent_index].parent_index.has_value()) {
            throw std::invalid_argument(
                "parameter deformer '" + deformer.id +
                "' exceeds the supported one-level parent chain");
        }
    }

    slot_parameter_deformer_indices_.assign(slots_.size(), std::nullopt);
    for (std::size_t index = 0; index < parameter_model_.parameter_deformers.size(); ++index) {
        const ParameterDeformerDefinition& deformer =
            parameter_model_.parameter_deformers[index];
        for (const std::size_t slot_index : deformer.target_slot_indices) {
            if (slot_parameter_deformer_indices_[slot_index].has_value()) {
                throw std::invalid_argument(
                    "slot '" + slots_[slot_index].name +
                    "' is targeted by multiple parameter deformer leaf chains");
            }
            slot_parameter_deformer_indices_[slot_index] = index;
        }
    }

    for (std::size_t index = 0; index < parameter_model_.art_paths.size(); ++index) {
        ArtPathDefinition& art_path = parameter_model_.art_paths[index];
        insert_unique_id(&art_path_indices_, art_path.id, index, "art path");
        if (art_path.name.empty() || !std::isfinite(art_path.width) || art_path.width <= 0.0 ||
            !valid_color(art_path.color) ||
            !std::all_of(art_path.points.begin(), art_path.points.end(), finite_vertex) ||
            !has_two_distinct_points(art_path.points)) {
            throw std::invalid_argument(
                "art path '" + art_path.id + "' has invalid name, points, or width");
        }
        art_path.parent_deformer_index.reset();
        if (art_path.parent_deformer.has_value()) {
            art_path.parent_deformer_index =
                find_parameter_deformer_index(*art_path.parent_deformer);
            if (!art_path.parent_deformer_index.has_value()) {
                throw std::invalid_argument(
                    "art path '" + art_path.id + "' references unknown parent deformer '" +
                    *art_path.parent_deformer + "'");
            }
        }
        if (art_path.parameter_keyforms.has_value()) {
            ArtPathParameterKeyforms& parameter_keyforms = *art_path.parameter_keyforms;
            parameter_keyforms.parameter_index = find_parameter_index(parameter_keyforms.parameter);
            if (!parameter_keyforms.parameter_index.has_value() ||
                parameters()[*parameter_keyforms.parameter_index].type !=
                    ParameterType::Continuous ||
                parameter_keyforms.keyforms.empty()) {
                throw std::invalid_argument(
                    "art path '" + art_path.id +
                    "' parameter keyforms require one continuous parameter");
            }
            for (std::size_t keyform_index = 0;
                 keyform_index < parameter_keyforms.keyforms.size();
                 ++keyform_index) {
                const ArtPathKeyform& keyform = parameter_keyforms.keyforms[keyform_index];
                if (!std::isfinite(keyform.value) || !std::isfinite(keyform.width) ||
                    keyform.width <= 0.0 || !valid_color(keyform.color) ||
                    keyform.points.size() != art_path.points.size() ||
                    !std::all_of(keyform.points.begin(), keyform.points.end(), finite_vertex) ||
                    !has_two_distinct_points(keyform.points) ||
                    (keyform_index > 0U &&
                     !(parameter_keyforms.keyforms[keyform_index - 1U].value < keyform.value))) {
                    throw std::invalid_argument(
                        "art path '" + art_path.id + "' has an invalid parameter keyform");
                }
            }
        }
    }

    for (std::size_t index = 0; index < parameter_model_.expressions.size(); ++index) {
        ExpressionDefinition& expression = parameter_model_.expressions[index];
        insert_unique_id(&expression_indices_, expression.id, index, "expression");
        if (expression.name.empty() || !std::isfinite(expression.duration) ||
            expression.duration < 0.0 || expression.targets.empty()) {
            throw std::invalid_argument(
                "expression '" + expression.id + "' has invalid name, duration, or targets");
        }
        std::unordered_set<std::size_t> targets;
        for (ExpressionTargetDefinition& target : expression.targets) {
            target.parameter_index = find_parameter_index(target.parameter);
            if (!target.parameter_index.has_value() || !std::isfinite(target.value)) {
                throw std::invalid_argument(
                    "expression '" + expression.id + "' has an invalid parameter target");
            }
            if (!targets.insert(*target.parameter_index).second) {
                throw std::invalid_argument(
                    "expression '" + expression.id + "' repeats a parameter target");
            }
        }
    }

    std::unordered_set<std::size_t> lip_targets;
    for (LipSyncMappingDefinition& mapping : parameter_model_.lip_sync.mappings) {
        mapping.parameter_index = find_parameter_index(mapping.parameter);
        if (!mapping.parameter_index.has_value() || !std::isfinite(mapping.scale) ||
            !std::isfinite(mapping.bias) || !std::isfinite(mapping.smoothing) ||
            !std::isfinite(mapping.attack) || !std::isfinite(mapping.release) ||
            mapping.smoothing < 0.0 || mapping.attack < 0.0 || mapping.release < 0.0) {
            throw std::invalid_argument("lip-sync mapping has invalid target or filter values");
        }
        if (!lip_targets.insert(*mapping.parameter_index).second) {
            throw std::invalid_argument(
                "lip-sync mappings must not repeat target parameter '" + mapping.parameter + "'");
        }
        std::unordered_set<std::string> phonemes;
        for (const PhonemeValueDefinition& phoneme : mapping.phoneme_map) {
            if (phoneme.phoneme.empty() || !std::isfinite(phoneme.value) ||
                !phonemes.insert(phoneme.phoneme).second) {
                throw std::invalid_argument("lip-sync phoneme map contains an invalid entry");
            }
        }
    }

    slot_parameter_dependencies_.assign(slots_.size(), {});
    for (std::size_t slot_index = 0; slot_index < slots_.size(); ++slot_index) {
        std::vector<std::size_t>& dependencies = slot_parameter_dependencies_[slot_index];
        for (const std::size_t shape_index : slot_parameter_shape_indices_[slot_index]) {
            append_unique(
                &dependencies,
                *parameter_model_.parameter_shapes[shape_index].parameter_index);
        }
        if (slot_parameter_deformer_indices_[slot_index].has_value()) {
            const ParameterDeformerDefinition* deformer =
                &parameter_model_.parameter_deformers[
                    *slot_parameter_deformer_indices_[slot_index]];
            for (const ParameterBindingDefinition& binding : deformer->parameter_bindings) {
                append_unique(&dependencies, *binding.parameter_index);
            }
            if (deformer->parent_index.has_value()) {
                deformer = &parameter_model_.parameter_deformers[*deformer->parent_index];
                for (const ParameterBindingDefinition& binding : deformer->parameter_bindings) {
                    append_unique(&dependencies, *binding.parameter_index);
                }
            }
        }
        std::sort(dependencies.begin(), dependencies.end());
    }

    parameter_dependency_word_count_ = (parameter_model_.parameters.size() + 63U) / 64U;
    slot_parameter_dependency_words_.assign(
        slots_.size() * parameter_dependency_word_count_,
        0U);
    parameter_affected_slots_.assign(parameter_model_.parameters.size(), {});
    for (std::size_t slot_index = 0; slot_index < slot_parameter_dependencies_.size(); ++slot_index) {
        for (const std::size_t parameter_index : slot_parameter_dependencies_[slot_index]) {
            if (parameter_dependency_word_count_ != 0U) {
                slot_parameter_dependency_words_[
                    slot_index * parameter_dependency_word_count_ + parameter_index / 64U] |=
                    std::uint64_t{1} << (parameter_index % 64U);
            }
            parameter_affected_slots_[parameter_index].push_back(slot_index);
        }
    }

    art_path_parameter_dependencies_.assign(parameter_model_.art_paths.size(), {});
    for (std::size_t index = 0; index < parameter_model_.art_paths.size(); ++index) {
        const ArtPathDefinition& art_path = parameter_model_.art_paths[index];
        std::vector<std::size_t>& dependencies = art_path_parameter_dependencies_[index];
        if (art_path.parameter_keyforms.has_value()) {
            append_unique(&dependencies, *art_path.parameter_keyforms->parameter_index);
        }
        if (art_path.parent_deformer_index.has_value()) {
            const ParameterDeformerDefinition* deformer =
                &parameter_model_.parameter_deformers[*art_path.parent_deformer_index];
            for (const ParameterBindingDefinition& binding : deformer->parameter_bindings) {
                append_unique(&dependencies, *binding.parameter_index);
            }
            if (deformer->parent_index.has_value()) {
                deformer = &parameter_model_.parameter_deformers[*deformer->parent_index];
                for (const ParameterBindingDefinition& binding : deformer->parameter_bindings) {
                    append_unique(&dependencies, *binding.parameter_index);
                }
            }
        }
        std::sort(dependencies.begin(), dependencies.end());
    }
}

const std::vector<ParameterDefinition>& SkeletonData::parameters() const {
    return parameter_model_.parameters;
}

const std::vector<ParameterGroupDefinition>& SkeletonData::parameter_groups() const {
    return parameter_model_.parameter_groups;
}

const std::vector<ParameterShapeDefinition>& SkeletonData::parameter_shapes() const {
    return parameter_model_.parameter_shapes;
}

const std::vector<ParameterDeformerDefinition>& SkeletonData::parameter_deformers() const {
    return parameter_model_.parameter_deformers;
}

const std::vector<ArtPathDefinition>& SkeletonData::art_paths() const {
    return parameter_model_.art_paths;
}

const std::vector<ExpressionDefinition>& SkeletonData::expressions() const {
    return parameter_model_.expressions;
}

const LipSyncDefinition& SkeletonData::lip_sync() const {
    return parameter_model_.lip_sync;
}

std::optional<std::size_t> SkeletonData::find_parameter_index(std::string_view id) const {
    const auto found = parameter_indices_.find(std::string(id));
    return found == parameter_indices_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<std::size_t> SkeletonData::find_parameter_group_index(std::string_view id) const {
    const auto found = parameter_group_indices_.find(std::string(id));
    return found == parameter_group_indices_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<std::size_t> SkeletonData::find_parameter_shape_index(std::string_view id) const {
    const auto found = parameter_shape_indices_.find(std::string(id));
    return found == parameter_shape_indices_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<std::size_t> SkeletonData::find_parameter_deformer_index(std::string_view id) const {
    const auto found = parameter_deformer_indices_.find(std::string(id));
    return found == parameter_deformer_indices_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<std::size_t> SkeletonData::find_art_path_index(std::string_view id) const {
    const auto found = art_path_indices_.find(std::string(id));
    return found == art_path_indices_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<std::size_t> SkeletonData::find_expression_index(std::string_view id) const {
    const auto found = expression_indices_.find(std::string(id));
    return found == expression_indices_.end() ? std::nullopt : std::optional{found->second};
}

const std::vector<std::size_t>& SkeletonData::parameter_affected_slots(
    std::size_t parameter_index) const {
    static const std::vector<std::size_t> kEmpty;
    return parameter_index < parameter_affected_slots_.size()
        ? parameter_affected_slots_[parameter_index]
        : kEmpty;
}

bool SkeletonData::parameter_affects_slot(
    std::size_t parameter_index,
    std::size_t slot_index) const {
    if (parameter_index >= parameter_model_.parameters.size() || slot_index >= slots_.size() ||
        parameter_dependency_word_count_ == 0U) {
        return false;
    }
    const std::uint64_t word = slot_parameter_dependency_words_[
        slot_index * parameter_dependency_word_count_ + parameter_index / 64U];
    return (word & (std::uint64_t{1} << (parameter_index % 64U))) != 0U;
}

const std::vector<double>& Skeleton::parameter_values() const {
    return parameter_values_;
}

const std::vector<double>& Skeleton::direct_parameter_values() const {
    return direct_parameter_values_;
}

double Skeleton::normalize_parameter_value(std::size_t index, double value) const {
    const ParameterDefinition& definition = data_->parameters()[index];
    if (definition.type == ParameterType::Discrete) {
        value = std::round(value);
    }
    if (definition.clamp) {
        value = std::clamp(value, definition.min_value, definition.max_value);
    }
    return value;
}

bool Skeleton::set_parameter_value(std::string_view id, double value) {
    const std::optional<std::size_t> index = data_->find_parameter_index(id);
    if (!index.has_value()) {
        report_error("Unknown parameter id '" + std::string(id) + "'.");
        return false;
    }
    return set_parameter_value(*index, value);
}

bool Skeleton::set_parameter_value(std::size_t index, double value) {
    if (index >= direct_parameter_values_.size()) {
        report_error("Parameter index " + std::to_string(index) + " is out of range.");
        return false;
    }
    if (!std::isfinite(value)) {
        report_error("Parameter values must be finite.");
        return false;
    }
    if (direct_parameter_values_[index] == value) {
        return true;
    }
    direct_parameter_values_[index] = value;
    const double normalized = normalize_parameter_value(index, value);
    if (parameter_values_[index] != normalized) {
        parameter_values_[index] = normalized;
        ++parameter_value_revisions_[index];
        ++parameter_revision_;
    }
    return true;
}

void Skeleton::reset_parameters() {
    bool final_changed = false;
    for (std::size_t index = 0; index < data_->parameters().size(); ++index) {
        direct_parameter_values_[index] = data_->parameters()[index].default_value;
        const double value = normalize_parameter_value(
            index,
            data_->parameters()[index].default_value);
        if (parameter_values_[index] != value) {
            parameter_values_[index] = value;
            ++parameter_value_revisions_[index];
            final_changed = true;
        }
    }
    if (final_changed) {
        ++parameter_revision_;
    }
}

std::uint64_t Skeleton::parameter_revision() const {
    return parameter_revision_;
}

bool Skeleton::apply_composed_parameter_values(const std::vector<double>& values) {
    if (values.size() != parameter_values_.size()) {
        report_error("Composed parameter buffer size does not match SkeletonData.");
        return false;
    }
    std::vector<double> normalized(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            report_error("Composed parameter values must be finite.");
            return false;
        }
        normalized[index] = normalize_parameter_value(index, values[index]);
    }

    bool changed = false;
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        if (parameter_values_[index] == normalized[index]) {
            continue;
        }
        parameter_values_[index] = normalized[index];
        ++parameter_value_revisions_[index];
        changed = true;
    }
    if (changed) {
        ++parameter_revision_;
    }
    return true;
}

const std::vector<double>* Skeleton::current_final_mesh_vertex_offsets(
    std::size_t slot_index) const {
    if (slot_index >= final_mesh_offset_caches_.size() ||
        slot_index >= data_->slots().size()) {
        return nullptr;
    }
    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        final_mesh_offset_caches_[slot_index].valid = false;
        return nullptr;
    }

    FinalMeshOffsetCache& cache = final_mesh_offset_caches_[slot_index];
    const std::vector<double>* animation_offsets = current_mesh_vertex_offsets(slot_index);
    const std::vector<double> empty_offsets;
    const std::vector<double>& animation_snapshot =
        animation_offsets != nullptr ? *animation_offsets : empty_offsets;
    const std::vector<std::size_t>& dependencies =
        data_->slot_parameter_dependencies_[slot_index];
    if (cache.valid && cache.attachment == attachment &&
        cache.animation_offsets == animation_snapshot &&
        revisions_match(cache.parameter_revisions, dependencies, parameter_value_revisions_)) {
        return cache.has_offsets ? &cache.final_offsets : nullptr;
    }

    cache.attachment = attachment;
    cache.animation_offsets = animation_snapshot;
    capture_revisions(dependencies, parameter_value_revisions_, &cache.parameter_revisions);
    cache.final_offsets.assign(attachment->mesh_geometry->vertices.size(), 0.0);
    cache.has_offsets = animation_offsets != nullptr;
    if (animation_offsets != nullptr) {
        cache.final_offsets = *animation_offsets;
    }

    std::vector<double> sampled_shape;
    // normalized_override always replaces the complete animation FFD layer before any
    // additive_clamped shapes, independent of the override's declaration position.
    for (const std::size_t shape_index : data_->slot_parameter_shape_indices_[slot_index]) {
        const ParameterShapeDefinition& shape = data_->parameter_shapes()[shape_index];
        if (shape.blend_mode != ParameterShapeBlendMode::NormalizedOverride ||
            !detail::attachment_matches_mesh_deform_source(
                *attachment,
                shape.target_attachment)) {
            continue;
        }
        sample_shape(shape, parameter_values_[*shape.parameter_index], &sampled_shape);
        cache.final_offsets = sampled_shape;
        cache.has_offsets = true;
    }
    for (const std::size_t shape_index : data_->slot_parameter_shape_indices_[slot_index]) {
        const ParameterShapeDefinition& shape = data_->parameter_shapes()[shape_index];
        if (shape.blend_mode != ParameterShapeBlendMode::AdditiveClamped ||
            !detail::attachment_matches_mesh_deform_source(
                *attachment,
                shape.target_attachment)) {
            continue;
        }
        sample_shape(shape, parameter_values_[*shape.parameter_index], &sampled_shape);
        for (std::size_t index = 0; index < cache.final_offsets.size(); ++index) {
            cache.final_offsets[index] += sampled_shape[index];
        }
        cache.has_offsets = true;
    }

    if (data_->slot_parameter_deformer_indices_[slot_index].has_value()) {
        const std::size_t deformer_index =
            *data_->slot_parameter_deformer_indices_[slot_index];
        const std::vector<double>& base_vertices = attachment->mesh_geometry->vertices;
        for (std::size_t index = 0; index < base_vertices.size(); index += 2U) {
            const AttachmentVertex input{
                base_vertices[index] + cache.final_offsets[index],
                base_vertices[index + 1U] + cache.final_offsets[index + 1U],
            };
            const AttachmentVertex output = apply_deformer(
                data_->parameter_model_,
                deformer_index,
                parameter_values_,
                input);
            cache.final_offsets[index] = output.x - base_vertices[index];
            cache.final_offsets[index + 1U] = output.y - base_vertices[index + 1U];
        }
        cache.has_offsets = true;
    }

    cache.valid = true;
    return cache.has_offsets ? &cache.final_offsets : nullptr;
}

const std::vector<EvaluatedArtPath>& Skeleton::current_art_paths() const {
    for (std::size_t index = 0; index < data_->art_paths().size(); ++index) {
        ArtPathEvaluationCache& cache = art_path_evaluation_caches_[index];
        const std::vector<std::size_t>& dependencies =
            data_->art_path_parameter_dependencies_[index];
        if (!cache.valid ||
            !revisions_match(cache.parameter_revisions, dependencies, parameter_value_revisions_)) {
            const ArtPathDefinition& definition = data_->art_paths()[index];
            sample_art_path(definition, parameter_values_, &cache.value);
            if (definition.parent_deformer_index.has_value()) {
                for (AttachmentVertex& point : cache.value.points) {
                    point = apply_deformer(
                        data_->parameter_model_,
                        *definition.parent_deformer_index,
                        parameter_values_,
                        point);
                }
            }
            capture_revisions(
                dependencies,
                parameter_value_revisions_,
                &cache.parameter_revisions);
            cache.valid = true;
            evaluated_art_paths_[index] = cache.value;
        }
    }
    return evaluated_art_paths_;
}

} // namespace marrow::runtime
