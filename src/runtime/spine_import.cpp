#include "marrow/runtime/spine_import.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/runtime/atlas.hpp"
#include "skeleton_internal.hpp"

namespace marrow::runtime {
namespace {

using json::Document;
using json::LoadError;
using json::SourceLocation;
using json::Value;

constexpr std::string_view kMarrowVersion = "1.0";
constexpr int kMarrowFormatVersion = 1;

struct SpineSlotInfo {
    std::string name;
    std::size_t bone_index{0};
    std::string bone_name;
    std::string setup_attachment;
};

struct AttachmentRecord {
    std::string skin_name;
    std::string slot_name;
    std::string attachment_name;
    bool is_path{false};
    double path_total_length{0.0};
};

struct ImportContext {
    const Document& document;
    std::string skeleton_name;
    std::vector<BoneData> bones;
    std::vector<BoneWorldTransform> bone_world_transforms;
    std::vector<SpineSlotInfo> slots;
    std::vector<std::string> setup_draw_order;
    std::vector<AttachmentRecord> attachment_records;
    std::optional<std::string> default_skin_name;
};

struct ConvertedAttachmentResult {
    Value attachment_value;
    std::optional<AttachmentRecord> record;
};

struct AtlasImportLine {
    std::string_view text;
    SourceLocation location;
    bool blank{false};
    bool indented{false};
};

struct AtlasImportRegion {
    std::string name;
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
    double origin_x{0.0};
    double origin_y{0.0};
    double rotate_degrees{0.0};
};

struct AtlasImportPage {
    std::string image_name;
    double width{0.0};
    double height{0.0};
    std::string filter_min{"nearest"};
    std::string filter_mag{"nearest"};
    std::string wrap_x{"clamp_to_edge"};
    std::string wrap_y{"clamp_to_edge"};
    bool premultiplied_alpha{false};
    std::vector<AtlasImportRegion> regions;
};

LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    return json::make_validation_error(
        document,
        location,
        std::move(json_path),
        std::move(message));
}

Value make_null_value() {
    return Value(nullptr, SourceLocation{});
}

Value make_boolean_value(bool value) {
    return Value(value, SourceLocation{});
}

Value make_number_value(double value) {
    return Value(value, SourceLocation{});
}

Value make_string_value(std::string value) {
    return Value(std::move(value), SourceLocation{});
}

Value make_array_value(Value::Array values = {}) {
    return Value(std::move(values), SourceLocation{});
}

Value make_object_value(Value::Object values = {}) {
    return Value(std::move(values), SourceLocation{});
}

const Value* find_optional_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    return json::find_member(object, key);
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

std::optional<LoadError> read_required_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::string* value_out) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document,
            object,
            key,
            Value::Type::String,
            json_path,
            &member)) {
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

std::optional<LoadError> read_required_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    double* value_out) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document,
            object,
            key,
            Value::Type::Number,
            json_path,
            &member)) {
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
    if (!std::isfinite(numeric_value) ||
        std::floor(numeric_value) != numeric_value ||
        numeric_value < static_cast<double>(std::numeric_limits<int>::min()) ||
        numeric_value > static_cast<double>(std::numeric_limits<int>::max())) {
        return validation_error(
            document,
            member->location(),
            std::string(json_path) + "." + std::string(key),
            "integer values must be whole numbers within the runtime int range");
    }

    *value_out = static_cast<int>(numeric_value);
    return std::nullopt;
}

std::optional<LoadError> parse_hex_color(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    SlotColor* color_out) {
    if (const auto error = json::require_type(
            document,
            value,
            Value::Type::String,
            json_path)) {
        return error;
    }

    const std::string_view encoded = value.as_string();
    if (encoded.size() != 6 && encoded.size() != 8) {
        return validation_error(
            document,
            value.location(),
            std::string(json_path),
            "colors must be 6- or 8-digit hexadecimal strings");
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
            value.location(),
            std::string(json_path),
            "colors must be valid hexadecimal strings");
    }

    color_out->r = *red;
    color_out->g = *green;
    color_out->b = *blue;
    color_out->a = *alpha;
    return std::nullopt;
}

std::optional<LoadError> parse_optional_color_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::optional<std::string>* color_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        color_out->reset();
        return std::nullopt;
    }

    SlotColor unused;
    if (const auto error = parse_hex_color(
            document,
            *member,
            std::string(json_path) + "." + std::string(key),
            &unused)) {
        return error;
    }

    *color_out = member->as_string();
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
        const Value& entry = value.as_array()[index];
        const std::string entry_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                entry,
                Value::Type::Number,
                entry_path)) {
            return error;
        }

        values_out->push_back(entry.as_number());
    }

    return std::nullopt;
}

std::optional<LoadError> parse_string_array(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    std::vector<std::string>* values_out) {
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
        const Value& entry = value.as_array()[index];
        const std::string entry_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                entry,
                Value::Type::String,
                entry_path)) {
            return error;
        }
        if (entry.as_string().empty()) {
            return validation_error(
                document,
                entry.location(),
                entry_path,
                "string entries must not be empty");
        }

        values_out->push_back(entry.as_string());
    }

    return std::nullopt;
}

std::optional<LoadError> parse_path_points(
    const Document& document,
    const Value& vertices_value,
    std::size_t vertex_count,
    std::string_view json_path,
    std::vector<AttachmentVertex>* points_out) {
    std::vector<double> coordinates;
    if (const auto error = parse_number_array(
            document,
            vertices_value,
            json_path,
            &coordinates)) {
        return error;
    }

    if (coordinates.size() != vertex_count * 2U) {
        return validation_error(
            document,
            vertices_value.location(),
            std::string(json_path),
            "weighted path attachments are not supported by the Marrow importer");
    }

    if (vertex_count < 4 || ((vertex_count - 1U) % 3U) != 0U) {
        return validation_error(
            document,
            vertices_value.location(),
            std::string(json_path),
            "path attachments must provide 3n+1 control points");
    }

    points_out->clear();
    points_out->reserve(vertex_count);
    for (std::size_t index = 0; index < coordinates.size(); index += 2) {
        points_out->push_back({coordinates[index], coordinates[index + 1]});
    }

    return std::nullopt;
}

std::optional<BoneInherit> parse_spine_transform_mode(std::string_view value) {
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

std::vector<BoneWorldTransform> compute_setup_world_transforms(
    const std::vector<BoneData>& bones) {
    std::vector<BoneWorldTransform> world_transforms(bones.size());
    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        const BonePose pose{bones[bone_index].setup_pose, bones[bone_index].inherit};
        if (!bones[bone_index].parent_index.has_value()) {
            world_transforms[bone_index] =
                detail::root_world_transform(pose.local_pose, 1.0, 1.0);
            continue;
        }

        world_transforms[bone_index] = detail::compose_world_transform(
            world_transforms[*bones[bone_index].parent_index],
            pose,
            1.0,
            1.0);
    }
    return world_transforms;
}

std::optional<std::size_t> find_bone_index(
    const std::vector<BoneData>& bones,
    std::string_view name) {
    return detail::find_bone_index(bones, name);
}

std::optional<std::size_t> find_slot_index(
    const std::vector<SpineSlotInfo>& slots,
    std::string_view name) {
    const auto it = std::find_if(
        slots.begin(),
        slots.end(),
        [&](const SpineSlotInfo& slot) {
            return slot.name == name;
        });
    if (it == slots.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(slots.begin(), it));
}

Value build_curve_value(
    const Document& document,
    const Value& keyframe_value,
    std::string_view json_path,
    bool* stepped_out) {
    *stepped_out = false;

    const Value* curve_value = find_optional_member(keyframe_value, "curve");
    if (curve_value == nullptr) {
        return make_string_value("linear");
    }

    const std::string curve_path = std::string(json_path) + ".curve";
    if (curve_value->is_string()) {
        const std::string& curve_name = curve_value->as_string();
        if (curve_name == "stepped") {
            *stepped_out = true;
            return make_string_value("stepped");
        }
        if (curve_name == "linear") {
            return make_string_value("linear");
        }

        throw validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be linear, stepped, a 4-number array, or the legacy c1/c2/c3/c4 form");
    }

    if (curve_value->is_array()) {
        if (curve_value->as_array().size() != 4U) {
            throw validation_error(
                document,
                curve_value->location(),
                curve_path,
                "bezier curves must contain exactly 4 control point numbers");
        }

        Value::Array control_points;
        control_points.reserve(4);
        for (std::size_t index = 0; index < 4U; ++index) {
            const Value& control_point = curve_value->as_array()[index];
            if (const auto error = json::require_type(
                    document,
                    control_point,
                    Value::Type::Number,
                    curve_path + "[" + std::to_string(index) + "]")) {
                throw *error;
            }

            control_points.push_back(make_number_value(control_point.as_number()));
        }
        return make_array_value(std::move(control_points));
    }

    if (!curve_value->is_number()) {
        throw validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be linear, stepped, a 4-number array, or the legacy c1/c2/c3/c4 form");
    }

    double c1 = curve_value->as_number();
    double c2 = 0.0;
    double c3 = 1.0;
    double c4 = 1.0;
    if (const auto error = read_optional_number(
            document,
            keyframe_value,
            "c2",
            json_path,
            &c2)) {
        throw *error;
    }
    if (const auto error = read_optional_number(
            document,
            keyframe_value,
            "c3",
            json_path,
            &c3)) {
        throw *error;
    }
    if (const auto error = read_optional_number(
            document,
            keyframe_value,
            "c4",
            json_path,
            &c4)) {
        throw *error;
    }

    Value::Array control_points;
    control_points.reserve(4);
    control_points.push_back(make_number_value(c1));
    control_points.push_back(make_number_value(c2));
    control_points.push_back(make_number_value(c3));
    control_points.push_back(make_number_value(c4));
    return make_array_value(std::move(control_points));
}

Value build_weight_influence_object(
    std::string bone_name,
    double x,
    double y,
    double weight) {
    Value::Object influence;
    influence.emplace("bone", make_string_value(std::move(bone_name)));
    influence.emplace("x", make_number_value(x));
    influence.emplace("y", make_number_value(y));
    influence.emplace("weight", make_number_value(weight));
    return make_object_value(std::move(influence));
}

