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
    /// @brief Constructs a linear interpolation descriptor.
    Interpolation();

    /// @brief Creates a linear interpolation descriptor.
    /// @return A linear interpolation value.
    static Interpolation linear();
    /// @brief Creates a stepped interpolation descriptor.
    /// @return A stepped interpolation value.
    static Interpolation stepped();
    /**
     * @brief Creates a cubic-bezier interpolation descriptor.
     * @param cx1 First control point x coordinate.
     * @param cy1 First control point y coordinate.
     * @param cx2 Second control point x coordinate.
     * @param cy2 Second control point y coordinate.
     * @return A cubic-bezier interpolation value.
     */
    static Interpolation cubic_bezier(
        double cx1,
        double cy1,
        double cx2,
        double cy2);

    /// @brief Returns the interpolation kind.
    /// @return The active interpolation type.
    InterpolationKind kind() const;
    /// @brief Returns the stored cubic-bezier control points.
    /// @return The cubic-bezier payload.
    const CubicBezierControlPoints& cubic_bezier() const;
    /**
     * @brief Maps a normalized alpha through the interpolation curve.
     * @param alpha Normalized interpolation alpha in `[0, 1]`.
     * @return The transformed alpha.
     */
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

enum class BoneInherit {
    Normal,
    OnlyTranslation,
    NoRotationOrReflection,
    NoScale,
    NoScaleOrReflection,
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

struct InheritKeyframe {
    double time{0.0};
    BoneInherit inherit{BoneInherit::Normal};
};

struct BoneRotateTimeline {
    std::size_t bone_index{0};
    double setup_rotation{0.0};
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

/**
 * @brief Interpolates two scalar values using a Marrow interpolation descriptor.
 * @param from_value Start value.
 * @param to_value End value.
 * @param interpolation Interpolation descriptor to apply.
 * @param alpha Normalized interpolation alpha in `[0, 1]`.
 * @return The interpolated scalar value.
 */
double interpolate_value(
    double from_value,
    double to_value,
    const Interpolation& interpolation,
    double alpha);

/**
 * @brief Samples a rotate timeline at a specific time.
 * @param timeline Rotate timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled rotation in degrees, or `std::nullopt` when the timeline is empty.
 */
std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time);
/**
 * @brief Samples a bone inherit timeline at a specific time.
 * @param timeline Inherit timeline to sample.
 * @param time Sample time in seconds.
 * @return The active inherit keyframe, or `nullptr` when the timeline is empty.
 */
const InheritKeyframe* sample_inherit_timeline(
    const BoneInheritTimeline& timeline,
    double time);
/**
 * @brief Samples a translate timeline at a specific time.
 * @param timeline Translate timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled translation, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_translate_timeline(
    const BoneTranslateTimeline& timeline,
    double time);
/**
 * @brief Samples a scale timeline at a specific time.
 * @param timeline Scale timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled scale, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_scale_timeline(
    const BoneScaleTimeline& timeline,
    double time);
/**
 * @brief Samples a shear timeline at a specific time.
 * @param timeline Shear timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled shear, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_shear_timeline(
    const BoneShearTimeline& timeline,
    double time);
/**
 * @brief Samples a slot attachment timeline at a specific time.
 * @param timeline Attachment timeline to sample.
 * @param time Sample time in seconds.
 * @return The active attachment keyframe, or `nullptr` when the timeline is empty.
 */
const AttachmentKeyframe* sample_attachment_timeline(
    const SlotAttachmentTimeline& timeline,
    double time);
/**
 * @brief Samples a slot color timeline at a specific time.
 * @param timeline Color timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled slot color, or `std::nullopt` when the timeline is empty.
 */
std::optional<SlotColor> sample_color_timeline(
    const SlotColorTimeline& timeline,
    double time);
/**
 * @brief Samples a mesh deform timeline at a specific time.
 * @param timeline Deform timeline to sample.
 * @param time Sample time in seconds.
 * @return The sampled vertex offsets, or `std::nullopt` when the timeline is empty.
 */
std::optional<std::vector<double>> sample_deform_timeline(
    const MeshDeformTimeline& timeline,
    double time);
/**
 * @brief Samples a draw-order timeline at a specific time.
 * @param timeline Draw-order timeline to sample.
 * @param time Sample time in seconds.
 * @return The active draw-order keyframe, or `nullptr` when the timeline is empty.
 */
const DrawOrderKeyframe* sample_draw_order_timeline(
    const DrawOrderTimeline& timeline,
    double time);

} // namespace marrow::runtime
