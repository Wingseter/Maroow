#include "skeleton_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace marrow::runtime {
namespace detail {

using json::Document;
using json::LoadError;
using json::SourceLocation;
using json::Value;

struct ParsedBoneData {
    BoneData bone;
    std::optional<std::string> parent_name;
    SourceLocation parent_location{};
};

constexpr int kSupportedSkeletonFormatVersion = 1;

LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    return json::make_validation_error(
        document, location, std::move(json_path), std::move(message));
}

std::runtime_error invalid_transform_constraint_bone_error(
    std::string_view constraint_name,
    std::string_view role,
    std::size_t bone_index,
    std::size_t data_bone_count,
    std::size_t pose_bone_count,
    std::size_t world_bone_count) {
    return std::runtime_error(
        "Transform constraint '" + std::string(constraint_name) + "' has invalid " +
        std::string(role) + " bone index " + std::to_string(bone_index) +
        " (bones=" + std::to_string(data_bone_count) +
        ", poses=" + std::to_string(pose_bone_count) +
        ", world=" + std::to_string(world_bone_count) + ")");
}

std::string describe_bone_cycle(
    const std::vector<BoneData>& bones,
    const std::vector<std::size_t>& remaining_indegree) {
    std::optional<std::size_t> start_index;
    for (std::size_t bone_index = 0; bone_index < remaining_indegree.size(); ++bone_index) {
        if (remaining_indegree[bone_index] > 0) {
            start_index = bone_index;
            break;
        }
    }

    if (!start_index.has_value()) {
        return "cyclic bone hierarchy detected";
    }

    std::vector<std::size_t> path;
    std::vector<std::ptrdiff_t> first_seen(bones.size(), -1);
    std::size_t current = *start_index;
    while (current < bones.size()) {
        if (first_seen[current] >= 0) {
            std::string message = "cyclic bone hierarchy detected: ";
            for (std::size_t index = static_cast<std::size_t>(first_seen[current]);
                 index < path.size();
                 ++index) {
                if (index > static_cast<std::size_t>(first_seen[current])) {
                    message += " -> ";
                }
                message += bones[path[index]].name;
            }
            message += " -> " + bones[current].name;
            return message;
        }

        first_seen[current] = static_cast<std::ptrdiff_t>(path.size());
        path.push_back(current);
        if (!bones[current].parent_index.has_value()) {
            break;
        }

        current = *bones[current].parent_index;
    }

    return "cyclic bone hierarchy detected";
}

std::vector<std::size_t> build_bone_evaluation_order(const std::vector<BoneData>& bones) {
    std::vector<std::vector<std::size_t>> children_by_parent(bones.size());
    std::vector<std::size_t> indegree(bones.size(), 0);

    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        const std::optional<std::size_t> parent_index = bones[bone_index].parent_index;
        if (!parent_index.has_value()) {
            continue;
        }
        if (*parent_index >= bones.size()) {
            throw std::invalid_argument(
                "bone '" + bones[bone_index].name + "' references invalid parent index " +
                std::to_string(*parent_index));
        }

        children_by_parent[*parent_index].push_back(bone_index);
        indegree[bone_index] = 1;
    }

    std::vector<std::size_t> pending = indegree;
    std::vector<std::size_t> queue;
    queue.reserve(bones.size());
    for (std::size_t bone_index = 0; bone_index < pending.size(); ++bone_index) {
        if (pending[bone_index] == 0) {
            queue.push_back(bone_index);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(bones.size());
    for (std::size_t queue_index = 0; queue_index < queue.size(); ++queue_index) {
        const std::size_t bone_index = queue[queue_index];
        order.push_back(bone_index);

        for (const std::size_t child_index : children_by_parent[bone_index]) {
            if (pending[child_index] == 0) {
                continue;
            }

            --pending[child_index];
            if (pending[child_index] == 0) {
                queue.push_back(child_index);
            }
        }
    }

    if (order.size() != bones.size()) {
        throw std::invalid_argument(describe_bone_cycle(bones, pending));
    }

    return order;
}

void normalize_mesh_vertex_weights(
    MeshGeometry::VertexWeights* vertex_weights,
    double total_weight) {
    if (vertex_weights == nullptr || total_weight <= 0.0) {
        return;
    }

    for (MeshGeometry::VertexWeight& influence : vertex_weights->influences) {
        influence.weight /= total_weight;
    }
}

const Value* find_optional_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    return json::find_member(object, key);
}