std::array<AttachmentVertex, 4> build_region_quad(
    double x,
    double y,
    double scale_x,
    double scale_y,
    double rotation_degrees,
    double width,
    double height) {
    const double half_width = width * scale_x * 0.5;
    const double half_height = height * scale_y * 0.5;
    std::array<AttachmentVertex, 4> corners{{
        {-half_width, -half_height},
        {half_width, -half_height},
        {half_width, half_height},
        {-half_width, half_height},
    }};

    const double radians = detail::degrees_to_radians(rotation_degrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    for (AttachmentVertex& corner : corners) {
        const double rotated_x = corner.x * cosine - corner.y * sine;
        const double rotated_y = corner.x * sine + corner.y * cosine;
        corner.x = rotated_x + x;
        corner.y = rotated_y + y;
    }
    return corners;
}

std::string derive_skeleton_name(const Document& document) {
    if (!document.source_path.stem().empty()) {
        return document.source_path.stem().string();
    }
    return "spine_import";
}

std::optional<LoadError> parse_spine_version(const Document& document, const Value& root) {
    const Value* skeleton_value = nullptr;
    if (const auto error = json::require_member(
            document,
            root,
            "skeleton",
            Value::Type::Object,
            "$",
            &skeleton_value)) {
        return error;
    }

    std::optional<std::string> spine_version;
    if (const auto error = read_optional_string(
            document,
            *skeleton_value,
            "spine",
            "$.skeleton",
            &spine_version)) {
        return error;
    }
    if (!spine_version.has_value() || spine_version->empty()) {
        return validation_error(
            document,
            skeleton_value->location(),
            "$.skeleton.spine",
            "Spine JSON imports require a skeleton.spine version string");
    }

    const std::string_view version = *spine_version;
    if (version.empty() || version.front() != '4') {
        return validation_error(
            document,
            skeleton_value->location(),
            "$.skeleton.spine",
            "only Spine 4.x JSON exports are supported");
    }

    return std::nullopt;
}

std::optional<LoadError> parse_bones(
    ImportContext* context,
    Value* bones_out) {
    const Document& document = context->document;
    const Value* bones_value = nullptr;
    if (const auto error = json::require_member(
            document,
            document.root,
            "bones",
            Value::Type::Array,
            "$",
            &bones_value)) {
        return error;
    }
    if (bones_value->as_array().empty()) {
        return validation_error(
            document,
            bones_value->location(),
            "$.bones",
            "bones array must not be empty");
    }

    struct PendingParent {
        std::optional<std::string> parent_name;
        SourceLocation location{};
    };

    std::vector<PendingParent> pending_parents;
    pending_parents.reserve(bones_value->as_array().size());
    context->bones.clear();
    context->bones.reserve(bones_value->as_array().size());

    Value::Array output_bones;
    output_bones.reserve(bones_value->as_array().size());

    for (std::size_t bone_index = 0; bone_index < bones_value->as_array().size(); ++bone_index) {
        const Value& bone_value = bones_value->as_array()[bone_index];
        const std::string bone_path = "$.bones[" + std::to_string(bone_index) + "]";
        if (const auto error = json::require_type(
                document,
                bone_value,
                Value::Type::Object,
                bone_path)) {
            return error;
        }

        BoneData bone;
        if (const auto error = read_required_string(
                document,
                bone_value,
                "name",
                bone_path,
                &bone.name)) {
            return error;
        }
        if (bone.name.empty()) {
            return validation_error(
                document,
                bone_value.location(),
                bone_path + ".name",
                "bone names must not be empty");
        }
        if (find_bone_index(context->bones, bone.name).has_value()) {
            return validation_error(
                document,
                bone_value.location(),
                bone_path + ".name",
                "bone names must be unique");
        }

        if (const auto error = read_optional_number(
                document,
                bone_value,
                "x",
                bone_path,
                &bone.setup_pose.x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "y",
                bone_path,
                &bone.setup_pose.y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "rotation",
                bone_path,
                &bone.setup_pose.rotation)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "scaleX",
                bone_path,
                &bone.setup_pose.scale_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "scaleY",
                bone_path,
                &bone.setup_pose.scale_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "shearX",
                bone_path,
                &bone.setup_pose.shear_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                bone_value,
                "shearY",
                bone_path,
                &bone.setup_pose.shear_y)) {
            return error;
        }

        if (const Value* transform_value = find_optional_member(bone_value, "transform")) {
            if (const auto error = json::require_type(
                    document,
                    *transform_value,
                    Value::Type::String,
                    bone_path + ".transform")) {
                return error;
            }
            const std::optional<BoneInherit> inherit =
                parse_spine_transform_mode(transform_value->as_string());
            if (!inherit.has_value()) {
                return validation_error(
                    document,
                    transform_value->location(),
                    bone_path + ".transform",
                    "bone transform mode must be normal, onlyTranslation, "
                    "noRotationOrReflection, noScale, or noScaleOrReflection");
            }
            bone.inherit = *inherit;
        }

        PendingParent pending_parent;
        if (const Value* parent_value = find_optional_member(bone_value, "parent")) {
            if (const auto error = json::require_type(
                    document,
                    *parent_value,
                    Value::Type::String,
                    bone_path + ".parent")) {
                return error;
            }
            pending_parent.parent_name = parent_value->as_string();
            pending_parent.location = parent_value->location();
        }

        Value::Object output_bone;
        output_bone.emplace("name", make_string_value(bone.name));
        if (pending_parent.parent_name.has_value()) {
            output_bone.emplace("parent", make_string_value(*pending_parent.parent_name));
        }
        if (bone.setup_pose.x != 0.0) {
            output_bone.emplace("x", make_number_value(bone.setup_pose.x));
        }
        if (bone.setup_pose.y != 0.0) {
            output_bone.emplace("y", make_number_value(bone.setup_pose.y));
        }
        if (bone.setup_pose.rotation != 0.0) {
            output_bone.emplace("rotation", make_number_value(bone.setup_pose.rotation));
        }
        if (bone.setup_pose.scale_x != 1.0) {
            output_bone.emplace("scaleX", make_number_value(bone.setup_pose.scale_x));
        }
        if (bone.setup_pose.scale_y != 1.0) {
            output_bone.emplace("scaleY", make_number_value(bone.setup_pose.scale_y));
        }
        if (bone.setup_pose.shear_x != 0.0) {
            output_bone.emplace("shearX", make_number_value(bone.setup_pose.shear_x));
        }
        if (bone.setup_pose.shear_y != 0.0) {
            output_bone.emplace("shearY", make_number_value(bone.setup_pose.shear_y));
        }
        if (bone.inherit != BoneInherit::Normal) {
            switch (bone.inherit) {
            case BoneInherit::OnlyTranslation:
                output_bone.emplace("inherit", make_string_value("onlyTranslation"));
                break;
            case BoneInherit::NoRotationOrReflection:
                output_bone.emplace("inherit", make_string_value("noRotationOrReflection"));
                break;
            case BoneInherit::NoScale:
                output_bone.emplace("inherit", make_string_value("noScale"));
                break;
            case BoneInherit::NoScaleOrReflection:
                output_bone.emplace("inherit", make_string_value("noScaleOrReflection"));
                break;
            case BoneInherit::Normal:
                break;
            }
        }

        context->bones.push_back(std::move(bone));
        pending_parents.push_back(std::move(pending_parent));
        output_bones.push_back(make_object_value(std::move(output_bone)));
    }

    for (std::size_t bone_index = 0; bone_index < context->bones.size(); ++bone_index) {
        if (!pending_parents[bone_index].parent_name.has_value()) {
            continue;
        }

        const auto parent_index = find_bone_index(context->bones, *pending_parents[bone_index].parent_name);
        if (!parent_index.has_value()) {
            return validation_error(
                document,
                pending_parents[bone_index].location,
                "$.bones[" + std::to_string(bone_index) + "].parent",
                "bone references unknown parent '" + *pending_parents[bone_index].parent_name + "'");
        }
        context->bones[bone_index].parent_index = *parent_index;
    }

    context->bone_world_transforms = compute_setup_world_transforms(context->bones);
    *bones_out = make_array_value(std::move(output_bones));
    return std::nullopt;
}

std::optional<LoadError> parse_slots(
    ImportContext* context,
    Value* slots_out) {
    const Document& document = context->document;
    const Value* slots_value = nullptr;
    if (const auto error = json::require_member(
            document,
            document.root,
            "slots",
            Value::Type::Array,
            "$",
            &slots_value)) {
        return error;
    }
    if (slots_value->as_array().empty()) {
        return validation_error(
            document,
            slots_value->location(),
            "$.slots",
            "slots array must not be empty");
    }

    context->slots.clear();
    context->setup_draw_order.clear();
    context->slots.reserve(slots_value->as_array().size());
    context->setup_draw_order.reserve(slots_value->as_array().size());

    Value::Array output_slots;
    output_slots.reserve(slots_value->as_array().size());

    auto validate_blend_mode = [&](const Value& slot_value,
                                   std::string_view slot_path,
                                   std::optional<std::string>* blend_out) -> std::optional<LoadError> {
        if (const auto error = read_optional_string(
                document,
                slot_value,
                "blend",
                slot_path,
                blend_out)) {
            return error;
        }
        if (!blend_out->has_value()) {
            return std::nullopt;
        }

        const std::string_view blend = **blend_out;
        if (blend == "normal" || blend == "additive" ||
            blend == "multiply" || blend == "screen") {
            return std::nullopt;
        }

        return validation_error(
            document,
            slot_value.location(),
            std::string(slot_path) + ".blend",
            "slot blend mode must be normal, additive, multiply, or screen");
    };

    for (std::size_t slot_index = 0; slot_index < slots_value->as_array().size(); ++slot_index) {
        const Value& slot_value = slots_value->as_array()[slot_index];
        const std::string slot_path = "$.slots[" + std::to_string(slot_index) + "]";
        if (const auto error = json::require_type(
                document,
                slot_value,
                Value::Type::Object,
                slot_path)) {
            return error;
        }

        SpineSlotInfo slot;
        if (const auto error = read_required_string(
                document,
                slot_value,
                "name",
                slot_path,
                &slot.name)) {
            return error;
        }
        if (slot.name.empty()) {
            return validation_error(
                document,
                slot_value.location(),
                slot_path + ".name",
                "slot names must not be empty");
        }
        if (find_slot_index(context->slots, slot.name).has_value()) {
            return validation_error(
                document,
                slot_value.location(),
                slot_path + ".name",
                "slot names must be unique");
        }

        if (const auto error = read_required_string(
                document,
                slot_value,
                "bone",
                slot_path,
                &slot.bone_name)) {
            return error;
        }
        const auto bone_index = find_bone_index(context->bones, slot.bone_name);
        if (!bone_index.has_value()) {
            return validation_error(
                document,
                slot_value.location(),
                slot_path + ".bone",
                "slot references unknown bone '" + slot.bone_name + "'");
        }
        slot.bone_index = *bone_index;

        std::optional<std::string> attachment_name;
        if (const auto error = read_optional_string(
                document,
                slot_value,
                "attachment",
                slot_path,
                &attachment_name)) {
            return error;
        }
        slot.setup_attachment = attachment_name.value_or("");

        std::optional<std::string> blend_mode;
        if (const auto error = validate_blend_mode(slot_value, slot_path, &blend_mode)) {
            return error;
        }

        std::optional<std::string> color;
        if (const auto error = parse_optional_color_string(
                document,
                slot_value,
                "color",
                slot_path,
                &color)) {
            return error;
        }

        std::optional<std::string> dark_color;
        if (const auto error = parse_optional_color_string(
                document,
                slot_value,
                "dark",
                slot_path,
                &dark_color)) {
            return error;
        }

        Value::Object output_slot;
        output_slot.emplace("name", make_string_value(slot.name));
        output_slot.emplace("bone", make_string_value(slot.bone_name));
        output_slot.emplace("attachment", make_string_value(slot.setup_attachment));
        if (blend_mode.has_value() && *blend_mode != "normal") {
            output_slot.emplace("blend", make_string_value(*blend_mode));
        }
        if (color.has_value()) {
            output_slot.emplace("color", make_string_value(*color));
        }
        if (dark_color.has_value()) {
            output_slot.emplace("dark", make_string_value(*dark_color));
        }

        context->setup_draw_order.push_back(slot.name);
        context->slots.push_back(std::move(slot));
        output_slots.push_back(make_object_value(std::move(output_slot)));
    }

    *slots_out = make_array_value(std::move(output_slots));
    return std::nullopt;
}

std::optional<LoadError> build_region_mesh_attachment(
    const Document& document,
    const Value& attachment_value,
    const SpineSlotInfo& slot,
    std::string_view attachment_name,
    std::string region_name,
    std::string_view json_path,
    Value* attachment_out) {
    double x = 0.0;
    double y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0;
    double width = 0.0;
    double height = 0.0;

    if (const auto error = read_optional_number(document, attachment_value, "x", json_path, &x)) {
        return error;
    }
    if (const auto error = read_optional_number(document, attachment_value, "y", json_path, &y)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            attachment_value,
            "scaleX",
            json_path,
            &scale_x)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            attachment_value,
            "scaleY",
            json_path,
            &scale_y)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            attachment_value,
            "rotation",
            json_path,
            &rotation)) {
        return error;
    }
    if (const auto error = read_required_number(
            document,
            attachment_value,
            "width",
            json_path,
            &width)) {
        return error;
    }
    if (const auto error = read_required_number(
            document,
            attachment_value,
            "height",
            json_path,
            &height)) {
        return error;
    }

    const auto quad = build_region_quad(x, y, scale_x, scale_y, rotation, width, height);

    Value::Array vertices;
    Value::Array weights;
    vertices.reserve(8);
    weights.reserve(4);
    for (const AttachmentVertex& corner : quad) {
        vertices.push_back(make_number_value(corner.x));
        vertices.push_back(make_number_value(corner.y));

        Value::Array influences;
        influences.push_back(build_weight_influence_object(slot.bone_name, corner.x, corner.y, 1.0));
        weights.push_back(make_array_value(std::move(influences)));
    }

    Value::Array triangles;
    triangles.push_back(make_number_value(0));
    triangles.push_back(make_number_value(1));
    triangles.push_back(make_number_value(2));
    triangles.push_back(make_number_value(2));
    triangles.push_back(make_number_value(3));
    triangles.push_back(make_number_value(0));

    Value::Array uvs;
    uvs.push_back(make_number_value(0.0));
    uvs.push_back(make_number_value(0.0));
    uvs.push_back(make_number_value(1.0));
    uvs.push_back(make_number_value(0.0));
    uvs.push_back(make_number_value(1.0));
    uvs.push_back(make_number_value(1.0));
    uvs.push_back(make_number_value(0.0));
    uvs.push_back(make_number_value(1.0));

    Value::Object attachment_object;
    attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
    attachment_object.emplace("type", make_string_value("mesh"));
    attachment_object.emplace("region", make_string_value(std::move(region_name)));
    attachment_object.emplace("vertices", make_array_value(std::move(vertices)));
    attachment_object.emplace("triangles", make_array_value(std::move(triangles)));
    attachment_object.emplace("uvs", make_array_value(std::move(uvs)));
    attachment_object.emplace("weights", make_array_value(std::move(weights)));
    *attachment_out = make_object_value(std::move(attachment_object));
    return std::nullopt;
}

