#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace marrow::runtime {

struct AnimationData;
struct CubicBezierLookupTable;

using AnimationScalar = float;

enum class InterpolationKind {
    Linear,
    Stepped,
    CubicBezier,
};

struct CubicBezierControlPoints {
    constexpr CubicBezierControlPoints() = default;
    constexpr CubicBezierControlPoints(
        double cx1_in,
        double cy1_in,
        double cx2_in,
        double cy2_in)
        : cx1(static_cast<AnimationScalar>(cx1_in)),
          cy1(static_cast<AnimationScalar>(cy1_in)),
          cx2(static_cast<AnimationScalar>(cx2_in)),
          cy2(static_cast<AnimationScalar>(cy2_in)) {}

    AnimationScalar cx1{0.0f};
    AnimationScalar cy1{0.0f};
    AnimationScalar cx2{1.0f};
    AnimationScalar cy2{1.0f};
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
    AnimationScalar transform_scalar(AnimationScalar alpha) const;
    Interpolation(
        InterpolationKind kind,
        CubicBezierControlPoints cubic_bezier,
        const CubicBezierLookupTable* cubic_bezier_lut);

    InterpolationKind kind_{InterpolationKind::Linear};
    CubicBezierControlPoints cubic_bezier_{};
    const CubicBezierLookupTable* cubic_bezier_lut_{nullptr};
};

struct RotateKeyframe {
    RotateKeyframe() = default;
    RotateKeyframe(double time_in, double angle_in, Interpolation interpolation_in = {})
        : time(static_cast<AnimationScalar>(time_in)),
          angle(static_cast<AnimationScalar>(angle_in)),
          interpolation(std::move(interpolation_in)) {}

    AnimationScalar time{0.0f};
    AnimationScalar angle{0.0f};
    Interpolation interpolation{};
};

struct VectorKeyframe {
    VectorKeyframe() = default;
    VectorKeyframe(
        double time_in,
        double x_in,
        double y_in,
        Interpolation interpolation_in = {})
        : time(static_cast<AnimationScalar>(time_in)),
          x(static_cast<AnimationScalar>(x_in)),
          y(static_cast<AnimationScalar>(y_in)),
          interpolation(std::move(interpolation_in)) {}

    AnimationScalar time{0.0f};
    AnimationScalar x{0.0f};
    AnimationScalar y{0.0f};
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
    constexpr SlotColor() = default;
    constexpr SlotColor(double r_in, double g_in, double b_in, double a_in)
        : r(static_cast<AnimationScalar>(r_in)),
          g(static_cast<AnimationScalar>(g_in)),
          b(static_cast<AnimationScalar>(b_in)),
          a(static_cast<AnimationScalar>(a_in)) {}

    AnimationScalar r{1.0f};
    AnimationScalar g{1.0f};
    AnimationScalar b{1.0f};
    AnimationScalar a{1.0f};
};

struct ColorKeyframe {
    AnimationScalar time{0.0f};
    SlotColor color{};
    Interpolation interpolation{};
};

struct AttachmentKeyframe {
    AttachmentKeyframe() = default;
    AttachmentKeyframe(double time_in, std::optional<std::string> attachment_name_in)
        : time(static_cast<AnimationScalar>(time_in)),
          attachment_name(std::move(attachment_name_in)) {}

    AnimationScalar time{0.0f};
    std::optional<std::string> attachment_name;
};

struct DeformKeyframe {
    AnimationScalar time{0.0f};
    std::vector<AnimationScalar> vertex_offsets;
    Interpolation interpolation{};
};

struct InheritKeyframe {
    InheritKeyframe() = default;
    InheritKeyframe(double time_in, BoneInherit inherit_in)
        : time(static_cast<AnimationScalar>(time_in)),
          inherit(inherit_in) {}

    AnimationScalar time{0.0f};
    BoneInherit inherit{BoneInherit::Normal};
};

struct BoneRotateTimeline {
    std::size_t bone_index{0};
    AnimationScalar setup_rotation{0.0f};
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
    AnimationScalar time{0.0f};
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
    AnimationScalar time{0.0f};
    std::size_t event_index{0};
    std::optional<int> int_value;
    std::optional<AnimationScalar> float_value;
    std::optional<std::string> string_value;
    std::optional<std::string> audio_path;
    std::optional<AnimationScalar> volume;
    std::optional<AnimationScalar> balance;
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

struct SamplingContext {
    const AnimationData* animation{nullptr};
    double last_sample_time{0.0};
    std::vector<std::size_t> rotate_last_keyframe_indices;
    std::vector<std::size_t> inherit_last_keyframe_indices;
    std::vector<std::size_t> translate_last_keyframe_indices;
    std::vector<std::size_t> scale_last_keyframe_indices;
    std::vector<std::size_t> shear_last_keyframe_indices;
    std::vector<std::size_t> attachment_last_keyframe_indices;
    std::vector<std::size_t> color_last_keyframe_indices;
    std::vector<std::size_t> deform_last_keyframe_indices;
    std::size_t draw_order_last_keyframe_index{0};

    void reset();
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
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled rotation in degrees, or `std::nullopt` when the timeline is empty.
 */
std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a bone inherit timeline at a specific time.
 * @param timeline Inherit timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The active inherit keyframe, or `nullptr` when the timeline is empty.
 */
const InheritKeyframe* sample_inherit_timeline(
    const BoneInheritTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a translate timeline at a specific time.
 * @param timeline Translate timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled translation, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_translate_timeline(
    const BoneTranslateTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a scale timeline at a specific time.
 * @param timeline Scale timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled scale, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_scale_timeline(
    const BoneScaleTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a shear timeline at a specific time.
 * @param timeline Shear timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled shear, or `std::nullopt` when the timeline is empty.
 */
std::optional<VectorSample> sample_shear_timeline(
    const BoneShearTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a slot attachment timeline at a specific time.
 * @param timeline Attachment timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The active attachment keyframe, or `nullptr` when the timeline is empty.
 */
const AttachmentKeyframe* sample_attachment_timeline(
    const SlotAttachmentTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a slot color timeline at a specific time.
 * @param timeline Color timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled slot color, or `std::nullopt` when the timeline is empty.
 */
std::optional<SlotColor> sample_color_timeline(
    const SlotColorTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a mesh deform timeline at a specific time.
 * @param timeline Deform timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The sampled vertex offsets, or `std::nullopt` when the timeline is empty.
 */
std::optional<std::vector<double>> sample_deform_timeline(
    const MeshDeformTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);
/**
 * @brief Samples a draw-order timeline at a specific time.
 * @param timeline Draw-order timeline to sample.
 * @param time Sample time in seconds.
 * @param last_keyframe_index Optional cache for the last sampled keyframe index.
 * @return The active draw-order keyframe, or `nullptr` when the timeline is empty.
 */
const DrawOrderKeyframe* sample_draw_order_timeline(
    const DrawOrderTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index = nullptr);

} // namespace marrow::runtime