std::optional<LoadError> validate_format_version(const Document& document, const Value& root) {
    const Value* version_value = json::find_member(root, "version");
    if (version_value == nullptr) {
        return validation_error(
            document,
            root.location(),
            "$.version",
            "missing .mskl format version; re-export or migrate the asset to version " +
                std::to_string(kSupportedSkeletonFormatVersion));
    }
    if (const auto error = json::require_type(
            document, *version_value, Value::Type::Number, "$.version")) {
        return error;
    }

    const double version_number = version_value->as_number();
    if (!std::isfinite(version_number) || std::floor(version_number) != version_number ||
        version_number < 1.0) {
        return validation_error(
            document,
            version_value->location(),
            "$.version",
            ".mskl format version must be a positive integer");
    }

    const int version = static_cast<int>(version_number);
    if (version != kSupportedSkeletonFormatVersion) {
        return validation_error(
            document,
            version_value->location(),
            "$.version",
            "unsupported .mskl format version " + std::to_string(version) +
                "; supported version is " + std::to_string(kSupportedSkeletonFormatVersion) +
                ". Re-export or migrate the asset to version " +
                std::to_string(kSupportedSkeletonFormatVersion));
    }

    return std::nullopt;
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
    return angle - (std::ceil(angle / 360.0 - 0.5) * 360.0);
}

double normalize_rotation_radians(double angle) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kPi2 = kPi * 2.0;
    return angle - (std::ceil(angle / kPi2 - 0.5) * kPi2);
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

BoneWorldTransform root_world_transform(
    const BoneTransform& transform,
    double skeleton_scale_x,
    double skeleton_scale_y) {
    const double rotation_x = degrees_to_radians(transform.rotation + transform.shear_x);
    const double rotation_y = degrees_to_radians(transform.rotation + 90.0 + transform.shear_y);

    BoneWorldTransform world_transform;
    world_transform.a = std::cos(rotation_x) * transform.scale_x * skeleton_scale_x;
    world_transform.b = std::cos(rotation_y) * transform.scale_y * skeleton_scale_x;
    world_transform.c = std::sin(rotation_x) * transform.scale_x * skeleton_scale_y;
    world_transform.d = std::sin(rotation_y) * transform.scale_y * skeleton_scale_y;
    world_transform.world_x = transform.x * skeleton_scale_x;
    world_transform.world_y = transform.y * skeleton_scale_y;
    return world_transform;
}

BoneWorldTransform compose_world_transform(
    const BoneWorldTransform& parent,
    const BonePose& pose,
    double skeleton_scale_x,
    double skeleton_scale_y) {
    const BoneTransform& local_pose = pose.local_pose;
    BoneWorldTransform world_transform;
    world_transform.world_x = parent.a * local_pose.x + parent.b * local_pose.y + parent.world_x;
    world_transform.world_y = parent.c * local_pose.x + parent.d * local_pose.y + parent.world_y;

    double pa = parent.a;
    double pb = parent.b;
    double pc = parent.c;
    double pd = parent.d;

    switch (pose.inherit) {
    case BoneInherit::Normal: {
        const BoneWorldTransform local = local_world_transform(local_pose);
        world_transform.a = pa * local.a + pb * local.c;
        world_transform.b = pa * local.b + pb * local.d;
        world_transform.c = pc * local.a + pd * local.c;
        world_transform.d = pc * local.b + pd * local.d;
        return world_transform;
    }
    case BoneInherit::OnlyTranslation: {
        const BoneWorldTransform local = local_world_transform(local_pose);
        world_transform.a = local.a;
        world_transform.b = local.b;
        world_transform.c = local.c;
        world_transform.d = local.d;
        break;
    }
    case BoneInherit::NoRotationOrReflection: {
        const double inverse_skeleton_scale_x = 1.0 / skeleton_scale_x;
        const double inverse_skeleton_scale_y = 1.0 / skeleton_scale_y;
        pa *= inverse_skeleton_scale_x;
        pc *= inverse_skeleton_scale_y;

        double scale = (pa * pa) + (pc * pc);
        double parent_rotation = 0.0;
        if (scale > 1e-4) {
            scale = std::abs(pa * pd * skeleton_scale_y - pb * skeleton_scale_x * pc) / scale;
            pb = pc * scale;
            pd = pa * scale;
            parent_rotation = radians_to_degrees(std::atan2(pc, pa));
        } else {
            pa = 0.0;
            pc = 0.0;
            parent_rotation = 90.0 - radians_to_degrees(std::atan2(pd, pb));
        }

        const double rotation_x =
            degrees_to_radians(local_pose.rotation + local_pose.shear_x - parent_rotation);
        const double rotation_y =
            degrees_to_radians(local_pose.rotation + local_pose.shear_y - parent_rotation + 90.0);
        const double la = std::cos(rotation_x) * local_pose.scale_x;
        const double lb = std::cos(rotation_y) * local_pose.scale_y;
        const double lc = std::sin(rotation_x) * local_pose.scale_x;
        const double ld = std::sin(rotation_y) * local_pose.scale_y;
        world_transform.a = pa * la - pb * lc;
        world_transform.b = pa * lb - pb * ld;
        world_transform.c = pc * la + pd * lc;
        world_transform.d = pc * lb + pd * ld;
        break;
    }
    case BoneInherit::NoScale:
    case BoneInherit::NoScaleOrReflection: {
        double rotation = degrees_to_radians(local_pose.rotation);
        const double cos_rotation = std::cos(rotation);
        const double sin_rotation = std::sin(rotation);
        double za = (pa * cos_rotation + pb * sin_rotation) / skeleton_scale_x;
        double zc = (pc * cos_rotation + pd * sin_rotation) / skeleton_scale_y;
        double scale = std::sqrt((za * za) + (zc * zc));
        if (scale > 1e-5) {
            scale = 1.0 / scale;
        }
        za *= scale;
        zc *= scale;
        scale = std::sqrt((za * za) + (zc * zc));
        if (pose.inherit == BoneInherit::NoScale &&
            ((pa * pd - pb * pc) < 0.0) != ((skeleton_scale_x < 0.0) != (skeleton_scale_y < 0.0))) {
            scale = -scale;
        }

        constexpr double kPi = 3.14159265358979323846;
        rotation = (kPi / 2.0) + std::atan2(zc, za);
        const double zb = std::cos(rotation) * scale;
        const double zd = std::sin(rotation) * scale;
        const double shear_x = degrees_to_radians(local_pose.shear_x);
        const double shear_y = degrees_to_radians(90.0 + local_pose.shear_y);
        const double la = std::cos(shear_x) * local_pose.scale_x;
        const double lb = std::cos(shear_y) * local_pose.scale_y;
        const double lc = std::sin(shear_x) * local_pose.scale_x;
        const double ld = std::sin(shear_y) * local_pose.scale_y;
        world_transform.a = za * la + zb * lc;
        world_transform.b = za * lb + zb * ld;
        world_transform.c = zc * la + zd * lc;
        world_transform.d = zc * lb + zd * ld;
        break;
    }
    }

    world_transform.a *= skeleton_scale_x;
    world_transform.b *= skeleton_scale_x;
    world_transform.c *= skeleton_scale_y;
    world_transform.d *= skeleton_scale_y;
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

std::string normalize_enum_name(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }

    return normalized;
}