std::optional<LoadError> build_mesh_attachment(
    const ImportContext& context,
    const Value& attachment_value,
    const SpineSlotInfo& slot,
    std::string_view attachment_name,
    std::string region_name,
    std::string_view json_path,
    Value* attachment_out) {
    const Document& document = context.document;
    const Value* uvs_value = nullptr;
    const Value* triangles_value = nullptr;
    const Value* vertices_value = nullptr;
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
            "triangles",
            Value::Type::Array,
            json_path,
            &triangles_value)) {
        return error;
    }
    if (const auto error = json::require_member(
            document,
            attachment_value,
            "vertices",
            Value::Type::Array,
            json_path,
            &vertices_value)) {
        return error;
    }

    std::vector<double> uvs;
    std::vector<double> vertices;
    std::vector<double> spine_vertices;
    if (const auto error = parse_number_array(
            document,
            *uvs_value,
            std::string(json_path) + ".uvs",
            &uvs)) {
        return error;
    }
    if (const auto error = parse_number_array(
            document,
            *vertices_value,
            std::string(json_path) + ".vertices",
            &spine_vertices)) {
        return error;
    }

    if (uvs.empty() || (uvs.size() % 2U) != 0U) {
        return validation_error(
            document,
            uvs_value->location(),
            std::string(json_path) + ".uvs",
            "mesh uvs must contain x/y pairs");
    }

    Value::Array triangles;
    triangles.reserve(triangles_value->as_array().size());
    for (std::size_t index = 0; index < triangles_value->as_array().size(); ++index) {
        const Value& triangle = triangles_value->as_array()[index];
        const std::string triangle_path =
            std::string(json_path) + ".triangles[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                triangle,
                Value::Type::Number,
                triangle_path)) {
            return error;
        }
        if (triangle.as_number() < 0.0 || std::floor(triangle.as_number()) != triangle.as_number()) {
            return validation_error(
                document,
                triangle.location(),
                triangle_path,
                "mesh triangle indices must be non-negative integers");
        }
        triangles.push_back(make_number_value(triangle.as_number()));
    }
    if ((triangles.size() % 3U) != 0U) {
        return validation_error(
            document,
            triangles_value->location(),
            std::string(json_path) + ".triangles",
            "mesh triangle indices must be grouped into triangles");
    }

    const std::size_t vertex_count = uvs.size() / 2U;
    Value::Array vertices_array;
    Value::Array weights_array;
    vertices_array.reserve(vertex_count * 2U);
    weights_array.reserve(vertex_count);

    const bool weighted = spine_vertices.size() > uvs.size();
    if (!weighted) {
        if (spine_vertices.size() != uvs.size()) {
            return validation_error(
                document,
                vertices_value->location(),
                std::string(json_path) + ".vertices",
                "unweighted mesh vertices must align with the uv count");
        }

        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const double local_x = spine_vertices[vertex_index * 2U];
            const double local_y = spine_vertices[(vertex_index * 2U) + 1U];
            vertices_array.push_back(make_number_value(local_x));
            vertices_array.push_back(make_number_value(local_y));

            Value::Array influences;
            influences.push_back(build_weight_influence_object(slot.bone_name, local_x, local_y, 1.0));
            weights_array.push_back(make_array_value(std::move(influences)));
        }
    } else {
        const BoneWorldTransform& slot_bone_world = context.bone_world_transforms[slot.bone_index];
        std::size_t cursor = 0;
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            if (cursor >= spine_vertices.size()) {
                return validation_error(
                    document,
                    vertices_value->location(),
                    std::string(json_path) + ".vertices",
                    "weighted mesh vertices ended before all uv pairs were decoded");
            }

            const double bone_count_value = spine_vertices[cursor++];
            if (bone_count_value < 1.0 || std::floor(bone_count_value) != bone_count_value) {
                return validation_error(
                    document,
                    vertices_value->location(),
                    std::string(json_path) + ".vertices",
                    "weighted mesh vertices must start with an integer bone influence count");
            }
            const std::size_t influence_count = static_cast<std::size_t>(bone_count_value);
            if (influence_count > 4U) {
                return validation_error(
                    document,
                    vertices_value->location(),
                    std::string(json_path) + ".vertices",
                    "Marrow mesh vertices support at most 4 bone influences");
            }
            if (cursor + influence_count * 4U > spine_vertices.size()) {
                return validation_error(
                    document,
                    vertices_value->location(),
                    std::string(json_path) + ".vertices",
                    "weighted mesh vertices were truncated while reading influences");
            }

            Value::Array influences;
            influences.reserve(influence_count);
            double total_weight = 0.0;
            AttachmentVertex averaged_world{};
            for (std::size_t influence_index = 0; influence_index < influence_count; ++influence_index) {
                const double bone_index_value = spine_vertices[cursor++];
                if (bone_index_value < 0.0 ||
                    std::floor(bone_index_value) != bone_index_value ||
                    bone_index_value >= static_cast<double>(context.bones.size())) {
                    return validation_error(
                        document,
                        vertices_value->location(),
                        std::string(json_path) + ".vertices",
                        "weighted mesh influence references an invalid bone index");
                }
                const std::size_t bone_index = static_cast<std::size_t>(bone_index_value);
                const double local_x = spine_vertices[cursor++];
                const double local_y = spine_vertices[cursor++];
                const double weight = spine_vertices[cursor++];
                if (weight <= 0.0) {
                    return validation_error(
                        document,
                        vertices_value->location(),
                        std::string(json_path) + ".vertices",
                        "weighted mesh bone weights must be positive");
                }

                influences.push_back(build_weight_influence_object(
                    context.bones[bone_index].name,
                    local_x,
                    local_y,
                    weight));
                total_weight += weight;

                const MeshWorldVertex world_vertex = detail::transform_mesh_point(
                    context.bone_world_transforms[bone_index],
                    local_x,
                    local_y);
                averaged_world.x += world_vertex.x * weight;
                averaged_world.y += world_vertex.y * weight;
            }
            if (total_weight <= 0.0) {
                return validation_error(
                    document,
                    vertices_value->location(),
                    std::string(json_path) + ".vertices",
                    "weighted mesh vertices must preserve a positive total weight");
            }

            const AttachmentVertex representative_local = detail::inverse_transform_point(
                slot_bone_world,
                averaged_world.x / total_weight,
                averaged_world.y / total_weight);
            vertices_array.push_back(make_number_value(representative_local.x));
            vertices_array.push_back(make_number_value(representative_local.y));
            weights_array.push_back(make_array_value(std::move(influences)));
        }
        if (cursor != spine_vertices.size()) {
            return validation_error(
                document,
                vertices_value->location(),
                std::string(json_path) + ".vertices",
                "weighted mesh vertices contained trailing data after decoding");
        }
    }

    Value::Array uvs_array;
    uvs_array.reserve(uvs.size());
    for (const double coordinate : uvs) {
        uvs_array.push_back(make_number_value(coordinate));
    }

    Value::Object attachment_object;
    attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
    attachment_object.emplace("type", make_string_value("mesh"));
    attachment_object.emplace("region", make_string_value(std::move(region_name)));
    attachment_object.emplace("vertices", make_array_value(std::move(vertices_array)));
    attachment_object.emplace("triangles", make_array_value(std::move(triangles)));
    attachment_object.emplace("uvs", make_array_value(std::move(uvs_array)));
    attachment_object.emplace("weights", make_array_value(std::move(weights_array)));
    *attachment_out = make_object_value(std::move(attachment_object));
    return std::nullopt;
}

std::optional<LoadError> build_polygon_attachment(
    const Document& document,
    const Value& attachment_value,
    std::string_view attachment_name,
    std::string_view output_type,
    std::string_view json_path,
    Value* attachment_out) {
    double vertex_count_value = 0.0;
    if (const auto error = read_required_number(
            document,
            attachment_value,
            "vertexCount",
            json_path,
            &vertex_count_value)) {
        return error;
    }
    if (vertex_count_value < 0.0 || std::floor(vertex_count_value) != vertex_count_value) {
        return validation_error(
            document,
            attachment_value.location(),
            std::string(json_path) + ".vertexCount",
            "vertexCount must be a non-negative integer");
    }
    const std::size_t vertex_count = static_cast<std::size_t>(vertex_count_value);

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

    std::vector<double> coordinates;
    if (const auto error = parse_number_array(
            document,
            *vertices_value,
            std::string(json_path) + ".vertices",
            &coordinates)) {
        return error;
    }
    if (coordinates.size() != vertex_count * 2U) {
        return validation_error(
            document,
            vertices_value->location(),
            std::string(json_path) + ".vertices",
            "weighted polygon attachments are not supported by the Marrow importer");
    }

    Value::Array vertices;
    vertices.reserve(coordinates.size());
    for (const double coordinate : coordinates) {
        vertices.push_back(make_number_value(coordinate));
    }

    Value::Object attachment_object;
    attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
    attachment_object.emplace("type", make_string_value(std::string(output_type)));
    attachment_object.emplace("vertices", make_array_value(std::move(vertices)));
    *attachment_out = make_object_value(std::move(attachment_object));
    return std::nullopt;
}

std::optional<LoadError> convert_spine_attachment(
    ImportContext* context,
    std::string_view skin_name,
    const SpineSlotInfo& slot,
    std::string_view attachment_name,
    const Value& attachment_value,
    std::string_view json_path,
    ConvertedAttachmentResult* result_out) {
    const Document& document = context->document;
    if (const auto error = json::require_type(
            document,
            attachment_value,
            Value::Type::Object,
            json_path)) {
        return error;
    }

    std::optional<std::string> actual_name;
    if (const auto error = read_optional_string(
            document,
            attachment_value,
            "name",
            json_path,
            &actual_name)) {
        return error;
    }
    const std::string lookup_name =
        actual_name.has_value() && !actual_name->empty() ? *actual_name : std::string(attachment_name);

    std::optional<std::string> region_path;
    if (const auto error = read_optional_string(
            document,
            attachment_value,
            "path",
            json_path,
            &region_path)) {
        return error;
    }
    std::string region_name = region_path.value_or(lookup_name);

    std::optional<std::string> type_name;
    if (const auto error = read_optional_string(
            document,
            attachment_value,
            "type",
            json_path,
            &type_name)) {
        return error;
    }
    const std::string normalized_type = normalize_enum_name(type_name.value_or("region"));

    ConvertedAttachmentResult result;
    if (normalized_type == "region") {
        if (const auto error = build_region_mesh_attachment(
                document,
                attachment_value,
                slot,
                attachment_name,
                std::move(region_name),
                json_path,
                &result.attachment_value)) {
            return error;
        }
    } else if (normalized_type == "mesh") {
        if (const auto error = build_mesh_attachment(
                *context,
                attachment_value,
                slot,
                attachment_name,
                std::move(region_name),
                json_path,
                &result.attachment_value)) {
            return error;
        }
    } else if (normalized_type == "linkedmesh") {
        std::string parent_attachment;
        if (const auto error = read_required_string(
                document,
                attachment_value,
                "parent",
                json_path,
                &parent_attachment)) {
            return error;
        }
        if (parent_attachment.empty()) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path) + ".parent",
                "linked mesh parents must not be empty");
        }

        std::optional<std::string> parent_skin;
        if (const auto error = read_optional_string(
                document,
                attachment_value,
                "skin",
                json_path,
                &parent_skin)) {
            return error;
        }
        bool deform = true;
        if (const auto error = read_optional_boolean(
                document,
                attachment_value,
                "deform",
                json_path,
                &deform)) {
            return error;
        }

        Value::Object attachment_object;
        attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
        attachment_object.emplace("type", make_string_value("linked_mesh"));
        attachment_object.emplace("region", make_string_value(std::move(region_name)));
        attachment_object.emplace("parent", make_string_value(std::move(parent_attachment)));
        if (parent_skin.has_value()) {
            attachment_object.emplace("skin", make_string_value(*parent_skin));
        }
        attachment_object.emplace("deform", make_boolean_value(deform));
        result.attachment_value = make_object_value(std::move(attachment_object));
    } else if (normalized_type == "point") {
        double x = 0.0;
        double y = 0.0;
        double rotation = 0.0;
        if (const auto error = read_optional_number(document, attachment_value, "x", json_path, &x)) {
            return error;
        }
        if (const auto error = read_optional_number(document, attachment_value, "y", json_path, &y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                attachment_value,
                "rotation",
                json_path,
                &rotation)) {
            return error;
        }

        Value::Object attachment_object;
        attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
        attachment_object.emplace("type", make_string_value("point"));
        if (x != 0.0) {
            attachment_object.emplace("x", make_number_value(x));
        }
        if (y != 0.0) {
            attachment_object.emplace("y", make_number_value(y));
        }
        if (rotation != 0.0) {
            attachment_object.emplace("rotation", make_number_value(rotation));
        }
        result.attachment_value = make_object_value(std::move(attachment_object));
    } else if (normalized_type == "boundingbox") {
        if (const auto error = build_polygon_attachment(
                document,
                attachment_value,
                attachment_name,
                "bounding_box",
                json_path,
                &result.attachment_value)) {
            return error;
        }
    } else if (normalized_type == "clipping") {
        if (const auto error = build_polygon_attachment(
                document,
                attachment_value,
                attachment_name,
                "clipping",
                json_path,
                &result.attachment_value)) {
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
            result.attachment_value.as_object().emplace("end", make_string_value(*end_slot_name));
        }
    } else if (normalized_type == "path") {
        double vertex_count_value = 0.0;
        if (const auto error = read_required_number(
                document,
                attachment_value,
                "vertexCount",
                json_path,
                &vertex_count_value)) {
            return error;
        }
        if (vertex_count_value < 0.0 || std::floor(vertex_count_value) != vertex_count_value) {
            return validation_error(
                document,
                attachment_value.location(),
                std::string(json_path) + ".vertexCount",
                "vertexCount must be a non-negative integer");
        }
        const std::size_t vertex_count = static_cast<std::size_t>(vertex_count_value);

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

        std::vector<AttachmentVertex> points;
        if (const auto error = parse_path_points(
                document,
                *vertices_value,
                vertex_count,
                std::string(json_path) + ".vertices",
                &points)) {
            return error;
        }

        Value::Array point_values;
        point_values.reserve(points.size() * 2U);
        for (const AttachmentVertex& point : points) {
            point_values.push_back(make_number_value(point.x));
            point_values.push_back(make_number_value(point.y));
        }

        Value::Object attachment_object;
        attachment_object.emplace("attachment", make_string_value(std::string(attachment_name)));
        attachment_object.emplace("type", make_string_value("path"));
        attachment_object.emplace("points", make_array_value(std::move(point_values)));
        result.attachment_value = make_object_value(std::move(attachment_object));

        AttachmentRecord record;
        record.skin_name = std::string(skin_name);
        record.slot_name = slot.name;
        record.attachment_name = std::string(attachment_name);
        record.is_path = true;

        std::vector<AttachmentVertex> world_points;
        world_points.reserve(points.size());
        const BoneWorldTransform& slot_bone_world =
            context->bone_world_transforms[slot.bone_index];
        for (const AttachmentVertex& point : points) {
            world_points.push_back(detail::transform_attachment_vertex(
                slot_bone_world,
                point.x,
                point.y));
        }
        const std::vector<detail::PathDistanceSample> samples =
            detail::build_path_distance_samples(world_points);
        if (!samples.empty()) {
            record.path_total_length = samples.back().distance;
        }
        result.record = std::move(record);
    } else {
        return validation_error(
            document,
            attachment_value.location(),
            std::string(json_path) + ".type",
            "unsupported Spine attachment type '" + type_name.value_or("region") + "'");
    }

    *result_out = std::move(result);
    return std::nullopt;
}

