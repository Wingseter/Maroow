#include "marrow/runtime/skeleton.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace marrow::runtime {
namespace {

using json::Document;
using json::LoadError;
using json::SourceLocation;
using json::Value;

struct ParsedBoneData {
    BoneData bone;
    std::optional<std::string> parent_name;
    SourceLocation parent_location{};
};

struct PathDistanceSample {
    double distance{0.0};
    AttachmentVertex point{};
    AttachmentVertex tangent{};
};

LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    return json::make_validation_error(
        document, location, std::move(json_path), std::move(message));
}

const Value* find_optional_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    return json::find_member(object, key);
}

double degrees_to_radians(double degrees) {
    constexpr double kPi = 3.14159265358979323846;
    return degrees * kPi / 180.0;
}

double radians_to_degrees(double radians) {
    constexpr double kPi = 3.14159265358979323846;
    return radians * 180.0 / kPi;
}

double clamp_mix(double mix) {
    return std::clamp(mix, 0.0, 1.0);
}

double normalize_rotation_degrees(double angle) {
    while (angle > 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

double mix_rotation_degrees(double current, double target, double mix) {
    return current + (normalize_rotation_degrees(target - current) * clamp_mix(mix));
}

double mix_scalar(double current, double target, double mix) {
    return current + ((target - current) * clamp_mix(mix));
}

BoneWorldTransform identity_world_transform() {
    return BoneWorldTransform{};
}

AttachmentVertex inverse_transform_point(
    const BoneWorldTransform& transform,
    double world_x,
    double world_y) {
    constexpr double kEpsilon = 1e-8;
    const double determinant = (transform.a * transform.d) - (transform.b * transform.c);
    if (std::abs(determinant) <= kEpsilon) {
        return {};
    }

    const double inverse_determinant = 1.0 / determinant;
    const double translated_x = world_x - transform.world_x;
    const double translated_y = world_y - transform.world_y;
    return {
        ((translated_x * transform.d) - (translated_y * transform.b)) * inverse_determinant,
        ((translated_y * transform.a) - (translated_x * transform.c)) * inverse_determinant};
}

AttachmentVertex inverse_transform_vector(
    const BoneWorldTransform& transform,
    double world_x,
    double world_y) {
    constexpr double kEpsilon = 1e-8;
    const double determinant = (transform.a * transform.d) - (transform.b * transform.c);
    if (std::abs(determinant) <= kEpsilon) {
        return {};
    }

    const double inverse_determinant = 1.0 / determinant;
    return {
        ((world_x * transform.d) - (world_y * transform.b)) * inverse_determinant,
        ((world_y * transform.a) - (world_x * transform.c)) * inverse_determinant};
}

BoneWorldTransform local_world_transform(const BoneTransform& transform) {
    const double rotation_x = degrees_to_radians(transform.rotation + transform.shear_x);
    const double rotation_y = degrees_to_radians(transform.rotation + 90.0 + transform.shear_y);

    BoneWorldTransform world_transform;
    world_transform.a = std::cos(rotation_x) * transform.scale_x;
    world_transform.b = std::cos(rotation_y) * transform.scale_y;
    world_transform.c = std::sin(rotation_x) * transform.scale_x;
    world_transform.d = std::sin(rotation_y) * transform.scale_y;
    world_transform.world_x = transform.x;
    world_transform.world_y = transform.y;
    return world_transform;
}

double axis_length(double x, double y) {
    return std::hypot(x, y);
}

double signed_axis_scale(double x, double y) {
    const double length = axis_length(x, y);
    if (length <= 1e-8) {
        return 1.0;
    }

    const double dominant_component = std::abs(x) >= std::abs(y) ? x : y;
    return dominant_component < 0.0 ? -length : length;
}

BoneWorldTransform normalized_parent_axes(const BoneWorldTransform& parent) {
    BoneWorldTransform normalized{};

    const double x_length = axis_length(parent.a, parent.c);
    if (x_length > 1e-8) {
        normalized.a = parent.a / x_length;
        normalized.c = parent.c / x_length;
    } else {
        normalized.a = 1.0;
        normalized.c = 0.0;
    }

    const double y_length = axis_length(parent.b, parent.d);
    if (y_length > 1e-8) {
        normalized.b = parent.b / y_length;
        normalized.d = parent.d / y_length;
    } else {
        normalized.b = 0.0;
        normalized.d = 1.0;
    }

    return normalized;
}

BoneWorldTransform compose_world_transform(
    const BoneWorldTransform& parent,
    const BonePose& pose) {
    const BoneTransform& local_pose = pose.local_pose;
    const BoneWorldTransform local = local_world_transform(local_pose);

    BoneWorldTransform world_transform;
    world_transform.world_x = parent.a * local_pose.x + parent.b * local_pose.y + parent.world_x;
    world_transform.world_y = parent.c * local_pose.x + parent.d * local_pose.y + parent.world_y;

    BoneWorldTransform parent_basis{};
    if (pose.inherit.inherit_rotation && pose.inherit.inherit_scale) {
        parent_basis = parent;
    } else if (pose.inherit.inherit_rotation) {
        parent_basis = normalized_parent_axes(parent);
    } else if (pose.inherit.inherit_scale) {
        parent_basis.a = signed_axis_scale(parent.a, parent.c);
        parent_basis.b = 0.0;
        parent_basis.c = 0.0;
        parent_basis.d = signed_axis_scale(parent.b, parent.d);
    } else {
        parent_basis = identity_world_transform();
    }
    if (!pose.inherit.inherit_reflection) {
        const double parent_determinant =
            (parent_basis.a * parent_basis.d) - (parent_basis.b * parent_basis.c);
        if (parent_determinant < 0.0) {
            parent_basis.b = -parent_basis.b;
            parent_basis.d = -parent_basis.d;
        }
    }

    world_transform.a = parent_basis.a * local.a + parent_basis.b * local.c;
    world_transform.b = parent_basis.a * local.b + parent_basis.b * local.d;
    world_transform.c = parent_basis.c * local.a + parent_basis.d * local.c;
    world_transform.d = parent_basis.c * local.b + parent_basis.d * local.d;
    return world_transform;
}

AttachmentVertex transform_attachment_vertex(
    const BoneWorldTransform& transform,
    double x,
    double y) {
    return {
        transform.a * x + transform.b * y + transform.world_x,
        transform.c * x + transform.d * y + transform.world_y};
}

double transform_attachment_rotation(
    const BoneWorldTransform& transform,
    double local_rotation) {
    const double local_radians = degrees_to_radians(local_rotation);
    const double direction_x = std::cos(local_radians);
    const double direction_y = std::sin(local_radians);
    const double world_direction_x = transform.a * direction_x + transform.b * direction_y;
    const double world_direction_y = transform.c * direction_x + transform.d * direction_y;
    return radians_to_degrees(std::atan2(world_direction_y, world_direction_x));
}

MeshWorldVertex transform_mesh_point(
    const BoneWorldTransform& transform,
    double x,
    double y) {
    const AttachmentVertex transformed = transform_attachment_vertex(transform, x, y);
    return {transformed.x, transformed.y};
}

std::size_t clamp_sequence_frame_index(
    const AttachmentSequenceData& sequence,
    std::size_t frame_index) {
    if (sequence.frame_regions.empty()) {
        return 0;
    }

    return std::min(frame_index, sequence.frame_regions.size() - 1);
}

std::size_t positive_mod(long long value, std::size_t modulus) {
    if (modulus == 0) {
        return 0;
    }

    const long long wrapped = value % static_cast<long long>(modulus);
    if (wrapped < 0) {
        return static_cast<std::size_t>(wrapped + static_cast<long long>(modulus));
    }

    return static_cast<std::size_t>(wrapped);
}

std::size_t map_ping_pong_frame(std::size_t cycle_position, std::size_t frame_count) {
    if (frame_count <= 1) {
        return 0;
    }

    const std::size_t max_index = frame_count - 1;
    return cycle_position <= max_index ? cycle_position : (max_index * 2) - cycle_position;
}

std::optional<SequencePlaybackMode> parse_sequence_playback_mode(std::string_view mode_name) {
    if (mode_name == "hold") {
        return SequencePlaybackMode::Hold;
    }
    if (mode_name == "once") {
        return SequencePlaybackMode::Once;
    }
    if (mode_name == "loop") {
        return SequencePlaybackMode::Loop;
    }
    if (mode_name == "pingpong") {
        return SequencePlaybackMode::PingPong;
    }
    if (mode_name == "once_reverse") {
        return SequencePlaybackMode::OnceReverse;
    }
    if (mode_name == "loop_reverse") {
        return SequencePlaybackMode::LoopReverse;
    }
    if (mode_name == "pingpong_reverse") {
        return SequencePlaybackMode::PingPongReverse;
    }

    return std::nullopt;
}

std::optional<PathConstraintSpacingMode> parse_path_constraint_spacing_mode(
    std::string_view mode_name) {
    if (mode_name == "length") {
        return PathConstraintSpacingMode::Length;
    }
    if (mode_name == "percent") {
        return PathConstraintSpacingMode::Percent;
    }

    return std::nullopt;
}

std::optional<std::size_t> find_bone_index(
    const std::vector<BoneData>& bones,
    std::string_view name) {
    const auto it = std::find_if(
        bones.begin(),
        bones.end(),
        [&](const BoneData& bone) {
            return bone.name == name;
        });
    if (it == bones.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(bones.begin(), it));
}

std::optional<std::size_t> find_slot_index(
    const std::vector<SlotData>& slots,
    std::string_view name) {
    const auto it = std::find_if(
        slots.begin(),
        slots.end(),
        [&](const SlotData& slot) {
            return slot.name == name;
        });
    if (it == slots.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(slots.begin(), it));
}

std::optional<std::size_t> find_skin_index(
    const std::vector<SkinData>& skins,
    std::string_view name) {
    const auto it = std::find_if(
        skins.begin(),
        skins.end(),
        [&](const SkinData& skin) {
            return skin.name == name;
        });
    if (it == skins.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(skins.begin(), it));
}

std::optional<std::size_t> find_event_index(
    const std::vector<EventDefinition>& events,
    std::string_view name) {
    const auto it = std::find_if(
        events.begin(),
        events.end(),
        [&](const EventDefinition& event_definition) {
            return event_definition.name == name;
        });
    if (it == events.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(events.begin(), it));
}

template <typename NamedValue>
std::optional<std::size_t> find_named_index(
    const std::vector<NamedValue>& values,
    std::string_view name) {
    const auto it = std::find_if(
        values.begin(),
        values.end(),
        [&](const NamedValue& value) {
            return value.name == name;
        });
    if (it == values.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(values.begin(), it));
}

const AnimationData* find_animation(
    const std::vector<AnimationData>& animations,
    std::string_view name) {
    const auto it = std::find_if(
        animations.begin(),
        animations.end(),
        [&](const AnimationData& animation) {
            return animation.name == name;
        });
    if (it == animations.end()) {
        return nullptr;
    }

    return &(*it);
}

const AttachmentData* find_attachment_source_in_skins(
    const std::vector<SkinData>& skins,
    std::optional<std::size_t> default_skin_index,
    std::size_t slot_index,
    std::string_view attachment_name,
    std::optional<std::size_t>* skin_index_out = nullptr) {
    if (skin_index_out != nullptr) {
        skin_index_out->reset();
    }

    const auto find_in_skin = [&](std::size_t skin_index) -> const AttachmentData* {
        const AttachmentData* attachment = skins[skin_index].find_attachment(slot_index);
        if (attachment != nullptr && attachment->name == attachment_name) {
            if (skin_index_out != nullptr) {
                *skin_index_out = skin_index;
            }
            return attachment;
        }

        attachment = skins[skin_index].find_attachment(attachment_name);
        if (attachment != nullptr) {
            if (skin_index_out != nullptr) {
                *skin_index_out = skin_index;
            }
            return attachment;
        }

        return nullptr;
    };

    if (default_skin_index.has_value() && *default_skin_index < skins.size()) {
        if (const AttachmentData* attachment = find_in_skin(*default_skin_index)) {
            return attachment;
        }
    }

    for (std::size_t skin_index = 0; skin_index < skins.size(); ++skin_index) {
        if (default_skin_index.has_value() && skin_index == *default_skin_index) {
            continue;
        }
        if (const AttachmentData* attachment = find_in_skin(skin_index)) {
            return attachment;
        }
    }

    return nullptr;
}

bool is_skin_reserved_key(std::string_view key) {
    return key == "bones" || key == "ik" || key == "path" ||
        key == "transform" || key == "physics";
}

bool attachment_matches_mesh_deform_source(
    const AttachmentData& attachment,
    std::string_view attachment_name) {
    if (attachment.name == attachment_name) {
        return true;
    }

    return attachment.linked_mesh.has_value() &&
        attachment.linked_mesh->deform &&
        attachment.linked_mesh->parent_attachment == attachment_name;
}

AttachmentVertex add_vertices(
    const AttachmentVertex& lhs,
    const AttachmentVertex& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

AttachmentVertex subtract_vertices(
    const AttachmentVertex& lhs,
    const AttachmentVertex& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

AttachmentVertex scale_vertex(const AttachmentVertex& vertex, double scalar) {
    return {vertex.x * scalar, vertex.y * scalar};
}

double vertex_length(const AttachmentVertex& vertex) {
    return std::hypot(vertex.x, vertex.y);
}

AttachmentVertex cubic_bezier_point(
    const AttachmentVertex& p0,
    const AttachmentVertex& p1,
    const AttachmentVertex& p2,
    const AttachmentVertex& p3,
    double t) {
    const double inverse_t = 1.0 - t;
    const double inverse_t2 = inverse_t * inverse_t;
    const double inverse_t3 = inverse_t2 * inverse_t;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return {
        inverse_t3 * p0.x + 3.0 * inverse_t2 * t * p1.x +
            3.0 * inverse_t * t2 * p2.x + t3 * p3.x,
        inverse_t3 * p0.y + 3.0 * inverse_t2 * t * p1.y +
            3.0 * inverse_t * t2 * p2.y + t3 * p3.y};
}

AttachmentVertex cubic_bezier_tangent(
    const AttachmentVertex& p0,
    const AttachmentVertex& p1,
    const AttachmentVertex& p2,
    const AttachmentVertex& p3,
    double t) {
    const double inverse_t = 1.0 - t;
    return {
        3.0 * inverse_t * inverse_t * (p1.x - p0.x) +
            6.0 * inverse_t * t * (p2.x - p1.x) +
            3.0 * t * t * (p3.x - p2.x),
        3.0 * inverse_t * inverse_t * (p1.y - p0.y) +
            6.0 * inverse_t * t * (p2.y - p1.y) +
            3.0 * t * t * (p3.y - p2.y)};
}

std::vector<PathDistanceSample> build_path_distance_samples(
    const std::vector<AttachmentVertex>& control_points) {
    constexpr std::size_t kSubdivisionsPerSegment = 32;

    std::vector<PathDistanceSample> samples;
    if (control_points.size() < 4) {
        return samples;
    }

    AttachmentVertex previous_point = control_points.front();
    AttachmentVertex previous_tangent = subtract_vertices(control_points[1], control_points[0]);
    if (vertex_length(previous_tangent) <= 1e-8) {
        previous_tangent = {1.0, 0.0};
    }
    samples.push_back(PathDistanceSample{0.0, previous_point, previous_tangent});

    double accumulated_distance = 0.0;
    for (std::size_t point_index = 0; point_index + 3 < control_points.size(); point_index += 3) {
        const AttachmentVertex& p0 = control_points[point_index];
        const AttachmentVertex& p1 = control_points[point_index + 1];
        const AttachmentVertex& p2 = control_points[point_index + 2];
        const AttachmentVertex& p3 = control_points[point_index + 3];

        for (std::size_t step = 1; step <= kSubdivisionsPerSegment; ++step) {
            const double t =
                static_cast<double>(step) / static_cast<double>(kSubdivisionsPerSegment);
            const AttachmentVertex point = cubic_bezier_point(p0, p1, p2, p3, t);
            AttachmentVertex tangent = cubic_bezier_tangent(p0, p1, p2, p3, t);
            if (vertex_length(tangent) <= 1e-8) {
                tangent = subtract_vertices(point, previous_point);
            }

            accumulated_distance += vertex_length(subtract_vertices(point, previous_point));
            samples.push_back(PathDistanceSample{accumulated_distance, point, tangent});
            previous_point = point;
        }
    }

    return samples;
}

PathDistanceSample sample_path_distance(
    const std::vector<PathDistanceSample>& samples,
    double distance) {
    if (samples.empty()) {
        return {};
    }
    if (samples.size() == 1 || distance <= samples.front().distance) {
        return samples.front();
    }
    if (distance >= samples.back().distance) {
        return samples.back();
    }

    const auto upper = std::upper_bound(
        samples.begin(),
        samples.end(),
        distance,
        [](double probe_distance, const PathDistanceSample& sample) {
            return probe_distance < sample.distance;
        });
    const std::size_t upper_index =
        static_cast<std::size_t>(std::distance(samples.begin(), upper));
    const PathDistanceSample& previous = samples[upper_index - 1];
    const PathDistanceSample& current = samples[upper_index];
    const double range = current.distance - previous.distance;
    const double alpha = range > 0.0 ? (distance - previous.distance) / range : 0.0;
    return {
        distance,
        add_vertices(
            previous.point,
            scale_vertex(subtract_vertices(current.point, previous.point), alpha)),
        add_vertices(
            previous.tangent,
            scale_vertex(subtract_vertices(current.tangent, previous.tangent), alpha))};
}

template <typename Keyframe>
double timeline_duration(const std::vector<Keyframe>& keyframes) {
    if (keyframes.empty()) {
        return 0.0;
    }

    return keyframes.back().time;
}

bool point_on_segment(
    const AttachmentVertex& start,
    const AttachmentVertex& end,
    double x,
    double y) {
    constexpr double kTolerance = 1e-6;

    const double cross =
        (x - start.x) * (end.y - start.y) -
        (y - start.y) * (end.x - start.x);
    if (std::abs(cross) > kTolerance) {
        return false;
    }

    const double min_x = std::min(start.x, end.x) - kTolerance;
    const double max_x = std::max(start.x, end.x) + kTolerance;
    const double min_y = std::min(start.y, end.y) - kTolerance;
    const double max_y = std::max(start.y, end.y) + kTolerance;
    return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
}

bool polygon_contains_point(
    const std::vector<AttachmentVertex>& polygon,
    double x,
    double y) {
    if (polygon.size() < 3) {
        return false;
    }

    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const AttachmentVertex& start = polygon[index];
        const AttachmentVertex& end = polygon[(index + 1) % polygon.size()];
        if (point_on_segment(start, end, x, y)) {
            return true;
        }
    }

    bool inside = false;
    for (std::size_t index = 0, previous = polygon.size() - 1;
         index < polygon.size();
         previous = index++) {
        const AttachmentVertex& current = polygon[index];
        const AttachmentVertex& prior = polygon[previous];
        const bool crosses_scanline = (current.y > y) != (prior.y > y);
        if (!crosses_scanline) {
            continue;
        }

        const double intersect_x =
            ((prior.x - current.x) * (y - current.y) / (prior.y - current.y)) + current.x;
        if (x < intersect_x) {
            inside = !inside;
        }
    }

    return inside;
}

double signed_area_twice(
    const AttachmentVertex& a,
    const AttachmentVertex& b,
    const AttachmentVertex& c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

bool segments_intersect(
    const AttachmentVertex& a,
    const AttachmentVertex& b,
    const AttachmentVertex& c,
    const AttachmentVertex& d) {
    constexpr double kTolerance = 1e-6;

    const double ab_c = signed_area_twice(a, b, c);
    const double ab_d = signed_area_twice(a, b, d);
    const double cd_a = signed_area_twice(c, d, a);
    const double cd_b = signed_area_twice(c, d, b);

    if (std::abs(ab_c) <= kTolerance && point_on_segment(a, b, c.x, c.y)) {
        return true;
    }
    if (std::abs(ab_d) <= kTolerance && point_on_segment(a, b, d.x, d.y)) {
        return true;
    }
    if (std::abs(cd_a) <= kTolerance && point_on_segment(c, d, a.x, a.y)) {
        return true;
    }
    if (std::abs(cd_b) <= kTolerance && point_on_segment(c, d, b.x, b.y)) {
        return true;
    }

    const bool ab_straddles = (ab_c > kTolerance && ab_d < -kTolerance) ||
        (ab_c < -kTolerance && ab_d > kTolerance);
    const bool cd_straddles = (cd_a > kTolerance && cd_b < -kTolerance) ||
        (cd_a < -kTolerance && cd_b > kTolerance);
    return ab_straddles && cd_straddles;
}

bool polygon_intersects_segment(
    const std::vector<AttachmentVertex>& polygon,
    const AttachmentVertex& start,
    const AttachmentVertex& end) {
    if (polygon.size() < 2) {
        return false;
    }

    if (polygon_contains_point(polygon, start.x, start.y) ||
        polygon_contains_point(polygon, end.x, end.y)) {
        return true;
    }

    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const AttachmentVertex& edge_start = polygon[index];
        const AttachmentVertex& edge_end = polygon[(index + 1) % polygon.size()];
        if (segments_intersect(start, end, edge_start, edge_end)) {
            return true;
        }
    }

    return false;
}

std::optional<LoadError> read_required_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::string* value_out) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document, object, key, Value::Type::String, json_path, &member)) {
        return error;
    }

    *value_out = member->as_string();
    return std::nullopt;
}

