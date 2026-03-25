#include "marrow/runtime/skeleton.hpp"

#include <algorithm>
#include <cmath>
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

BoneWorldTransform compose_world_transform(
    const BoneWorldTransform& parent,
    const BoneTransform& local_pose) {
    const BoneWorldTransform local = local_world_transform(local_pose);

    BoneWorldTransform world_transform;
    world_transform.a = parent.a * local.a + parent.b * local.c;
    world_transform.b = parent.a * local.b + parent.b * local.d;
    world_transform.c = parent.c * local.a + parent.d * local.c;
    world_transform.d = parent.c * local.b + parent.d * local.d;
    world_transform.world_x = parent.a * local_pose.x + parent.b * local_pose.y + parent.world_x;
    world_transform.world_y = parent.c * local_pose.x + parent.d * local_pose.y + parent.world_y;
    return world_transform;
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

        parsed_slots.push_back(std::move(slot));
    }

    *slots_out = std::move(parsed_slots);
    return std::nullopt;
}

std::optional<LoadError> parse_animations(
    const Document& document,
    const Value& root,
    const std::vector<BoneData>& bones,
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
            }
        }

        parsed_animations.push_back(std::move(animation));
    }

    *animations_out = std::move(parsed_animations);
    return std::nullopt;
}

} // namespace

SkeletonData::SkeletonData(
    SkeletonInfo info,
    std::vector<BoneData> bones,
    std::vector<SlotData> slots,
    std::vector<AnimationData> animations)
    : info_(std::move(info)),
      bones_(std::move(bones)),
      slots_(std::move(slots)),
      animations_(std::move(animations)) {}

const SkeletonInfo& SkeletonData::info() const {
    return info_;
}

const std::vector<BoneData>& SkeletonData::bones() const {
    return bones_;
}

const std::vector<SlotData>& SkeletonData::slots() const {
    return slots_;
}

const std::vector<AnimationData>& SkeletonData::animations() const {
    return animations_;
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

std::optional<double> AnimationData::sample_bone_rotation(std::size_t bone_index, double time) const {
    const BoneRotateTimeline* timeline = find_rotate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_rotate_timeline(*timeline, time);
}

std::optional<std::size_t> SkeletonData::find_bone_index(std::string_view name) const {
    return marrow::runtime::find_bone_index(bones_, name);
}

std::optional<std::size_t> SkeletonData::find_slot_index(std::string_view name) const {
    const auto it = std::find_if(
        slots_.begin(),
        slots_.end(),
        [&](const SlotData& slot) {
            return slot.name == name;
        });
    if (it == slots_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(slots_.begin(), it));
}

Skeleton::Skeleton(std::shared_ptr<const SkeletonData> data)
    : data_(std::move(data)) {
    if (data_ == nullptr) {
        throw std::invalid_argument("Skeleton requires SkeletonData");
    }

    bone_poses_.resize(data_->bones().size());
    bone_world_transforms_.resize(data_->bones().size());
    slot_states_.resize(data_->slots().size());
    draw_order_.resize(data_->slots().size());

    set_to_setup_pose();
}

const std::shared_ptr<const SkeletonData>& Skeleton::data() const {
    return data_;
}

void Skeleton::set_to_setup_pose() {
    if (bone_poses_.size() != data_->bones().size()) {
        bone_poses_.resize(data_->bones().size());
    }
    if (bone_world_transforms_.size() != data_->bones().size()) {
        bone_world_transforms_.resize(data_->bones().size());
    }
    if (slot_states_.size() != data_->slots().size()) {
        slot_states_.resize(data_->slots().size());
    }
    if (draw_order_.size() != data_->slots().size()) {
        draw_order_.resize(data_->slots().size());
    }

    for (std::size_t index = 0; index < data_->bones().size(); ++index) {
        bone_poses_[index].local_pose = data_->bones()[index].setup_pose;
    }

    for (std::size_t index = 0; index < data_->slots().size(); ++index) {
        slot_states_[index].attachment_name = data_->slots()[index].setup_attachment;
        draw_order_[index] = index;
    }

    update_world_transforms();
}

void Skeleton::update_world_transforms() {
    if (bone_world_transforms_.size() != bone_poses_.size()) {
        bone_world_transforms_.resize(bone_poses_.size());
    }

    std::vector<bool> evaluated(bone_poses_.size(), false);
    std::vector<bool> evaluating(bone_poses_.size(), false);

    const auto evaluate_bone = [&](const auto& self, std::size_t bone_index) -> void {
        if (evaluated[bone_index]) {
            return;
        }
        if (evaluating[bone_index]) {
            throw std::runtime_error("Skeleton contains a cyclic bone hierarchy");
        }

        evaluating[bone_index] = true;

        const BonePose& pose = bone_poses_[bone_index];
        const BoneData& bone_data = data_->bones()[bone_index];
        if (!bone_data.parent_index.has_value()) {
            bone_world_transforms_[bone_index] = local_world_transform(pose.local_pose);
        } else {
            self(self, *bone_data.parent_index);
            bone_world_transforms_[bone_index] =
                compose_world_transform(bone_world_transforms_[*bone_data.parent_index], pose.local_pose);
        }

        evaluating[bone_index] = false;
        evaluated[bone_index] = true;
    };

    for (std::size_t bone_index = 0; bone_index < bone_poses_.size(); ++bone_index) {
        evaluate_bone(evaluate_bone, bone_index);
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

const std::vector<std::size_t>& Skeleton::draw_order() const {
    return draw_order_;
}

std::vector<std::size_t>& Skeleton::draw_order() {
    return draw_order_;
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

    std::vector<SlotData> slots;
    if (const auto error = parse_slots(document, root, bones, &slots)) {
        result.error = *error;
        return result;
    }

    std::vector<AnimationData> animations;
    if (const auto error = parse_animations(document, root, bones, &animations)) {
        result.error = *error;
        return result;
    }

    result.skeleton_data = std::make_shared<SkeletonData>(
        std::move(info), std::move(bones), std::move(slots), std::move(animations));
    return result;
}

SkeletonDataResult load_skeleton_data(const std::filesystem::path& path) {
    const auto document_result = json::load_document(path);
    if (!document_result) {
        SkeletonDataResult result;
        result.error = *document_result.error;
        return result;
    }

    return load_skeleton_data(*document_result.document);
}

} // namespace marrow::runtime