template <typename Callback>
std::optional<LoadError> for_each_spine_skin(
    const Document& document,
    const Value& root,
    Callback&& callback) {
    const Value* skins_value = find_optional_member(root, "skins");
    if (skins_value == nullptr) {
        return validation_error(
            document,
            root.location(),
            "$.skins",
            "Spine JSON imports require a skins section");
    }

    if (skins_value->is_array()) {
        for (std::size_t index = 0; index < skins_value->as_array().size(); ++index) {
            const Value& skin_value = skins_value->as_array()[index];
            const std::string skin_path = "$.skins[" + std::to_string(index) + "]";
            if (const auto error = json::require_type(
                    document,
                    skin_value,
                    Value::Type::Object,
                    skin_path)) {
                return error;
            }

            std::string skin_name;
            if (const auto error = read_required_string(
                    document,
                    skin_value,
                    "name",
                    skin_path,
                    &skin_name)) {
                return error;
            }
            if (skin_name.empty()) {
                return validation_error(
                    document,
                    skin_value.location(),
                    skin_path + ".name",
                    "skin names must not be empty");
            }

            if (const auto error = callback(skin_name, skin_value, skin_path)) {
                return error;
            }
        }
        return std::nullopt;
    }

    if (const auto error = json::require_type(
            document,
            *skins_value,
            Value::Type::Object,
            "$.skins")) {
        return error;
    }

    for (const auto& [skin_name, skin_value] : skins_value->as_object()) {
        const std::string skin_path = "$.skins." + skin_name;
        if (const auto error = json::require_type(
                document,
                skin_value,
                Value::Type::Object,
                skin_path)) {
            return error;
        }

        if (const auto error = callback(skin_name, skin_value, skin_path)) {
            return error;
        }
    }

    return std::nullopt;
}

std::optional<LoadError> parse_skin_scopes(
    const Document& document,
    const Value& skin_value,
    std::string_view json_path,
    Value::Object* skin_object_out) {
    const std::array<std::string_view, 4> scope_keys{{"bones", "ik", "path", "transform"}};
    for (const std::string_view scope_key : scope_keys) {
        const Value* scope_value = find_optional_member(skin_value, scope_key);
        if (scope_value == nullptr) {
            continue;
        }

        std::vector<std::string> scope_names;
        if (const auto error = parse_string_array(
                document,
                *scope_value,
                std::string(json_path) + "." + std::string(scope_key),
                &scope_names)) {
            return error;
        }

        Value::Array names;
        names.reserve(scope_names.size());
        for (const std::string& scope_name : scope_names) {
            names.push_back(make_string_value(scope_name));
        }
        skin_object_out->emplace(std::string(scope_key), make_array_value(std::move(names)));
    }

    return std::nullopt;
}

std::optional<LoadError> parse_skins(
    ImportContext* context,
    Value* skins_out) {
    const Document& document = context->document;
    Value::Object output_skins;

    const auto handle_skin = [&](const std::string& skin_name,
                                 const Value& skin_value,
                                 const std::string& skin_path) -> std::optional<LoadError> {
        if (output_skins.find(skin_name) != output_skins.end()) {
            return validation_error(
                document,
                skin_value.location(),
                skin_path,
                "skin names must be unique");
        }

        if (skin_name == "default") {
            context->default_skin_name = skin_name;
        }

        Value::Object skin_object;
        if (const auto error = parse_skin_scopes(document, skin_value, skin_path, &skin_object)) {
            return error;
        }

        const Value* attachments_value = find_optional_member(skin_value, "attachments");
        if (attachments_value != nullptr) {
            if (const auto error = json::require_type(
                    document,
                    *attachments_value,
                    Value::Type::Object,
                    skin_path + ".attachments")) {
                return error;
            }

            Value::Object slot_attachments;
            for (const auto& [slot_name, slot_value] : attachments_value->as_object()) {
                const std::string slot_path = skin_path + ".attachments." + slot_name;
                if (const auto error = json::require_type(
                        document,
                        slot_value,
                        Value::Type::Object,
                        slot_path)) {
                    return error;
                }

                const auto slot_index = find_slot_index(context->slots, slot_name);
                if (!slot_index.has_value()) {
                    return validation_error(
                        document,
                        slot_value.location(),
                        slot_path,
                        "skin references unknown slot '" + slot_name + "'");
                }

                Value::Object attachment_map;
                for (const auto& [attachment_name, attachment_value] : slot_value.as_object()) {
                    ConvertedAttachmentResult converted_attachment;
                    if (const auto error = convert_spine_attachment(
                            context,
                            skin_name,
                            context->slots[*slot_index],
                            attachment_name,
                            attachment_value,
                            slot_path + "." + attachment_name,
                            &converted_attachment)) {
                        return error;
                    }

                    attachment_map.emplace(
                        attachment_name,
                        std::move(converted_attachment.attachment_value));
                    if (converted_attachment.record.has_value()) {
                        context->attachment_records.push_back(std::move(*converted_attachment.record));
                    }
                }

                if (!attachment_map.empty()) {
                    slot_attachments.emplace(slot_name, make_object_value(std::move(attachment_map)));
                }
            }

            if (!slot_attachments.empty()) {
                skin_object.emplace("attachments", make_object_value(std::move(slot_attachments)));
            }
        }

        output_skins.emplace(skin_name, make_object_value(std::move(skin_object)));
        return std::nullopt;
    };

    if (const auto error = for_each_spine_skin(document, document.root, handle_skin)) {
        return error;
    }

    if (output_skins.empty()) {
        return validation_error(
            document,
            document.root.location(),
            "$.skins",
            "Spine JSON imports require at least one skin");
    }

    *skins_out = make_object_value(std::move(output_skins));
    return std::nullopt;
}

const AttachmentRecord* find_path_attachment_record(
    const ImportContext& context,
    std::string_view slot_name,
    std::string_view attachment_name) {
    if (context.default_skin_name.has_value()) {
        const auto it = std::find_if(
            context.attachment_records.begin(),
            context.attachment_records.end(),
            [&](const AttachmentRecord& record) {
                return record.is_path &&
                    record.skin_name == *context.default_skin_name &&
                    record.slot_name == slot_name &&
                    record.attachment_name == attachment_name;
            });
        if (it != context.attachment_records.end()) {
            return &(*it);
        }
    }

    const auto it = std::find_if(
        context.attachment_records.begin(),
        context.attachment_records.end(),
        [&](const AttachmentRecord& record) {
            return record.is_path &&
                record.slot_name == slot_name &&
                record.attachment_name == attachment_name;
        });
    return it == context.attachment_records.end() ? nullptr : &(*it);
}

std::optional<LoadError> parse_string_list_member(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    Value::Array* values_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        values_out->clear();
        return std::nullopt;
    }

    std::vector<std::string> values;
    if (const auto error = parse_string_array(
            document,
            *member,
            std::string(json_path) + "." + std::string(key),
            &values)) {
        return error;
    }

    values_out->clear();
    values_out->reserve(values.size());
    for (const std::string& value : values) {
        values_out->push_back(make_string_value(value));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_ik_constraints(
    const ImportContext& context,
    Value* ik_out) {
    const Document& document = context.document;
    const Value* ik_value = find_optional_member(document.root, "ik");
    if (ik_value == nullptr) {
        *ik_out = make_array_value();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *ik_value,
            Value::Type::Array,
            "$.ik")) {
        return error;
    }

    Value::Array constraints;
    constraints.reserve(ik_value->as_array().size());
    for (std::size_t index = 0; index < ik_value->as_array().size(); ++index) {
        const Value& constraint_value = ik_value->as_array()[index];
        const std::string constraint_path = "$.ik[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                constraint_path)) {
            return error;
        }

        std::string name;
        std::string target_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                constraint_path,
                &name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "target",
                constraint_path,
                &target_name)) {
            return error;
        }

        Value::Array bones;
        if (const auto error = parse_string_list_member(
                document,
                constraint_value,
                "bones",
                constraint_path,
                &bones)) {
            return error;
        }
        if (bones.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".bones",
                "ik constraints must target at least one bone");
        }

        double mix = 1.0;
        double softness = 0.0;
        bool bend_positive = true;
        bool compress = false;
        bool stretch = false;
        if (const auto error = read_optional_number(document, constraint_value, "mix", constraint_path, &mix)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "softness",
                constraint_path,
                &softness)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "bendPositive",
                constraint_path,
                &bend_positive)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "compress",
                constraint_path,
                &compress)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "stretch",
                constraint_path,
                &stretch)) {
            return error;
        }

        Value::Object output_constraint;
        output_constraint.emplace("name", make_string_value(std::move(name)));
        output_constraint.emplace("bones", make_array_value(std::move(bones)));
        output_constraint.emplace("target", make_string_value(std::move(target_name)));
        output_constraint.emplace("mix", make_number_value(mix));
        output_constraint.emplace("softness", make_number_value(softness));
        output_constraint.emplace("bendPositive", make_boolean_value(bend_positive));
        output_constraint.emplace("compress", make_boolean_value(compress));
        output_constraint.emplace("stretch", make_boolean_value(stretch));
        constraints.push_back(make_object_value(std::move(output_constraint)));
    }

    *ik_out = make_array_value(std::move(constraints));
    return std::nullopt;
}