std::optional<LoadError> read_optional_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::optional<std::string>* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        value_out->reset();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::String,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    *value_out = member->as_string();
    return std::nullopt;
}

std::optional<LoadError> read_required_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    double* value_out) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document, object, key, Value::Type::Number, json_path, &member)) {
        return error;
    }

    *value_out = member->as_number();
    return std::nullopt;
}

std::optional<LoadError> read_optional_boolean(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    bool* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Boolean,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    *value_out = member->as_boolean();
    return std::nullopt;
}

std::optional<LoadError> read_optional_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    double* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Number,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    *value_out = member->as_number();
    return std::nullopt;
}

std::optional<LoadError> parse_optional_xy_vector(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    AttachmentVertex* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Object,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    if (const auto error = read_required_number(
            document,
            *member,
            "x",
            std::string(json_path) + "." + std::string(key),
            &value_out->x)) {
        return error;
    }
    if (const auto error = read_required_number(
            document,
            *member,
            "y",
            std::string(json_path) + "." + std::string(key),
            &value_out->y)) {
        return error;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_hex_color(
    const Document& document,
    const Value& color_value,
    std::string_view json_path,
    SlotColor* color_out) {
    if (const auto error = json::require_type(
            document,
            color_value,
            Value::Type::String,
            json_path)) {
        return error;
    }

    const std::string_view encoded = color_value.as_string();
    if (encoded.size() != 6 && encoded.size() != 8) {
        return validation_error(
            document,
            color_value.location(),
            std::string(json_path),
            "slot tint colors must be 6- or 8-digit hexadecimal strings");
    }

    auto parse_nibble = [](char character) -> std::optional<int> {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }

        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (lower >= 'a' && lower <= 'f') {
            return 10 + (lower - 'a');
        }

        return std::nullopt;
    };

    auto parse_byte = [&](std::size_t offset) -> std::optional<double> {
        const auto high = parse_nibble(encoded[offset]);
        const auto low = parse_nibble(encoded[offset + 1]);
        if (!high.has_value() || !low.has_value()) {
            return std::nullopt;
        }

        return static_cast<double>((*high * 16) + *low) / 255.0;
    };

    const auto red = parse_byte(0);
    const auto green = parse_byte(2);
    const auto blue = parse_byte(4);
    const auto alpha = encoded.size() == 8 ? parse_byte(6) : std::optional<double>{1.0};
    if (!red.has_value() || !green.has_value() || !blue.has_value() || !alpha.has_value()) {
        return validation_error(
            document,
            color_value.location(),
            std::string(json_path),
            "slot tint colors must be 6- or 8-digit hexadecimal strings");
    }

    color_out->r = *red;
    color_out->g = *green;
    color_out->b = *blue;
    color_out->a = *alpha;
    return std::nullopt;
}

std::optional<LoadError> parse_optional_slot_color(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::optional<SlotColor>* color_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        color_out->reset();
        return std::nullopt;
    }

    SlotColor color;
    if (const auto error = parse_hex_color(
            document,
            *member,
            std::string(json_path) + "." + std::string(key),
            &color)) {
        return error;
    }

    *color_out = color;
    return std::nullopt;
}

std::optional<LoadError> parse_slot_color(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    SlotColor* color_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        *color_out = SlotColor{};
        return std::nullopt;
    }

    return parse_hex_color(
        document,
        *member,
        std::string(json_path) + "." + std::string(key),
        color_out);
}

std::optional<LoadError> parse_slot_blend_mode(
    const Document& document,
    const Value& slot_value,
    std::string_view json_path,
    BlendMode* blend_mode_out) {
    *blend_mode_out = BlendMode::Normal;

    const Value* blend_value = find_optional_member(slot_value, "blend");
    if (blend_value == nullptr) {
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *blend_value,
            Value::Type::String,
            std::string(json_path) + ".blend")) {
        return error;
    }

    const std::string_view blend_mode = blend_value->as_string();
    if (blend_mode == "normal") {
        *blend_mode_out = BlendMode::Normal;
        return std::nullopt;
    }
    if (blend_mode == "additive") {
        *blend_mode_out = BlendMode::Additive;
        return std::nullopt;
    }
    if (blend_mode == "multiply") {
        *blend_mode_out = BlendMode::Multiply;
        return std::nullopt;
    }
    if (blend_mode == "screen") {
        *blend_mode_out = BlendMode::Screen;
        return std::nullopt;
    }

    return validation_error(
        document,
        blend_value->location(),
        std::string(json_path) + ".blend",
        "slot blend mode must be one of normal, additive, multiply, or screen");
}

std::optional<LoadError> read_optional_integer(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    int* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Number,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    const double numeric_value = member->as_number();
    if (std::floor(numeric_value) != numeric_value ||
        numeric_value < static_cast<double>(std::numeric_limits<int>::min()) ||
        numeric_value > static_cast<double>(std::numeric_limits<int>::max())) {
        return validation_error(
            document,
            member->location(),
            std::string(json_path) + "." + std::string(key),
            "event integer values must be whole numbers within the runtime int range");
    }

    *value_out = static_cast<int>(numeric_value);
    return std::nullopt;
}

std::optional<LoadError> parse_number_array(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    std::vector<double>* values_out) {
    if (const auto error = json::require_type(
            document,
            value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    values_out->clear();
    values_out->reserve(value.as_array().size());
    for (std::size_t index = 0; index < value.as_array().size(); ++index) {
        const Value& element = value.as_array()[index];
        const std::string element_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                element,
                Value::Type::Number,
                element_path)) {
            return error;
        }

        values_out->push_back(element.as_number());
    }

    return std::nullopt;
}

std::optional<LoadError> parse_polygon(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    std::string_view polygon_label,
    std::vector<AttachmentVertex>* polygon_out) {
    std::vector<double> coordinates;
    if (const auto error = parse_number_array(document, value, json_path, &coordinates)) {
        return error;
    }

    if (coordinates.size() < 6 || coordinates.size() % 2 != 0) {
        return validation_error(
            document,
            value.location(),
            std::string(json_path),
            std::string(polygon_label) + " vertices must contain at least 3 x/y pairs");
    }

    polygon_out->clear();
    polygon_out->reserve(coordinates.size() / 2);
    for (std::size_t index = 0; index < coordinates.size(); index += 2) {
        polygon_out->push_back(AttachmentVertex{coordinates[index], coordinates[index + 1]});
    }

    return std::nullopt;
}

std::optional<LoadError> parse_path_control_points(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    std::vector<AttachmentVertex>* control_points_out) {
    std::vector<double> coordinates;
    if (const auto error = parse_number_array(document, value, json_path, &coordinates)) {
        return error;
    }

    if (coordinates.size() < 8 || coordinates.size() % 2 != 0) {
        return validation_error(
            document,
            value.location(),
            std::string(json_path),
            "path control points must contain at least 4 x/y pairs");
    }

    const std::size_t point_count = coordinates.size() / 2;
    if ((point_count - 1) % 3 != 0) {
        return validation_error(
            document,
            value.location(),
            std::string(json_path),
            "path control points must be encoded as 3n+1 x/y pairs");
    }

    control_points_out->clear();
    control_points_out->reserve(point_count);
    for (std::size_t index = 0; index < coordinates.size(); index += 2) {
        control_points_out->push_back(AttachmentVertex{coordinates[index], coordinates[index + 1]});
    }

    return std::nullopt;
}

std::optional<LoadError> parse_index_array(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    std::vector<std::size_t>* values_out) {
    if (const auto error = json::require_type(
            document,
            value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    values_out->clear();
    values_out->reserve(value.as_array().size());
    for (std::size_t index = 0; index < value.as_array().size(); ++index) {
        const Value& element = value.as_array()[index];
        const std::string element_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                element,
                Value::Type::Number,
                element_path)) {
            return error;
        }

        const double numeric_value = element.as_number();
        if (numeric_value < 0.0 || std::floor(numeric_value) != numeric_value) {
            return validation_error(
                document,
                element.location(),
                element_path,
                "mesh triangle indices must be non-negative integers");
        }

        values_out->push_back(static_cast<std::size_t>(numeric_value));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_mesh_weights(
    const Document& document,
    const Value& value,
    const std::vector<BoneData>& bones,
    std::size_t vertex_count,
    std::string_view json_path,
    std::vector<MeshGeometry::VertexWeights>* weights_out) {
    if (const auto error = json::require_type(
            document,
            value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (value.as_array().size() != vertex_count) {
        return validation_error(
            document,
            value.location(),
            std::string(json_path),
            "mesh weights must contain one vertex influence list per vertex");
    }

    weights_out->clear();
    weights_out->reserve(value.as_array().size());

    for (std::size_t vertex_index = 0; vertex_index < value.as_array().size(); ++vertex_index) {
        const Value& vertex_value = value.as_array()[vertex_index];
        const std::string vertex_path =
            std::string(json_path) + "[" + std::to_string(vertex_index) + "]";
        if (vertex_value.is_number()) {
            const double scalar_weight = vertex_value.as_number();
            if (scalar_weight <= 0.0) {
                return validation_error(
                    document,
                    vertex_value.location(),
                    vertex_path,
                    "mesh shorthand weights must be positive");
            }

            MeshGeometry::VertexWeights vertex_weights;
            // Preserve the legacy scalar shorthand until weighted-mesh fixtures replace it.
            vertex_weights.influences.push_back(MeshGeometry::VertexWeight{
                0,
                0.0,
                0.0,
                scalar_weight});
            weights_out->push_back(std::move(vertex_weights));
            continue;
        }
        if (const auto error = json::require_type(
                document,
                vertex_value,
                Value::Type::Array,
                vertex_path)) {
            return error;
        }
        if (vertex_value.as_array().empty()) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh vertices must have at least one bone influence");
        }
        if (vertex_value.as_array().size() > 4) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh vertices support at most 4 bone influences");
        }

        MeshGeometry::VertexWeights vertex_weights;
        vertex_weights.influences.reserve(vertex_value.as_array().size());

        double total_weight = 0.0;
        for (std::size_t influence_index = 0;
             influence_index < vertex_value.as_array().size();
             ++influence_index) {
            const Value& influence_value = vertex_value.as_array()[influence_index];
            const std::string influence_path =
                vertex_path + "[" + std::to_string(influence_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    influence_value,
                    Value::Type::Object,
                    influence_path)) {
                return error;
            }

            std::string bone_name;
            MeshGeometry::VertexWeight influence;
            if (const auto error = read_required_string(
                    document,
                    influence_value,
                    "bone",
                    influence_path,
                    &bone_name)) {
                return error;
            }
            const auto bone_index = find_bone_index(bones, bone_name);
            if (!bone_index.has_value()) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".bone",
                    "mesh weight references unknown bone '" + bone_name + "'");
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "x",
                    influence_path,
                    &influence.x)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "y",
                    influence_path,
                    &influence.y)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "weight",
                    influence_path,
                    &influence.weight)) {
                return error;
            }
            if (influence.weight <= 0.0) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".weight",
                    "mesh bone weights must be positive");
            }

            influence.bone_index = *bone_index;
            total_weight += influence.weight;
            vertex_weights.influences.push_back(std::move(influence));
        }

        if (total_weight <= 0.0) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh vertex influences must sum to a positive weight");
        }

        weights_out->push_back(std::move(vertex_weights));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_interpolation(
    const Document& document,
    const Value& keyframe_value,
    std::string_view keyframe_path,
    Interpolation* interpolation_out) {
    *interpolation_out = Interpolation::linear();

    const Value* curve_value = find_optional_member(keyframe_value, "curve");
    if (curve_value == nullptr) {
        return std::nullopt;
    }

    const std::string curve_path = std::string(keyframe_path) + ".curve";
    if (curve_value->is_string()) {
        const std::string& curve_name = curve_value->as_string();
        if (curve_name == "linear") {
            *interpolation_out = Interpolation::linear();
            return std::nullopt;
        }
        if (curve_name == "stepped") {
            *interpolation_out = Interpolation::stepped();
            return std::nullopt;
        }

        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be 'linear', 'stepped', or a 4-number bezier array");
    }

    if (!curve_value->is_array()) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be 'linear', 'stepped', or a 4-number bezier array");
    }

    const Value::Array& control_points = curve_value->as_array();
    if (control_points.size() != 4) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "bezier curve arrays must contain exactly 4 control point numbers");
    }

    double coordinates[4] = {};
    for (std::size_t index = 0; index < control_points.size(); ++index) {
        const Value& control_point = control_points[index];
        if (const auto error = json::require_type(
                document,
                control_point,
                Value::Type::Number,
                curve_path + "[" + std::to_string(index) + "]")) {
            return error;
        }
        coordinates[index] = control_point.as_number();
    }

    if (coordinates[0] < 0.0 || coordinates[0] > 1.0 ||
        coordinates[2] < 0.0 || coordinates[2] > 1.0) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "bezier x control points must stay within [0, 1]");
    }

    *interpolation_out = Interpolation::cubic_bezier(
        coordinates[0],
        coordinates[1],
        coordinates[2],
        coordinates[3]);
    return std::nullopt;
}

std::optional<LoadError> parse_rotate_timeline(
    const Document& document,
    const Value& rotate_timeline_value,
    std::size_t bone_index,
    std::string_view json_path,
    BoneRotateTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            rotate_timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (rotate_timeline_value.as_array().empty()) {
        return validation_error(
            document,
            rotate_timeline_value.location(),
            std::string(json_path),
            "rotate timeline must contain at least one keyframe");
    }

    timeline_out->bone_index = bone_index;
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(rotate_timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < rotate_timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = rotate_timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        RotateKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "angle",
                keyframe_path,
                &keyframe.angle)) {
            return error;
        }
        if (const auto error = parse_interpolation(
                document,
                keyframe_value,
                keyframe_path,
                &keyframe.interpolation)) {
            return error;
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_inherit_timeline(
    const Document& document,
    const Value& timeline_value,
    std::size_t bone_index,
    std::string_view json_path,
    BoneInheritTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "inherit timeline must contain at least one keyframe");
    }

    timeline_out->bone_index = bone_index;
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        InheritKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }

        const Value* inherit_rotation_value = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "inheritRotation",
                Value::Type::Boolean,
                keyframe_path,
                &inherit_rotation_value)) {
            return error;
        }
        keyframe.flags.inherit_rotation = inherit_rotation_value->as_boolean();

        const Value* inherit_scale_value = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "inheritScale",
                Value::Type::Boolean,
                keyframe_path,
                &inherit_scale_value)) {
            return error;
        }
        keyframe.flags.inherit_scale = inherit_scale_value->as_boolean();

        const Value* inherit_reflection_value = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "inheritReflection",
                Value::Type::Boolean,
                keyframe_path,
                &inherit_reflection_value)) {
            return error;
        }
        keyframe.flags.inherit_reflection = inherit_reflection_value->as_boolean();

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

template <typename Timeline>
std::optional<LoadError> parse_vector_timeline(
    const Document& document,
    const Value& timeline_value,
    std::size_t bone_index,
    std::string_view json_path,
    std::string_view timeline_label,
    Timeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            std::string(timeline_label) + " timeline must contain at least one keyframe");
    }

    timeline_out->bone_index = bone_index;
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        VectorKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "x",
                keyframe_path,
                &keyframe.x)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "y",
                keyframe_path,
                &keyframe.y)) {
            return error;
        }
        if (const auto error = parse_interpolation(
                document,
                keyframe_value,
                keyframe_path,
                &keyframe.interpolation)) {
            return error;
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> validate_color_component(
    const Document& document,
    const Value& keyframe_value,
    std::string_view keyframe_path,
    std::string_view component_name,
    double value) {
    if (value >= 0.0 && value <= 1.0) {
        return std::nullopt;
    }

    return validation_error(
        document,
        keyframe_value.location(),
        std::string(keyframe_path) + "." + std::string(component_name),
        "slot color components must stay within [0, 1]");
}

std::optional<LoadError> parse_attachment_timeline(
    const Document& document,
    const Value& timeline_value,
    std::size_t slot_index,
    std::string_view json_path,
    SlotAttachmentTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "attachment timeline must contain at least one keyframe");
    }

    timeline_out->slot_index = slot_index;
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        AttachmentKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }

        const Value* attachment_value = json::find_member(keyframe_value, "attachment");
        if (attachment_value == nullptr) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path,
                "attachment timeline keyframes must define an attachment string or null");
        }
        if (!attachment_value->is_null() && !attachment_value->is_string()) {
            return validation_error(
                document,
                attachment_value->location(),
                keyframe_path + ".attachment",
                "attachment timeline keyframes must define an attachment string or null");
        }
        if (attachment_value->is_string()) {
            keyframe.attachment_name = attachment_value->as_string();
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_color_timeline(
    const Document& document,
    const Value& timeline_value,
    std::size_t slot_index,
    std::string_view json_path,
    SlotColorTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "color timeline must contain at least one keyframe");
    }

    timeline_out->slot_index = slot_index;
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        ColorKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "r",
                keyframe_path,
                &keyframe.color.r)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "g",
                keyframe_path,
                &keyframe.color.g)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "b",
                keyframe_path,
                &keyframe.color.b)) {
            return error;
        }
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "a",
                keyframe_path,
                &keyframe.color.a)) {
            return error;
        }
        if (const auto error = validate_color_component(
                document,
                keyframe_value,
                keyframe_path,
                "r",
                keyframe.color.r)) {
            return error;
        }
        if (const auto error = validate_color_component(
                document,
                keyframe_value,
                keyframe_path,
                "g",
                keyframe.color.g)) {
            return error;
        }
        if (const auto error = validate_color_component(
                document,
                keyframe_value,
                keyframe_path,
                "b",
                keyframe.color.b)) {
            return error;
        }
        if (const auto error = validate_color_component(
                document,
                keyframe_value,
                keyframe_path,
                "a",
                keyframe.color.a)) {
            return error;
        }
        if (const auto error = parse_interpolation(
                document,
                keyframe_value,
                keyframe_path,
                &keyframe.interpolation)) {
            return error;
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_deform_timeline(
    const Document& document,
    const Value& timeline_value,
    std::size_t slot_index,
    std::string_view attachment_name,
    std::size_t component_count,
    std::string_view json_path,
    MeshDeformTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "deform timeline must contain at least one keyframe");
    }

    timeline_out->slot_index = slot_index;
    timeline_out->attachment_name = std::string(attachment_name);
    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        DeformKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }

        const Value* vertices_value = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "vertices",
                Value::Type::Array,
                keyframe_path,
                &vertices_value)) {
            return error;
        }
        if (const auto error = parse_number_array(
                document,
                *vertices_value,
                keyframe_path + ".vertices",
                &keyframe.vertex_offsets)) {
            return error;
        }
        if (keyframe.vertex_offsets.size() != component_count) {
            return validation_error(
                document,
                vertices_value->location(),
                keyframe_path + ".vertices",
                "deform keyframes must provide one x/y offset pair per mesh vertex");
        }
        if (const auto error = parse_interpolation(
                document,
                keyframe_value,
                keyframe_path,
                &keyframe.interpolation)) {
            return error;
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_timeline(
    const Document& document,
    const Value& timeline_value,
    const std::vector<SlotData>& slots,
    std::string_view json_path,
    DrawOrderTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "draw order timeline must contain at least one keyframe");
    }

    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        DrawOrderKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }

        const Value* draw_order_slots = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "slots",
                Value::Type::Array,
                keyframe_path,
                &draw_order_slots)) {
            return error;
        }

        if (draw_order_slots->as_array().size() != slots.size()) {
            return validation_error(
                document,
                draw_order_slots->location(),
                keyframe_path + ".slots",
                "draw order keyframes must list every slot exactly once");
        }

        std::vector<bool> seen(slots.size(), false);
        keyframe.slot_indices.reserve(draw_order_slots->as_array().size());
        for (std::size_t slot_order_index = 0;
             slot_order_index < draw_order_slots->as_array().size();
             ++slot_order_index) {
            const Value& slot_name_value = draw_order_slots->as_array()[slot_order_index];
            const std::string slot_path =
                keyframe_path + ".slots[" + std::to_string(slot_order_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    slot_name_value,
                    Value::Type::String,
                    slot_path)) {
                return error;
            }

            const auto slot_index = find_slot_index(slots, slot_name_value.as_string());
            if (!slot_index.has_value()) {
                return validation_error(
                    document,
                    slot_name_value.location(),
                    slot_path,
                    "draw order references unknown slot '" + slot_name_value.as_string() + "'");
            }
            if (seen[*slot_index]) {
                return validation_error(
                    document,
                    slot_name_value.location(),
                    slot_path,
                    "draw order keyframes must not repeat slots");
            }

            seen[*slot_index] = true;
            keyframe.slot_indices.push_back(*slot_index);
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_event_definition(
    const Document& document,
    const Value& event_value,
    std::string_view event_name,
    std::string_view json_path,
    EventDefinition* event_out) {
    if (const auto error = json::require_type(
            document,
            event_value,
            Value::Type::Object,
            json_path)) {
        return error;
    }

    EventDefinition event_definition;
    event_definition.name = event_name;
    if (const auto error = read_optional_integer(
            document,
            event_value,
            "int",
            json_path,
            &event_definition.int_value)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            event_value,
            "float",
            json_path,
            &event_definition.float_value)) {
        return error;
    }
    std::optional<std::string> string_value;
    if (const auto error = read_optional_string(
            document,
            event_value,
            "string",
            json_path,
            &string_value)) {
        return error;
    }
    if (string_value.has_value()) {
        event_definition.string_value = *string_value;
    }
    if (const auto error = read_optional_string(
            document,
            event_value,
            "audio",
            json_path,
            &event_definition.audio_path)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            event_value,
            "volume",
            json_path,
            &event_definition.volume)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            event_value,
            "balance",
            json_path,
            &event_definition.balance)) {
        return error;
    }

    *event_out = std::move(event_definition);
    return std::nullopt;
}

