#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/animation.hpp"
#include "marrow/runtime/json.hpp"

namespace marrow::runtime {

struct SkeletonInfo {
    std::string name;
    double width{0.0};
    double height{0.0};
};

struct BoneTransform {
    double x{0.0};
    double y{0.0};
    double rotation{0.0};
    double scale_x{1.0};
    double scale_y{1.0};
    double shear_x{0.0};
    double shear_y{0.0};
};

struct BoneData {
    std::string name;
    std::optional<std::size_t> parent_index;
    BoneTransform setup_pose;
};

struct SlotData {
    std::string name;
    std::size_t bone_index{0};
    std::string setup_attachment;
};

struct AnimationData {
    std::string name;
    std::vector<std::size_t> targeted_bone_indices;
    std::vector<BoneRotateTimeline> bone_rotate_timelines;

    const BoneRotateTimeline* find_rotate_timeline(std::size_t bone_index) const;
    std::optional<double> sample_bone_rotation(std::size_t bone_index, double time) const;
};

class SkeletonData {
public:
    SkeletonData(
        SkeletonInfo info,
        std::vector<BoneData> bones,
        std::vector<SlotData> slots,
        std::vector<AnimationData> animations);

    const SkeletonInfo& info() const;
    const std::vector<BoneData>& bones() const;
    const std::vector<SlotData>& slots() const;
    const std::vector<AnimationData>& animations() const;

    std::optional<std::size_t> find_bone_index(std::string_view name) const;
    std::optional<std::size_t> find_slot_index(std::string_view name) const;

private:
    SkeletonInfo info_;
    std::vector<BoneData> bones_;
    std::vector<SlotData> slots_;
    std::vector<AnimationData> animations_;
};

struct BonePose {
    BoneTransform local_pose;
};

struct BoneWorldTransform {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double world_x{0.0};
    double world_y{0.0};
};

struct SlotState {
    std::string attachment_name;
};

class Skeleton {
public:
    explicit Skeleton(std::shared_ptr<const SkeletonData> data);

    const std::shared_ptr<const SkeletonData>& data() const;
    void set_to_setup_pose();
    void update_world_transforms();
    const std::vector<BonePose>& bone_poses() const;
    std::vector<BonePose>& bone_poses();
    const std::vector<BoneWorldTransform>& bone_world_transforms() const;
    const std::vector<SlotState>& slot_states() const;
    std::vector<SlotState>& slot_states();
    const std::vector<std::size_t>& draw_order() const;
    std::vector<std::size_t>& draw_order();

private:
    std::shared_ptr<const SkeletonData> data_;
    std::vector<BonePose> bone_poses_;
    std::vector<BoneWorldTransform> bone_world_transforms_;
    std::vector<SlotState> slot_states_;
    std::vector<std::size_t> draw_order_;
};

struct SkeletonDataResult {
    std::shared_ptr<const SkeletonData> skeleton_data;
    std::optional<json::LoadError> error;

    explicit operator bool() const {
        return skeleton_data != nullptr;
    }
};

SkeletonDataResult load_skeleton_data(const json::Document& document);
SkeletonDataResult load_skeleton_data(const std::filesystem::path& path);

} // namespace marrow::runtime