std::optional<LoadError> parse_transform_constraints(
    const ImportContext& context,
    Value* transform_out) {
    const Document& document = context.document;
    const Value* transform_value = find_optional_member(document.root, "transform");
    if (transform_value == nullptr) {
        *transform_out = make_array_value();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *transform_value,
            Value::Type::Array,
            "$.transform")) {
        return error;
    }

    Value::Array constraints;
    constraints.reserve(transform_value->as_array().size());
    for (std::size_t index = 0; index < transform_value->as_array().size(); ++index) {
        const Value& constraint_value = transform_value->as_array()[index];
        const std::string constraint_path = "$.transform[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                constraint_path)) {
            return error;
        }

        bool local = false;
        bool relative = false;
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "local",
                constraint_path,
                &local)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                constraint_value,
                "relative",
                constraint_path,
                &relative)) {
            return error;
        }
        if (local || relative) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path,
                "local/relative transform constraints are not supported by the Marrow importer");
        }

        std::string name;
        std::string target_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                constraint_path,
                &name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "target",
                constraint_path,
                &target_name)) {
            return error;
        }

        Value::Array bones;
        if (const auto error = parse_string_list_member(
                document,
                constraint_value,
                "bones",
                constraint_path,
                &bones)) {
            return error;
        }
        if (bones.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".bones",
                "transform constraints must target at least one bone");
        }

        double rotation = 0.0;
        double x = 0.0;
        double y = 0.0;
        double scale_x = 0.0;
        double scale_y = 0.0;
        double shear_y = 0.0;
        double rotate_mix = 1.0;
        double translate_mix = 1.0;
        double scale_mix = 1.0;
        double shear_mix = 1.0;
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotation",
                constraint_path,
                &rotation)) {
            return error;
        }
        if (const auto error = read_optional_number(document, constraint_value, "x", constraint_path, &x)) {
            return error;
        }
        if (const auto error = read_optional_number(document, constraint_value, "y", constraint_path, &y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "scaleX",
                constraint_path,
                &scale_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "scaleY",
                constraint_path,
                &scale_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "shearY",
                constraint_path,
                &shear_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotateMix",
                constraint_path,
                &rotate_mix)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "translateMix",
                constraint_path,
                &translate_mix)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "scaleMix",
                constraint_path,
                &scale_mix)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "shearMix",
                constraint_path,
                &shear_mix)) {
            return error;
        }

        Value::Object offset;
        offset.emplace("rotation", make_number_value(rotation));
        offset.emplace("x", make_number_value(x));
        offset.emplace("y", make_number_value(y));
        offset.emplace("scaleX", make_number_value(scale_x));
        offset.emplace("scaleY", make_number_value(scale_y));
        offset.emplace("shearX", make_number_value(0.0));
        offset.emplace("shearY", make_number_value(shear_y));

        Value::Object output_constraint;
        output_constraint.emplace("name", make_string_value(std::move(name)));
        output_constraint.emplace("source", make_string_value(std::move(target_name)));
        output_constraint.emplace("bones", make_array_value(std::move(bones)));
        output_constraint.emplace("rotateMix", make_number_value(rotate_mix));
        output_constraint.emplace("translateMix", make_number_value(translate_mix));
        output_constraint.emplace("scaleMix", make_number_value(scale_mix));
        output_constraint.emplace("shearMix", make_number_value(shear_mix));
        output_constraint.emplace("offset", make_object_value(std::move(offset)));
        constraints.push_back(make_object_value(std::move(output_constraint)));
    }

    *transform_out = make_array_value(std::move(constraints));
    return std::nullopt;
}

std::optional<LoadError> parse_path_constraints(
    const ImportContext& context,
    Value* path_out) {
    const Document& document = context.document;
    const Value* path_value = find_optional_member(document.root, "path");
    if (path_value == nullptr) {
        *path_out = make_array_value();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *path_value,
            Value::Type::Array,
            "$.path")) {
        return error;
    }

    Value::Array constraints;
    constraints.reserve(path_value->as_array().size());
    for (std::size_t index = 0; index < path_value->as_array().size(); ++index) {
        const Value& constraint_value = path_value->as_array()[index];
        const std::string constraint_path = "$.path[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document,
                constraint_value,
                Value::Type::Object,
                constraint_path)) {
            return error;
        }

        std::string name;
        std::string target_slot_name;
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "name",
                constraint_path,
                &name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document,
                constraint_value,
                "target",
                constraint_path,
                &target_slot_name)) {
            return error;
        }

        const auto slot_index = find_slot_index(context.slots, target_slot_name);
        if (!slot_index.has_value()) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".target",
                "path constraint references unknown slot '" + target_slot_name + "'");
        }

        Value::Array bones;
        if (const auto error = parse_string_list_member(
                document,
                constraint_value,
                "bones",
                constraint_path,
                &bones)) {
            return error;
        }
        if (bones.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".bones",
                "path constraints must target at least one bone");
        }

        std::optional<std::string> position_mode;
        std::optional<std::string> spacing_mode;
        std::optional<std::string> rotate_mode;
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "positionMode",
                constraint_path,
                &position_mode)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "spacingMode",
                constraint_path,
                &spacing_mode)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "rotateMode",
                constraint_path,
                &rotate_mode)) {
            return error;
        }

        double position = 0.0;
        double spacing = 0.0;
        double rotation = 0.0;
        double rotate_mix = 1.0;
        double translate_mix = 1.0;
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "position",
                constraint_path,
                &position)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "spacing",
                constraint_path,
                &spacing)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotation",
                constraint_path,
                &rotation)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "rotateMix",
                constraint_path,
                &rotate_mix)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                constraint_value,
                "translateMix",
                constraint_path,
                &translate_mix)) {
            return error;
        }

        if (rotate_mix > 0.0 && rotation != 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".rotation",
                "path constraint rotation offsets are not supported by the Marrow importer");
        }
        if (rotate_mix > 0.0 && rotate_mode.has_value() &&
            *rotate_mode != "tangent") {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".rotateMode",
                "only tangent path rotation is supported by the Marrow importer");
        }

        const std::string_view resolved_position_mode =
            position_mode.has_value() ? std::string_view(*position_mode) : std::string_view("percent");
        if (resolved_position_mode == "fixed") {
            const std::string& attachment_name = context.slots[*slot_index].setup_attachment;
            const AttachmentRecord* path_record = find_path_attachment_record(
                context,
                target_slot_name,
                attachment_name);
            if (path_record == nullptr || path_record->path_total_length <= 1e-8) {
                return validation_error(
                    document,
                    constraint_value.location(),
                    constraint_path + ".positionMode",
                    "fixed path positions require the target slot to use a setup path attachment");
            }
            position /= path_record->path_total_length;
        } else if (resolved_position_mode != "percent") {
            return validation_error(
                document,
                constraint_value.location(),
                constraint_path + ".positionMode",
                "path constraint positionMode must be fixed or percent");
        }

        std::string spacing_mode_value = "length";
        if (spacing_mode.has_value()) {
            if (*spacing_mode == "percent") {
                spacing_mode_value = "percent";
            } else if (*spacing_mode != "length" && *spacing_mode != "fixed") {
                return validation_error(
                    document,
                    constraint_value.location(),
                    constraint_path + ".spacingMode",
                    "path constraint spacingMode must be length, fixed, or percent");
            }
        }

        Value::Object output_constraint;
        output_constraint.emplace("name", make_string_value(std::move(name)));
        output_constraint.emplace("slot", make_string_value(std::move(target_slot_name)));
        output_constraint.emplace("bones", make_array_value(std::move(bones)));
        output_constraint.emplace("position", make_number_value(position));
        output_constraint.emplace("spacing", make_number_value(spacing));
        output_constraint.emplace("spacingMode", make_string_value(std::move(spacing_mode_value)));
        output_constraint.emplace("rotateMix", make_number_value(rotate_mix));
        output_constraint.emplace("translateMix", make_number_value(translate_mix));
        constraints.push_back(make_object_value(std::move(output_constraint)));
    }

    *path_out = make_array_value(std::move(constraints));
    return std::nullopt;
}

std::optional<LoadError> parse_events(
    const ImportContext& context,
    Value* events_out) {
    const Document& document = context.document;
    const Value* events_value = find_optional_member(document.root, "events");
    if (events_value == nullptr) {
        *events_out = make_object_value();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *events_value,
            Value::Type::Object,
            "$.events")) {
        return error;
    }

    Value::Object events;
    for (const auto& [event_name, event_value] : events_value->as_object()) {
        const std::string event_path = "$.events." + event_name;
        if (const auto error = json::require_type(
                document,
                event_value,
                Value::Type::Object,
                event_path)) {
            return error;
        }

        Value::Object output_event;
        int int_value = 0;
        if (const auto error = read_optional_integer(
                document,
                event_value,
                "int",
                event_path,
                &int_value)) {
            return error;
        }
        double float_value = 0.0;
        if (const auto error = read_optional_number(
                document,
                event_value,
                "float",
                event_path,
                &float_value)) {
            return error;
        }
        std::optional<std::string> string_value;
        std::optional<std::string> audio_path;
        if (const auto error = read_optional_string(
                document,
                event_value,
                "string",
                event_path,
                &string_value)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                event_value,
                "audio",
                event_path,
                &audio_path)) {
            return error;
        }

        double volume = 1.0;
        double balance = 0.0;
        if (const auto error = read_optional_number(
                document,
                event_value,
                "volume",
                event_path,
                &volume)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                event_value,
                "balance",
                event_path,
                &balance)) {
            return error;
        }

        output_event.emplace("int", make_number_value(int_value));
        output_event.emplace("float", make_number_value(float_value));
        if (string_value.has_value()) {
            output_event.emplace("string", make_string_value(*string_value));
        }
        if (audio_path.has_value()) {
            output_event.emplace("audio", make_string_value(*audio_path));
        }
        output_event.emplace("volume", make_number_value(volume));
        output_event.emplace("balance", make_number_value(balance));
        events.emplace(event_name, make_object_value(std::move(output_event)));
    }

    *events_out = make_object_value(std::move(events));
    return std::nullopt;
}

std::optional<LoadError> append_additive_vector_timeline(
    const ImportContext& context,
    const Value& timeline_value,
    std::string_view json_path,
    std::string_view x_key,
    std::string_view y_key,
    double base_x,
    double base_y,
    Value* output_out) {
    const Document& document = context.document;
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    Value::Array keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = -1.0;
    for (std::size_t keyframe_index = 0; keyframe_index < timeline_value.as_array().size();
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

        double time = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &time)) {
            return error;
        }
        if (time <= previous_time && keyframe_index > 0U) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }
        previous_time = time;

        double x = 0.0;
        double y = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                x_key,
                keyframe_path,
                &x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                y_key,
                keyframe_path,
                &y)) {
            return error;
        }

        Value::Object output_keyframe;
        output_keyframe.emplace("time", make_number_value(time));
        output_keyframe.emplace("x", make_number_value(base_x + x));
        output_keyframe.emplace("y", make_number_value(base_y + y));
        bool stepped = false;
        try {
            output_keyframe.emplace(
                "curve",
                build_curve_value(document, keyframe_value, keyframe_path, &stepped));
        } catch (const LoadError& error) {
            return error;
        }
        keyframes.push_back(make_object_value(std::move(output_keyframe)));
    }

    *output_out = make_array_value(std::move(keyframes));
    return std::nullopt;
}

std::optional<LoadError> append_scale_timeline(
    const ImportContext& context,
    const Value& timeline_value,
    std::string_view json_path,
    std::string_view x_key,
    std::string_view y_key,
    double base_x,
    double base_y,
    Value* output_out) {
    const Document& document = context.document;
    if (const auto error = json::require_type(
            document,
            timeline_value,
            Value::Type::Array,
            json_path)) {
        return error;
    }

    Value::Array keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = -1.0;
    for (std::size_t keyframe_index = 0; keyframe_index < timeline_value.as_array().size();
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

        double time = 0.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                "time",
                keyframe_path,
                &time)) {
            return error;
        }
        if (time <= previous_time && keyframe_index > 0U) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline keyframe times must be strictly increasing");
        }
        previous_time = time;

        double x = 1.0;
        double y = 1.0;
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                x_key,
                keyframe_path,
                &x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                keyframe_value,
                y_key,
                keyframe_path,
                &y)) {
            return error;
        }

        Value::Object output_keyframe;
        output_keyframe.emplace("time", make_number_value(time));
        output_keyframe.emplace("x", make_number_value(base_x * x));
        output_keyframe.emplace("y", make_number_value(base_y * y));
        bool stepped = false;
        try {
            output_keyframe.emplace(
                "curve",
                build_curve_value(document, keyframe_value, keyframe_path, &stepped));
        } catch (const LoadError& error) {
            return error;
        }
        keyframes.push_back(make_object_value(std::move(output_keyframe)));
    }

    *output_out = make_array_value(std::move(keyframes));
    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_offsets(
    const ImportContext& context,
    const Value& keyframe_value,
    std::string_view json_path,
    std::vector<std::string>* draw_order_out) {
    const Document& document = context.document;
    *draw_order_out = context.setup_draw_order;

    const Value* offsets_value = find_optional_member(keyframe_value, "offsets");
    if (offsets_value == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *offsets_value,
            Value::Type::Array,
            std::string(json_path) + ".offsets")) {
        return error;
    }

    std::vector<int> offsets(context.setup_draw_order.size(), 0);
    std::vector<bool> moved(context.setup_draw_order.size(), false);
    for (std::size_t offset_index = 0; offset_index < offsets_value->as_array().size(); ++offset_index) {
        const Value& offset_value = offsets_value->as_array()[offset_index];
        const std::string offset_path =
            std::string(json_path) + ".offsets[" + std::to_string(offset_index) + "]";
        if (const auto error = json::require_type(
                document,
                offset_value,
                Value::Type::Object,
                offset_path)) {
            return error;
        }

        std::string slot_name;
        int offset = 0;
        if (const auto error = read_required_string(
                document,
                offset_value,
                "slot",
                offset_path,
                &slot_name)) {
            return error;
        }
        if (const auto error = read_optional_integer(
                document,
                offset_value,
                "offset",
                offset_path,
                &offset)) {
            return error;
        }

        const auto slot_index = find_slot_index(context.slots, slot_name);
        if (!slot_index.has_value()) {
            return validation_error(
                document,
                offset_value.location(),
                offset_path + ".slot",
                "draworder references unknown slot '" + slot_name + "'");
        }
        if (moved[*slot_index]) {
            return validation_error(
                document,
                offset_value.location(),
                offset_path + ".slot",
                "draworder offsets must not repeat slots");
        }

        offsets[*slot_index] = offset;
        moved[*slot_index] = true;
    }

    std::vector<std::optional<std::string>> moved_slots(context.setup_draw_order.size());
    std::vector<std::string> unchanged;
    unchanged.reserve(context.setup_draw_order.size());

    for (std::size_t slot_index = 0; slot_index < context.setup_draw_order.size(); ++slot_index) {
        if (!moved[slot_index]) {
            unchanged.push_back(context.setup_draw_order[slot_index]);
            continue;
        }

        const int destination = static_cast<int>(slot_index) + offsets[slot_index];
        if (destination < 0 ||
            destination >= static_cast<int>(context.setup_draw_order.size()) ||
            moved_slots[static_cast<std::size_t>(destination)].has_value()) {
            return validation_error(
                document,
                keyframe_value.location(),
                std::string(json_path) + ".offsets",
                "draworder offsets produced an invalid slot ordering");
        }
        moved_slots[static_cast<std::size_t>(destination)] = context.setup_draw_order[slot_index];
    }

    draw_order_out->clear();
    draw_order_out->reserve(context.setup_draw_order.size());
    std::size_t unchanged_index = 0;
    for (std::size_t slot_index = 0; slot_index < moved_slots.size(); ++slot_index) {
        if (moved_slots[slot_index].has_value()) {
            draw_order_out->push_back(*moved_slots[slot_index]);
            continue;
        }
        if (unchanged_index >= unchanged.size()) {
            return validation_error(
                document,
                keyframe_value.location(),
                std::string(json_path) + ".offsets",
                "draworder offsets could not be resolved");
        }
        draw_order_out->push_back(unchanged[unchanged_index++]);
    }

    return std::nullopt;
}