std::optional<LoadError> parse_events(
    const Document& document,
    const Value& root,
    std::vector<EventDefinition>* events_out) {
    const Value* events_value = find_optional_member(root, "events");
    if (events_value == nullptr) {
        events_out->clear();
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *events_value,
            Value::Type::Object,
            "$.events")) {
        return error;
    }
    if (events_value->as_object().empty()) {
        return validation_error(
            document,
            events_value->location(),
            "$.events",
            "events object must not be empty");
    }

    std::vector<EventDefinition> parsed_events;
    parsed_events.reserve(events_value->as_object().size());
    for (const auto& [event_name, event_value] : events_value->as_object()) {
        EventDefinition event_definition;
        if (const auto error = parse_event_definition(
                document,
                event_value,
                event_name,
                "$.events." + event_name,
                &event_definition)) {
            return error;
        }

        parsed_events.push_back(std::move(event_definition));
    }

    *events_out = std::move(parsed_events);
    return std::nullopt;
}

std::optional<LoadError> parse_event_timeline(
    const Document& document,
    const Value& timeline_value,
    const std::vector<EventDefinition>& events,
    std::string_view json_path,
    EventTimeline* timeline_out) {
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "event timeline must contain at least one keyframe");
    }

    timeline_out->keyframes.clear();
    timeline_out->keyframes.reserve(timeline_value.as_array().size());

    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";

        if (const auto error = json::require_type(
                document,
                keyframe_value,
                Value::Type::Object,
                keyframe_path)) {
            return error;
        }

        EventKeyframe keyframe;
        if (const auto error = read_required_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &keyframe.time)) {
            return error;
        }

        std::string event_name;
        if (const auto error = read_required_string(
                document,
                keyframe_value,
                "name",
                keyframe_path,
                &event_name)) {
            return error;
        }
        const auto event_index = find_event_index(events, event_name);
        if (!event_index.has_value()) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".name",
                "event timeline references unknown event '" + event_name + "'");
        }
        keyframe.event_index = *event_index;

        int int_value = 0;
        if (const auto error = read_optional_integer(
                document,
                keyframe_value,
                "int",
                keyframe_path,
                &int_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "int") != nullptr) {
            keyframe.int_value = int_value;
        }

        double float_value = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                "float",
                keyframe_path,
                &float_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "float") != nullptr) {
            keyframe.float_value = float_value;
        }

        if (const auto error = read_optional_string(
                document,
                keyframe_value,
                "string",
                keyframe_path,
                &keyframe.string_value)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                keyframe_value,
                "audio",
                keyframe_path,
                &keyframe.audio_path)) {
            return error;
        }

        double volume = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                "volume",
                keyframe_path,
                &volume)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "volume") != nullptr) {
            keyframe.volume = volume;
        }

        double balance = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                "balance",
                keyframe_path,
                &balance)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "balance") != nullptr) {
            keyframe.balance = balance;
        }

        if (has_previous_time && keyframe.time < previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "event timeline keyframe times must be non-decreasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        timeline_out->keyframes.push_back(std::move(keyframe));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_skeleton_info(
    const Document& document,
    const Value& root,
    SkeletonInfo* info_out) {
    const Value* skeleton = nullptr;
    if (const auto error = json::require_member(
            document, root, "skeleton", Value::Type::Object, "$", &skeleton)) {
        return error;
    }

    if (const auto error = read_required_string(
            document, *skeleton, "name", "$.skeleton", &info_out->name)) {
        return error;
    }
    if (const auto error = read_required_number(
            document, *skeleton, "width", "$.skeleton", &info_out->width)) {
        return error;
    }
    if (const auto error = read_required_number(
            document, *skeleton, "height", "$.skeleton", &info_out->height)) {
        return error;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_bones(
    const Document& document,
    const Value& root,
    std::vector<BoneData>* bones_out) {
    const Value* bones = nullptr;
    if (const auto error = json::require_member(
            document, root, "bones", Value::Type::Array, "$", &bones)) {
        return error;
    }
    if (bones->as_array().empty()) {
        return validation_error(
            document, bones->location(), "$.bones", "array must not be empty");
    }

    std::vector<ParsedBoneData> parsed_bones;
    parsed_bones.reserve(bones->as_array().size());

    for (std::size_t index = 0; index < bones->as_array().size(); ++index) {
        const Value& bone_value = bones->as_array()[index];
        const std::string path = "$.bones[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, bone_value, Value::Type::Object, path)) {
            return error;
        }

        ParsedBoneData parsed_bone;
        if (const auto error = read_required_string(
                document, bone_value, "name", path, &parsed_bone.bone.name)) {
            return error;
        }
        if (parsed_bone.bone.name.empty()) {
            return validation_error(
                document,
                bone_value.location(),
                path,
                "bone name must not be empty");
        }
        if (const auto error = read_optional_number(
                document, bone_value, "x", path, &parsed_bone.bone.setup_pose.x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, bone_value, "y", path, &parsed_bone.bone.setup_pose.y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "rotation",
                path,
                &parsed_bone.bone.setup_pose.rotation)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "scaleX",
                path,
                &parsed_bone.bone.setup_pose.scale_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "scaleY",
                path,
                &parsed_bone.bone.setup_pose.scale_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "shearX",
                path,
                &parsed_bone.bone.setup_pose.shear_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "shearY",
                path,
                &parsed_bone.bone.setup_pose.shear_y)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                bone_value,
                "inheritRotation",
                path,
                &parsed_bone.bone.setup_inherit.inherit_rotation)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                bone_value,
                "inheritScale",
                path,
                &parsed_bone.bone.setup_inherit.inherit_scale)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                bone_value,
                "inheritReflection",
                path,
                &parsed_bone.bone.setup_inherit.inherit_reflection)) {
            return error;
        }

        if (const Value* parent = find_optional_member(bone_value, "parent")) {
            if (const auto error = json::require_type(
                    document,
                    *parent,
                    Value::Type::String,
                    path + ".parent")) {
                return error;
            }
            parsed_bone.parent_name = parent->as_string();
            parsed_bone.parent_location = parent->location();
        }

        const auto duplicate = std::find_if(
            parsed_bones.begin(),
            parsed_bones.end(),
            [&](const ParsedBoneData& existing) {
                return existing.bone.name == parsed_bone.bone.name;
            });
        if (duplicate != parsed_bones.end()) {
            return validation_error(
                document,
                bone_value.location(),
                path,
                "bone name must be unique");
        }

        parsed_bones.push_back(std::move(parsed_bone));
    }

    bones_out->clear();
    bones_out->reserve(parsed_bones.size());
    for (const ParsedBoneData& parsed_bone : parsed_bones) {
        bones_out->push_back(parsed_bone.bone);
    }

    for (std::size_t index = 0; index < parsed_bones.size(); ++index) {
        const ParsedBoneData& parsed_bone = parsed_bones[index];
        if (!parsed_bone.parent_name.has_value()) {
            continue;
        }

        const auto parent_it = std::find_if(
            bones_out->begin(),
            bones_out->end(),
            [&](const BoneData& candidate) {
                return candidate.name == *parsed_bone.parent_name;
            });
        if (parent_it == bones_out->end()) {
            return validation_error(
                document,
                parsed_bone.parent_location,
                "$.bones[" + std::to_string(index) + "].parent",
                "bone references unknown parent '" + *parsed_bone.parent_name + "'");
        }

        (*bones_out)[index].parent_index =
            static_cast<std::size_t>(std::distance(bones_out->begin(), parent_it));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_ik_constraints(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    std::vector<IkConstraintData>* ik_constraints_out) {
    const Value* ik_value = find_optional_member(root, "ik");
    if (ik_value == nullptr) {
        ik_constraints_out->clear();
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *ik_value,
            Value::Type::Array,
            "$.ik")) {
        return error;
    }
    if (ik_value->as_array().empty()) {
        return validation_error(
            document,
            ik_value->location(),
            "$.ik",
            "ik constraints must not be empty when provided");
    }

    std::vector<IkConstraintData> parsed_constraints;
    parsed_constraints.reserve(ik_value->as_array().size());

    for (std::size_t index = 0; index < ik_value->as_array().size(); ++index) {
        const Value& constraint_value = ik_value->as_array()[index];
        const std::string path = "$.ik[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                path)) {
            return error;
        }

        IkConstraintData constraint;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                path,
                &constraint.name)) {
            return error;
        }
        if (constraint.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint names must not be empty");
        }
        const auto duplicate = std::find_if(
            parsed_constraints.begin(),
            parsed_constraints.end(),
            [&](const IkConstraintData& existing_constraint) {
                return existing_constraint.name == constraint.name;
            });
        if (duplicate != parsed_constraints.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint names must be unique");
        }

        const Value* bones_value = nullptr;
        if (const auto error = json::require_member(
                document,
                constraint_value,
                "bones",
                Value::Type::Array,
                path,
                &bones_value)) {
            return error;
        }
        if (bones_value->as_array().empty() || bones_value->as_array().size() > 2) {
            return validation_error(
                document,
                bones_value->location(),
                path + ".bones",
                "ik constraints must target either 1 bone or 2 directly chained bones");
        }

        constraint.bone_indices.reserve(bones_value->as_array().size());
        for (std::size_t bone_name_index = 0;
             bone_name_index < bones_value->as_array().size();
             ++bone_name_index) {
            const Value& bone_name_value = bones_value->as_array()[bone_name_index];
            const std::string bone_path =
                path + ".bones[" + std::to_string(bone_name_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    bone_name_value,
                    Value::Type::String,
                    bone_path)) {
                return error;
            }

            const auto bone_index = find_bone_index(bones, bone_name_value.as_string());
            if (!bone_index.has_value()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "ik constraint references unknown bone '" +
                        bone_name_value.as_string() + "'");
            }
            if (std::find(
                    constraint.bone_indices.begin(),
                    constraint.bone_indices.end(),
                    *bone_index) != constraint.bone_indices.end()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "ik constraint bones must be unique");
            }

            constraint.bone_indices.push_back(*bone_index);
        }
        if (constraint.bone_indices.size() == 2 &&
            bones[constraint.bone_indices[1]].parent_index !=
                std::optional<std::size_t>{constraint.bone_indices[0]}) {
            return validation_error(
                document,
                bones_value->location(),
                path + ".bones",
                "two-bone ik constraints require the second bone to be a direct child of the first");
        }

        std::string target_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "target",
                path,
                &target_name)) {
            return error;
        }
        const auto target_bone_index = find_bone_index(bones, target_name);
        if (!target_bone_index.has_value()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".target",
                "ik constraint references unknown target bone '" + target_name + "'");
        }
        if (std::find(
                constraint.bone_indices.begin(),
                constraint.bone_indices.end(),
                *target_bone_index) != constraint.bone_indices.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".target",
                "ik target must not also be one of the constrained bones");
        }
        constraint.target_bone_index = *target_bone_index;

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "mix",
                path,
                &constraint.mix)) {
            return error;
        }
        if (constraint.mix < 0.0 || constraint.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".mix",
                "ik mix must stay within [0, 1]");
        }

        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "bendPositive",
                path,
                &constraint.bend_positive)) {
            return error;
        }

        parsed_constraints.push_back(std::move(constraint));
    }

    *ik_constraints_out = std::move(parsed_constraints);
    return std::nullopt;
}

std::optional<LoadError> parse_path_constraints(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    const std::vector<SlotData>& slots,
    const std::vector<SkinData>& skins,
    std::vector<PathConstraintData>* path_constraints_out) {
    const Value* path_value = find_optional_member(root, "path");
    if (path_value == nullptr) {
        path_constraints_out->clear();
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *path_value,
            Value::Type::Array,
            "$.path")) {
        return error;
    }
    if (path_value->as_array().empty()) {
        return validation_error(
            document,
            path_value->location(),
            "$.path",
            "path constraints must not be empty when provided");
    }

    const std::optional<std::size_t> default_skin_index = find_skin_index(skins, "default");
    std::vector<PathConstraintData> parsed_constraints;
    parsed_constraints.reserve(path_value->as_array().size());

    for (std::size_t index = 0; index < path_value->as_array().size(); ++index) {
        const Value& constraint_value = path_value->as_array()[index];
        const std::string path = "$.path[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                path)) {
            return error;
        }

        PathConstraintData constraint;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                path,
                &constraint.name)) {
            return error;
        }
        if (constraint.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "path constraint names must not be empty");
        }
        const auto duplicate = std::find_if(
            parsed_constraints.begin(),
            parsed_constraints.end(),
            [&](const PathConstraintData& existing_constraint) {
                return existing_constraint.name == constraint.name;
            });
        if (duplicate != parsed_constraints.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "path constraint names must be unique");
        }

        std::string slot_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "slot",
                path,
                &slot_name)) {
            return error;
        }
        const auto slot_index = find_slot_index(slots, slot_name);
        if (!slot_index.has_value()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".slot",
                "path constraint references unknown slot '" + slot_name + "'");
        }
        constraint.slot_index = *slot_index;

        const AttachmentData* path_attachment = find_attachment_source_in_skins(
            skins,
            default_skin_index,
            *slot_index,
            slots[*slot_index].setup_attachment);
        if (path_attachment == nullptr || !path_attachment->path_attachment.has_value()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".slot",
                "path constraint slot '" + slot_name +
                    "' must resolve to a path attachment in the setup skin");
        }

        const Value* bones_value = nullptr;
        if (const auto error = json::require_member(
                document,
                constraint_value,
                "bones",
                Value::Type::Array,
                path,
                &bones_value)) {
            return error;
        }
        if (bones_value->as_array().empty()) {
            return validation_error(
                document,
                bones_value->location(),
                path + ".bones",
                "path constraints must target at least one bone");
        }

        constraint.bone_indices.reserve(bones_value->as_array().size());
        for (std::size_t bone_name_index = 0;
             bone_name_index < bones_value->as_array().size();
             ++bone_name_index) {
            const Value& bone_name_value = bones_value->as_array()[bone_name_index];
            const std::string bone_path =
                path + ".bones[" + std::to_string(bone_name_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    bone_name_value,
                    Value::Type::String,
                    bone_path)) {
                return error;
            }

            const auto bone_index = find_bone_index(bones, bone_name_value.as_string());
            if (!bone_index.has_value()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "path constraint references unknown bone '" +
                        bone_name_value.as_string() + "'");
            }
            if (std::find(
                    constraint.bone_indices.begin(),
                    constraint.bone_indices.end(),
                    *bone_index) != constraint.bone_indices.end()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "path constraint bones must be unique");
            }
            if (!constraint.bone_indices.empty() &&
                bones[*bone_index].parent_index !=
                    std::optional<std::size_t>{constraint.bone_indices.back()}) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "path constraint bones must form a direct parent-child chain");
            }

            constraint.bone_indices.push_back(*bone_index);
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "position",
                path,
                &constraint.position)) {
            return error;
        }
        if (constraint.position < 0.0 || constraint.position > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".position",
                "path constraint position must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "spacing",
                path,
                &constraint.spacing)) {
            return error;
        }
        if (constraint.spacing < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".spacing",
                "path constraint spacing must be non-negative");
        }

        std::optional<std::string> spacing_mode_name;
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "spacingMode",
                path,
                &spacing_mode_name)) {
            return error;
        }
        if (spacing_mode_name.has_value()) {
            const std::optional<PathConstraintSpacingMode> spacing_mode =
                parse_path_constraint_spacing_mode(*spacing_mode_name);
            if (!spacing_mode.has_value()) {
                return validation_error(
                    document,
                    constraint_value.location(),
                    path + ".spacingMode",
                    "path constraint spacingMode must be 'length' or 'percent'");
            }
            constraint.spacing_mode = *spacing_mode;
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotateMix",
                path,
                &constraint.rotate_mix)) {
            return error;
        }
        if (constraint.rotate_mix < 0.0 || constraint.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".rotateMix",
                "path constraint rotateMix must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "translateMix",
                path,
                &constraint.translate_mix)) {
            return error;
        }
        if (constraint.translate_mix < 0.0 || constraint.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".translateMix",
                "path constraint translateMix must stay within [0, 1]");
        }

        parsed_constraints.push_back(std::move(constraint));
    }

    *path_constraints_out = std::move(parsed_constraints);
    return std::nullopt;
}