std::optional<BoneInherit> parse_bone_inherit(std::string_view value) {
    const std::string normalized = normalize_enum_name(value);
    if (normalized == "normal") {
        return BoneInherit::Normal;
    }
    if (normalized == "onlytranslation") {
        return BoneInherit::OnlyTranslation;
    }
    if (normalized == "norotationorreflection") {
        return BoneInherit::NoRotationOrReflection;
    }
    if (normalized == "noscale") {
        return BoneInherit::NoScale;
    }
    if (normalized == "noscaleorreflection") {
        return BoneInherit::NoScaleOrReflection;
    }

    return std::nullopt;
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

double world_axis_length(const BoneWorldTransform& transform) {
    return std::hypot(transform.a, transform.c);
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
            normalize_mesh_vertex_weights(&vertex_weights, scalar_weight);
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

        normalize_mesh_vertex_weights(&vertex_weights, total_weight);
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
    double setup_rotation,
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
    timeline_out->setup_rotation = setup_rotation;
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

        const Value* inherit_value = nullptr;
        if (const auto error = json::require_member(
                document,
                keyframe_value,
                "inherit",
                Value::Type::String,
                keyframe_path,
                &inherit_value)) {
            return error;
        }
        const std::optional<BoneInherit> inherit =
            parse_bone_inherit(inherit_value->as_string());
        if (!inherit.has_value()) {
            return validation_error(
                document,
                inherit_value->location(),
                keyframe_path + ".inherit",
                "inherit mode must be one of normal, onlyTranslation, noRotationOrReflection, noScale, or noScaleOrReflection");
        }
        keyframe.inherit = *inherit;

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

        // Events are discrete markers, so equal timestamps are valid and must
        // preserve the authored file order for simultaneous callbacks.
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
        if (const Value* inherit_value = find_optional_member(bone_value, "inherit")) {
            if (const auto error = json::require_type(
                    document,
                    *inherit_value,
                    Value::Type::String,
                    path + ".inherit")) {
                return error;
            }

            const std::optional<BoneInherit> inherit =
                parse_bone_inherit(inherit_value->as_string());
            if (!inherit.has_value()) {
                return validation_error(
                    document,
                    inherit_value->location(),
                    path + ".inherit",
                    "inherit mode must be one of normal, onlyTranslation, noRotationOrReflection, noScale, or noScaleOrReflection");
            }
            parsed_bone.bone.inherit = *inherit;
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
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "softness",
                path,
                &constraint.softness)) {
            return error;
        }

        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "bendPositive",
                path,
                &constraint.bend_positive)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "compress",
                path,
                &constraint.compress)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "stretch",
                path,
                &constraint.stretch)) {
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
                "step",
                path,
                &constraint.step)) {
            return error;
        }
        if (constraint.step <= 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".step",
                "physics step must be greater than zero");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "x",
                path,
                &constraint.x)) {
            return error;
        }
        if (constraint.x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".x",
                "physics x must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "y",
                path,
                &constraint.y)) {
            return error;
        }
        if (constraint.y < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".y",
                "physics y must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotate",
                path,
                &constraint.rotate)) {
            return error;
        }
        if (constraint.rotate < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".rotate",
                "physics rotate must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "scaleX",
                path,
                &constraint.scale_x)) {
            return error;
        }
        if (constraint.scale_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".scaleX",
                "physics scaleX must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "shearX",
                path,
                &constraint.shear_x)) {
            return error;
        }
        if (constraint.shear_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".shearX",
                "physics shearX must be non-negative");
        }

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "limit",
                path,
                &constraint.limit)) {
            return error;
        }
        if (constraint.limit < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".limit",
                "physics limit must be non-negative");
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

        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "massInverse",
                path,
                &constraint.mass_inverse)) {
            return error;
        }
        if (constraint.mass_inverse < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".massInverse",
                "physics massInverse must be non-negative");
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
                            bones[*bone_index].setup_pose.rotation,
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

} // namespace detail