std::optional<LoadError> parse_animations(
    const ImportContext& context,
    Value* animations_out) {
    const Document& document = context.document;
    const Value* animations_value = find_optional_member(document.root, "animations");
    if (animations_value == nullptr) {
        return validation_error(
            document,
            document.root.location(),
            "$.animations",
            "Spine JSON imports require at least one animation");
    }
    if (const auto error = json::require_type(
            document,
            *animations_value,
            Value::Type::Object,
            "$.animations")) {
        return error;
    }
    if (animations_value->as_object().empty()) {
        return validation_error(
            document,
            animations_value->location(),
            "$.animations",
            "animations object must not be empty");
    }

    Value::Object animations;
    for (const auto& [animation_name, animation_value] : animations_value->as_object()) {
        const std::string animation_path = "$.animations." + animation_name;
        if (const auto error = json::require_type(
                document,
                animation_value,
                Value::Type::Object,
                animation_path)) {
            return error;
        }

        Value::Object output_animation;

        if (const Value* bones_value = find_optional_member(animation_value, "bones")) {
            if (const auto error = json::require_type(
                    document,
                    *bones_value,
                    Value::Type::Object,
                    animation_path + ".bones")) {
                return error;
            }

            Value::Object output_bones;
            for (const auto& [bone_name, tracks_value] : bones_value->as_object()) {
                const std::string bone_path = animation_path + ".bones." + bone_name;
                if (const auto error = json::require_type(
                        document,
                        tracks_value,
                        Value::Type::Object,
                        bone_path)) {
                    return error;
                }

                const auto bone_index = find_bone_index(context.bones, bone_name);
                if (!bone_index.has_value()) {
                    return validation_error(
                        document,
                        tracks_value.location(),
                        bone_path,
                        "animation references unknown bone '" + bone_name + "'");
                }

                Value::Object output_tracks;
                const BoneData& bone = context.bones[*bone_index];

                if (const Value* rotate_value = find_optional_member(tracks_value, "rotate")) {
                    if (const auto error = json::require_type(
                            document,
                            *rotate_value,
                            Value::Type::Array,
                            bone_path + ".rotate")) {
                        return error;
                    }

                    Value::Array keyframes;
                    keyframes.reserve(rotate_value->as_array().size());
                    double previous_time = -1.0;
                    for (std::size_t keyframe_index = 0;
                         keyframe_index < rotate_value->as_array().size();
                         ++keyframe_index) {
                        const Value& keyframe_value = rotate_value->as_array()[keyframe_index];
                        const std::string keyframe_path =
                            bone_path + ".rotate[" + std::to_string(keyframe_index) + "]";
                        if (const auto error = json::require_type(
                                document,
                                keyframe_value,
                                Value::Type::Object,
                                keyframe_path)) {
                            return error;
                        }

                        double time = 0.0;
                        double angle = 0.0;
                        if (const auto error = read_optional_number(
                                document,
                                keyframe_value,
                                "time",
                                keyframe_path,
                                &time)) {
                            return error;
                        }
                        if (const auto error = read_optional_number(
                                document,
                                keyframe_value,
                                "angle",
                                keyframe_path,
                                &angle)) {
                            return error;
                        }
                        if (time <= previous_time && keyframe_index > 0U) {
                            return validation_error(
                                document,
                                keyframe_value.location(),
                                keyframe_path + ".time",
                                "timeline keyframe times must be strictly increasing");
                        }
                        previous_time = time;

                        Value::Object output_keyframe;
                        output_keyframe.emplace("time", make_number_value(time));
                        output_keyframe.emplace("angle", make_number_value(angle));
                        bool stepped = false;
                        try {
                            output_keyframe.emplace(
                                "curve",
                                build_curve_value(document, keyframe_value, keyframe_path, &stepped));
                        } catch (const LoadError& error) {
                            return error;
                        }
                        keyframes.push_back(make_object_value(std::move(output_keyframe)));
                    }
                    output_tracks.emplace("rotate", make_array_value(std::move(keyframes)));
                }

                if (const Value* translate_value = find_optional_member(tracks_value, "translate")) {
                    Value converted;
                    if (const auto error = append_additive_vector_timeline(
                            context,
                            *translate_value,
                            bone_path + ".translate",
                            "x",
                            "y",
                            bone.setup_pose.x,
                            bone.setup_pose.y,
                            &converted)) {
                        return error;
                    }
                    output_tracks.emplace("translate", std::move(converted));
                }

                if (const Value* scale_value = find_optional_member(tracks_value, "scale")) {
                    Value converted;
                    if (const auto error = append_scale_timeline(
                            context,
                            *scale_value,
                            bone_path + ".scale",
                            "x",
                            "y",
                            bone.setup_pose.scale_x,
                            bone.setup_pose.scale_y,
                            &converted)) {
                        return error;
                    }
                    output_tracks.emplace("scale", std::move(converted));
                }

                if (const Value* shear_value = find_optional_member(tracks_value, "shear")) {
                    Value converted;
                    if (const auto error = append_additive_vector_timeline(
                            context,
                            *shear_value,
                            bone_path + ".shear",
                            "x",
                            "y",
                            bone.setup_pose.shear_x,
                            bone.setup_pose.shear_y,
                            &converted)) {
                        return error;
                    }
                    output_tracks.emplace("shear", std::move(converted));
                }

                if (!output_tracks.empty()) {
                    output_bones.emplace(bone_name, make_object_value(std::move(output_tracks)));
                }
            }
            if (!output_bones.empty()) {
                output_animation.emplace("bones", make_object_value(std::move(output_bones)));
            }
        }

        if (const Value* slots_value = find_optional_member(animation_value, "slots")) {
            if (const auto error = json::require_type(
                    document,
                    *slots_value,
                    Value::Type::Object,
                    animation_path + ".slots")) {
                return error;
            }

            Value::Object output_slots;
            for (const auto& [slot_name, tracks_value] : slots_value->as_object()) {
                const std::string slot_path = animation_path + ".slots." + slot_name;
                if (const auto error = json::require_type(
                        document,
                        tracks_value,
                        Value::Type::Object,
                        slot_path)) {
                    return error;
                }

                if (!find_slot_index(context.slots, slot_name).has_value()) {
                    return validation_error(
                        document,
                        tracks_value.location(),
                        slot_path,
                        "animation references unknown slot '" + slot_name + "'");
                }

                if (find_optional_member(tracks_value, "twoColor") != nullptr) {
                    return validation_error(
                        document,
                        tracks_value.location(),
                        slot_path + ".twoColor",
                        "twoColor timelines are not supported by the Marrow importer");
                }

                Value::Object output_tracks;
                if (const Value* attachment_timeline = find_optional_member(tracks_value, "attachment")) {
                    if (const auto error = json::require_type(
                            document,
                            *attachment_timeline,
                            Value::Type::Array,
                            slot_path + ".attachment")) {
                        return error;
                    }

                    Value::Array keyframes;
                    keyframes.reserve(attachment_timeline->as_array().size());
                    double previous_time = -1.0;
                    for (std::size_t keyframe_index = 0;
                         keyframe_index < attachment_timeline->as_array().size();
                         ++keyframe_index) {
                        const Value& keyframe_value = attachment_timeline->as_array()[keyframe_index];
                        const std::string keyframe_path =
                            slot_path + ".attachment[" + std::to_string(keyframe_index) + "]";
                        if (const auto error = json::require_type(
                                document,
                                keyframe_value,
                                Value::Type::Object,
                                keyframe_path)) {
                            return error;
                        }

                        double time = 0.0;
                        if (const auto error = read_optional_number(
                                document,
                                keyframe_value,
                                "time",
                                keyframe_path,
                                &time)) {
                            return error;
                        }
                        if (time <= previous_time && keyframe_index > 0U) {
                            return validation_error(
                                document,
                                keyframe_value.location(),
                                keyframe_path + ".time",
                                "timeline keyframe times must be strictly increasing");
                        }
                        previous_time = time;

                        Value::Object output_keyframe;
                        output_keyframe.emplace("time", make_number_value(time));
                        const Value* name_value = find_optional_member(keyframe_value, "name");
                        if (name_value != nullptr) {
                            if (const auto error = json::require_type(
                                    document,
                                    *name_value,
                                    Value::Type::String,
                                    keyframe_path + ".name")) {
                                return error;
                            }
                            output_keyframe.emplace(
                                "attachment",
                                make_string_value(name_value->as_string()));
                        } else {
                            output_keyframe.emplace("attachment", make_null_value());
                        }
                        keyframes.push_back(make_object_value(std::move(output_keyframe)));
                    }
                    output_tracks.emplace("attachment", make_array_value(std::move(keyframes)));
                }

                if (const Value* color_timeline = find_optional_member(tracks_value, "color")) {
                    if (const auto error = json::require_type(
                            document,
                            *color_timeline,
                            Value::Type::Array,
                            slot_path + ".color")) {
                        return error;
                    }

                    Value::Array keyframes;
                    keyframes.reserve(color_timeline->as_array().size());
                    double previous_time = -1.0;
                    for (std::size_t keyframe_index = 0;
                         keyframe_index < color_timeline->as_array().size();
                         ++keyframe_index) {
                        const Value& keyframe_value = color_timeline->as_array()[keyframe_index];
                        const std::string keyframe_path =
                            slot_path + ".color[" + std::to_string(keyframe_index) + "]";
                        if (const auto error = json::require_type(
                                document,
                                keyframe_value,
                                Value::Type::Object,
                                keyframe_path)) {
                            return error;
                        }

                        double time = 0.0;
                        if (const auto error = read_optional_number(
                                document,
                                keyframe_value,
                                "time",
                                keyframe_path,
                                &time)) {
                            return error;
                        }
                        if (time <= previous_time && keyframe_index > 0U) {
                            return validation_error(
                                document,
                                keyframe_value.location(),
                                keyframe_path + ".time",
                                "timeline keyframe times must be strictly increasing");
                        }
                        previous_time = time;

                        const Value* color_value = nullptr;
                        if (const auto error = json::require_member(
                                document,
                                keyframe_value,
                                "color",
                                Value::Type::String,
                                keyframe_path,
                                &color_value)) {
                            return error;
                        }

                        SlotColor color;
                        if (const auto error = parse_hex_color(
                                document,
                                *color_value,
                                keyframe_path + ".color",
                                &color)) {
                            return error;
                        }

                        Value::Object output_keyframe;
                        output_keyframe.emplace("time", make_number_value(time));
                        output_keyframe.emplace("r", make_number_value(color.r));
                        output_keyframe.emplace("g", make_number_value(color.g));
                        output_keyframe.emplace("b", make_number_value(color.b));
                        output_keyframe.emplace("a", make_number_value(color.a));
                        bool stepped = false;
                        try {
                            output_keyframe.emplace(
                                "curve",
                                build_curve_value(document, keyframe_value, keyframe_path, &stepped));
                        } catch (const LoadError& error) {
                            return error;
                        }
                        keyframes.push_back(make_object_value(std::move(output_keyframe)));
                    }
                    output_tracks.emplace("color", make_array_value(std::move(keyframes)));
                }

                if (!output_tracks.empty()) {
                    output_slots.emplace(slot_name, make_object_value(std::move(output_tracks)));
                }
            }
            if (!output_slots.empty()) {
                output_animation.emplace("slots", make_object_value(std::move(output_slots)));
            }
        }

        if (const Value* events_value = find_optional_member(animation_value, "events")) {
            if (const auto error = json::require_type(
                    document,
                    *events_value,
                    Value::Type::Array,
                    animation_path + ".events")) {
                return error;
            }

            Value::Array keyframes;
            keyframes.reserve(events_value->as_array().size());
            double previous_time = -1.0;
            for (std::size_t keyframe_index = 0;
                 keyframe_index < events_value->as_array().size();
                 ++keyframe_index) {
                const Value& keyframe_value = events_value->as_array()[keyframe_index];
                const std::string keyframe_path =
                    animation_path + ".events[" + std::to_string(keyframe_index) + "]";
                if (const auto error = json::require_type(
                        document,
                        keyframe_value,
                        Value::Type::Object,
                        keyframe_path)) {
                    return error;
                }

                double time = 0.0;
                if (const auto error = read_optional_number(
                        document,
                        keyframe_value,
                        "time",
                        keyframe_path,
                        &time)) {
                    return error;
                }
                if (time < previous_time) {
                    return validation_error(
                        document,
                        keyframe_value.location(),
                        keyframe_path + ".time",
                        "event keyframe times must be non-decreasing");
                }
                previous_time = time;

                std::string name;
                if (const auto error = read_required_string(
                        document,
                        keyframe_value,
                        "name",
                        keyframe_path,
                        &name)) {
                    return error;
                }

                Value::Object output_keyframe;
                output_keyframe.emplace("time", make_number_value(time));
                output_keyframe.emplace("name", make_string_value(std::move(name)));

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
                    output_keyframe.emplace("int", make_number_value(int_value));
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
                    output_keyframe.emplace("float", make_number_value(float_value));
                }

                std::optional<std::string> string_value;
                if (const auto error = read_optional_string(
                        document,
                        keyframe_value,
                        "string",
                        keyframe_path,
                        &string_value)) {
                    return error;
                }
                if (string_value.has_value()) {
                    output_keyframe.emplace("string", make_string_value(*string_value));
                }

                std::optional<std::string> audio_path;
                if (const auto error = read_optional_string(
                        document,
                        keyframe_value,
                        "audio",
                        keyframe_path,
                        &audio_path)) {
                    return error;
                }
                if (audio_path.has_value()) {
                    output_keyframe.emplace("audio", make_string_value(*audio_path));
                }

                double volume = 1.0;
                if (const auto error = read_optional_number(
                        document,
                        keyframe_value,
                        "volume",
                        keyframe_path,
                        &volume)) {
                    return error;
                }
                if (find_optional_member(keyframe_value, "volume") != nullptr) {
                    output_keyframe.emplace("volume", make_number_value(volume));
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
                    output_keyframe.emplace("balance", make_number_value(balance));
                }

                keyframes.push_back(make_object_value(std::move(output_keyframe)));
            }
            if (!keyframes.empty()) {
                output_animation.emplace("events", make_array_value(std::move(keyframes)));
            }
        }

        const Value* draw_order_value = find_optional_member(animation_value, "drawOrder");
        if (draw_order_value == nullptr) {
            draw_order_value = find_optional_member(animation_value, "draworder");
        }
        if (draw_order_value != nullptr) {
            if (const auto error = json::require_type(
                    document,
                    *draw_order_value,
                    Value::Type::Array,
                    animation_path + ".draworder")) {
                return error;
            }

            Value::Array keyframes;
            keyframes.reserve(draw_order_value->as_array().size());
            double previous_time = -1.0;
            for (std::size_t keyframe_index = 0;
                 keyframe_index < draw_order_value->as_array().size();
                 ++keyframe_index) {
                const Value& keyframe_value = draw_order_value->as_array()[keyframe_index];
                const std::string keyframe_path =
                    animation_path + ".draworder[" + std::to_string(keyframe_index) + "]";
                if (const auto error = json::require_type(
                        document,
                        keyframe_value,
                        Value::Type::Object,
                        keyframe_path)) {
                    return error;
                }

                double time = 0.0;
                if (const auto error = read_optional_number(
                        document,
                        keyframe_value,
                        "time",
                        keyframe_path,
                        &time)) {
                    return error;
                }
                if (time <= previous_time && keyframe_index > 0U) {
                    return validation_error(
                        document,
                        keyframe_value.location(),
                        keyframe_path + ".time",
                        "draworder keyframe times must be strictly increasing");
                }
                previous_time = time;

                std::vector<std::string> draw_order;
                if (const auto error = parse_draw_order_offsets(
                        context,
                        keyframe_value,
                        keyframe_path,
                        &draw_order)) {
                    return error;
                }

                Value::Array slot_names;
                slot_names.reserve(draw_order.size());
                for (const std::string& slot_name : draw_order) {
                    slot_names.push_back(make_string_value(slot_name));
                }

                Value::Object output_keyframe;
                output_keyframe.emplace("time", make_number_value(time));
                output_keyframe.emplace("slots", make_array_value(std::move(slot_names)));
                keyframes.push_back(make_object_value(std::move(output_keyframe)));
            }

            if (!keyframes.empty()) {
                output_animation.emplace("drawOrder", make_array_value(std::move(keyframes)));
            }
        }

        if (find_optional_member(animation_value, "deform") != nullptr) {
            return validation_error(
                document,
                animation_value.location(),
                animation_path + ".deform",
                "deform timelines are not supported by the Marrow importer");
        }
        if (find_optional_member(animation_value, "ik") != nullptr ||
            find_optional_member(animation_value, "transform") != nullptr ||
            find_optional_member(animation_value, "path") != nullptr) {
            return validation_error(
                document,
                animation_value.location(),
                animation_path,
                "constraint timelines are not supported by the Marrow importer");
        }

        animations.emplace(animation_name, make_object_value(std::move(output_animation)));
    }

    *animations_out = make_object_value(std::move(animations));
    return std::nullopt;
}