std::optional<LoadError> parse_transform_constraints(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    std::vector<TransformConstraintData>* transform_constraints_out) {
    const Value* transform_value = find_optional_member(root, "transform");
    if (transform_value == nullptr) {
        transform_constraints_out->clear();
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *transform_value,
            Value::Type::Array,
            "$.transform")) {
        return error;
    }
    if (transform_value->as_array().empty()) {
        return validation_error(
            document,
            transform_value->location(),
            "$.transform",
            "transform constraints must not be empty when provided");
    }

    std::vector<TransformConstraintData> parsed_constraints;
    parsed_constraints.reserve(transform_value->as_array().size());

    for (std::size_t index = 0; index < transform_value->as_array().size(); ++index) {
        const Value& constraint_value = transform_value->as_array()[index];
        const std::string path = "$.transform[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                path)) {
            return error;
        }

        TransformConstraintData constraint;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                path,
                &constraint.name)) {
            return error;
        }
        if (constraint.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "transform constraint names must not be empty");
        }
        const auto duplicate = std::find_if(
            parsed_constraints.begin(),
            parsed_constraints.end(),
            [&](const TransformConstraintData& existing_constraint) {
                return existing_constraint.name == constraint.name;
            });
        if (duplicate != parsed_constraints.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "transform constraint names must be unique");
        }

        std::string source_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "source",
                path,
                &source_name)) {
            return error;
        }
        const auto source_bone_index = find_bone_index(bones, source_name);
        if (!source_bone_index.has_value()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".source",
                "transform constraint references unknown source bone '" + source_name + "'");
        }
        constraint.source_bone_index = *source_bone_index;

        const Value* bones_value = nullptr;
        if (const auto error = json::require_member(
                document,
                constraint_value,
                "bones",
                Value::Type::Array,
                path,
                &bones_value)) {
            return error;
        }
        if (bones_value->as_array().empty()) {
            return validation_error(
                document,
                bones_value->location(),
                path + ".bones",
                "transform constraints must target at least one bone");
        }

        constraint.target_bone_indices.reserve(bones_value->as_array().size());
        for (std::size_t bone_name_index = 0;
             bone_name_index < bones_value->as_array().size();
             ++bone_name_index) {
            const Value& bone_name_value = bones_value->as_array()[bone_name_index];
            const std::string bone_path =
                path + ".bones[" + std::to_string(bone_name_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    bone_name_value,
                    Value::Type::String,
                    bone_path)) {
                return error;
            }

            const auto bone_index = find_bone_index(bones, bone_name_value.as_string());
            if (!bone_index.has_value()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "transform constraint references unknown bone '" +
                        bone_name_value.as_string() + "'");
            }
            if (*bone_index == constraint.source_bone_index) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "transform constraint source bone must not also be a target");
            }
            if (std::find(
                    constraint.target_bone_indices.begin(),
                    constraint.target_bone_indices.end(),
                    *bone_index) != constraint.target_bone_indices.end()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "transform constraint target bones must be unique");
            }

            constraint.target_bone_indices.push_back(*bone_index);
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotateMix",
                path,
                &constraint.rotate_mix)) {
            return error;
        }
        if (constraint.rotate_mix < 0.0 || constraint.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".rotateMix",
                "transform constraint rotateMix must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "translateMix",
                path,
                &constraint.translate_mix)) {
            return error;
        }
        if (constraint.translate_mix < 0.0 || constraint.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".translateMix",
                "transform constraint translateMix must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "scaleMix",
                path,
                &constraint.scale_mix)) {
            return error;
        }
        if (constraint.scale_mix < 0.0 || constraint.scale_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".scaleMix",
                "transform constraint scaleMix must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "shearMix",
                path,
                &constraint.shear_mix)) {
            return error;
        }
        if (constraint.shear_mix < 0.0 || constraint.shear_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".shearMix",
                "transform constraint shearMix must stay within [0, 1]");
        }

        const Value* offset_value = find_optional_member(constraint_value, "offset");
        if (offset_value != nullptr) {
            if (const auto error = json::require_type(
                    document,
                    *offset_value,
                    Value::Type::Object,
                    path + ".offset")) {
                return error;
            }

            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "rotation",
                    path + ".offset",
                    &constraint.offsets.rotation)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "x",
                    path + ".offset",
                    &constraint.offsets.x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "y",
                    path + ".offset",
                    &constraint.offsets.y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "scaleX",
                    path + ".offset",
                    &constraint.offsets.scale_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "scaleY",
                    path + ".offset",
                    &constraint.offsets.scale_y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "shearX",
                    path + ".offset",
                    &constraint.offsets.shear_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document,
                    *offset_value,
                    "shearY",
                    path + ".offset",
                    &constraint.offsets.shear_y)) {
                return error;
            }
        }

        parsed_constraints.push_back(std::move(constraint));
    }

    *transform_constraints_out = std::move(parsed_constraints);
    return std::nullopt;
}

std::optional<LoadError> parse_physics_constraints(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    std::vector<PhysicsConstraintData>* physics_constraints_out) {
    const Value* physics_value = find_optional_member(root, "physics");
    if (physics_value == nullptr) {
        physics_constraints_out->clear();
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *physics_value,
            Value::Type::Array,
            "$.physics")) {
        return error;
    }
    if (physics_value->as_array().empty()) {
        return validation_error(
            document,
            physics_value->location(),
            "$.physics",
            "physics constraints must not be empty when provided");
    }

    std::vector<PhysicsConstraintData> parsed_constraints;
    parsed_constraints.reserve(physics_value->as_array().size());
    for (std::size_t index = 0; index < physics_value->as_array().size(); ++index) {
        const Value& constraint_value = physics_value->as_array()[index];
        const std::string path = "$.physics[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                path)) {
            return error;
        }

        PhysicsConstraintData constraint;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                path,
                &constraint.name)) {
            return error;
        }
        if (constraint.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "physics constraint names must not be empty");
        }
        const auto duplicate = std::find_if(
            parsed_constraints.begin(),
            parsed_constraints.end(),
            [&](const PhysicsConstraintData& existing_constraint) {
                return existing_constraint.name == constraint.name;
            });
        if (duplicate != parsed_constraints.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "physics constraint names must be unique");
        }

        const Value* bones_value = nullptr;
        if (const auto error = json::require_member(
                document,
                constraint_value,
                "bones",
                Value::Type::Array,
                path,
                &bones_value)) {
            return error;
        }
        if (bones_value->as_array().empty()) {
            return validation_error(
                document,
                bones_value->location(),
                path + ".bones",
                "physics constraints must target at least one directly chained bone");
        }

        constraint.bone_indices.reserve(bones_value->as_array().size());
        std::optional<std::size_t> previous_bone_index;
        for (std::size_t bone_name_index = 0;
             bone_name_index < bones_value->as_array().size();
             ++bone_name_index) {
            const Value& bone_name_value = bones_value->as_array()[bone_name_index];
            const std::string bone_path =
                path + ".bones[" + std::to_string(bone_name_index) + "]";
            if (const auto error = json::require_type(
                    document,
                    bone_name_value,
                    Value::Type::String,
                    bone_path)) {
                return error;
            }

            const auto bone_index = find_bone_index(bones, bone_name_value.as_string());
            if (!bone_index.has_value()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "physics constraint references unknown bone '" +
                        bone_name_value.as_string() + "'");
            }
            if (std::find(
                    constraint.bone_indices.begin(),
                    constraint.bone_indices.end(),
                    *bone_index) != constraint.bone_indices.end()) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "physics constraint bones must be unique");
            }
            if (previous_bone_index.has_value() &&
                bones[*bone_index].parent_index != previous_bone_index) {
                return validation_error(
                    document,
                    bone_name_value.location(),
                    bone_path,
                    "physics constraint bones must form a directly chained hierarchy");
            }

            previous_bone_index = *bone_index;
            constraint.bone_indices.push_back(*bone_index);
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "inertia",
                path,
                &constraint.inertia)) {
            return error;
        }
        if (constraint.inertia < 0.0 || constraint.inertia > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".inertia",
                "physics inertia must stay within [0, 1]");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "damping",
                path,
                &constraint.damping)) {
            return error;
        }
        if (constraint.damping < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".damping",
                "physics damping must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "strength",
                path,
                &constraint.strength)) {
            return error;
        }
        if (constraint.strength < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".strength",
                "physics strength must be non-negative");
        }

        if (const auto error = parse_optional_xy_vector(
                document,
                constraint_value,
                "gravity",
                path,
                &constraint.gravity)) {
            return error;
        }
        if (const auto error = parse_optional_xy_vector(
                document,
                constraint_value,
                "wind",
                path,
                &constraint.wind)) {
            return error;
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "mix",
                path,
                &constraint.mix)) {
            return error;
        }
        if (constraint.mix < 0.0 || constraint.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".mix",
                "physics mix must stay within [0, 1]");
        }

        parsed_constraints.push_back(std::move(constraint));
    }

    *physics_constraints_out = std::move(parsed_constraints);
    return std::nullopt;
}

std::optional<LoadError> parse_slots(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    std::vector<SlotData>* slots_out) {
    const Value* slots = nullptr;
    if (const auto error = json::require_member(
            document, root, "slots", Value::Type::Array, "$", &slots)) {
        return error;
    }
    if (slots->as_array().empty()) {
        return validation_error(
            document, slots->location(), "$.slots", "array must not be empty");
    }

    std::vector<SlotData> parsed_slots;
    parsed_slots.reserve(slots->as_array().size());

    for (std::size_t index = 0; index < slots->as_array().size(); ++index) {
        const Value& slot_value = slots->as_array()[index];
        const std::string path = "$.slots[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, slot_value, Value::Type::Object, path)) {
            return error;
        }

        SlotData slot;
        if (const auto error = read_required_string(
                document, slot_value, "name", path, &slot.name)) {
            return error;
        }
        if (slot.name.empty()) {
            return validation_error(
                document,
                slot_value.location(),
                path,
                "slot name must not be empty");
        }

        const auto duplicate = std::find_if(
            parsed_slots.begin(),
            parsed_slots.end(),
            [&](const SlotData& existing) {
                return existing.name == slot.name;
            });
        if (duplicate != parsed_slots.end()) {
            return validation_error(
                document,
                slot_value.location(),
                path,
                "slot name must be unique");
        }

        std::string bone_name;
        if (const auto error = read_required_string(
                document, slot_value, "bone", path, &bone_name)) {
            return error;
        }
        const auto bone_index = find_bone_index(bones, bone_name);
        if (!bone_index.has_value()) {
            return validation_error(
                document,
                slot_value.location(),
                path + ".bone",
                "slot references unknown bone '" + bone_name + "'");
        }
        slot.bone_index = *bone_index;

        if (const auto error = read_required_string(
                document,
                slot_value,
                "attachment",
                path,
                &slot.setup_attachment)) {
            return error;
        }
        if (const auto error = parse_slot_blend_mode(
                document,
                slot_value,
                path,
                &slot.blend_mode)) {
            return error;
        }
        if (const auto error = parse_slot_color(
                document,
                slot_value,
                "color",
                path,
                &slot.color)) {
            return error;
        }
        if (const auto error = parse_optional_slot_color(
                document,
                slot_value,
                "dark",
                path,
                &slot.dark_color)) {
            return error;
        }

        parsed_slots.push_back(std::move(slot));
    }

    *slots_out = std::move(parsed_slots);
    return std::nullopt;
}

std::optional<std::size_t> find_skin_slot_attachment_index(
    const SkinData& skin,
    std::string_view attachment_name) {
    const auto it = std::find_if(
        skin.slot_attachments.begin(),
        skin.slot_attachments.end(),
        [&](const SkinSlotData& skin_slot) {
            return skin_slot.attachment.name == attachment_name;
        });
    if (it == skin.slot_attachments.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(skin.slot_attachments.begin(), it));
}

std::optional<LoadError> parse_attachment_sequence(
    const Document& document,
    const Value& attachment_value,
    std::string_view json_path,
    AttachmentData* attachment_out) {
    const Value* sequence_value = find_optional_member(attachment_value, "sequence");
    const bool has_flat_sequence_fields =
        find_optional_member(attachment_value, "start") != nullptr ||
        find_optional_member(attachment_value, "end") != nullptr ||
        find_optional_member(attachment_value, "fps") != nullptr ||
        find_optional_member(attachment_value, "mode") != nullptr ||
        find_optional_member(attachment_value, "setup_frame") != nullptr;
    if (sequence_value == nullptr && !has_flat_sequence_fields) {
        return std::nullopt;
    }

    const Value* sequence_source = sequence_value != nullptr ? sequence_value : &attachment_value;
    const std::string sequence_path =
        sequence_value != nullptr ? std::string(json_path) + ".sequence" : std::string(json_path);
    if (sequence_value != nullptr) {
        if (const auto error = json::require_type(
                document,
                *sequence_value,
                Value::Type::Object,
                sequence_path)) {
            return error;
        }
    }

    AttachmentSequenceData sequence;
    if (const Value* frames_value = find_optional_member(*sequence_source, "frames")) {
        if (const auto error = json::require_type(
                document,
                *frames_value,
                Value::Type::Array,
                sequence_path + ".frames")) {
            return error;
        }

        sequence.frame_regions.reserve(frames_value->as_array().size());
        for (std::size_t index = 0; index < frames_value->as_array().size(); ++index) {
            const Value& frame_value = frames_value->as_array()[index];
            const std::string frame_path =
                sequence_path + ".frames[" + std::to_string(index) + "]";
            if (const auto error = json::require_type(
                    document,
                    frame_value,
                    Value::Type::String,
                    frame_path)) {
                return error;
            }
            if (frame_value.as_string().empty()) {
                return validation_error(
                    document,
                    frame_value.location(),
                    frame_path,
                    "sequence frame names must not be empty");
            }
            sequence.frame_regions.push_back(frame_value.as_string());
        }
    } else {
        if (attachment_out->region_name.empty()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path) + ".region",
                "sequence attachments require a non-empty region prefix");
        }

        auto read_required_index = [&](std::string_view key, int* value_out) -> std::optional<LoadError> {
            double numeric_value = 0.0;
            if (const auto error = read_required_number(
                    document,
                    *sequence_source,
                    key,
                    sequence_path,
                    &numeric_value)) {
                return error;
            }
            if (numeric_value < 0.0 || std::floor(numeric_value) != numeric_value ||
                numeric_value > static_cast<double>(std::numeric_limits<int>::max())) {
                return validation_error(
                    document,
                    sequence_source->location(),
                    sequence_path + "." + std::string(key),
                    "sequence frame bounds must be non-negative integers");
            }

            *value_out = static_cast<int>(numeric_value);
            return std::nullopt;
        };

        int start_frame = 0;
        int end_frame = 0;
        if (const auto error = read_required_index("start", &start_frame)) {
            return error;
        }
        if (const auto error = read_required_index("end", &end_frame)) {
            return error;
        }
        if (end_frame < start_frame) {
            return validation_error(
                document,
                sequence_source->location(),
                sequence_path + ".end",
                "sequence end must be greater than or equal to start");
        }

        sequence.frame_regions.reserve(static_cast<std::size_t>(end_frame - start_frame + 1));
        for (int frame = start_frame; frame <= end_frame; ++frame) {
            sequence.frame_regions.push_back(attachment_out->region_name + std::to_string(frame));
        }
    }

    if (sequence.frame_regions.empty()) {
        return validation_error(
            document,
            sequence_source->location(),
            sequence_path + ".frames",
            "sequence attachments require at least one frame");
    }

    if (const auto error = read_required_number(
            document,
            *sequence_source,
            "fps",
            sequence_path,
            &sequence.fps)) {
        return error;
    }
    if (sequence.fps < 0.0) {
        return validation_error(
            document,
            sequence_source->location(),
            sequence_path + ".fps",
            "sequence fps must be non-negative");
    }

    std::optional<std::string> mode_name;
    if (const auto error = read_optional_string(
            document,
            *sequence_source,
            "mode",
            sequence_path,
            &mode_name)) {
        return error;
    }

    const std::optional<SequencePlaybackMode> playback_mode =
        parse_sequence_playback_mode(mode_name.value_or("hold"));
    if (!playback_mode.has_value()) {
        return validation_error(
            document,
            sequence_source->location(),
            sequence_path + ".mode",
            "sequence mode must be 'hold', 'once', 'loop', 'pingpong', "
            "'once_reverse', 'loop_reverse', or 'pingpong_reverse'");
    }
    sequence.playback_mode = *playback_mode;

    double setup_frame_value = 0.0;
    if (const auto error = read_optional_number(
            document,
            *sequence_source,
            "setup_frame",
            sequence_path,
            &setup_frame_value)) {
        return error;
    }
    if (setup_frame_value < 0.0 || std::floor(setup_frame_value) != setup_frame_value) {
        return validation_error(
            document,
            sequence_source->location(),
            sequence_path + ".setup_frame",
            "sequence setup_frame must be a non-negative integer");
    }
    sequence.setup_frame = static_cast<std::size_t>(setup_frame_value);
    if (sequence.setup_frame >= sequence.frame_regions.size()) {
        return validation_error(
            document,
            sequence_source->location(),
            sequence_path + ".setup_frame",
            "sequence setup_frame must be inside the frame list");
    }

    attachment_out->sequence = std::move(sequence);
    attachment_out->region_name =
        attachment_out->sequence->frame_regions[attachment_out->sequence->setup_frame];

    return std::nullopt;
}

std::optional<LoadError> parse_skin_attachment(
    const Document& document,
    const Value& attachment_value,
    const std::vector<BoneData>& bones,
    const std::vector<SlotData>& slots,
    std::string_view slot_name,
    std::string_view json_path,
    SkinSlotData* skin_slot_out) {
    if (const auto error = json::require_type(
            document,
            attachment_value,
            Value::Type::Object,
            json_path)) {
        return error;
    }

    const auto slot_index = find_slot_index(slots, slot_name);
    if (!slot_index.has_value()) {
        return validation_error(
            document,
            attachment_value.location(),
            std::string(json_path),
            "skin references unknown slot '" + std::string(slot_name) + "'");
    }

    SkinSlotData skin_slot;
    skin_slot.slot_index = *slot_index;

    if (const auto error = read_required_string(
            document,
            attachment_value,
            "attachment",
            json_path,
            &skin_slot.attachment.name)) {
        return error;
    }
    if (skin_slot.attachment.name.empty()) {
        return validation_error(
            document,
            attachment_value.location(),
            std::string(json_path) + ".attachment",
            "skin attachment name must not be empty");
    }

    std::optional<std::string> type_name;
    if (const auto error = read_optional_string(
            document,
            attachment_value,
            "type",
            json_path,
            &type_name)) {
        return error;
    }

    const bool is_sequence_type = type_name.has_value() && *type_name == "sequence";
    if (!type_name.has_value() || *type_name == "region" || is_sequence_type) {
        skin_slot.attachment.kind = AttachmentKind::Region;
    } else if (*type_name == "mesh") {
        skin_slot.attachment.kind = AttachmentKind::Mesh;
    } else if (*type_name == "linked_mesh") {
        skin_slot.attachment.kind = AttachmentKind::LinkedMesh;
    } else if (*type_name == "point") {
        skin_slot.attachment.kind = AttachmentKind::Point;
    } else if (*type_name == "bounding_box") {
        skin_slot.attachment.kind = AttachmentKind::BoundingBox;
    } else if (*type_name == "clipping") {
        skin_slot.attachment.kind = AttachmentKind::Clipping;
    } else if (*type_name == "path") {
        skin_slot.attachment.kind = AttachmentKind::Path;
    } else {
        return validation_error(
            document,
            attachment_value.location(),
            std::string(json_path) + ".type",
            "attachment type must be 'region', 'mesh', 'linked_mesh', 'point', "
            "'bounding_box', 'clipping', 'path', or 'sequence'");
    }

    if (skin_slot.attachment.kind == AttachmentKind::Region ||
        skin_slot.attachment.kind == AttachmentKind::Mesh ||
        skin_slot.attachment.kind == AttachmentKind::LinkedMesh) {
        skin_slot.attachment.region_name = skin_slot.attachment.name;
        std::optional<std::string> region_name;
        if (const auto error = read_optional_string(
                document,
                attachment_value,
                "region",
                json_path,
                &region_name)) {
            return error;
        }
        if (region_name.has_value()) {
            skin_slot.attachment.region_name = *region_name;
        }
    }

    if (skin_slot.attachment.kind == AttachmentKind::Clipping) {
        const Value* vertices_value = nullptr;
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "vertices",
                Value::Type::Array,
                json_path,
                &vertices_value)) {
            return error;
        }

        ClippingAttachmentData clipping_attachment;
        if (const auto error = parse_polygon(
                document,
                *vertices_value,
                std::string(json_path) + ".vertices",
                "clipping",
                &clipping_attachment.polygon)) {
            return error;
        }

        std::optional<std::string> end_slot_name;
        if (const auto error = read_optional_string(
                document,
                attachment_value,
                "end",
                json_path,
                &end_slot_name)) {
            return error;
        }
        if (end_slot_name.has_value()) {
            const auto end_slot_index = find_slot_index(slots, *end_slot_name);
            if (!end_slot_index.has_value()) {
                return validation_error(
                    document,
                    attachment_value.location(),
                    std::string(json_path) + ".end",
                    "clipping attachment references unknown end slot '" + *end_slot_name + "'");
            }

            clipping_attachment.end_slot_name = *end_slot_name;
            clipping_attachment.end_slot_index = *end_slot_index;
        }

        skin_slot.attachment.clipping_attachment = std::move(clipping_attachment);
    }

    if (skin_slot.attachment.kind == AttachmentKind::Mesh) {
        const Value* vertices_value = nullptr;
        const Value* triangles_value = nullptr;
        const Value* uvs_value = nullptr;
        const Value* weights_value = nullptr;
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "vertices",
                Value::Type::Array,
                json_path,
                &vertices_value)) {
            return error;
        }
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "triangles",
                Value::Type::Array,
                json_path,
                &triangles_value)) {
            return error;
        }
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "uvs",
                Value::Type::Array,
                json_path,
                &uvs_value)) {
            return error;
        }
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "weights",
                Value::Type::Array,
                json_path,
                &weights_value)) {
            return error;
        }

        auto geometry = std::make_shared<MeshGeometry>();
        if (const auto error = parse_number_array(
                document,
                *vertices_value,
                std::string(json_path) + ".vertices",
                &geometry->vertices)) {
            return error;
        }
        if (const auto error = parse_index_array(
                document,
                *triangles_value,
                std::string(json_path) + ".triangles",
                &geometry->triangles)) {
            return error;
        }
        if (const auto error = parse_number_array(
                document,
                *uvs_value,
                std::string(json_path) + ".uvs",
                &geometry->uvs)) {
            return error;
        }
        if (geometry->vertices.size() % 2 != 0 || geometry->uvs.size() % 2 != 0) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path),
                "mesh vertices and uvs must be 2D coordinate arrays");
        }
        if (geometry->uvs.size() != geometry->vertices.size()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path),
                "mesh uvs must align with the vertex coordinate count");
        }
        if (geometry->triangles.size() % 3 != 0) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path),
                "mesh triangle indices must be grouped into triangles");
        }
        if (const auto error = parse_mesh_weights(
                document,
                *weights_value,
                bones,
                geometry->vertices.size() / 2,
                std::string(json_path) + ".weights",
                &geometry->weights)) {
            return error;
        }
        if (geometry->vertices.empty() || geometry->uvs.empty() || geometry->triangles.empty() ||
            geometry->weights.empty()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path),
                "mesh attachments require non-empty vertices, triangles, uvs, and weights arrays");
        }

        skin_slot.attachment.mesh_geometry = std::move(geometry);
    }

    if (skin_slot.attachment.kind == AttachmentKind::Point) {
        PointAttachmentData point_attachment;
        if (const auto error = read_optional_number(
                document,
                attachment_value,
                "x",
                json_path,
                &point_attachment.local_position.x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                attachment_value,
                "y",
                json_path,
                &point_attachment.local_position.y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                attachment_value,
                "rotation",
                json_path,
                &point_attachment.rotation)) {
            return error;
        }

        skin_slot.attachment.point_attachment = std::move(point_attachment);
    }

    if (skin_slot.attachment.kind == AttachmentKind::BoundingBox) {
        const Value* vertices_value = nullptr;
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "vertices",
                Value::Type::Array,
                json_path,
                &vertices_value)) {
            return error;
        }

        BoundingBoxAttachmentData bounding_box;
        if (const auto error = parse_polygon(
                document,
                *vertices_value,
                std::string(json_path) + ".vertices",
                "bounding-box",
                &bounding_box.polygon)) {
            return error;
        }

        skin_slot.attachment.bounding_box = std::move(bounding_box);
    }

    if (skin_slot.attachment.kind == AttachmentKind::Path) {
        const Value* points_value = nullptr;
        if (const auto error = json::require_member(
                document,
                attachment_value,
                "points",
                Value::Type::Array,
                json_path,
                &points_value)) {
            return error;
        }

        PathAttachmentData path_attachment;
        if (const auto error = parse_path_control_points(
                document,
                *points_value,
                std::string(json_path) + ".points",
                &path_attachment.control_points)) {
            return error;
        }

        skin_slot.attachment.path_attachment = std::move(path_attachment);
    }

    if (skin_slot.attachment.kind == AttachmentKind::LinkedMesh) {
        LinkedMeshData linked_mesh;
        if (const auto error = read_required_string(
                document,
                attachment_value,
                "parent",
                json_path,
                &linked_mesh.parent_attachment)) {
            return error;
        }
        if (linked_mesh.parent_attachment.empty()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path) + ".parent",
                "linked mesh parent must not be empty");
        }
        if (const auto error = read_optional_string(
                document,
                attachment_value,
                "skin",
                json_path,
                &linked_mesh.parent_skin_name)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                attachment_value,
                "deform",
                json_path,
                &linked_mesh.deform)) {
            return error;
        }

        skin_slot.attachment.linked_mesh = std::move(linked_mesh);
    }

    if (skin_slot.attachment.kind == AttachmentKind::Region ||
        skin_slot.attachment.kind == AttachmentKind::Mesh ||
        skin_slot.attachment.kind == AttachmentKind::LinkedMesh) {
        if (const auto error = parse_attachment_sequence(
                document,
                attachment_value,
                json_path,
                &skin_slot.attachment)) {
            return error;
        }
        if (is_sequence_type && !skin_slot.attachment.sequence.has_value()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path),
                "sequence attachments require region, start, end, fps, and optional mode metadata");
        }
    }

    *skin_slot_out = std::move(skin_slot);
    return std::nullopt;
}