SkeletonDataResult load_skeleton_data(const json::Document& document) {
    SkeletonDataResult result;

    const json::Value& root = document.root;
    if (const auto error = json::require_type(
            document, root, json::Value::Type::Object, "$")) {
        result.error = *error;
        return result;
    }
    if (const auto error = json::require_member(
            document, root, "marrow", json::Value::Type::String, "$")) {
        result.error = *error;
        return result;
    }
    if (const auto error = detail::validate_format_version(document, root)) {
        result.error = *error;
        return result;
    }

    SkeletonInfo info;
    if (const auto error = detail::parse_skeleton_info(document, root, &info)) {
        result.error = *error;
        return result;
    }

    std::vector<BoneData> bones;
    if (const auto error = detail::parse_bones(document, root, &bones)) {
        result.error = *error;
        return result;
    }

    std::vector<IkConstraintData> ik_constraints;
    if (const auto error = detail::parse_ik_constraints(document, root, bones, &ik_constraints)) {
        result.error = *error;
        return result;
    }

    std::vector<SlotData> slots;
    if (const auto error = detail::parse_slots(document, root, bones, &slots)) {
        result.error = *error;
        return result;
    }

    std::vector<EventDefinition> events;
    if (const auto error = detail::parse_events(document, root, &events)) {
        result.error = *error;
        return result;
    }

    std::vector<SkinData> skins;
    if (const auto error = detail::parse_skins(document, root, bones, slots, &skins)) {
        result.error = *error;
        return result;
    }

    std::vector<PathConstraintData> path_constraints;
    if (const auto error = detail::parse_path_constraints(
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
    if (const auto error = detail::parse_transform_constraints(
            document,
            root,
            bones,
            &transform_constraints)) {
        result.error = *error;
        return result;
    }

    std::vector<PhysicsConstraintData> physics_constraints;
    if (const auto error = detail::parse_physics_constraints(
            document,
            root,
            bones,
            &physics_constraints)) {
        result.error = *error;
        return result;
    }

    if (const auto error = detail::resolve_skin_scopes(
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
    if (const auto error = detail::parse_animations(
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
    if (const auto error = detail::parse_mixing(
            document,
            root,
            animations,
            &default_mix_duration,
            &mix_definitions)) {
        result.error = *error;
        return result;
    }

    const json::Value* bones_value = json::find_member(root, "bones");
    const json::SourceLocation bones_location =
        bones_value != nullptr ? bones_value->location() : root.location();
    try {
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
    } catch (const std::invalid_argument& error) {
        result.error = detail::validation_error(document, bones_location, "$.bones", error.what());
    }
    return result;
}

SkeletonDataResult load_skeleton_data(const std::filesystem::path& path) {
    if (path.extension() == skeleton_binary_extension()) {
        const auto document_result = detail::load_binary_skeleton_document(path, true, nullptr);
        if (!document_result) {
            SkeletonDataResult result;
            result.error = *document_result.error;
            return result;
        }

        return load_skeleton_data(*document_result.document);
    }

    const auto document_result = load_skeleton_document(path);
    if (!document_result) {
        SkeletonDataResult result;
        result.error = *document_result.error;
        return result;
    }

    return load_skeleton_data(*document_result.document);
}

} // namespace marrow::runtime