std::optional<LoadError> parse_skeleton_metadata(
    ImportContext* context,
    Value* skeleton_out) {
    const Document& document = context->document;
    const Value* skeleton_value = nullptr;
    if (const auto error = json::require_member(
            document,
            document.root,
            "skeleton",
            Value::Type::Object,
            "$",
            &skeleton_value)) {
        return error;
    }

    double width = 0.0;
    double height = 0.0;
    if (const auto error = read_optional_number(
            document,
            *skeleton_value,
            "width",
            "$.skeleton",
            &width)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            *skeleton_value,
            "height",
            "$.skeleton",
            &height)) {
        return error;
    }

    Value::Object output_skeleton;
    output_skeleton.emplace("name", make_string_value(context->skeleton_name));
    output_skeleton.emplace("width", make_number_value(width));
    output_skeleton.emplace("height", make_number_value(height));
    *skeleton_out = make_object_value(std::move(output_skeleton));
    return std::nullopt;
}

std::string_view trim_ascii(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::vector<AtlasImportLine> scan_atlas_import_lines(std::string_view text) {
    std::vector<AtlasImportLine> lines;

    std::size_t line_number = 1;
    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        std::size_t line_end = line_start;
        while (line_end < text.size() && text[line_end] != '\n') {
            ++line_end;
        }

        std::string_view raw_line = text.substr(line_start, line_end - line_start);
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.remove_suffix(1);
        }

        std::size_t leading = 0;
        while (leading < raw_line.size() &&
               std::isspace(static_cast<unsigned char>(raw_line[leading])) != 0) {
            ++leading;
        }

        const std::string_view trimmed =
            leading >= raw_line.size() ? std::string_view() : trim_ascii(raw_line.substr(leading));
        AtlasImportLine line;
        line.text = trimmed;
        line.blank = trimmed.empty();
        line.indented = !line.blank && leading > 0;
        line.location = {
            line_number,
            line.blank ? 1U : leading + 1U,
            line_start + (line.blank ? 0U : leading),
        };
        lines.push_back(line);

        if (line_end == text.size()) {
            break;
        }
        line_start = line_end + 1;
        ++line_number;
    }

    return lines;
}