void build_default_skin(
    const std::vector<SlotData>& slots,
    std::vector<SkinData>* skins_out) {
    SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments.reserve(slots.size());

    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
        SkinSlotData slot_attachment;
        slot_attachment.slot_index = slot_index;
        slot_attachment.attachment.name = slots[slot_index].setup_attachment;
        slot_attachment.attachment.kind = AttachmentKind::Region;
        slot_attachment.attachment.region_name = slots[slot_index].setup_attachment;
        default_skin.slot_attachments.push_back(std::move(slot_attachment));
    }

    skins_out->clear();
    skins_out->push_back(std::move(default_skin));
}

std::optional<LoadError> resolve_linked_meshes(
    const Document& document,
    const std::optional<std::size_t>& default_skin_index,
    std::vector<SkinData>* skins_out) {
    std::vector<std::vector<bool>> resolved(skins_out->size());
    std::vector<std::vector<bool>> evaluating(skins_out->size());
    for (std::size_t skin_index = 0; skin_index < skins_out->size(); ++skin_index) {
        resolved[skin_index].assign((*skins_out)[skin_index].slot_attachments.size(), false);
        evaluating[skin_index].assign((*skins_out)[skin_index].slot_attachments.size(), false);
    }

    const auto resolve_attachment = [&](const auto& self,
                                        std::size_t skin_index,
                                        std::size_t attachment_index) -> std::optional<LoadError> {
        if (resolved[skin_index][attachment_index]) {
            return std::nullopt;
        }
        if (evaluating[skin_index][attachment_index]) {
            return validation_error(
                document,
                (*skins_out)[skin_index].slot_attachments[attachment_index].attachment.linked_mesh
                    ? document.root.location()
                    : document.root.location(),
                "$.skins." + (*skins_out)[skin_index].name,
                "linked mesh inheritance must not contain cycles");
        }

        evaluating[skin_index][attachment_index] = true;

        SkinSlotData& skin_slot = (*skins_out)[skin_index].slot_attachments[attachment_index];
        if (skin_slot.attachment.kind == AttachmentKind::LinkedMesh) {
            LinkedMeshData& linked_mesh = *skin_slot.attachment.linked_mesh;
            std::size_t parent_skin_index = skin_index;
            if (linked_mesh.parent_skin_name.has_value()) {
                const auto found_parent_skin =
                    find_skin_index(*skins_out, *linked_mesh.parent_skin_name);
                if (!found_parent_skin.has_value()) {
                    return validation_error(
                        document,
                        document.root.location(),
                        "$.skins." + (*skins_out)[skin_index].name + "." +
                            std::to_string(skin_slot.slot_index) + ".skin",
                        "linked mesh references unknown skin '" + *linked_mesh.parent_skin_name + "'");
                }
                parent_skin_index = *found_parent_skin;
            } else if (default_skin_index.has_value()) {
                parent_skin_index = *default_skin_index;
            }

            linked_mesh.parent_skin_index = parent_skin_index;

            const auto parent_attachment_index = find_skin_slot_attachment_index(
                (*skins_out)[parent_skin_index],
                linked_mesh.parent_attachment);
            if (!parent_attachment_index.has_value()) {
                return validation_error(
                    document,
                    document.root.location(),
                    "$.skins." + (*skins_out)[skin_index].name,
                    "linked mesh references unknown parent attachment '" +
                        linked_mesh.parent_attachment + "'");
            }

            if (const auto error = self(self, parent_skin_index, *parent_attachment_index)) {
                return error;
            }

            const AttachmentData& parent_attachment =
                (*skins_out)[parent_skin_index].slot_attachments[*parent_attachment_index].attachment;
            if (parent_attachment.mesh_geometry == nullptr) {
                return validation_error(
                    document,
                    document.root.location(),
                    "$.skins." + (*skins_out)[skin_index].name,
                    "linked mesh parent attachment '" + linked_mesh.parent_attachment +
                        "' does not provide mesh geometry");
            }

            skin_slot.attachment.mesh_geometry = parent_attachment.mesh_geometry;
            if (skin_slot.attachment.region_name.empty()) {
                skin_slot.attachment.region_name = parent_attachment.region_name;
            }
        }

        evaluating[skin_index][attachment_index] = false;
        resolved[skin_index][attachment_index] = true;
        return std::nullopt;
    };

    for (std::size_t skin_index = 0; skin_index < skins_out->size(); ++skin_index) {
        for (std::size_t attachment_index = 0;
             attachment_index < (*skins_out)[skin_index].slot_attachments.size();
             ++attachment_index) {
            if (const auto error = resolve_attachment(resolve_attachment, skin_index, attachment_index)) {
                return error;
            }
        }
    }

    return std::nullopt;
}

std::optional<LoadError> parse_skins(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    const std::vector<SlotData>& slots,
    std::vector<SkinData>* skins_out) {
    const Value* skins_value = find_optional_member(root, "skins");
    if (skins_value == nullptr) {
        build_default_skin(slots, skins_out);
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *skins_value,
            Value::Type::Object,
            "$.skins")) {
        return error;
    }
    if (skins_value->as_object().empty()) {
        return validation_error(
            document,
            skins_value->location(),
            "$.skins",
            "skins object must not be empty");
    }

    std::vector<SkinData> parsed_skins;
    parsed_skins.reserve(skins_value->as_object().size());

    for (const auto& [skin_name, skin_value] : skins_value->as_object()) {
        const std::string path = "$.skins." + skin_name;
        if (const auto error = json::require_type(
                document,
                skin_value,
                Value::Type::Object,
                path)) {
            return error;
        }

        SkinData skin;
        skin.name = skin_name;
        skin.slot_attachments.reserve(skin_value.as_object().size());
        for (const auto& [slot_name, attachment_value] : skin_value.as_object()) {
            if (is_skin_reserved_key(slot_name)) {
                continue;
            }

            SkinSlotData skin_slot;
            if (const auto error = parse_skin_attachment(
                    document,
                    attachment_value,
                    bones,
                    slots,
                    slot_name,
                    path + "." + slot_name,
                    &skin_slot)) {
                return error;
            }

            const auto duplicate = std::find_if(
                skin.slot_attachments.begin(),
                skin.slot_attachments.end(),
                [&](const SkinSlotData& existing) {
                    return existing.attachment.name == skin_slot.attachment.name;
                });
            if (duplicate != skin.slot_attachments.end()) {
                return validation_error(
                    document,
                    attachment_value.location(),
                    path + "." + slot_name,
                    "skin attachment names must be unique within a skin");
            }

            skin.slot_attachments.push_back(std::move(skin_slot));
        }

        parsed_skins.push_back(std::move(skin));
    }

    if (const auto error = resolve_linked_meshes(
            document,
            find_skin_index(parsed_skins, "default"),
            &parsed_skins)) {
        return error;
    }

    *skins_out = std::move(parsed_skins);
    return std::nullopt;
}

template <typename NamedValue>
std::optional<LoadError> parse_skin_scope_members(
    const Document& document,
    const Value& skin_value,
    std::string_view key,
    std::string_view json_path,
    const std::vector<NamedValue>& values,
    std::string_view label,
    std::vector<std::size_t>* indices_out) {
    indices_out->clear();

    const Value* member = find_optional_member(skin_value, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Array,
            std::string(json_path) + "." + std::string(key))) {
        return error;
    }

    indices_out->reserve(member->as_array().size());
    for (std::size_t name_index = 0; name_index < member->as_array().size(); ++name_index) {
        const Value& name_value = member->as_array()[name_index];
        const std::string item_path =
            std::string(json_path) + "." + std::string(key) + "[" +
            std::to_string(name_index) + "]";
        if (const auto error = json::require_type(
                document,
                name_value,
                Value::Type::String,
                item_path)) {
            return error;
        }

        const auto resolved_index = find_named_index(values, name_value.as_string());
        if (!resolved_index.has_value()) {
            return validation_error(
                document,
                name_value.location(),
                item_path,
                "skin references unknown " + std::string(label) + " '" +
                    name_value.as_string() + "'");
        }
        if (std::find(indices_out->begin(), indices_out->end(), *resolved_index) !=
            indices_out->end()) {
            return validation_error(
                document,
                name_value.location(),
                item_path,
                "skin-scoped " + std::string(label) + " names must be unique");
        }

        indices_out->push_back(*resolved_index);
    }

    return std::nullopt;
}

std::optional<LoadError> resolve_skin_scopes(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    const std::vector<IkConstraintData>& ik_constraints,
    const std::vector<PathConstraintData>& path_constraints,
    const std::vector<TransformConstraintData>& transform_constraints,
    const std::vector<PhysicsConstraintData>& physics_constraints,
    std::vector<SkinData>* skins_out) {
    const Value* skins_value = find_optional_member(root, "skins");
    if (skins_value == nullptr) {
        return std::nullopt;
    }

    for (SkinData& skin : *skins_out) {
        const Value* skin_value = find_optional_member(*skins_value, skin.name);
        if (skin_value == nullptr || !skin_value->is_object()) {
            continue;
        }

        if (const auto error = parse_skin_scope_members(
                document,
                *skin_value,
                "bones",
                "$.skins." + skin.name,
                bones,
                "bone",
                &skin.bone_indices)) {
            return error;
        }
        if (const auto error = parse_skin_scope_members(
                document,
                *skin_value,
                "ik",
                "$.skins." + skin.name,
                ik_constraints,
                "ik constraint",
                &skin.ik_constraint_indices)) {
            return error;
        }
        if (const auto error = parse_skin_scope_members(
                document,
                *skin_value,
                "path",
                "$.skins." + skin.name,
                path_constraints,
                "path constraint",
                &skin.path_constraint_indices)) {
            return error;
        }
        if (const auto error = parse_skin_scope_members(
                document,
                *skin_value,
                "transform",
                "$.skins." + skin.name,
                transform_constraints,
                "transform constraint",
                &skin.transform_constraint_indices)) {
            return error;
        }
        if (const auto error = parse_skin_scope_members(
                document,
                *skin_value,
                "physics",
                "$.skins." + skin.name,
                physics_constraints,
                "physics constraint",
                &skin.physics_constraint_indices)) {
            return error;
        }
    }

    return std::nullopt;
}

