#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace marrow::runtime {

enum class InterpolationKind {
    Linear,
    Stepped,
    CubicBezier,
};

struct CubicBezierControlPoints {
    double cx1{0.0};
    double cy1{0.0};
    double cx2{1.0};
    double cy2{1.0};
};

class Interpolation {
public:
    Interpolation();

    static Interpolation linear();
    static Interpolation stepped();
    static Interpolation cubic_bezier(
        double cx1,
        double cy1,
        double cx2,
        double cy2);

    InterpolationKind kind() const;
    const CubicBezierControlPoints& cubic_bezier() const;
    double transform(double alpha) const;

private:
    Interpolation(InterpolationKind kind, CubicBezierControlPoints cubic_bezier);

    InterpolationKind kind_{InterpolationKind::Linear};
    CubicBezierControlPoints cubic_bezier_{};
};

struct RotateKeyframe {
    double time{0.0};
    double angle{0.0};
    Interpolation interpolation{};
};

struct VectorKeyframe {
    double time{0.0};
    double x{0.0};
    double y{0.0};
    Interpolation interpolation{};
};

struct VectorSample {
    double x{0.0};
    double y{0.0};
};

struct SlotColor {
    double r{1.0};
    double g{1.0};
    double b{1.0};
    double a{1.0};
};

struct ColorKeyframe {
    double time{0.0};
    SlotColor color{};
    Interpolation interpolation{};
};

struct AttachmentKeyframe {
    double time{0.0};
    std::optional<std::string> attachment_name;
};

struct DeformKeyframe {
    double time{0.0};
    std::vector<double> vertex_offsets;
    Interpolation interpolation{};
};

struct BoneInheritFlags {
    bool inherit_rotation{true};
    bool inherit_scale{true};
    bool inherit_reflection{true};
};

struct InheritKeyframe {
    double time{0.0};
    BoneInheritFlags flags{};
};

struct BoneRotateTimeline {
    std::size_t bone_index{0};
    std::vector<RotateKeyframe> keyframes;
};

struct BoneInheritTimeline {
    std::size_t bone_index{0};
    std::vector<InheritKeyframe> keyframes;
};

struct BoneTranslateTimeline {
    std::size_t bone_index{0};
    std::vector<VectorKeyframe> keyframes;
};

struct BoneScaleTimeline {
    std::size_t bone_index{0};
    std::vector<VectorKeyframe> keyframes;
};

struct BoneShearTimeline {
    std::size_t bone_index{0};
    std::vector<VectorKeyframe> keyframes;
};

struct SlotAttachmentTimeline {
    std::size_t slot_index{0};
    std::vector<AttachmentKeyframe> keyframes;
};

struct SlotColorTimeline {
    std::size_t slot_index{0};
    std::vector<ColorKeyframe> keyframes;
};

struct MeshDeformTimeline {
    std::size_t slot_index{0};
    std::string attachment_name;
    std::vector<DeformKeyframe> keyframes;
};

struct DrawOrderKeyframe {
    double time{0.0};
    std::vector<std::size_t> slot_indices;
};

struct DrawOrderTimeline {
    std::vector<DrawOrderKeyframe> keyframes;
};

struct EventDefinition {
    std::string name;
    int int_value{0};
    double float_value{0.0};
    std::string string_value;
    std::optional<std::string> audio_path;
    double volume{1.0};
    double balance{0.0};
};

struct EventKeyframe {
    double time{0.0};
    std::size_t event_index{0};
    std::optional<int> int_value;
    std::optional<double> float_value;
    std::optional<std::string> string_value;
    std::optional<std::string> audio_path;
    std::optional<double> volume;
    std::optional<double> balance;
};

struct EventTimeline {
    std::vector<EventKeyframe> keyframes;
};

struct AnimationEvent {
    std::size_t event_index{0};
    std::string name;
    double time{0.0};
    int int_value{0};
    double float_value{0.0};
    std::string string_value;
    std::optional<std::string> audio_path;
    double volume{1.0};
    double balance{0.0};
};

double interpolate_value(
    double from_value,
    double to_value,
    const Interpolation& interpolation,
    double alpha);

std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time);
const InheritKeyframe* sample_inherit_timeline(
    const BoneInheritTimeline& timeline,
    double time);
std::optional<VectorSample> sample_translate_timeline(
    const BoneTranslateTimeline& timeline,
    double time);
std::optional<VectorSample> sample_scale_timeline(
    const BoneScaleTimeline& timeline,
    double time);
std::optional<VectorSample> sample_shear_timeline(
    const BoneShearTimeline& timeline,
    double time);
const AttachmentKeyframe* sample_attachment_timeline(
    const SlotAttachmentTimeline& timeline,
    double time);
std::optional<SlotColor> sample_color_timeline(
    const SlotColorTimeline& timeline,
    double time);
std::optional<std::vector<double>> sample_deform_timeline(
    const MeshDeformTimeline& timeline,
    double time);
const DrawOrderKeyframe* sample_draw_order_timeline(
    const DrawOrderTimeline& timeline,
    double time);

} // namespace marrow::runtime