std::optional<double> parse_double_token(std::string_view token) {
    const std::string text(token);
    std::size_t consumed = 0;
    try {
        const double value = std::stod(text, &consumed);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<bool> parse_bool_token(std::string_view token) {
    const std::string normalized = normalize_enum_name(token);
    if (normalized == "true") {
        return true;
    }
    if (normalized == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<LoadError> parse_numeric_tuple(
    const Document& atlas_document,
    const AtlasImportLine& line,
    std::string_view field_path,
    std::string_view value_text,
    std::size_t expected_count,
    std::vector<double>* values_out) {
    values_out->clear();
    values_out->reserve(expected_count);

    std::size_t start = 0;
    while (start <= value_text.size()) {
        const std::size_t comma = value_text.find(',', start);
        const std::string_view token = trim_ascii(value_text.substr(
            start,
            comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (token.empty()) {
            return validation_error(
                atlas_document,
                line.location,
                std::string(field_path),
                "expected " + std::to_string(expected_count) + " comma-separated numbers");
        }

        const std::optional<double> parsed_value = parse_double_token(token);
        if (!parsed_value.has_value()) {
            return validation_error(
                atlas_document,
                line.location,
                std::string(field_path),
                "invalid numeric value '" + std::string(token) + "'");
        }

        values_out->push_back(*parsed_value);
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }

    if (values_out->size() != expected_count) {
        return validation_error(
            atlas_document,
            line.location,
            std::string(field_path),
            "expected " + std::to_string(expected_count) + " comma-separated numbers");
    }

    return std::nullopt;
}

std::optional<LoadError> split_property_line(
    const Document& atlas_document,
    const AtlasImportLine& line,
    std::string_view entry_path,
    std::string_view* key_out,
    std::string_view* value_out) {
    const std::size_t separator = line.text.find(':');
    if (separator == std::string_view::npos) {
        return validation_error(
            atlas_document,
            line.location,
            std::string(entry_path),
            "expected a 'name: value' property");
    }

    *key_out = trim_ascii(line.text.substr(0, separator));
    *value_out = trim_ascii(line.text.substr(separator + 1));
    if (key_out->empty()) {
        return validation_error(
            atlas_document,
            line.location,
            std::string(entry_path),
            "property name must not be empty");
    }
    return std::nullopt;
}

std::string map_spine_filter_name(std::string_view value) {
    return normalize_enum_name(value) == "nearest" ? "nearest" : "linear";
}

std::optional<LoadError> parse_page_repeat_mode(
    const Document& atlas_document,
    const AtlasImportLine& line,
    std::string_view field_path,
    std::string_view value_text,
    AtlasImportPage* page_out) {
    const std::string normalized = normalize_enum_name(value_text);
    if (normalized == "none") {
        page_out->wrap_x = "clamp_to_edge";
        page_out->wrap_y = "clamp_to_edge";
        return std::nullopt;
    }
    if (normalized == "x") {
        page_out->wrap_x = "repeat";
        page_out->wrap_y = "clamp_to_edge";
        return std::nullopt;
    }
    if (normalized == "y") {
        page_out->wrap_x = "clamp_to_edge";
        page_out->wrap_y = "repeat";
        return std::nullopt;
    }
    if (normalized == "xy") {
        page_out->wrap_x = "repeat";
        page_out->wrap_y = "repeat";
        return std::nullopt;
    }

    return validation_error(
        atlas_document,
        line.location,
        std::string(field_path),
        "repeat must be one of none, x, y, or xy");
}

std::optional<LoadError> parse_page_property(
    const Document& atlas_document,
    const AtlasImportLine& line,
    std::size_t page_index,
    AtlasImportPage* page_out) {
    std::string_view key;
    std::string_view value;
    if (const auto error = split_property_line(
            atlas_document,
            line,
            "atlas.page[" + std::to_string(page_index) + "]",
            &key,
            &value)) {
        return error;
    }

    const std::string normalized_key = normalize_enum_name(key);
    const std::string field_path =
        "atlas.page[" + std::to_string(page_index) + "]." + std::string(key);

    if (normalized_key == "size") {
        std::vector<double> values;
        if (const auto error = parse_numeric_tuple(
                atlas_document,
                line,
                field_path,
                value,
                2,
                &values)) {
            return error;
        }
        page_out->width = values[0];
        page_out->height = values[1];
        return std::nullopt;
    }
    if (normalized_key == "filter") {
        const std::size_t comma = value.find(',');
        if (comma == std::string_view::npos) {
            return validation_error(
                atlas_document,
                line.location,
                field_path,
                "filter must provide min and mag values");
        }

        const std::string_view min_filter = trim_ascii(value.substr(0, comma));
        const std::string_view mag_filter = trim_ascii(value.substr(comma + 1));
        if (min_filter.empty() || mag_filter.empty()) {
            return validation_error(
                atlas_document,
                line.location,
                field_path,
                "filter must provide min and mag values");
        }

        page_out->filter_min = map_spine_filter_name(min_filter);
        page_out->filter_mag = map_spine_filter_name(mag_filter);
        return std::nullopt;
    }
    if (normalized_key == "repeat") {
        return parse_page_repeat_mode(atlas_document, line, field_path, value, page_out);
    }
    if (normalized_key == "pma") {
        const std::optional<bool> parsed = parse_bool_token(value);
        if (!parsed.has_value()) {
            return validation_error(
                atlas_document,
                line.location,
                field_path,
                "pma must be true or false");
        }

        page_out->premultiplied_alpha = *parsed;
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_region_property(
    const Document& atlas_document,
    const AtlasImportLine& line,
    std::size_t page_index,
    std::size_t region_index,
    AtlasImportRegion* region_out) {
    std::string_view key;
    std::string_view value;
    if (const auto error = split_property_line(
            atlas_document,
            line,
            "atlas.page[" + std::to_string(page_index) + "].regions[" +
                std::to_string(region_index) + "]",
            &key,
            &value)) {
        return error;
    }

    const std::string normalized_key = normalize_enum_name(key);
    const std::string field_path =
        "atlas.page[" + std::to_string(page_index) + "].regions[" +
        std::to_string(region_index) + "]." + std::string(key);

    if (normalized_key == "bounds") {
        std::vector<double> values;
        if (const auto error = parse_numeric_tuple(
                atlas_document,
                line,
                field_path,
                value,
                4,
                &values)) {
            return error;
        }
        region_out->x = values[0];
        region_out->y = values[1];
        region_out->width = values[2];
        region_out->height = values[3];
        return std::nullopt;
    }
    if (normalized_key == "origin") {
        std::vector<double> values;
        if (const auto error = parse_numeric_tuple(
                atlas_document,
                line,
                field_path,
                value,
                2,
                &values)) {
            return error;
        }
        region_out->origin_x = values[0];
        region_out->origin_y = values[1];
        return std::nullopt;
    }
    if (normalized_key == "rotate") {
        if (const std::optional<bool> parsed = parse_bool_token(value); parsed.has_value()) {
            region_out->rotate_degrees = *parsed ? 90.0 : 0.0;
            return std::nullopt;
        }

        const std::optional<double> parsed = parse_double_token(value);
        if (!parsed.has_value()) {
            return validation_error(
                atlas_document,
                line.location,
                field_path,
                "rotate must be a number or boolean");
        }

        region_out->rotate_degrees = *parsed;
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<LoadError> validate_atlas_page(
    const Document& atlas_document,
    const AtlasImportLine& page_line,
    std::size_t page_index,
    const AtlasImportPage& page) {
    if (page.image_name.empty()) {
        return validation_error(
            atlas_document,
            page_line.location,
            "atlas.page[" + std::to_string(page_index) + "].image",
            "page image name must not be empty");
    }
    if (page.width <= 0.0 || page.height <= 0.0) {
        return validation_error(
            atlas_document,
            page_line.location,
            "atlas.page[" + std::to_string(page_index) + "].size",
            "page size must be greater than zero");
    }
    if (page.regions.empty()) {
        return validation_error(
            atlas_document,
            page_line.location,
            "atlas.page[" + std::to_string(page_index) + "].regions",
            "page must contain at least one region");
    }

    for (std::size_t region_index = 0; region_index < page.regions.size(); ++region_index) {
        const AtlasImportRegion& region = page.regions[region_index];
        if (region.name.empty()) {
            return validation_error(
                atlas_document,
                page_line.location,
                "atlas.page[" + std::to_string(page_index) + "].regions[" +
                    std::to_string(region_index) + "].name",
                "region name must not be empty");
        }
        if (region.width <= 0.0 || region.height <= 0.0) {
            return validation_error(
                atlas_document,
                page_line.location,
                "atlas.page[" + std::to_string(page_index) + "].regions[" +
                    std::to_string(region_index) + "].bounds",
                "region bounds must be greater than zero");
        }
        if (!std::isfinite(region.rotate_degrees)) {
            return validation_error(
                atlas_document,
                page_line.location,
                "atlas.page[" + std::to_string(page_index) + "].regions[" +
                    std::to_string(region_index) + "].rotate",
                "rotation must be finite");
        }
    }

    return std::nullopt;
}

std::string derive_atlas_page_name(std::string_view image_name) {
    const std::filesystem::path image_path(image_name);
    const std::string stem = image_path.stem().string();
    return stem.empty() ? std::string(image_name) : stem;
}

Document build_imported_atlas_document(
    const std::filesystem::path& source_path,
    const AtlasImportPage& page) {
    Document document;
    document.source_path = source_path;

    Value::Object atlas_object;
    atlas_object.emplace("name", make_string_value(derive_atlas_page_name(page.image_name)));
    atlas_object.emplace("image", make_string_value(page.image_name));
    atlas_object.emplace("width", make_number_value(page.width));
    atlas_object.emplace("height", make_number_value(page.height));
    atlas_object.emplace("filter_min", make_string_value(page.filter_min));
    atlas_object.emplace("filter_mag", make_string_value(page.filter_mag));
    atlas_object.emplace("wrap_x", make_string_value(page.wrap_x));
    atlas_object.emplace("wrap_y", make_string_value(page.wrap_y));
    atlas_object.emplace("premultiplied_alpha", make_boolean_value(page.premultiplied_alpha));

    Value::Array regions;
    regions.reserve(page.regions.size());
    for (const AtlasImportRegion& region : page.regions) {
        Value::Object region_object;
        region_object.emplace("name", make_string_value(region.name));
        region_object.emplace("x", make_number_value(region.x));
        region_object.emplace("y", make_number_value(region.y));
        region_object.emplace("width", make_number_value(region.width));
        region_object.emplace("height", make_number_value(region.height));
        region_object.emplace("origin_x", make_number_value(region.origin_x));
        region_object.emplace("origin_y", make_number_value(region.origin_y));
        if (region.rotate_degrees != 0.0) {
            region_object.emplace("rotate", make_number_value(region.rotate_degrees));
        }
        regions.push_back(make_object_value(std::move(region_object)));
    }

    Value::Object root;
    root.emplace("marrow", make_string_value(std::string(kMarrowVersion)));
    root.emplace("version", make_number_value(kMarrowFormatVersion));
    root.emplace("atlas", make_object_value(std::move(atlas_object)));
    root.emplace("regions", make_array_value(std::move(regions)));
    document.root = make_object_value(std::move(root));
    document.source_text = json::serialize_pretty(document.root);
    return document;
}

SpineAtlasImportResult import_spine_atlas_text_impl(
    std::string_view spine_atlas_text,
    std::filesystem::path source_path) {
    SpineAtlasImportResult result;

    Document atlas_document;
    atlas_document.source_path = std::move(source_path);
    atlas_document.source_text.assign(spine_atlas_text.begin(), spine_atlas_text.end());
    atlas_document.root = make_null_value();

    const std::vector<AtlasImportLine> lines = scan_atlas_import_lines(atlas_document.source_text);
    std::size_t index = 0;
    while (index < lines.size() && lines[index].blank) {
        ++index;
    }
    if (index == lines.size()) {
        result.error = validation_error(
            atlas_document,
            SourceLocation{},
            "atlas",
            "expected at least one atlas page");
        return result;
    }

    std::vector<AtlasImportPage> pages;
    pages.reserve(2);

    while (index < lines.size()) {
        while (index < lines.size() && lines[index].blank) {
            ++index;
        }
        if (index >= lines.size()) {
            break;
        }
        if (lines[index].text.find(':') != std::string_view::npos) {
            result.error = validation_error(
                atlas_document,
                lines[index].location,
                "atlas",
                "expected a page image name");
            return result;
        }

        const AtlasImportLine page_line = lines[index];
        AtlasImportPage page;
        page.image_name = std::string(page_line.text);
        const std::size_t page_index = pages.size();
        ++index;

        AtlasImportRegion* current_region = nullptr;
        while (index < lines.size()) {
            const AtlasImportLine& line = lines[index];
            if (line.blank) {
                ++index;
                break;
            }

            if (line.text.find(':') != std::string_view::npos) {
                if (current_region == nullptr) {
                    if (const auto error = parse_page_property(
                            atlas_document,
                            line,
                            page_index,
                            &page)) {
                        result.error = *error;
                        return result;
                    }
                } else {
                    if (const auto error = parse_region_property(
                            atlas_document,
                            line,
                            page_index,
                            page.regions.size() - 1U,
                            current_region)) {
                        result.error = *error;
                        return result;
                    }
                }
                ++index;
                continue;
            }

            page.regions.push_back({});
            current_region = &page.regions.back();
            current_region->name = std::string(line.text);
            ++index;
        }

        if (const auto error = validate_atlas_page(
                atlas_document,
                page_line,
                page_index,
                page)) {
            result.error = *error;
            return result;
        }

        pages.push_back(std::move(page));
    }

    result.documents.reserve(pages.size());
    for (const AtlasImportPage& page : pages) {
        Document document = build_imported_atlas_document(atlas_document.source_path, page);
        const AtlasDataResult validation = AtlasLoader::load(document);
        if (!validation) {
            result.error = validation.error;
            result.documents.clear();
            return result;
        }
        result.documents.push_back(std::move(document));
    }

    return result;
}

SpineImportResult import_spine_json_document_impl(const Document& spine_document) {
    SpineImportResult result;
    if (const auto error = json::require_type(
            spine_document,
            spine_document.root,
            Value::Type::Object,
            "$")) {
        result.error = *error;
        return result;
    }
    if (const auto error = parse_spine_version(spine_document, spine_document.root)) {
        result.error = *error;
        return result;
    }

    ImportContext context{spine_document};
    context.skeleton_name = derive_skeleton_name(spine_document);

    Value skeleton_value;
    if (const auto error = parse_skeleton_metadata(&context, &skeleton_value)) {
        result.error = *error;
        return result;
    }

    Value bones_value;
    if (const auto error = parse_bones(&context, &bones_value)) {
        result.error = *error;
        return result;
    }

    Value slots_value;
    if (const auto error = parse_slots(&context, &slots_value)) {
        result.error = *error;
        return result;
    }

    Value skins_value;
    if (const auto error = parse_skins(&context, &skins_value)) {
        result.error = *error;
        return result;
    }

    Value ik_value;
    if (const auto error = parse_ik_constraints(context, &ik_value)) {
        result.error = *error;
        return result;
    }

    Value path_value;
    if (const auto error = parse_path_constraints(context, &path_value)) {
        result.error = *error;
        return result;
    }

    Value transform_value;
    if (const auto error = parse_transform_constraints(context, &transform_value)) {
        result.error = *error;
        return result;
    }

    Value events_value;
    if (const auto error = parse_events(context, &events_value)) {
        result.error = *error;
        return result;
    }

    Value animations_value;
    if (const auto error = parse_animations(context, &animations_value)) {
        result.error = *error;
        return result;
    }

    Document marrow_document;
    marrow_document.source_path = spine_document.source_path;
    marrow_document.source_path.replace_extension(".mskl");

    Value::Object root;
    root.emplace("marrow", make_string_value(std::string(kMarrowVersion)));
    root.emplace("version", make_number_value(kMarrowFormatVersion));
    root.emplace("skeleton", std::move(skeleton_value));
    root.emplace("bones", std::move(bones_value));
    root.emplace("slots", std::move(slots_value));
    if (!ik_value.as_array().empty()) {
        root.emplace("ik", std::move(ik_value));
    }
    if (!path_value.as_array().empty()) {
        root.emplace("path", std::move(path_value));
    }
    if (!transform_value.as_array().empty()) {
        root.emplace("transform", std::move(transform_value));
    }
    root.emplace("skins", std::move(skins_value));
    if (!events_value.as_object().empty()) {
        root.emplace("events", std::move(events_value));
    }
    root.emplace("animations", std::move(animations_value));
    marrow_document.root = make_object_value(std::move(root));

    const SkeletonDataResult validation = load_skeleton_data(marrow_document);
    if (!validation) {
        result.error = validation.error;
        return result;
    }

    result.document = std::move(marrow_document);
    return result;
}

} // namespace

SpineImportResult import_spine_json_document(const json::Document& spine_document) {
    return import_spine_json_document_impl(spine_document);
}

SpineImportResult import_spine_json_file(const std::filesystem::path& spine_json_path) {
    const auto load_result = json::load_document(spine_json_path);
    if (!load_result) {
        SpineImportResult result;
        result.error = load_result.error;
        return result;
    }

    return import_spine_json_document(*load_result.document);
}

SpineAtlasImportResult import_spine_atlas_text(
    std::string_view spine_atlas_text,
    std::filesystem::path source_path) {
    return import_spine_atlas_text_impl(spine_atlas_text, std::move(source_path));
}

SpineAtlasImportResult import_spine_atlas_file(const std::filesystem::path& spine_atlas_path) {
    std::ifstream input(spine_atlas_path, std::ios::binary);
    if (!input) {
        SpineAtlasImportResult result;
        json::LoadError error;
        error.source_path = spine_atlas_path;
        error.message =
            "Failed to open Spine atlas file '" + spine_atlas_path.string() + "'.";
        result.error = std::move(error);
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        SpineAtlasImportResult result;
        json::LoadError error;
        error.source_path = spine_atlas_path;
        error.message =
            "Failed while reading Spine atlas file '" + spine_atlas_path.string() + "'.";
        result.error = std::move(error);
        return result;
    }

    return import_spine_atlas_text(buffer.str(), spine_atlas_path);
}

} // namespace marrow::runtime