std::optional<LoadError> parse_animations(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
    const std::vector<EventDefinition>& events,
    const std::vector<SlotData>& slots,
    const std::vector<SkinData>& skins,
    std::vector<AnimationData>* animations_out) {
    const Value* animations = nullptr;
    if (const auto error = json::require_member(
            document, root, "animations", Value::Type::Object, "$", &animations)) {
        return error;
    }
    if (animations->as_object().empty()) {
        return validation_error(
            document,
            animations->location(),
            "$.animations",
            "object must not be empty");
    }

    std::vector<AnimationData> parsed_animations;
    parsed_animations.reserve(animations->as_object().size());
    const std::optional<std::size_t> default_skin_index = find_skin_index(skins, "default");

    for (const auto& [name, animation_value] : animations->as_object()) {
        const std::string path = "$.animations." + name;
        if (const auto error = json::require_type(
                document, animation_value, Value::Type::Object, path)) {
            return error;
        }

        AnimationData animation;
        animation.name = name;

        if (const Value* animation_bones = find_optional_member(animation_value, "bones")) {
            if (const auto error = json::require_type(
                    document, *animation_bones, Value::Type::Object, path + ".bones")) {
                return error;
            }

            for (const auto& [bone_name, bone_track_value] : animation_bones->as_object()) {
                if (const auto error = json::require_type(
                        document,
                        bone_track_value,
                        Value::Type::Object,
                        path + ".bones." + bone_name)) {
                    return error;
                }

                const auto bone_index = find_bone_index(bones, bone_name);
                if (!bone_index.has_value()) {
                    return validation_error(
                        document,
                        bone_track_value.location(),
                        path + ".bones." + bone_name,
                        "animation references unknown bone '" + bone_name + "'");
                }

                if (std::find(
                        animation.targeted_bone_indices.begin(),
                        animation.targeted_bone_indices.end(),
                        *bone_index) == animation.targeted_bone_indices.end()) {
                    animation.targeted_bone_indices.push_back(*bone_index);
                }

                if (const Value* rotate_timeline = find_optional_member(bone_track_value, "rotate")) {
                    BoneRotateTimeline parsed_timeline;
                    if (const auto error = parse_rotate_timeline(
                            document,
                            *rotate_timeline,
                            *bone_index,
                            path + ".bones." + bone_name + ".rotate",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.bone_rotate_timelines.push_back(std::move(parsed_timeline));
                }
                if (const Value* inherit_timeline = find_optional_member(bone_track_value, "inherit")) {
                    BoneInheritTimeline parsed_timeline;
                    if (const auto error = parse_inherit_timeline(
                            document,
                            *inherit_timeline,
                            *bone_index,
                            path + ".bones." + bone_name + ".inherit",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.bone_inherit_timelines.push_back(std::move(parsed_timeline));
                }
                if (const Value* translate_timeline = find_optional_member(bone_track_value, "translate")) {
                    BoneTranslateTimeline parsed_timeline;
                    if (const auto error = parse_vector_timeline(
                            document,
                            *translate_timeline,
                            *bone_index,
                            path + ".bones." + bone_name + ".translate",
                            "translate",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.bone_translate_timelines.push_back(std::move(parsed_timeline));
                }
                if (const Value* scale_timeline = find_optional_member(bone_track_value, "scale")) {
                    BoneScaleTimeline parsed_timeline;
                    if (const auto error = parse_vector_timeline(
                            document,
                            *scale_timeline,
                            *bone_index,
                            path + ".bones." + bone_name + ".scale",
                            "scale",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.bone_scale_timelines.push_back(std::move(parsed_timeline));
                }
                if (const Value* shear_timeline = find_optional_member(bone_track_value, "shear")) {
                    BoneShearTimeline parsed_timeline;
                    if (const auto error = parse_vector_timeline(
                            document,
                            *shear_timeline,
                            *bone_index,
                            path + ".bones." + bone_name + ".shear",
                            "shear",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.bone_shear_timelines.push_back(std::move(parsed_timeline));
                }
            }
        }

        if (const Value* animation_slots = find_optional_member(animation_value, "slots")) {
            if (const auto error = json::require_type(
                    document, *animation_slots, Value::Type::Object, path + ".slots")) {
                return error;
            }

            for (const auto& [slot_name, slot_track_value] : animation_slots->as_object()) {
                if (const auto error = json::require_type(
                        document,
                        slot_track_value,
                        Value::Type::Object,
                        path + ".slots." + slot_name)) {
                    return error;
                }

                const auto slot_index = find_slot_index(slots, slot_name);
                if (!slot_index.has_value()) {
                    return validation_error(
                        document,
                        slot_track_value.location(),
                        path + ".slots." + slot_name,
                        "animation references unknown slot '" + slot_name + "'");
                }

                if (const Value* attachment_timeline =
                        find_optional_member(slot_track_value, "attachment")) {
                    SlotAttachmentTimeline parsed_timeline;
                    if (const auto error = parse_attachment_timeline(
                            document,
                            *attachment_timeline,
                            *slot_index,
                            path + ".slots." + slot_name + ".attachment",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.slot_attachment_timelines.push_back(std::move(parsed_timeline));
                }
                if (const Value* color_timeline = find_optional_member(slot_track_value, "color")) {
                    SlotColorTimeline parsed_timeline;
                    if (const auto error = parse_color_timeline(
                            document,
                            *color_timeline,
                            *slot_index,
                            path + ".slots." + slot_name + ".color",
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.slot_color_timelines.push_back(std::move(parsed_timeline));
                }
            }
        }

        if (const Value* animation_deform = find_optional_member(animation_value, "deform")) {
            if (const auto error = json::require_type(
                    document,
                    *animation_deform,
                    Value::Type::Object,
                    path + ".deform")) {
                return error;
            }

            for (const auto& [slot_name, slot_deform_value] : animation_deform->as_object()) {
                if (const auto error = json::require_type(
                        document,
                        slot_deform_value,
                        Value::Type::Object,
                        path + ".deform." + slot_name)) {
                    return error;
                }

                const auto slot_index = find_slot_index(slots, slot_name);
                if (!slot_index.has_value()) {
                    return validation_error(
                        document,
                        slot_deform_value.location(),
                        path + ".deform." + slot_name,
                        "animation references unknown slot '" + slot_name + "'");
                }

                for (const auto& [attachment_name, deform_timeline_value] :
                     slot_deform_value.as_object()) {
                    const std::string deform_path =
                        path + ".deform." + slot_name + "." + attachment_name;
                    const AttachmentData* attachment = find_attachment_source_in_skins(
                        skins,
                        default_skin_index,
                        *slot_index,
                        attachment_name);
                    if (attachment == nullptr) {
                        return validation_error(
                            document,
                            deform_timeline_value.location(),
                            deform_path,
                            "deform timeline references unknown attachment '" +
                                attachment_name + "'");
                    }
                    if (attachment->mesh_geometry == nullptr) {
                        return validation_error(
                            document,
                            deform_timeline_value.location(),
                            deform_path,
                            "deform timelines require a mesh attachment target");
                    }

                    MeshDeformTimeline parsed_timeline;
                    if (const auto error = parse_deform_timeline(
                            document,
                            deform_timeline_value,
                            *slot_index,
                            attachment_name,
                            attachment->mesh_geometry->vertices.size(),
                            deform_path,
                            &parsed_timeline)) {
                        return error;
                    }

                    animation.mesh_deform_timelines.push_back(std::move(parsed_timeline));
                }
            }
        }

        if (const Value* draw_order_timeline = find_optional_member(animation_value, "drawOrder")) {
            DrawOrderTimeline parsed_timeline;
            if (const auto error = parse_draw_order_timeline(
                    document,
                    *draw_order_timeline,
                    slots,
                    path + ".drawOrder",
                    &parsed_timeline)) {
                return error;
            }

            animation.draw_order_timeline_data = std::move(parsed_timeline);
        }
        if (const Value* event_timeline = find_optional_member(animation_value, "events")) {
            EventTimeline parsed_timeline;
            if (const auto error = parse_event_timeline(
                    document,
                    *event_timeline,
                    events,
                    path + ".events",
                    &parsed_timeline)) {
                return error;
            }

            animation.event_timeline_data = std::move(parsed_timeline);
        }

        parsed_animations.push_back(std::move(animation));
    }

    *animations_out = std::move(parsed_animations);
    return std::nullopt;
}

std::optional<LoadError> parse_mixing(
    const Document& document,
    const Value& root,
    const std::vector<AnimationData>& animations,
    double* default_mix_duration_out,
    std::vector<AnimationMixDefinition>* mix_definitions_out) {
    *default_mix_duration_out = 0.0;
    mix_definitions_out->clear();

    const Value* mixing_value = find_optional_member(root, "mixing");
    if (mixing_value == nullptr) {
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *mixing_value,
            Value::Type::Object,
            "$.mixing")) {
        return error;
    }

    if (const auto error = read_optional_number(
            document,
            *mixing_value,
            "default_mix",
            "$.mixing",
            default_mix_duration_out)) {
        return error;
    }
    if (*default_mix_duration_out < 0.0) {
        return validation_error(
            document,
            mixing_value->location(),
            "$.mixing.default_mix",
            "default mix duration must be non-negative");
    }

    const Value* entries_value = find_optional_member(*mixing_value, "entries");
    if (entries_value == nullptr) {
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *entries_value,
            Value::Type::Array,
            "$.mixing.entries")) {
        return error;
    }
    if (entries_value->as_array().empty()) {
        return validation_error(
            document,
            entries_value->location(),
            "$.mixing.entries",
            "mix entries must not be empty when provided");
    }

    mix_definitions_out->reserve(entries_value->as_array().size());
    for (std::size_t entry_index = 0; entry_index < entries_value->as_array().size(); ++entry_index) {
        const Value& entry_value = entries_value->as_array()[entry_index];
        const std::string entry_path =
            "$.mixing.entries[" + std::to_string(entry_index) + "]";
        if (const auto error = json::require_type(
                document,
                entry_value,
                Value::Type::Object,
                entry_path)) {
            return error;
        }

        AnimationMixDefinition mix_definition;
        if (const auto error = read_required_string(
                document,
                entry_value,
                "from",
                entry_path,
                &mix_definition.from_animation)) {
            return error;
        }
        if (mix_definition.from_animation.empty()) {
            return validation_error(
                document,
                entry_value.location(),
                entry_path + ".from",
                "mix entry source animation must not be empty");
        }
        mix_definition.from_any = mix_definition.from_animation == "*";

        if (const auto error = read_required_string(
                document,
                entry_value,
                "to",
                entry_path,
                &mix_definition.to_animation)) {
            return error;
        }
        if (mix_definition.to_animation.empty()) {
            return validation_error(
                document,
                entry_value.location(),
                entry_path + ".to",
                "mix entry target animation must not be empty");
        }
        if (find_animation(animations, mix_definition.to_animation) == nullptr) {
            return validation_error(
                document,
                entry_value.location(),
                entry_path + ".to",
                "mix entry references unknown target animation '" +
                    mix_definition.to_animation + "'");
        }
        if (!mix_definition.from_any &&
            find_animation(animations, mix_definition.from_animation) == nullptr) {
            return validation_error(
                document,
                entry_value.location(),
                entry_path + ".from",
                "mix entry references unknown source animation '" +
                    mix_definition.from_animation + "'");
        }

        if (const auto error = read_required_number(
                document,
                entry_value,
                "duration",
                entry_path,
                &mix_definition.duration)) {
            return error;
        }
        if (mix_definition.duration < 0.0) {
            return validation_error(
                document,
                entry_value.location(),
                entry_path + ".duration",
                "mix duration must be non-negative");
        }

        mix_definitions_out->push_back(std::move(mix_definition));
    }

    return std::nullopt;
}

} // namespace

const AttachmentData* SkinData::find_attachment(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_attachments.begin(),
        slot_attachments.end(),
        [&](const SkinSlotData& slot_attachment) {
            return slot_attachment.slot_index == slot_index;
        });
    if (it == slot_attachments.end()) {
        return nullptr;
    }

    return &it->attachment;
}

const AttachmentData* SkinData::find_attachment(std::string_view attachment_name) const {
    const auto it = std::find_if(
        slot_attachments.begin(),
        slot_attachments.end(),
        [&](const SkinSlotData& slot_attachment) {
            return slot_attachment.attachment.name == attachment_name;
        });
    if (it == slot_attachments.end()) {
        return nullptr;
    }

    return &it->attachment;
}

SkeletonData::SkeletonData(
    SkeletonInfo info,
    std::vector<BoneData> bones,
    std::vector<IkConstraintData> ik_constraints,
    std::vector<PathConstraintData> path_constraints,
    std::vector<TransformConstraintData> transform_constraints,
    std::vector<PhysicsConstraintData> physics_constraints,
    std::vector<SlotData> slots,
    std::vector<EventDefinition> events,
    std::vector<AnimationData> animations,
    std::vector<SkinData> skins,
    double default_mix_duration,
    std::vector<AnimationMixDefinition> mix_definitions)
    : info_(std::move(info)),
      bones_(std::move(bones)),
      ik_constraints_(std::move(ik_constraints)),
      path_constraints_(std::move(path_constraints)),
      transform_constraints_(std::move(transform_constraints)),
      physics_constraints_(std::move(physics_constraints)),
      slots_(std::move(slots)),
      events_(std::move(events)),
      animations_(std::move(animations)),
      skins_(std::move(skins)),
      default_skin_index_(marrow::runtime::find_skin_index(skins_, "default")),
      default_mix_duration_(default_mix_duration),
      mix_definitions_(std::move(mix_definitions)) {}

const SkeletonInfo& SkeletonData::info() const {
    return info_;
}

const std::vector<BoneData>& SkeletonData::bones() const {
    return bones_;
}

const std::vector<IkConstraintData>& SkeletonData::ik_constraints() const {
    return ik_constraints_;
}

const std::vector<PathConstraintData>& SkeletonData::path_constraints() const {
    return path_constraints_;
}

const std::vector<TransformConstraintData>& SkeletonData::transform_constraints() const {
    return transform_constraints_;
}

const std::vector<PhysicsConstraintData>& SkeletonData::physics_constraints() const {
    return physics_constraints_;
}

const std::vector<SlotData>& SkeletonData::slots() const {
    return slots_;
}

const std::vector<EventDefinition>& SkeletonData::events() const {
    return events_;
}

const std::vector<AnimationData>& SkeletonData::animations() const {
    return animations_;
}

const std::vector<SkinData>& SkeletonData::skins() const {
    return skins_;
}

const std::vector<AnimationMixDefinition>& SkeletonData::mix_definitions() const {
    return mix_definitions_;
}

std::optional<std::size_t> SkeletonData::default_skin_index() const {
    return default_skin_index_;
}

double SkeletonData::default_mix_duration() const {
    return default_mix_duration_;
}

const BoneRotateTimeline* AnimationData::find_rotate_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_rotate_timelines.begin(),
        bone_rotate_timelines.end(),
        [&](const BoneRotateTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_rotate_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneInheritTimeline* AnimationData::find_inherit_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_inherit_timelines.begin(),
        bone_inherit_timelines.end(),
        [&](const BoneInheritTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_inherit_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneTranslateTimeline* AnimationData::find_translate_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_translate_timelines.begin(),
        bone_translate_timelines.end(),
        [&](const BoneTranslateTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_translate_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneScaleTimeline* AnimationData::find_scale_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_scale_timelines.begin(),
        bone_scale_timelines.end(),
        [&](const BoneScaleTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_scale_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneShearTimeline* AnimationData::find_shear_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_shear_timelines.begin(),
        bone_shear_timelines.end(),
        [&](const BoneShearTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_shear_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const SlotAttachmentTimeline* AnimationData::find_attachment_timeline(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_attachment_timelines.begin(),
        slot_attachment_timelines.end(),
        [&](const SlotAttachmentTimeline& timeline) {
            return timeline.slot_index == slot_index;
        });
    if (it == slot_attachment_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const SlotColorTimeline* AnimationData::find_color_timeline(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_color_timelines.begin(),
        slot_color_timelines.end(),
        [&](const SlotColorTimeline& timeline) {
            return timeline.slot_index == slot_index;
        });
    if (it == slot_color_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const MeshDeformTimeline* AnimationData::find_deform_timeline(
    std::size_t slot_index,
    std::string_view attachment_name) const {
    const auto it = std::find_if(
        mesh_deform_timelines.begin(),
        mesh_deform_timelines.end(),
        [&](const MeshDeformTimeline& timeline) {
            return timeline.slot_index == slot_index &&
                timeline.attachment_name == attachment_name;
        });
    if (it == mesh_deform_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const DrawOrderTimeline* AnimationData::find_draw_order_timeline() const {
    if (!draw_order_timeline_data.has_value()) {
        return nullptr;
    }

    return &(*draw_order_timeline_data);
}

const EventTimeline* AnimationData::find_event_timeline() const {
    if (!event_timeline_data.has_value()) {
        return nullptr;
    }

    return &(*event_timeline_data);
}

std::optional<double> AnimationData::sample_bone_rotation(std::size_t bone_index, double time) const {
    const BoneRotateTimeline* timeline = find_rotate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_rotate_timeline(*timeline, time);
}

const InheritKeyframe* AnimationData::sample_bone_inherit(
    std::size_t bone_index,
    double time) const {
    const BoneInheritTimeline* timeline = find_inherit_timeline(bone_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_inherit_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_translation(
    std::size_t bone_index,
    double time) const {
    const BoneTranslateTimeline* timeline = find_translate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_translate_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_scale(
    std::size_t bone_index,
    double time) const {
    const BoneScaleTimeline* timeline = find_scale_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_scale_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_shear(
    std::size_t bone_index,
    double time) const {
    const BoneShearTimeline* timeline = find_shear_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_shear_timeline(*timeline, time);
}

const AttachmentKeyframe* AnimationData::sample_slot_attachment(
    std::size_t slot_index,
    double time) const {
    const SlotAttachmentTimeline* timeline = find_attachment_timeline(slot_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_attachment_timeline(*timeline, time);
}

std::optional<SlotColor> AnimationData::sample_slot_color(
    std::size_t slot_index,
    double time) const {
    const SlotColorTimeline* timeline = find_color_timeline(slot_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_color_timeline(*timeline, time);
}

std::optional<std::vector<double>> AnimationData::sample_slot_deform(
    std::size_t slot_index,
    std::string_view attachment_name,
    double time) const {
    const MeshDeformTimeline* timeline = find_deform_timeline(slot_index, attachment_name);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_deform_timeline(*timeline, time);
}

const DrawOrderKeyframe* AnimationData::sample_draw_order(double time) const {
    const DrawOrderTimeline* timeline = find_draw_order_timeline();
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_draw_order_timeline(*timeline, time);
}

double AnimationData::duration() const {
    double max_time = 0.0;
    for (const BoneRotateTimeline& timeline : bone_rotate_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneInheritTimeline& timeline : bone_inherit_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneTranslateTimeline& timeline : bone_translate_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneScaleTimeline& timeline : bone_scale_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneShearTimeline& timeline : bone_shear_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const SlotAttachmentTimeline& timeline : slot_attachment_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const SlotColorTimeline& timeline : slot_color_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const MeshDeformTimeline& timeline : mesh_deform_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    if (draw_order_timeline_data.has_value()) {
        max_time = std::max(max_time, timeline_duration(draw_order_timeline_data->keyframes));
    }
    if (event_timeline_data.has_value()) {
        max_time = std::max(max_time, timeline_duration(event_timeline_data->keyframes));
    }

    return max_time;
}

std::optional<std::size_t> SkeletonData::find_bone_index(std::string_view name) const {
    return marrow::runtime::find_bone_index(bones_, name);
}

std::optional<std::size_t> SkeletonData::find_slot_index(std::string_view name) const {
    return marrow::runtime::find_slot_index(slots_, name);
}

std::optional<std::size_t> SkeletonData::find_skin_index(std::string_view name) const {
    return marrow::runtime::find_skin_index(skins_, name);
}

const AnimationData* SkeletonData::find_animation(std::string_view name) const {
    return marrow::runtime::find_animation(animations_, name);
}

const SkinData* SkeletonData::find_skin(std::string_view name) const {
    const auto skin_index = find_skin_index(name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return &skins_[*skin_index];
}

const AttachmentData* SkeletonData::find_attachment(
    std::size_t skin_index,
    std::size_t slot_index) const {
    if (skin_index >= skins_.size()) {
        return nullptr;
    }

    return skins_[skin_index].find_attachment(slot_index);
}

const AttachmentData* SkeletonData::find_attachment(
    std::string_view skin_name,
    std::size_t slot_index) const {
    const auto skin_index = find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return find_attachment(*skin_index, slot_index);
}

const AttachmentData* SkeletonData::find_attachment_source(
    std::size_t slot_index,
    std::string_view attachment_name,
    std::optional<std::size_t>* skin_index_out) const {
    return find_attachment_source_in_skins(
        skins_,
        default_skin_index_,
        slot_index,
        attachment_name,
        skin_index_out);
}

double SkeletonData::mix_duration(
    std::string_view from_animation,
    std::string_view to_animation) const {
    const auto exact = std::find_if(
        mix_definitions_.begin(),
        mix_definitions_.end(),
        [&](const AnimationMixDefinition& mix_definition) {
            return !mix_definition.from_any &&
                mix_definition.from_animation == from_animation &&
                mix_definition.to_animation == to_animation;
        });
    if (exact != mix_definitions_.end()) {
        return exact->duration;
    }

    const auto wildcard = std::find_if(
        mix_definitions_.begin(),
        mix_definitions_.end(),
        [&](const AnimationMixDefinition& mix_definition) {
            return mix_definition.from_any &&
                mix_definition.to_animation == to_animation;
        });
    if (wildcard != mix_definitions_.end()) {
        return wildcard->duration;
    }

    return default_mix_duration_;
}

std::optional<std::size_t> sample_sequence_frame(
    const AttachmentSequenceData& sequence,
    double time_seconds) {
    if (sequence.frame_regions.empty()) {
        return std::nullopt;
    }

    const std::size_t frame_count = sequence.frame_regions.size();
    const std::size_t setup_frame = clamp_sequence_frame_index(sequence, sequence.setup_frame);
    if (frame_count == 1 || sequence.fps <= 0.0) {
        return setup_frame;
    }

    const long long advanced_frames = static_cast<long long>(
        std::floor(std::max(0.0, time_seconds) * sequence.fps));
    switch (sequence.playback_mode) {
    case SequencePlaybackMode::Hold:
        return std::min(
            setup_frame + static_cast<std::size_t>(advanced_frames),
            frame_count - 1);
    case SequencePlaybackMode::Once:
        return std::min(
            setup_frame + static_cast<std::size_t>(advanced_frames),
            frame_count - 1);
    case SequencePlaybackMode::Loop:
        return (setup_frame + (static_cast<std::size_t>(advanced_frames) % frame_count)) % frame_count;
    case SequencePlaybackMode::PingPong: {
        const std::size_t cycle_length = (frame_count - 1) * 2;
        const std::size_t cycle_position =
            (setup_frame + (static_cast<std::size_t>(advanced_frames) % cycle_length)) % cycle_length;
        return map_ping_pong_frame(cycle_position, frame_count);
    }
    case SequencePlaybackMode::OnceReverse:
        if (advanced_frames >= static_cast<long long>(setup_frame)) {
            return 0;
        }
        return static_cast<std::size_t>(static_cast<long long>(setup_frame) - advanced_frames);
    case SequencePlaybackMode::LoopReverse:
        return positive_mod(
            static_cast<long long>(setup_frame) -
                static_cast<long long>(static_cast<std::size_t>(advanced_frames) % frame_count),
            frame_count);
    case SequencePlaybackMode::PingPongReverse: {
        const std::size_t cycle_length = (frame_count - 1) * 2;
        const std::size_t start_position = positive_mod(
            static_cast<long long>(cycle_length) - static_cast<long long>(setup_frame),
            cycle_length);
        const std::size_t cycle_position =
            (start_position + (static_cast<std::size_t>(advanced_frames) % cycle_length)) % cycle_length;
        return map_ping_pong_frame(cycle_position, frame_count);
    }
    }

    return setup_frame;
}

Skeleton::Skeleton(std::shared_ptr<const SkeletonData> data)
    : data_(std::move(data)) {
    if (data_ == nullptr) {
        throw std::invalid_argument("Skeleton requires SkeletonData");
    }

    bone_poses_.resize(data_->bones().size());
    bone_world_transforms_.resize(data_->bones().size());
    slot_states_.resize(data_->slots().size());
    mesh_deform_states_.resize(data_->slots().size());
    draw_order_.resize(data_->slots().size());
    physics_constraint_states_.resize(data_->physics_constraints().size());

    update_active_skin_scopes({});
    set_to_setup_pose();
}

const std::shared_ptr<const SkeletonData>& Skeleton::data() const {
    return data_;
}

void Skeleton::prepare_animation_pose() {
    reset_to_setup_pose_state(true);
}

void Skeleton::apply_setup_attachments() {
    if (slot_states_.size() != data_->slots().size()) {
        slot_states_.resize(data_->slots().size());
    }
    if (mesh_deform_states_.size() != data_->slots().size()) {
        mesh_deform_states_.resize(data_->slots().size());
    }

    for (std::size_t index = 0; index < data_->slots().size(); ++index) {
        slot_states_[index].attachment_name = data_->slots()[index].setup_attachment;
        slot_states_[index].color = data_->slots()[index].color;
        slot_states_[index].dark_color = data_->slots()[index].dark_color;
        mesh_deform_states_[index].attachment_name.clear();
        mesh_deform_states_[index].vertex_offsets.clear();
        std::optional<std::size_t> attachment_skin_index;
        data_->find_attachment_source(
            index,
            slot_states_[index].attachment_name,
            &attachment_skin_index);
        slot_states_[index].attachment_skin_index = attachment_skin_index;
    }

    apply_active_skin_attachments();
}

void Skeleton::apply_active_skin_attachments() {
    for (const std::size_t skin_index : active_skin_indices_) {
        if (skin_index >= data_->skins().size()) {
            continue;
        }

        for (const SkinSlotData& slot_attachment : data_->skins()[skin_index].slot_attachments) {
            if (slot_attachment.slot_index >= slot_states_.size()) {
                continue;
            }

            slot_states_[slot_attachment.slot_index].attachment_name =
                slot_attachment.attachment.name;
            slot_states_[slot_attachment.slot_index].attachment_skin_index = skin_index;
        }
    }
}

void Skeleton::update_active_skin_scopes(const std::vector<std::size_t>& skin_indices) {
    active_skin_indices_.clear();
    if (const std::optional<std::size_t> default_skin_index = data_->default_skin_index();
        default_skin_index.has_value()) {
        active_skin_indices_.push_back(*default_skin_index);
    }
    for (const std::size_t skin_index : skin_indices) {
        if (std::find(active_skin_indices_.begin(), active_skin_indices_.end(), skin_index) ==
            active_skin_indices_.end()) {
            active_skin_indices_.push_back(skin_index);
        }
    }

    active_bones_.assign(data_->bones().size(), true);
    active_ik_constraints_.assign(data_->ik_constraints().size(), true);
    active_path_constraints_.assign(data_->path_constraints().size(), true);
    active_transform_constraints_.assign(data_->transform_constraints().size(), true);
    active_physics_constraints_.assign(data_->physics_constraints().size(), true);

    for (const SkinData& skin : data_->skins()) {
        for (const std::size_t bone_index : skin.bone_indices) {
            if (bone_index < active_bones_.size()) {
                active_bones_[bone_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.ik_constraint_indices) {
            if (constraint_index < active_ik_constraints_.size()) {
                active_ik_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.path_constraint_indices) {
            if (constraint_index < active_path_constraints_.size()) {
                active_path_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.transform_constraint_indices) {
            if (constraint_index < active_transform_constraints_.size()) {
                active_transform_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.physics_constraint_indices) {
            if (constraint_index < active_physics_constraints_.size()) {
                active_physics_constraints_[constraint_index] = false;
            }
        }
    }

    for (const std::size_t skin_index : active_skin_indices_) {
        if (skin_index >= data_->skins().size()) {
            continue;
        }

        const SkinData& skin = data_->skins()[skin_index];
        for (const std::size_t bone_index : skin.bone_indices) {
            if (bone_index < active_bones_.size()) {
                active_bones_[bone_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.ik_constraint_indices) {
            if (constraint_index < active_ik_constraints_.size()) {
                active_ik_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.path_constraint_indices) {
            if (constraint_index < active_path_constraints_.size()) {
                active_path_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.transform_constraint_indices) {
            if (constraint_index < active_transform_constraints_.size()) {
                active_transform_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.physics_constraint_indices) {
            if (constraint_index < active_physics_constraints_.size()) {
                active_physics_constraints_[constraint_index] = true;
            }
        }
    }
}

bool Skeleton::apply_skin_indices(const std::vector<std::size_t>& skin_indices) {
    for (const std::size_t skin_index : skin_indices) {
        if (skin_index >= data_->skins().size()) {
            return false;
        }
    }

    update_active_skin_scopes(skin_indices);
    apply_setup_attachments();
    return true;
}

void Skeleton::reset_physics_state() {
    pending_physics_delta_seconds_ = 0.0;
    physics_constraint_states_.clear();
    physics_constraint_states_.resize(data_->physics_constraints().size());
}

void Skeleton::reset_to_setup_pose_state(bool reset_slots_and_draw_order) {
    if (bone_poses_.size() != data_->bones().size()) {
        bone_poses_.resize(data_->bones().size());
    }
    if (bone_world_transforms_.size() != data_->bones().size()) {
        bone_world_transforms_.resize(data_->bones().size());
    }
    if (mesh_deform_states_.size() != data_->slots().size()) {
        mesh_deform_states_.resize(data_->slots().size());
    }

    for (std::size_t index = 0; index < data_->bones().size(); ++index) {
        bone_poses_[index].local_pose = data_->bones()[index].setup_pose;
        bone_poses_[index].inherit = data_->bones()[index].setup_inherit;
    }
    for (MeshDeformState& deform_state : mesh_deform_states_) {
        deform_state.attachment_name.clear();
        deform_state.vertex_offsets.clear();
    }

    if (reset_slots_and_draw_order) {
        apply_setup_attachments();
        if (draw_order_.size() != data_->slots().size()) {
            draw_order_.resize(data_->slots().size());
        }
        for (std::size_t index = 0; index < data_->slots().size(); ++index) {
            draw_order_[index] = index;
        }
    }
}

void Skeleton::set_to_setup_pose() {
    reset_to_setup_pose_state(true);
    reset_physics_state();
    update_world_transforms();
}

void Skeleton::apply_animation(const AnimationData& animation, double time) {
    apply_animation(animation, time, time, AnimationEventCallback{});
}

void Skeleton::apply_animation(
    const AnimationData& animation,
    double previous_time,
    double time,
    const AnimationEventCallback& event_callback) {
    reset_to_setup_pose_state(false);
    apply_setup_attachments();
    if (draw_order_.size() != data_->slots().size()) {
        draw_order_.resize(data_->slots().size());
    }
    std::iota(draw_order_.begin(), draw_order_.end(), 0);

    for (const std::size_t bone_index : animation.targeted_bone_indices) {
        BoneTransform& pose = bone_poses_[bone_index].local_pose;
        BoneInheritFlags& inherit = bone_poses_[bone_index].inherit;

        if (const std::optional<double> rotation = animation.sample_bone_rotation(bone_index, time)) {
            pose.rotation = *rotation;
        }
        if (const InheritKeyframe* keyframe = animation.sample_bone_inherit(bone_index, time)) {
            inherit = keyframe->flags;
        }
        if (const std::optional<VectorSample> translation =
                animation.sample_bone_translation(bone_index, time)) {
            pose.x = translation->x;
            pose.y = translation->y;
        }
        if (const std::optional<VectorSample> scale = animation.sample_bone_scale(bone_index, time)) {
            pose.scale_x = scale->x;
            pose.scale_y = scale->y;
        }
        if (const std::optional<VectorSample> shear = animation.sample_bone_shear(bone_index, time)) {
            pose.shear_x = shear->x;
            pose.shear_y = shear->y;
        }
    }

    for (const SlotAttachmentTimeline& timeline : animation.slot_attachment_timelines) {
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        const AttachmentKeyframe* keyframe = sample_attachment_timeline(timeline, time);
        if (keyframe == nullptr) {
            continue;
        }

        slot_states_[timeline.slot_index].attachment_name =
            keyframe->attachment_name.value_or(std::string{});
        slot_states_[timeline.slot_index].attachment_skin_index.reset();
        if (keyframe->attachment_name.has_value()) {
            std::optional<std::size_t> attachment_skin_index;
            data_->find_attachment_source(
                timeline.slot_index,
                *keyframe->attachment_name,
                &attachment_skin_index);
            slot_states_[timeline.slot_index].attachment_skin_index = attachment_skin_index;
        }
    }

    for (const SlotColorTimeline& timeline : animation.slot_color_timelines) {
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        if (const std::optional<SlotColor> color = sample_color_timeline(timeline, time)) {
            slot_states_[timeline.slot_index].color = *color;
        }
    }

    for (const MeshDeformTimeline& timeline : animation.mesh_deform_timelines) {
        if (timeline.slot_index >= mesh_deform_states_.size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(timeline.slot_index);
        if (attachment == nullptr ||
            !attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
            continue;
        }

        const std::optional<std::vector<double>> vertex_offsets =
            sample_deform_timeline(timeline, time);
        if (!vertex_offsets.has_value()) {
            continue;
        }

        mesh_deform_states_[timeline.slot_index].attachment_name = timeline.attachment_name;
        mesh_deform_states_[timeline.slot_index].vertex_offsets = *vertex_offsets;
    }

    if (const DrawOrderKeyframe* draw_order_keyframe = animation.sample_draw_order(time)) {
        draw_order_ = draw_order_keyframe->slot_indices;
    }

    if (event_callback && time > previous_time) {
        if (const EventTimeline* event_timeline = animation.find_event_timeline()) {
            for (const EventKeyframe& keyframe : event_timeline->keyframes) {
                if (keyframe.time <= previous_time || keyframe.time > time) {
                    continue;
                }
                if (keyframe.event_index >= data_->events().size()) {
                    continue;
                }

                const EventDefinition& definition = data_->events()[keyframe.event_index];
                AnimationEvent event;
                event.event_index = keyframe.event_index;
                event.name = definition.name;
                event.time = keyframe.time;
                event.int_value = keyframe.int_value.value_or(definition.int_value);
                event.float_value = keyframe.float_value.value_or(definition.float_value);
                event.string_value = keyframe.string_value.value_or(definition.string_value);
                event.audio_path = keyframe.audio_path.has_value()
                    ? keyframe.audio_path
                    : definition.audio_path;
                event.volume = keyframe.volume.value_or(definition.volume);
                event.balance = keyframe.balance.value_or(definition.balance);
                event_callback(event);
            }
        }
    }

    update_world_transforms();
}

void Skeleton::update_physics(double delta_seconds) {
    pending_physics_delta_seconds_ = std::max(0.0, delta_seconds);
    update_world_transforms();
    pending_physics_delta_seconds_ = 0.0;
}

void Skeleton::set_attachment_playback_time(double time_seconds) {
    attachment_playback_time_ = std::max(0.0, time_seconds);
}

void Skeleton::advance_attachment_playback(double delta_seconds) {
    attachment_playback_time_ = std::max(0.0, attachment_playback_time_ + delta_seconds);
}

double Skeleton::attachment_playback_time() const {
    return attachment_playback_time_;
}

void Skeleton::update_world_transforms() {
    if (bone_world_transforms_.size() != bone_poses_.size()) {
        bone_world_transforms_.resize(bone_poses_.size());
    }

    std::vector<BonePose> solved_poses = bone_poses_;
    const auto bone_is_active = [&](std::size_t bone_index) {
        return bone_index < active_bones_.size() ? active_bones_[bone_index] : true;
    };

    const auto compute_world_transforms = [&]() {
        std::vector<bool> evaluated(solved_poses.size(), false);
        std::vector<bool> evaluating(solved_poses.size(), false);

        const auto evaluate_bone = [&](const auto& self, std::size_t bone_index) -> void {
            if (evaluated[bone_index]) {
                return;
            }
            if (evaluating[bone_index]) {
                throw std::runtime_error("Skeleton contains a cyclic bone hierarchy");
            }

            evaluating[bone_index] = true;

            const BonePose& pose = solved_poses[bone_index];
            const BoneData& bone_data = data_->bones()[bone_index];
            if (!bone_data.parent_index.has_value()) {
                bone_world_transforms_[bone_index] = local_world_transform(pose.local_pose);
            } else {
                self(self, *bone_data.parent_index);
                bone_world_transforms_[bone_index] = compose_world_transform(
                    bone_world_transforms_[*bone_data.parent_index],
                    pose);
            }

            evaluating[bone_index] = false;
            evaluated[bone_index] = true;
        };

        for (std::size_t bone_index = 0; bone_index < solved_poses.size(); ++bone_index) {
            evaluate_bone(evaluate_bone, bone_index);
        }
    };

    const auto bone_tip_local_vector = [&](std::size_t bone_index) -> AttachmentVertex {
        AttachmentVertex tip;
        double best_length_squared = 0.0;
        for (std::size_t child_index = 0; child_index < data_->bones().size(); ++child_index) {
            if (data_->bones()[child_index].parent_index != std::optional<std::size_t>{bone_index}) {
                continue;
            }

            const BoneTransform& child_pose = solved_poses[child_index].local_pose;
            const double length_squared =
                (child_pose.x * child_pose.x) + (child_pose.y * child_pose.y);
            if (length_squared <= best_length_squared) {
                continue;
            }

            tip = AttachmentVertex{child_pose.x, child_pose.y};
            best_length_squared = length_squared;
        }

        return tip;
    };

    const auto to_parent_local = [&](std::optional<std::size_t> parent_index,
                                     double world_x,
                                     double world_y) -> AttachmentVertex {
        if (!parent_index.has_value()) {
            return {world_x, world_y};
        }

        return inverse_transform_point(bone_world_transforms_[*parent_index], world_x, world_y);
    };

    compute_world_transforms();

    constexpr double kEpsilon = 1e-8;
    for (std::size_t constraint_index = 0;
         constraint_index < data_->path_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_path_constraints_.size() &&
            !active_path_constraints_[constraint_index]) {
            continue;
        }

        const PathConstraintData& constraint = data_->path_constraints()[constraint_index];
        const double translate_mix = clamp_mix(constraint.translate_mix);
        const double rotate_mix = clamp_mix(constraint.rotate_mix);
        if ((translate_mix <= 0.0 && rotate_mix <= 0.0) ||
            constraint.slot_index >= slot_states_.size() ||
            constraint.slot_index >= data_->slots().size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(constraint.slot_index);
        if (attachment == nullptr || !attachment->path_attachment.has_value()) {
            continue;
        }

        const std::size_t path_bone_index = data_->slots()[constraint.slot_index].bone_index;
        if (path_bone_index >= bone_world_transforms_.size()) {
            continue;
        }

        std::vector<AttachmentVertex> world_control_points;
        world_control_points.reserve(attachment->path_attachment->control_points.size());
        const BoneWorldTransform& path_bone_transform = bone_world_transforms_[path_bone_index];
        for (const AttachmentVertex& point : attachment->path_attachment->control_points) {
            world_control_points.push_back(transform_attachment_vertex(
                path_bone_transform,
                point.x,
                point.y));
        }

        const std::vector<PathDistanceSample> path_samples =
            build_path_distance_samples(world_control_points);
        if (path_samples.empty()) {
            continue;
        }

        const double total_length = path_samples.back().distance;
        const double spacing_distance =
            constraint.spacing_mode == PathConstraintSpacingMode::Percent
            ? total_length * constraint.spacing
            : constraint.spacing;

        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            const double sample_distance = std::clamp(
                (constraint.position * total_length) +
                    (spacing_distance * static_cast<double>(chain_index)),
                0.0,
                total_length);
            const PathDistanceSample sample = sample_path_distance(path_samples, sample_distance);
            BoneTransform& pose = solved_poses[bone_index].local_pose;
            const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;

            if (translate_mix > 0.0) {
                const AttachmentVertex target_local =
                    to_parent_local(parent_index, sample.point.x, sample.point.y);
                pose.x += (target_local.x - pose.x) * translate_mix;
                pose.y += (target_local.y - pose.y) * translate_mix;
            }

            if (rotate_mix > 0.0 && vertex_length(sample.tangent) > kEpsilon) {
                const AttachmentVertex tangent_end_world = add_vertices(sample.point, sample.tangent);
                const AttachmentVertex tangent_origin_local =
                    to_parent_local(parent_index, sample.point.x, sample.point.y);
                const AttachmentVertex tangent_end_local = to_parent_local(
                    parent_index,
                    tangent_end_world.x,
                    tangent_end_world.y);
                const AttachmentVertex tangent_local = subtract_vertices(
                    tangent_end_local,
                    tangent_origin_local);
                if (vertex_length(tangent_local) > kEpsilon) {
                    const double desired_rotation =
                        radians_to_degrees(std::atan2(tangent_local.y, tangent_local.x)) -
                        pose.shear_x;
                    pose.rotation = mix_rotation_degrees(
                        pose.rotation,
                        desired_rotation,
                        rotate_mix);
                }
            }

            compute_world_transforms();
        }
    }

    for (std::size_t constraint_index = 0;
         constraint_index < data_->transform_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_transform_constraints_.size() &&
            !active_transform_constraints_[constraint_index]) {
            continue;
        }

        const TransformConstraintData& constraint =
            data_->transform_constraints()[constraint_index];
        if (constraint.source_bone_index >= solved_poses.size()) {
            continue;
        }
        if (!bone_is_active(constraint.source_bone_index)) {
            continue;
        }

        const double rotate_mix = clamp_mix(constraint.rotate_mix);
        const double translate_mix = clamp_mix(constraint.translate_mix);
        const double scale_mix = clamp_mix(constraint.scale_mix);
        const double shear_mix = clamp_mix(constraint.shear_mix);
        if (rotate_mix <= 0.0 && translate_mix <= 0.0 &&
            scale_mix <= 0.0 && shear_mix <= 0.0) {
            continue;
        }

        const BoneTransform source_pose = solved_poses[constraint.source_bone_index].local_pose;
        const BoneWorldTransform source_world =
            bone_world_transforms_[constraint.source_bone_index];
        for (const std::size_t bone_index : constraint.target_bone_indices) {
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            BoneTransform& pose = solved_poses[bone_index].local_pose;
            if (rotate_mix > 0.0) {
                const double desired_rotation =
                    source_pose.rotation + constraint.offsets.rotation;
                pose.rotation = mix_rotation_degrees(
                    pose.rotation,
                    desired_rotation,
                    rotate_mix);
            }
            if (translate_mix > 0.0) {
                const AttachmentVertex target_local = to_parent_local(
                    data_->bones()[bone_index].parent_index,
                    source_world.world_x + constraint.offsets.x,
                    source_world.world_y + constraint.offsets.y);
                pose.x = mix_scalar(pose.x, target_local.x, translate_mix);
                pose.y = mix_scalar(pose.y, target_local.y, translate_mix);
            }
            if (scale_mix > 0.0) {
                pose.scale_x = mix_scalar(
                    pose.scale_x,
                    source_pose.scale_x + constraint.offsets.scale_x,
                    scale_mix);
                pose.scale_y = mix_scalar(
                    pose.scale_y,
                    source_pose.scale_y + constraint.offsets.scale_y,
                    scale_mix);
            }
            if (shear_mix > 0.0) {
                pose.shear_x = mix_scalar(
                    pose.shear_x,
                    source_pose.shear_x + constraint.offsets.shear_x,
                    shear_mix);
                pose.shear_y = mix_scalar(
                    pose.shear_y,
                    source_pose.shear_y + constraint.offsets.shear_y,
                    shear_mix);
            }
        }

        compute_world_transforms();
    }

    for (std::size_t constraint_index = 0;
         constraint_index < data_->ik_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_ik_constraints_.size() &&
            !active_ik_constraints_[constraint_index]) {
            continue;
        }

        const IkConstraintData& constraint = data_->ik_constraints()[constraint_index];
        const double mix = clamp_mix(constraint.mix);
        if (mix <= 0.0 || constraint.target_bone_index >= bone_world_transforms_.size()) {
            continue;
        }
        if (!bone_is_active(constraint.target_bone_index)) {
            continue;
        }

        const AttachmentVertex target_world{
            bone_world_transforms_[constraint.target_bone_index].world_x,
            bone_world_transforms_[constraint.target_bone_index].world_y};

        if (constraint.bone_indices.size() == 1) {
            const std::size_t bone_index = constraint.bone_indices.front();
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            BoneTransform& pose = solved_poses[bone_index].local_pose;
            const AttachmentVertex target_parent = to_parent_local(
                data_->bones()[bone_index].parent_index,
                target_world.x,
                target_world.y);
            const double delta_x = target_parent.x - pose.x;
            const double delta_y = target_parent.y - pose.y;
            if (std::hypot(delta_x, delta_y) <= kEpsilon) {
                continue;
            }

            const double desired_rotation =
                radians_to_degrees(std::atan2(delta_y, delta_x)) - pose.shear_x;
            pose.rotation = mix_rotation_degrees(pose.rotation, desired_rotation, mix);
            compute_world_transforms();
            continue;
        }

        if (constraint.bone_indices.size() != 2) {
            continue;
        }

        const std::size_t parent_bone_index = constraint.bone_indices[0];
        const std::size_t child_bone_index = constraint.bone_indices[1];
        if (parent_bone_index >= solved_poses.size() || child_bone_index >= solved_poses.size() ||
            !bone_is_active(parent_bone_index) || !bone_is_active(child_bone_index)) {
            continue;
        }

        BoneTransform& parent_pose = solved_poses[parent_bone_index].local_pose;
        BoneTransform& child_pose = solved_poses[child_bone_index].local_pose;
        const std::optional<std::size_t> grandparent_index = data_->bones()[parent_bone_index].parent_index;
        const AttachmentVertex target_root = to_parent_local(
            grandparent_index,
            target_world.x,
            target_world.y);
        const double target_x = target_root.x - parent_pose.x;
        const double target_y = target_root.y - parent_pose.y;

        const AttachmentVertex child_origin_local{child_pose.x, child_pose.y};
        const AttachmentVertex child_tip_local = bone_tip_local_vector(child_bone_index);
        const double parent_length = std::hypot(child_origin_local.x, child_origin_local.y);
        const double child_length = std::hypot(child_tip_local.x, child_tip_local.y);
        if (parent_length <= kEpsilon || child_length <= kEpsilon) {
            continue;
        }

        double distance = std::hypot(target_x, target_y);
        double direction_x = target_x;
        double direction_y = target_y;
        if (distance <= kEpsilon) {
            distance = kEpsilon;
            direction_x = parent_length + child_length;
            direction_y = 0.0;
        }

        const double min_distance = std::abs(parent_length - child_length);
        const double max_distance = parent_length + child_length;
        const double clamped_distance = std::clamp(distance, min_distance, max_distance);
        const double along =
            ((clamped_distance * clamped_distance) + (parent_length * parent_length) -
             (child_length * child_length)) /
            (2.0 * clamped_distance);
        const double perpendicular_squared =
            std::max(0.0, (parent_length * parent_length) - (along * along));
        const double perpendicular = std::sqrt(perpendicular_squared);

        const double unit_x = direction_x / distance;
        const double unit_y = direction_y / distance;
        const double bend_sign = constraint.bend_positive ? 1.0 : -1.0;
        const double elbow_x = (unit_x * along) + ((-unit_y) * perpendicular * bend_sign);
        const double elbow_y = (unit_y * along) + (unit_x * perpendicular * bend_sign);

        const double child_origin_angle =
            radians_to_degrees(std::atan2(child_origin_local.y, child_origin_local.x));
        const double child_tip_angle =
            radians_to_degrees(std::atan2(child_tip_local.y, child_tip_local.x));
        const double desired_parent_rotation =
            radians_to_degrees(std::atan2(elbow_y, elbow_x)) -
            child_origin_angle -
            parent_pose.shear_x;
        const double desired_child_rotation =
            radians_to_degrees(std::atan2(target_y - elbow_y, target_x - elbow_x)) -
            desired_parent_rotation -
            child_tip_angle -
            child_pose.shear_x;

        parent_pose.rotation = mix_rotation_degrees(
            parent_pose.rotation,
            desired_parent_rotation,
            mix);
        child_pose.rotation = mix_rotation_degrees(
            child_pose.rotation,
            desired_child_rotation,
            mix);
        compute_world_transforms();
    }

    if (physics_constraint_states_.size() != data_->physics_constraints().size()) {
        physics_constraint_states_.resize(data_->physics_constraints().size());
    }
    const double physics_delta_seconds = std::max(0.0, pending_physics_delta_seconds_);
    for (std::size_t constraint_index = 0;
         constraint_index < data_->physics_constraints().size();
         ++constraint_index) {
        const PhysicsConstraintData& constraint = data_->physics_constraints()[constraint_index];
        if (constraint_index < active_physics_constraints_.size() &&
            !active_physics_constraints_[constraint_index]) {
            continue;
        }
        const double mix = clamp_mix(constraint.mix);
        if (mix <= 0.0 || constraint.bone_indices.empty()) {
            continue;
        }

        PhysicsConstraintState& constraint_state = physics_constraint_states_[constraint_index];
        if (constraint_state.bones.size() != constraint.bone_indices.size()) {
            constraint_state.bones.clear();
            constraint_state.bones.resize(constraint.bone_indices.size());
        }

        const double lag_mix = clamp_mix(constraint.inertia);
        const double strength = std::max(0.0, constraint.strength);
        const double damping_factor =
            std::exp(-std::max(0.0, constraint.damping) * physics_delta_seconds);
        const AttachmentVertex external_force = add_vertices(constraint.gravity, constraint.wind);

        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            const AttachmentVertex tip_local = bone_tip_local_vector(bone_index);
            if (vertex_length(tip_local) <= kEpsilon) {
                continue;
            }

            BoneTransform& pose = solved_poses[bone_index].local_pose;
            const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
            const BoneWorldTransform desired_world_transform =
                parent_index.has_value()
                ? compose_world_transform(
                      bone_world_transforms_[*parent_index],
                      solved_poses[bone_index])
                : local_world_transform(pose);
            const AttachmentVertex desired_tip = transform_attachment_vertex(
                desired_world_transform,
                tip_local.x,
                tip_local.y);

            PhysicsBoneState& bone_state = constraint_state.bones[chain_index];
            if (!bone_state.initialized) {
                bone_state.tip_position = desired_tip;
                bone_state.tip_velocity = {};
                bone_state.initialized = true;
            }

            if (physics_delta_seconds > 0.0) {
                const AttachmentVertex spring_force = scale_vertex(
                    subtract_vertices(desired_tip, bone_state.tip_position),
                    strength);
                bone_state.tip_velocity = scale_vertex(
                    add_vertices(
                        bone_state.tip_velocity,
                        scale_vertex(
                            add_vertices(spring_force, external_force),
                            physics_delta_seconds)),
                    damping_factor);
                bone_state.tip_position = add_vertices(
                    bone_state.tip_position,
                    scale_vertex(bone_state.tip_velocity, physics_delta_seconds));
            }

            const AttachmentVertex visible_tip = add_vertices(
                desired_tip,
                scale_vertex(
                    subtract_vertices(bone_state.tip_position, desired_tip),
                    lag_mix));
            const AttachmentVertex target_parent = to_parent_local(
                parent_index,
                visible_tip.x,
                visible_tip.y);
            const AttachmentVertex parent_delta{
                target_parent.x - pose.x,
                target_parent.y - pose.y};
            if (vertex_length(parent_delta) <= kEpsilon) {
                continue;
            }

            const double tip_angle =
                radians_to_degrees(std::atan2(tip_local.y, tip_local.x));
            const double desired_rotation =
                radians_to_degrees(std::atan2(parent_delta.y, parent_delta.x)) -
                tip_angle -
                pose.shear_x;
            pose.rotation = mix_rotation_degrees(pose.rotation, desired_rotation, mix);
            compute_world_transforms();
            bone_state.tip_position = transform_attachment_vertex(
                bone_world_transforms_[bone_index],
                tip_local.x,
                tip_local.y);
        }
    }
}

const std::vector<BonePose>& Skeleton::bone_poses() const {
    return bone_poses_;
}

std::vector<BonePose>& Skeleton::bone_poses() {
    return bone_poses_;
}

const std::vector<BoneWorldTransform>& Skeleton::bone_world_transforms() const {
    return bone_world_transforms_;
}

const std::vector<SlotState>& Skeleton::slot_states() const {
    return slot_states_;
}

std::vector<SlotState>& Skeleton::slot_states() {
    return slot_states_;
}

const std::vector<MeshDeformState>& Skeleton::mesh_deform_states() const {
    return mesh_deform_states_;
}

std::vector<MeshDeformState>& Skeleton::mesh_deform_states() {
    return mesh_deform_states_;
}

const std::vector<std::size_t>& Skeleton::draw_order() const {
    return draw_order_;
}

std::vector<std::size_t>& Skeleton::draw_order() {
    return draw_order_;
}

bool Skeleton::set_skin(std::string_view skin_name) {
    const auto skin_index = data_->find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return false;
    }

    return apply_skin_indices({*skin_index});
}

bool Skeleton::set_skin_composition(const std::vector<std::string_view>& skin_names) {
    std::vector<std::size_t> skin_indices;
    skin_indices.reserve(skin_names.size());
    for (const std::string_view skin_name : skin_names) {
        const auto skin_index = data_->find_skin_index(skin_name);
        if (!skin_index.has_value()) {
            return false;
        }

        skin_indices.push_back(*skin_index);
    }

    return apply_skin_indices(skin_indices);
}

bool Skeleton::is_bone_active(std::size_t bone_index) const {
    if (bone_index >= active_bones_.size()) {
        return false;
    }

    return active_bones_[bone_index];
}

const AttachmentData* Skeleton::current_attachment(std::size_t slot_index) const {
    if (slot_index >= slot_states_.size()) {
        return nullptr;
    }

    const SlotState& slot_state = slot_states_[slot_index];
    if (slot_state.attachment_name.empty()) {
        return nullptr;
    }

    if (slot_state.attachment_skin_index.has_value()) {
        const AttachmentData* attachment =
            data_->find_attachment(*slot_state.attachment_skin_index, slot_index);
        if (attachment != nullptr && attachment->name == slot_state.attachment_name) {
            return attachment;
        }
    }

    return data_->find_attachment_source(slot_index, slot_state.attachment_name);
}

std::optional<std::size_t> Skeleton::current_sequence_frame(std::size_t slot_index) const {
    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || !attachment->sequence.has_value()) {
        return std::nullopt;
    }

    return sample_sequence_frame(*attachment->sequence, attachment_playback_time_);
}

std::string_view Skeleton::current_region_name(std::size_t slot_index) const {
    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment != nullptr) {
        if (attachment->sequence.has_value()) {
            const std::optional<std::size_t> sequence_frame = current_sequence_frame(slot_index);
            if (sequence_frame.has_value() &&
                *sequence_frame < attachment->sequence->frame_regions.size()) {
                return attachment->sequence->frame_regions[*sequence_frame];
            }
        }

        if (!attachment->region_name.empty()) {
            return attachment->region_name;
        }
    }

    if (slot_index < slot_states_.size()) {
        return slot_states_[slot_index].attachment_name;
    }

    return {};
}

const std::vector<double>* Skeleton::current_mesh_vertex_offsets(std::size_t slot_index) const {
    if (slot_index >= mesh_deform_states_.size()) {
        return nullptr;
    }

    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return nullptr;
    }

    const MeshDeformState& deform_state = mesh_deform_states_[slot_index];
    if (deform_state.vertex_offsets.empty() ||
        !attachment_matches_mesh_deform_source(*attachment, deform_state.attachment_name) ||
        deform_state.vertex_offsets.size() != attachment->mesh_geometry->vertices.size()) {
        return nullptr;
    }

    return &deform_state.vertex_offsets;
}

std::optional<PointAttachmentPose> Skeleton::evaluate_current_point_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || !attachment->point_attachment.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    const PointAttachmentData& point_attachment = *attachment->point_attachment;
    PointAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.position = transform_attachment_vertex(
        bone_world_transforms_[bone_index],
        point_attachment.local_position.x,
        point_attachment.local_position.y);
    pose.rotation = transform_attachment_rotation(
        bone_world_transforms_[bone_index],
        point_attachment.rotation);
    return pose;
}

std::optional<BoundingBoxAttachmentPose> Skeleton::evaluate_current_bounding_box_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || !attachment->bounding_box.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    BoundingBoxAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.polygon.reserve(attachment->bounding_box->polygon.size());

    for (const AttachmentVertex& vertex : attachment->bounding_box->polygon) {
        pose.polygon.push_back(transform_attachment_vertex(
            bone_world_transforms_[bone_index],
            vertex.x,
            vertex.y));
    }

    return pose;
}

std::optional<ClippingAttachmentPose> Skeleton::evaluate_current_clipping_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || !attachment->clipping_attachment.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    ClippingAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.end_slot_index = attachment->clipping_attachment->end_slot_index;
    pose.end_slot_name = attachment->clipping_attachment->end_slot_name;
    pose.polygon.reserve(attachment->clipping_attachment->polygon.size());

    for (const AttachmentVertex& vertex : attachment->clipping_attachment->polygon) {
        pose.polygon.push_back(transform_attachment_vertex(
            bone_world_transforms_[bone_index],
            vertex.x,
            vertex.y));
    }

    return pose;
}

std::optional<MeshAttachmentPose> Skeleton::evaluate_current_mesh_attachment(
    std::size_t slot_index) const {
    const AttachmentData* attachment = current_attachment(slot_index);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    const MeshGeometry& geometry = *attachment->mesh_geometry;
    const std::size_t vertex_count = geometry.vertices.size() / 2;
    if (vertex_count == 0 || geometry.weights.size() != vertex_count) {
        return std::nullopt;
    }

    MeshAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.region_name = std::string(current_region_name(slot_index));
    pose.vertices.reserve(vertex_count);
    pose.triangles = geometry.triangles;
    pose.uvs = geometry.uvs;

    const std::vector<double>* vertex_offsets = current_mesh_vertex_offsets(slot_index);

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const MeshGeometry::VertexWeights& vertex_weights = geometry.weights[vertex_index];
        const double offset_x = vertex_offsets != nullptr ? (*vertex_offsets)[vertex_index * 2] : 0.0;
        const double offset_y =
            vertex_offsets != nullptr ? (*vertex_offsets)[(vertex_index * 2) + 1] : 0.0;
        MeshWorldVertex world_vertex;
        for (const MeshGeometry::VertexWeight& influence : vertex_weights.influences) {
            if (influence.bone_index >= bone_world_transforms_.size()) {
                return std::nullopt;
            }

            const MeshWorldVertex transformed = transform_mesh_point(
                bone_world_transforms_[influence.bone_index],
                influence.x + offset_x,
                influence.y + offset_y);
            world_vertex.x += transformed.x * influence.weight;
            world_vertex.y += transformed.y * influence.weight;
        }

        pose.vertices.push_back(world_vertex);
    }

    return pose;
}

void SkeletonBounds::update(const Skeleton& skeleton, bool compute_aabb) {
    bounding_boxes_.clear();
    last_bounding_box_index_.reset();
    has_aabb_ = false;
    min_x_ = 0.0;
    min_y_ = 0.0;
    max_x_ = 0.0;
    max_y_ = 0.0;

    const std::size_t slot_count = skeleton.slot_states().size();
    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
        const std::optional<BoundingBoxAttachmentPose> pose =
            skeleton.evaluate_current_bounding_box_attachment(slot_index);
        if (!pose.has_value()) {
            continue;
        }

        bounding_boxes_.push_back(*pose);
        if (!compute_aabb || pose->polygon.empty()) {
            continue;
        }

        for (const AttachmentVertex& vertex : pose->polygon) {
            if (!has_aabb_) {
                min_x_ = max_x_ = vertex.x;
                min_y_ = max_y_ = vertex.y;
                has_aabb_ = true;
                continue;
            }

            min_x_ = std::min(min_x_, vertex.x);
            min_y_ = std::min(min_y_, vertex.y);
            max_x_ = std::max(max_x_, vertex.x);
            max_y_ = std::max(max_y_, vertex.y);
        }
    }
}

bool SkeletonBounds::contains_point(double x, double y) {
    return contains_point(x, y, nullptr);
}

bool SkeletonBounds::contains_point(
    double x,
    double y,
    const BoundingBoxAttachmentPose** bounding_box) {
    if (bounding_box != nullptr) {
        *bounding_box = nullptr;
    }
    last_bounding_box_index_.reset();

    for (std::size_t index = 0; index < bounding_boxes_.size(); ++index) {
        if (!polygon_contains_point(bounding_boxes_[index].polygon, x, y)) {
            continue;
        }

        last_bounding_box_index_ = index;
        if (bounding_box != nullptr) {
            *bounding_box = &bounding_boxes_[index];
        }
        return true;
    }

    return false;
}

bool SkeletonBounds::intersects_segment(double x1, double y1, double x2, double y2) {
    return intersects_segment(x1, y1, x2, y2, nullptr);
}

bool SkeletonBounds::intersects_segment(
    double x1,
    double y1,
    double x2,
    double y2,
    const BoundingBoxAttachmentPose** bounding_box) {
    if (bounding_box != nullptr) {
        *bounding_box = nullptr;
    }
    last_bounding_box_index_.reset();

    const AttachmentVertex start{x1, y1};
    const AttachmentVertex end{x2, y2};
    for (std::size_t index = 0; index < bounding_boxes_.size(); ++index) {
        if (!polygon_intersects_segment(bounding_boxes_[index].polygon, start, end)) {
            continue;
        }

        last_bounding_box_index_ = index;
        if (bounding_box != nullptr) {
            *bounding_box = &bounding_boxes_[index];
        }
        return true;
    }

    return false;
}

const BoundingBoxAttachmentPose* SkeletonBounds::get_bounding_box() const {
    if (!last_bounding_box_index_.has_value() ||
        *last_bounding_box_index_ >= bounding_boxes_.size()) {
        return nullptr;
    }

    return &bounding_boxes_[*last_bounding_box_index_];
}

const std::vector<BoundingBoxAttachmentPose>& SkeletonBounds::bounding_boxes() const {
    return bounding_boxes_;
}

const std::vector<AttachmentVertex>* SkeletonBounds::get_polygon(
    std::size_t bounding_box_index) const {
    if (bounding_box_index >= bounding_boxes_.size()) {
        return nullptr;
    }

    return &bounding_boxes_[bounding_box_index].polygon;
}

const std::vector<AttachmentVertex>* SkeletonBounds::get_polygon(
    std::string_view attachment_name) const {
    const auto it = std::find_if(
        bounding_boxes_.begin(),
        bounding_boxes_.end(),
        [&](const BoundingBoxAttachmentPose& bounding_box) {
            return bounding_box.attachment_name == attachment_name;
        });
    if (it == bounding_boxes_.end()) {
        return nullptr;
    }

    return &it->polygon;
}

bool SkeletonBounds::has_aabb() const {
    return has_aabb_;
}

double SkeletonBounds::min_x() const {
    return min_x_;
}

double SkeletonBounds::min_y() const {
    return min_y_;
}

double SkeletonBounds::max_x() const {
    return max_x_;
}

double SkeletonBounds::max_y() const {
    return max_y_;
}

SkeletonDataResult load_skeleton_data(const json::Document& document) {
    SkeletonDataResult result;

    const Value& root = document.root;
    if (const auto error = json::require_type(
            document, root, Value::Type::Object, "$")) {
        result.error = *error;
        return result;
    }
    if (const auto error = json::require_member(
            document, root, "marrow", Value::Type::String, "$")) {
        result.error = *error;
        return result;
    }

    SkeletonInfo info;
    if (const auto error = parse_skeleton_info(document, root, &info)) {
        result.error = *error;
        return result;
    }

    std::vector<BoneData> bones;
    if (const auto error = parse_bones(document, root, &bones)) {
        result.error = *error;
        return result;
    }

    std::vector<IkConstraintData> ik_constraints;
    if (const auto error = parse_ik_constraints(document, root, bones, &ik_constraints)) {
        result.error = *error;
        return result;
    }

    std::vector<SlotData> slots;
    if (const auto error = parse_slots(document, root, bones, &slots)) {
        result.error = *error;
        return result;
    }

    std::vector<EventDefinition> events;
    if (const auto error = parse_events(document, root, &events)) {
        result.error = *error;
        return result;
    }

    std::vector<SkinData> skins;
    if (const auto error = parse_skins(document, root, bones, slots, &skins)) {
        result.error = *error;
        return result;
    }

    std::vector<PathConstraintData> path_constraints;
    if (const auto error = parse_path_constraints(
            document,
            root,
            bones,
            slots,
            skins,
            &path_constraints)) {
        result.error = *error;
        return result;
    }

    std::vector<TransformConstraintData> transform_constraints;
    if (const auto error = parse_transform_constraints(
            document,
            root,
            bones,
            &transform_constraints)) {
        result.error = *error;
        return result;
    }

    std::vector<PhysicsConstraintData> physics_constraints;
    if (const auto error = parse_physics_constraints(
            document,
            root,
            bones,
            &physics_constraints)) {
        result.error = *error;
        return result;
    }

    if (const auto error = resolve_skin_scopes(
            document,
            root,
            bones,
            ik_constraints,
            path_constraints,
            transform_constraints,
            physics_constraints,
            &skins)) {
        result.error = *error;
        return result;
    }

    std::vector<AnimationData> animations;
    if (const auto error = parse_animations(
            document,
            root,
            bones,
            events,
            slots,
            skins,
            &animations)) {
        result.error = *error;
        return result;
    }

    double default_mix_duration = 0.0;
    std::vector<AnimationMixDefinition> mix_definitions;
    if (const auto error = parse_mixing(
            document,
            root,
            animations,
            &default_mix_duration,
            &mix_definitions)) {
        result.error = *error;
        return result;
    }

    result.skeleton_data = std::make_shared<SkeletonData>(
        std::move(info),
        std::move(bones),
        std::move(ik_constraints),
        std::move(path_constraints),
        std::move(transform_constraints),
        std::move(physics_constraints),
        std::move(slots),
        std::move(events),
        std::move(animations),
        std::move(skins),
        default_mix_duration,
        std::move(mix_definitions));
    return result;
}

SkeletonDataResult load_skeleton_data(const std::filesystem::path& path) {
    const auto document_result = load_skeleton_document(path);
    if (!document_result) {
        SkeletonDataResult result;
        result.error = *document_result.error;
        return result;
    }

    return load_skeleton_data(*document_result.document);
}

} // namespace marrow::runtime
