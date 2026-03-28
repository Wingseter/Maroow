#include "marrow/editor/project.hpp"
#include "atlas_packer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>
#include <tuple>
#include <utility>

#include "marrow/allocator.hpp"

namespace marrow::editor {
namespace {

using marrow::runtime::AtlasLoader;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    return marrow::runtime::json::make_validation_error(
        document, location, std::move(json_path), std::move(message));
}

const Value* find_optional_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    return marrow::runtime::json::find_member(object, key);
}

std::optional<LoadError> read_required_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::string* value_out) {
    const Value* member = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
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
    std::string* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::String, member_path)) {
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
    if (const auto error = marrow::runtime::json::require_member(
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

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Number, member_path)) {
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

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Boolean, member_path)) {
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

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Number, member_path)) {
        return error;
    }

    const double raw_value = member->as_number();
    const double rounded_value = std::round(raw_value);
    if (std::abs(raw_value - rounded_value) > 1e-6) {
        return validation_error(
            document,
            member->location(),
            member_path,
            "integer values must be whole numbers");
    }

    *value_out = static_cast<int>(rounded_value);
    return std::nullopt;
}

std::string_view onion_skin_mode_json_key(OnionSkinMode mode);
std::optional<OnionSkinMode> onion_skin_mode_from_key(std::string_view key);

std::optional<LoadError> parse_string_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path,
    std::vector<std::string>* values_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, array_value, Value::Type::Array, json_path)) {
        return error;
    }

    std::vector<std::string> values;
    values.reserve(array_value.as_array().size());

    for (std::size_t index = 0; index < array_value.as_array().size(); ++index) {
        const Value& entry = array_value.as_array()[index];
        const std::string entry_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, entry, Value::Type::String, entry_path)) {
            return error;
        }
        if (entry.as_string().empty()) {
            return validation_error(
                document,
                entry.location(),
                entry_path,
                "path strings must not be empty");
        }

        values.push_back(entry.as_string());
    }

    *values_out = std::move(values);
    return std::nullopt;
}

std::optional<LoadError> parse_path_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path,
    std::vector<std::filesystem::path>* paths_out) {
    std::vector<std::string> values;
    if (const auto error = parse_string_array(document, array_value, json_path, &values)) {
        return error;
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(values.size());
    for (const std::string& value : values) {
        paths.emplace_back(value);
    }

    *paths_out = std::move(paths);
    return std::nullopt;
}

std::optional<LoadError> parse_runtime_assets(
    const Document& document,
    const Value& root,
    RuntimeAssetReferences* runtime_assets_out) {
    const Value* runtime_assets = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "runtime", Value::Type::Object, "$", &runtime_assets)) {
        return error;
    }

    std::string skeleton_path;
    if (const auto error = read_required_string(
            document, *runtime_assets, "skeleton", "$.runtime", &skeleton_path)) {
        return error;
    }
    if (skeleton_path.empty()) {
        return validation_error(
            document,
            runtime_assets->location(),
            "$.runtime.skeleton",
            "path must not be empty");
    }

    const Value* atlas_paths = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, *runtime_assets, "atlases", Value::Type::Array, "$.runtime", &atlas_paths)) {
        return error;
    }
    if (atlas_paths->as_array().empty()) {
        return validation_error(
            document,
            atlas_paths->location(),
            "$.runtime.atlases",
            "array must not be empty");
    }

    RuntimeAssetReferences runtime_assets_value;
    runtime_assets_value.skeleton_path = std::filesystem::path(skeleton_path);
    if (const auto error = parse_path_array(
            document, *atlas_paths, "$.runtime.atlases", &runtime_assets_value.atlas_paths)) {
        return error;
    }

    *runtime_assets_out = std::move(runtime_assets_value);
    return std::nullopt;
}

std::optional<LoadError> parse_editor_metadata(
    const Document& document,
    const Value& root,
    ProjectMetadata* metadata_out) {
    const Value* editor = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "editor", Value::Type::Object, "$", &editor)) {
        return error;
    }

    ProjectMetadata metadata;
    if (const auto error = read_required_string(
            document, *editor, "name", "$.editor", &metadata.name)) {
        return error;
    }
    if (metadata.name.empty()) {
        return validation_error(
            document,
            editor->location(),
            "$.editor.name",
            "name must not be empty");
    }

    if (const auto error = read_optional_string(
            document, *editor, "active_animation", "$.editor", &metadata.active_animation)) {
        return error;
    }
    std::string export_directory;
    if (const auto error = read_optional_string(
            document, *editor, "export_directory", "$.editor", &export_directory)) {
        return error;
    }
    if (!export_directory.empty()) {
        metadata.export_directory = std::filesystem::path(export_directory);
    }
    if (const auto error = read_optional_string(
            document, *editor, "notes", "$.editor", &metadata.notes)) {
        return error;
    }

    if (const Value* preview_skins = find_optional_member(*editor, "preview_skins")) {
        if (const auto error = parse_string_array(
                document, *preview_skins, "$.editor.preview_skins", &metadata.preview_skins)) {
            return error;
        }
    }

    if (const Value* viewport = find_optional_member(*editor, "viewport")) {
        if (const auto error = marrow::runtime::json::require_type(
                document, *viewport, Value::Type::Object, "$.editor.viewport")) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "pan_x", "$.editor.viewport", &metadata.viewport.pan_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "pan_y", "$.editor.viewport", &metadata.viewport.pan_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "zoom", "$.editor.viewport", &metadata.viewport.zoom)) {
            return error;
        }
        if (metadata.viewport.zoom <= 0.0) {
            return validation_error(
                document,
                viewport->location(),
                "$.editor.viewport.zoom",
                "zoom must be greater than zero");
        }

        if (const Value* onion_skin = find_optional_member(*viewport, "onion_skin")) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *onion_skin,
                    Value::Type::Object,
                    "$.editor.viewport.onion_skin")) {
                return error;
            }

            std::string mode_name;
            if (const auto error = read_optional_string(
                    document,
                    *onion_skin,
                    "mode",
                    "$.editor.viewport.onion_skin",
                    &mode_name)) {
                return error;
            }
            if (!mode_name.empty()) {
                const auto mode = onion_skin_mode_from_key(mode_name);
                if (!mode.has_value()) {
                    return validation_error(
                        document,
                        onion_skin->location(),
                        "$.editor.viewport.onion_skin.mode",
                        "mode must be 'frame' or 'keyframe'");
                }
                metadata.viewport.onion_skin.mode = *mode;
            }

            if (const auto error = read_optional_boolean(
                    document,
                    *onion_skin,
                    "enabled",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.enabled)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *onion_skin,
                    "anchor",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.anchor_to_zero)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "before",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.before_count)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "after",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.after_count)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "step",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.step)) {
                return error;
            }

            if (metadata.viewport.onion_skin.before_count < 0 ||
                metadata.viewport.onion_skin.before_count > 6) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.before",
                    "before must stay within [0, 6]");
            }
            if (metadata.viewport.onion_skin.after_count < 0 ||
                metadata.viewport.onion_skin.after_count > 6) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.after",
                    "after must stay within [0, 6]");
            }
            if (metadata.viewport.onion_skin.step <= 0) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.step",
                    "step must be greater than zero");
            }
        }

        if (const Value* debug_overlay = find_optional_member(*viewport, "debug_overlay")) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *debug_overlay,
                    Value::Type::Object,
                    "$.editor.viewport.debug_overlay")) {
                return error;
            }

            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "bones",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.bones)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "ik",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.ik_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "path",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.path_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "physics",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.physics_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "meshes",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.mesh_wireframes)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "bounds",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.bounding_boxes)) {
                return error;
            }
        }
    }

    *metadata_out = std::move(metadata);
    return std::nullopt;
}

std::optional<LoadError> parse_atlas_pack_sprites(
    const Document& document,
    const Value& sprites_value,
    std::string_view json_path,
    std::vector<AtlasPackSprite>* sprites_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, sprites_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (sprites_value.as_array().empty()) {
        return validation_error(
            document,
            sprites_value.location(),
            std::string(json_path),
            "atlas pack sprite arrays must not be empty");
    }

    std::vector<AtlasPackSprite> sprites;
    sprites.reserve(sprites_value.as_array().size());
    for (std::size_t index = 0; index < sprites_value.as_array().size(); ++index) {
        const Value& sprite_value = sprites_value.as_array()[index];
        const std::string sprite_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, sprite_value, Value::Type::Object, sprite_path)) {
            return error;
        }

        AtlasPackSprite sprite;
        if (const auto error = read_required_string(
                document, sprite_value, "name", sprite_path, &sprite.region_name)) {
            return error;
        }
        if (sprite.region_name.empty()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".name",
                "atlas pack sprite names must not be empty");
        }

        std::string image_path;
        if (const auto error = read_required_string(
                document, sprite_value, "image", sprite_path, &image_path)) {
            return error;
        }
        if (image_path.empty()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".image",
                "atlas pack sprite image paths must not be empty");
        }
        sprite.image_path = std::filesystem::path(image_path);

        double origin_x = 0.0;
        if (const auto error = read_optional_number(
                document, sprite_value, "origin_x", sprite_path, &origin_x)) {
            return error;
        }
        if (find_optional_member(sprite_value, "origin_x") != nullptr) {
            sprite.origin_x = origin_x;
        }

        double origin_y = 0.0;
        if (const auto error = read_optional_number(
                document, sprite_value, "origin_y", sprite_path, &origin_y)) {
            return error;
        }
        if (find_optional_member(sprite_value, "origin_y") != nullptr) {
            sprite.origin_y = origin_y;
        }

        const auto duplicate = std::find_if(
            sprites.begin(),
            sprites.end(),
            [&](const AtlasPackSprite& existing) {
                return existing.region_name == sprite.region_name;
            });
        if (duplicate != sprites.end()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".name",
                "atlas pack sprite names must be unique");
        }

        sprites.push_back(std::move(sprite));
    }

    *sprites_out = std::move(sprites);
    return std::nullopt;
}

std::optional<LoadError> parse_atlas_pack_definitions(
    const Document& document,
    const Value& root,
    std::vector<AtlasPackDefinition>* atlas_pack_definitions_out) {
    const Value* atlas_packs = find_optional_member(root, "atlas_packs");
    if (atlas_packs == nullptr) {
        atlas_pack_definitions_out->clear();
        return std::nullopt;
    }

    if (const auto error = marrow::runtime::json::require_type(
            document, *atlas_packs, Value::Type::Array, "$.atlas_packs")) {
        return error;
    }

    std::vector<AtlasPackDefinition> atlas_pack_definitions;
    atlas_pack_definitions.reserve(atlas_packs->as_array().size());
    for (std::size_t index = 0; index < atlas_packs->as_array().size(); ++index) {
        const Value& definition_value = atlas_packs->as_array()[index];
        const std::string definition_path = "$.atlas_packs[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, definition_value, Value::Type::Object, definition_path)) {
            return error;
        }

        AtlasPackDefinition definition;
        std::string atlas_path;
        if (const auto error = read_required_string(
                document, definition_value, "atlas", definition_path, &atlas_path)) {
            return error;
        }
        if (atlas_path.empty()) {
            return validation_error(
                document,
                definition_value.location(),
                definition_path + ".atlas",
                "atlas pack output paths must not be empty");
        }
        definition.atlas_path = std::filesystem::path(atlas_path);

        if (const auto error = read_optional_string(
                document,
                definition_value,
                "atlas_name",
                definition_path,
                &definition.atlas_name)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                definition_value,
                "filter_min",
                definition_path,
                &definition.filter_min)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                definition_value,
                "filter_mag",
                definition_path,
                &definition.filter_mag)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document, definition_value, "wrap_x", definition_path, &definition.wrap_x)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document, definition_value, "wrap_y", definition_path, &definition.wrap_y)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                definition_value,
                "premultiplied_alpha",
                definition_path,
                &definition.premultiplied_alpha)) {
            return error;
        }
        if (const auto error = read_optional_integer(
                document, definition_value, "padding", definition_path, &definition.padding)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document, definition_value, "trim", definition_path, &definition.trim)) {
            return error;
        }
        if (const auto error = read_optional_integer(
                document, definition_value, "bleed", definition_path, &definition.bleed)) {
            return error;
        }

        const Value* sprites_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                definition_value,
                "sprites",
                Value::Type::Array,
                definition_path,
                &sprites_value)) {
            return error;
        }
        if (const auto error = parse_atlas_pack_sprites(
                document,
                *sprites_value,
                definition_path + ".sprites",
                &definition.sprites)) {
            return error;
        }

        const auto duplicate = std::find_if(
            atlas_pack_definitions.begin(),
            atlas_pack_definitions.end(),
            [&](const AtlasPackDefinition& existing) {
                return existing.atlas_path == definition.atlas_path;
            });
        if (duplicate != atlas_pack_definitions.end()) {
            return validation_error(
                document,
                definition_value.location(),
                definition_path + ".atlas",
                "atlas pack output paths must be unique");
        }

        atlas_pack_definitions.push_back(std::move(definition));
    }

    *atlas_pack_definitions_out = std::move(atlas_pack_definitions);
    return std::nullopt;
}

std::filesystem::path absolutize_path(const std::filesystem::path& path) {
    if (path.empty()) {
        return path;
    }
    if (path.is_absolute()) {
        return path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path current_directory = std::filesystem::current_path(error);
    if (error) {
        return path.lexically_normal();
    }

    return (current_directory / path).lexically_normal();
}

std::filesystem::path make_project_relative_path(
    const std::filesystem::path& project_path,
    const std::filesystem::path& referenced_path) {
    const std::filesystem::path absolute_project = absolutize_path(project_path);
    const std::filesystem::path absolute_reference = absolutize_path(referenced_path);
    if (absolute_project.empty() || absolute_reference.empty()) {
        return referenced_path.lexically_normal();
    }

    const std::filesystem::path project_directory = absolute_project.parent_path();
    if (project_directory.empty()) {
        return referenced_path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path relative_path =
        std::filesystem::relative(absolute_reference, project_directory, error);
    if (error || relative_path.empty()) {
        return absolute_reference;
    }
    const std::string relative_text = relative_path.generic_string();
    if (relative_text == ".." || relative_text.rfind("../", 0) == 0) {
        return absolute_reference;
    }

    return relative_path.lexically_normal();
}

std::string default_project_name(
    const std::filesystem::path& project_path,
    const std::filesystem::path& skeleton_path) {
    if (!project_path.stem().empty()) {
        return project_path.stem().string();
    }
    if (!skeleton_path.stem().empty()) {
        return skeleton_path.stem().string();
    }
    return "marrow_project";
}

std::string_view transform_channel_json_key(TransformTimelineChannel channel) {
    switch (channel) {
    case TransformTimelineChannel::Rotate:
        return "rotate";
    case TransformTimelineChannel::Translate:
        return "translate";
    case TransformTimelineChannel::Scale:
        return "scale";
    case TransformTimelineChannel::Shear:
        return "shear";
    }

    return "rotate";
}

std::optional<TransformTimelineChannel> transform_channel_from_key(std::string_view key) {
    if (key == "rotate") {
        return TransformTimelineChannel::Rotate;
    }
    if (key == "translate") {
        return TransformTimelineChannel::Translate;
    }
    if (key == "scale") {
        return TransformTimelineChannel::Scale;
    }
    if (key == "shear") {
        return TransformTimelineChannel::Shear;
    }

    return std::nullopt;
}

std::string_view onion_skin_mode_json_key(OnionSkinMode mode) {
    switch (mode) {
    case OnionSkinMode::Frame:
        return "frame";
    case OnionSkinMode::Keyframe:
        return "keyframe";
    }

    return "frame";
}

std::optional<OnionSkinMode> onion_skin_mode_from_key(std::string_view key) {
    if (key == "frame") {
        return OnionSkinMode::Frame;
    }
    if (key == "keyframe") {
        return OnionSkinMode::Keyframe;
    }

    return std::nullopt;
}

std::string_view path_spacing_mode_json_key(runtime::PathConstraintSpacingMode mode) {
    switch (mode) {
    case runtime::PathConstraintSpacingMode::Length:
        return "length";
    case runtime::PathConstraintSpacingMode::Percent:
        return "percent";
    }

    return "length";
}

std::optional<runtime::PathConstraintSpacingMode> path_spacing_mode_from_key(
    std::string_view key) {
    if (key == "length") {
        return runtime::PathConstraintSpacingMode::Length;
    }
    if (key == "percent") {
        return runtime::PathConstraintSpacingMode::Percent;
    }

    return std::nullopt;
}

bool is_vector_channel(TransformTimelineChannel channel) {
    return channel != TransformTimelineChannel::Rotate;
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

Value* ensure_object_member(Value* object, std::string_view key) {
    if (object == nullptr) {
        return nullptr;
    }
    if (!object->is_object()) {
        *object = make_object_value();
    }

    auto& members = object->as_object();
    auto [iterator, inserted] =
        members.emplace(std::string(key), make_object_value());
    if (!inserted && !iterator->second.is_object()) {
        iterator->second = make_object_value();
    }

    return &iterator->second;
}

Value* ensure_array_member(Value* object, std::string_view key) {
    if (object == nullptr) {
        return nullptr;
    }
    if (!object->is_object()) {
        *object = make_object_value();
    }

    auto& members = object->as_object();
    auto [iterator, inserted] =
        members.emplace(std::string(key), make_array_value());
    if (!inserted && !iterator->second.is_array()) {
        iterator->second = make_array_value();
    }

    return &iterator->second;
}

std::optional<LoadError> parse_optional_xy_vector(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    runtime::AttachmentVertex* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Object, member_path)) {
        return error;
    }

    if (const auto error = read_optional_number(
            document, *member, "x", member_path, &value_out->x)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document, *member, "y", member_path, &value_out->y)) {
        return error;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_interpolation(
    const Document& document,
    const Value& keyframe_value,
    std::string_view keyframe_path,
    runtime::Interpolation* interpolation_out) {
    *interpolation_out = runtime::Interpolation::linear();

    const Value* curve_value = find_optional_member(keyframe_value, "curve");
    if (curve_value == nullptr) {
        return std::nullopt;
    }

    const std::string curve_path = std::string(keyframe_path) + ".curve";
    if (curve_value->is_string()) {
        const std::string& curve_name = curve_value->as_string();
        if (curve_name == "linear") {
            *interpolation_out = runtime::Interpolation::linear();
            return std::nullopt;
        }
        if (curve_name == "stepped") {
            *interpolation_out = runtime::Interpolation::stepped();
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
        if (const auto error = marrow::runtime::json::require_type(
                document,
                control_points[index],
                Value::Type::Number,
                curve_path + "[" + std::to_string(index) + "]")) {
            return error;
        }
        coordinates[index] = control_points[index].as_number();
    }

    if (coordinates[0] < 0.0 || coordinates[0] > 1.0 ||
        coordinates[2] < 0.0 || coordinates[2] > 1.0) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "bezier x control points must stay within [0, 1]");
    }

    *interpolation_out = runtime::Interpolation::cubic_bezier(
        coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
    return std::nullopt;
}

Value build_interpolation_value(const runtime::Interpolation& interpolation) {
    switch (interpolation.kind()) {
    case runtime::InterpolationKind::Linear:
        return make_string_value("linear");
    case runtime::InterpolationKind::Stepped:
        return make_string_value("stepped");
    case runtime::InterpolationKind::CubicBezier: {
        const auto& bezier = interpolation.cubic_bezier();
        Value::Array control_points;
        control_points.reserve(4);
        control_points.push_back(make_number_value(bezier.cx1));
        control_points.push_back(make_number_value(bezier.cy1));
        control_points.push_back(make_number_value(bezier.cx2));
        control_points.push_back(make_number_value(bezier.cy2));
        return make_array_value(std::move(control_points));
    }
    }

    return make_string_value("linear");
}

std::optional<LoadError> parse_transform_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    TransformTimelineChannel channel,
    std::vector<TransformKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "timeline edits must contain at least one keyframe");
    }

    std::vector<TransformKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        TransformKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        if (is_vector_channel(channel)) {
            if (const auto error = read_required_number(
                    document, keyframe_value, "x", keyframe_path, &keyframe.x)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, keyframe_value, "y", keyframe_path, &keyframe.y)) {
                return error;
            }
        } else {
            if (const auto error = read_required_number(
                    document, keyframe_value, "angle", keyframe_path, &keyframe.angle)) {
                return error;
            }
        }
        if (const auto error = parse_interpolation(
                document, keyframe_value, keyframe_path, &keyframe.interpolation)) {
            return error;
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_transform_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<TransformTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<TransformTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* bones = find_optional_member(animation_value, "bones");
        if (bones == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *bones, Value::Type::Object, animation_path + ".bones")) {
            return error;
        }

        for (const auto& [bone_name, bone_value] : bones->as_object()) {
            const std::string bone_path =
                animation_path + ".bones." + bone_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, bone_value, Value::Type::Object, bone_path)) {
                return error;
            }

            for (const auto& [channel_name, timeline_value] : bone_value.as_object()) {
                const auto channel = transform_channel_from_key(channel_name);
                if (!channel.has_value()) {
                    continue;
                }

                TransformTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.bone_name = bone_name;
                edit.channel = *channel;
                if (const auto error = parse_transform_keyframes(
                        document,
                        timeline_value,
                        bone_path + "." + channel_name,
                        *channel,
                        &edit.keyframes)) {
                    return error;
                }
                edits.push_back(std::move(edit));
            }
        }
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_deform_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<DeformKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "deform timeline edits must contain at least one keyframe");
    }

    std::vector<DeformKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        DeformKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }

        const Value* vertices_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "vertices",
                Value::Type::Array,
                keyframe_path,
                &vertices_value)) {
            return error;
        }
        if (vertices_value->as_array().empty() ||
            (vertices_value->as_array().size() % 2U) != 0U) {
            return validation_error(
                document,
                vertices_value->location(),
                keyframe_path + ".vertices",
                "deform keyframes must provide one x/y offset pair per vertex");
        }

        keyframe.vertex_offsets.reserve(vertices_value->as_array().size());
        for (std::size_t component_index = 0;
             component_index < vertices_value->as_array().size();
             ++component_index) {
            const Value& component_value = vertices_value->as_array()[component_index];
            const std::string component_path =
                keyframe_path + ".vertices[" + std::to_string(component_index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, component_value, Value::Type::Number, component_path)) {
                return error;
            }
            keyframe.vertex_offsets.push_back(component_value.as_number());
        }

        if (const auto error = parse_interpolation(
                document, keyframe_value, keyframe_path, &keyframe.interpolation)) {
            return error;
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "deform timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_deform_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<MeshDeformTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<MeshDeformTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* deform = find_optional_member(animation_value, "deform");
        if (deform == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *deform, Value::Type::Object, animation_path + ".deform")) {
            return error;
        }

        for (const auto& [slot_name, slot_value] : deform->as_object()) {
            const std::string slot_path = animation_path + ".deform." + slot_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, slot_value, Value::Type::Object, slot_path)) {
                return error;
            }

            for (const auto& [attachment_name, timeline_value] : slot_value.as_object()) {
                MeshDeformTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.slot_name = slot_name;
                edit.attachment_name = attachment_name;
                if (const auto error = parse_deform_keyframes(
                        document,
                        timeline_value,
                        slot_path + "." + attachment_name,
                        &edit.keyframes)) {
                    return error;
                }
                edits.push_back(std::move(edit));
            }
        }
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_weight_vertices(
    const Document& document,
    const Value& weights_value,
    std::string_view json_path,
    std::vector<MeshWeightVertexEdit>* vertices_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, weights_value, Value::Type::Array, json_path)) {
        return error;
    }

    if (weights_value.as_array().empty()) {
        return validation_error(
            document,
            weights_value.location(),
            std::string(json_path),
            "mesh weight edits must contain one vertex influence list per vertex");
    }

    std::vector<MeshWeightVertexEdit> vertices;
    vertices.reserve(weights_value.as_array().size());

    for (std::size_t vertex_index = 0;
         vertex_index < weights_value.as_array().size();
         ++vertex_index) {
        const Value& vertex_value = weights_value.as_array()[vertex_index];
        const std::string vertex_path =
            std::string(json_path) + "[" + std::to_string(vertex_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, vertex_value, Value::Type::Array, vertex_path)) {
            return error;
        }
        if (vertex_value.as_array().empty()) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices must preserve at least one bone influence");
        }
        if (vertex_value.as_array().size() > 4U) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices support at most 4 bone influences");
        }

        MeshWeightVertexEdit vertex;
        vertex.influences.reserve(vertex_value.as_array().size());
        std::vector<std::string> seen_bones;
        double total_weight = 0.0;

        for (std::size_t influence_index = 0;
             influence_index < vertex_value.as_array().size();
             ++influence_index) {
            const Value& influence_value = vertex_value.as_array()[influence_index];
            const std::string influence_path =
                vertex_path + "[" + std::to_string(influence_index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, influence_value, Value::Type::Object, influence_path)) {
                return error;
            }

            MeshWeightInfluenceEdit influence;
            if (const auto error = read_required_string(
                    document,
                    influence_value,
                    "bone",
                    influence_path,
                    &influence.bone_name)) {
                return error;
            }
            if (influence.bone_name.empty()) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".bone",
                    "mesh weight edit bone names must not be empty");
            }
            if (std::find(seen_bones.begin(), seen_bones.end(), influence.bone_name) !=
                seen_bones.end()) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".bone",
                    "mesh weight edit vertices must not repeat the same bone");
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
                    "mesh weight edit bone weights must be positive");
            }

            total_weight += influence.weight;
            seen_bones.push_back(influence.bone_name);
            vertex.influences.push_back(std::move(influence));
        }

        if (total_weight <= 0.0) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices must sum to a positive weight");
        }

        vertices.push_back(std::move(vertex));
    }

    *vertices_out = std::move(vertices);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_weight_attachment_edits(
    const Document& document,
    const Value& root,
    std::vector<MeshWeightAttachmentEdit>* edits_out) {
    const Value* mesh_edits = find_optional_member(root, "mesh_edits");
    if (mesh_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *mesh_edits, Value::Type::Object, "$.mesh_edits")) {
        return error;
    }

    const Value* weights_value = find_optional_member(*mesh_edits, "weights");
    if (weights_value == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *weights_value, Value::Type::Array, "$.mesh_edits.weights")) {
        return error;
    }

    std::vector<MeshWeightAttachmentEdit> edits;
    edits.reserve(weights_value->as_array().size());

    for (std::size_t edit_index = 0; edit_index < weights_value->as_array().size(); ++edit_index) {
        const Value& edit_value = weights_value->as_array()[edit_index];
        const std::string edit_path =
            "$.mesh_edits.weights[" + std::to_string(edit_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, edit_value, Value::Type::Object, edit_path)) {
            return error;
        }

        MeshWeightAttachmentEdit edit;
        if (const auto error = read_required_string(
                document, edit_value, "skin", edit_path, &edit.skin_name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, edit_value, "slot", edit_path, &edit.slot_name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, edit_value, "attachment", edit_path, &edit.attachment_name)) {
            return error;
        }
        if (edit.skin_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            return validation_error(
                document,
                edit_value.location(),
                edit_path,
                "mesh weight edits require non-empty skin, slot, and attachment names");
        }

        const Value* attachment_weights = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                edit_value,
                "weights",
                Value::Type::Array,
                edit_path,
                &attachment_weights)) {
            return error;
        }
        if (const auto error = parse_mesh_weight_vertices(
                document,
                *attachment_weights,
                edit_path + ".weights",
                &edit.vertices)) {
            return error;
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<DrawOrderKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "draw order timeline edits must contain at least one keyframe");
    }

    std::vector<DrawOrderKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        DrawOrderKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }

        const Value* slots_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "slots",
                Value::Type::Array,
                keyframe_path,
                &slots_value)) {
            return error;
        }
        if (const auto error = parse_string_array(
                document,
                *slots_value,
                keyframe_path + ".slots",
                &keyframe.slot_names)) {
            return error;
        }

        std::vector<std::string> sorted_slot_names = keyframe.slot_names;
        std::sort(sorted_slot_names.begin(), sorted_slot_names.end());
        if (std::adjacent_find(sorted_slot_names.begin(), sorted_slot_names.end()) !=
            sorted_slot_names.end()) {
            return validation_error(
                document,
                slots_value->location(),
                keyframe_path + ".slots",
                "draw order keyframes must not repeat slot names");
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "draw order timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<DrawOrderTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<DrawOrderTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path = "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* draw_order = find_optional_member(animation_value, "drawOrder");
        if (draw_order == nullptr) {
            continue;
        }

        DrawOrderTimelineEdit edit;
        edit.animation_name = animation_name;
        if (const auto error = parse_draw_order_keyframes(
                document,
                *draw_order,
                animation_path + ".drawOrder",
                &edit.keyframes)) {
            return error;
        }
        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_event_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<EventKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "event timeline edits must contain at least one keyframe");
    }

    std::vector<EventKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        EventKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, keyframe_value, "name", keyframe_path, &keyframe.event_name)) {
            return error;
        }
        if (keyframe.event_name.empty()) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".name",
                "event timeline edit names must not be empty");
        }

        int int_value = 0;
        if (const auto error = read_optional_integer(
                document, keyframe_value, "int", keyframe_path, &int_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "int") != nullptr) {
            keyframe.int_value = int_value;
        }

        double float_value = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "float", keyframe_path, &float_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "float") != nullptr) {
            keyframe.float_value = float_value;
        }

        std::string string_value;
        if (const auto error = read_optional_string(
                document, keyframe_value, "string", keyframe_path, &string_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "string") != nullptr) {
            keyframe.string_value = std::move(string_value);
        }

        std::string audio_path;
        if (const auto error = read_optional_string(
                document, keyframe_value, "audio", keyframe_path, &audio_path)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "audio") != nullptr) {
            keyframe.audio_path = std::move(audio_path);
        }

        double volume = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "volume", keyframe_path, &volume)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "volume") != nullptr) {
            keyframe.volume = volume;
        }

        double balance = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "balance", keyframe_path, &balance)) {
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
                "event timeline edit keyframe times must be non-decreasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_event_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<EventTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<EventTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path = "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* events = find_optional_member(animation_value, "events");
        if (events == nullptr) {
            continue;
        }

        EventTimelineEdit edit;
        edit.animation_name = animation_name;
        if (const auto error = parse_event_keyframes(
                document,
                *events,
                animation_path + ".events",
                &edit.keyframes)) {
            return error;
        }
        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_ik_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<IkConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* ik = find_optional_member(*constraint_edits, "ik");
    if (ik == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *ik, Value::Type::Array, "$.constraint_edits.ik")) {
        return error;
    }

    std::vector<IkConstraintEdit> edits;
    edits.reserve(ik->as_array().size());
    for (std::size_t index = 0; index < ik->as_array().size(); ++index) {
        const Value& constraint_value = ik->as_array()[index];
        const std::string path = "$.constraint_edits.ik[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, path)) {
            return error;
        }

        IkConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const IkConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint edit names must be unique");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, path, &bones)) {
            return error;
        }
        if (const auto error = parse_string_array(document, *bones, path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty() || edit.bone_names.size() > 2U) {
            return validation_error(
                document,
                bones->location(),
                path + ".bones",
                "ik constraint edits must target one or two bones");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) !=
                sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    path + ".bones",
                    "ik constraint edit bones must be unique");
            }
        }
        if (const auto error = read_required_string(
                document, constraint_value, "target", path, &edit.target_bone_name)) {
            return error;
        }
        if (edit.target_bone_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".target",
                "ik constraint edits require a target bone");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "mix", path, &edit.mix)) {
            return error;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".mix",
                "ik constraint edit mix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "softness", path, &edit.softness)) {
            return error;
        }
        const Value* bend_positive = find_optional_member(constraint_value, "bendPositive");
        if (bend_positive != nullptr) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *bend_positive,
                    Value::Type::Boolean,
                    path + ".bendPositive")) {
                return error;
            }
            edit.bend_positive = bend_positive->as_boolean();
        }
        if (const auto error = read_optional_boolean(
                document, constraint_value, "compress", path, &edit.compress)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document, constraint_value, "stretch", path, &edit.stretch)) {
            return error;
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_path_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<PathConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* path = find_optional_member(*constraint_edits, "path");
    if (path == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *path, Value::Type::Array, "$.constraint_edits.path")) {
        return error;
    }

    std::vector<PathConstraintEdit> edits;
    edits.reserve(path->as_array().size());
    for (std::size_t index = 0; index < path->as_array().size(); ++index) {
        const Value& constraint_value = path->as_array()[index];
        const std::string json_path = "$.constraint_edits.path[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        PathConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "path constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const PathConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "path constraint edit names must be unique");
        }
        if (const auto error = read_required_string(
                document, constraint_value, "slot", json_path, &edit.slot_name)) {
            return error;
        }
        if (edit.slot_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".slot",
                "path constraint edits require a slot");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "path constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "path constraint edit bones must be unique");
            }
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "position", json_path, &edit.position)) {
            return error;
        }
        if (edit.position < 0.0 || edit.position > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".position",
                "path constraint edit position must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "spacing", json_path, &edit.spacing)) {
            return error;
        }
        if (edit.spacing < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".spacing",
                "path constraint edit spacing must be non-negative");
        }

        std::string spacing_mode_name;
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "spacingMode",
                json_path,
                &spacing_mode_name)) {
            return error;
        }
        if (!spacing_mode_name.empty()) {
            const auto spacing_mode = path_spacing_mode_from_key(spacing_mode_name);
            if (!spacing_mode.has_value()) {
                return validation_error(
                    document,
                    constraint_value.location(),
                    json_path + ".spacingMode",
                    "path constraint edit spacingMode must be 'length' or 'percent'");
            }
            edit.spacing_mode = *spacing_mode;
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "rotateMix", json_path, &edit.rotate_mix)) {
            return error;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotateMix",
                "path constraint edit rotateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "translateMix", json_path, &edit.translate_mix)) {
            return error;
        }
        if (edit.translate_mix < 0.0 || edit.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".translateMix",
                "path constraint edit translateMix must stay within [0, 1]");
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_transform_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<TransformConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* transform = find_optional_member(*constraint_edits, "transform");
    if (transform == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *transform, Value::Type::Array, "$.constraint_edits.transform")) {
        return error;
    }

    std::vector<TransformConstraintEdit> edits;
    edits.reserve(transform->as_array().size());
    for (std::size_t index = 0; index < transform->as_array().size(); ++index) {
        const Value& constraint_value = transform->as_array()[index];
        const std::string json_path =
            "$.constraint_edits.transform[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        TransformConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "transform constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const TransformConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "transform constraint edit names must be unique");
        }
        if (const auto error = read_required_string(
                document, constraint_value, "source", json_path, &edit.source_bone_name)) {
            return error;
        }
        if (edit.source_bone_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".source",
                "transform constraint edits require a source bone");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "transform constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "transform constraint edit bones must be unique");
            }
        }

        if (std::find(edit.bone_names.begin(), edit.bone_names.end(), edit.source_bone_name) !=
            edit.bone_names.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".bones",
                "transform constraint source bone must not also be a target");
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "rotateMix", json_path, &edit.rotate_mix)) {
            return error;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotateMix",
                "transform constraint edit rotateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "translateMix", json_path, &edit.translate_mix)) {
            return error;
        }
        if (edit.translate_mix < 0.0 || edit.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".translateMix",
                "transform constraint edit translateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "scaleMix", json_path, &edit.scale_mix)) {
            return error;
        }
        if (edit.scale_mix < 0.0 || edit.scale_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".scaleMix",
                "transform constraint edit scaleMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "shearMix", json_path, &edit.shear_mix)) {
            return error;
        }
        if (edit.shear_mix < 0.0 || edit.shear_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".shearMix",
                "transform constraint edit shearMix must stay within [0, 1]");
        }

        const Value* offset = find_optional_member(constraint_value, "offset");
        if (offset != nullptr) {
            if (const auto error = marrow::runtime::json::require_type(
                    document, *offset, Value::Type::Object, json_path + ".offset")) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "rotation", json_path + ".offset", &edit.offsets.rotation)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "x", json_path + ".offset", &edit.offsets.x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "y", json_path + ".offset", &edit.offsets.y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "scaleX", json_path + ".offset", &edit.offsets.scale_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "scaleY", json_path + ".offset", &edit.offsets.scale_y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "shearX", json_path + ".offset", &edit.offsets.shear_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "shearY", json_path + ".offset", &edit.offsets.shear_y)) {
                return error;
            }
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_physics_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<PhysicsConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* physics = find_optional_member(*constraint_edits, "physics");
    if (physics == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *physics, Value::Type::Array, "$.constraint_edits.physics")) {
        return error;
    }

    std::vector<PhysicsConstraintEdit> edits;
    edits.reserve(physics->as_array().size());
    for (std::size_t index = 0; index < physics->as_array().size(); ++index) {
        const Value& constraint_value = physics->as_array()[index];
        const std::string json_path =
            "$.constraint_edits.physics[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        PhysicsConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "physics constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const PhysicsConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "physics constraint edit names must be unique");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "physics constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "physics constraint edit bones must be unique");
            }
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "step", json_path, &edit.step)) {
            return error;
        }
        if (edit.step <= 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".step",
                "physics constraint edit step must be greater than zero");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "x", json_path, &edit.x)) {
            return error;
        }
        if (edit.x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".x",
                "physics constraint edit x must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "y", json_path, &edit.y)) {
            return error;
        }
        if (edit.y < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".y",
                "physics constraint edit y must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "rotate", json_path, &edit.rotate)) {
            return error;
        }
        if (edit.rotate < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotate",
                "physics constraint edit rotate must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "scaleX", json_path, &edit.scale_x)) {
            return error;
        }
        if (edit.scale_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".scaleX",
                "physics constraint edit scaleX must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "shearX", json_path, &edit.shear_x)) {
            return error;
        }
        if (edit.shear_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".shearX",
                "physics constraint edit shearX must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "limit", json_path, &edit.limit)) {
            return error;
        }
        if (edit.limit < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".limit",
                "physics constraint edit limit must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "inertia", json_path, &edit.inertia)) {
            return error;
        }
        if (edit.inertia < 0.0 || edit.inertia > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".inertia",
                "physics constraint edit inertia must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "damping", json_path, &edit.damping)) {
            return error;
        }
        if (edit.damping < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".damping",
                "physics constraint edit damping must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "strength", json_path, &edit.strength)) {
            return error;
        }
        if (edit.strength < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".strength",
                "physics constraint edit strength must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "massInverse", json_path, &edit.mass_inverse)) {
            return error;
        }
        if (edit.mass_inverse < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".massInverse",
                "physics constraint edit massInverse must be non-negative");
        }
        if (const auto error = parse_optional_xy_vector(
                document, constraint_value, "gravity", json_path, &edit.gravity)) {
            return error;
        }
        if (const auto error = parse_optional_xy_vector(
                document, constraint_value, "wind", json_path, &edit.wind)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "mix", json_path, &edit.mix)) {
            return error;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".mix",
                "physics constraint edit mix must stay within [0, 1]");
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

Value build_transform_keyframes_value(
    const TransformTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        if (is_vector_channel(edit.channel)) {
            keyframe_object.emplace("x", make_number_value(keyframe.x));
            keyframe_object.emplace("y", make_number_value(keyframe.y));
        } else {
            keyframe_object.emplace("angle", make_number_value(keyframe.angle));
        }
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_deform_keyframes_value(const MeshDeformTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const DeformKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));

        Value::Array vertex_offsets;
        vertex_offsets.reserve(keyframe.vertex_offsets.size());
        for (const double value : keyframe.vertex_offsets) {
            vertex_offsets.push_back(make_number_value(value));
        }
        keyframe_object.emplace("vertices", make_array_value(std::move(vertex_offsets)));
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_mesh_weight_vertices_value(const MeshWeightAttachmentEdit& edit) {
    Value::Array vertices;
    vertices.reserve(edit.vertices.size());
    for (const MeshWeightVertexEdit& vertex : edit.vertices) {
        Value::Array influences;
        influences.reserve(vertex.influences.size());
        for (const MeshWeightInfluenceEdit& influence : vertex.influences) {
            Value::Object influence_object;
            influence_object.emplace("bone", make_string_value(influence.bone_name));
            influence_object.emplace("x", make_number_value(influence.x));
            influence_object.emplace("y", make_number_value(influence.y));
            influence_object.emplace("weight", make_number_value(influence.weight));
            influences.push_back(make_object_value(std::move(influence_object)));
        }
        vertices.push_back(make_array_value(std::move(influences)));
    }

    return make_array_value(std::move(vertices));
}

Value build_draw_order_keyframes_value(const DrawOrderTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const DrawOrderKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));

        Value::Array slots;
        slots.reserve(keyframe.slot_names.size());
        for (const std::string& slot_name : keyframe.slot_names) {
            slots.push_back(make_string_value(slot_name));
        }
        keyframe_object.emplace("slots", make_array_value(std::move(slots)));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_event_keyframes_value(const EventTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const EventKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        keyframe_object.emplace("name", make_string_value(keyframe.event_name));
        if (keyframe.int_value.has_value()) {
            keyframe_object.emplace("int", make_number_value(*keyframe.int_value));
        }
        if (keyframe.float_value.has_value()) {
            keyframe_object.emplace("float", make_number_value(*keyframe.float_value));
        }
        if (keyframe.string_value.has_value()) {
            keyframe_object.emplace("string", make_string_value(*keyframe.string_value));
        }
        if (keyframe.audio_path.has_value()) {
            keyframe_object.emplace("audio", make_string_value(*keyframe.audio_path));
        }
        if (keyframe.volume.has_value()) {
            keyframe_object.emplace("volume", make_number_value(*keyframe.volume));
        }
        if (keyframe.balance.has_value()) {
            keyframe_object.emplace("balance", make_number_value(*keyframe.balance));
        }
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_ik_constraint_edits_value(const std::vector<IkConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const IkConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("target", make_string_value(edit.target_bone_name));
        constraint_object.emplace("mix", make_number_value(edit.mix));
        constraint_object.emplace("bendPositive", make_boolean_value(edit.bend_positive));
        constraint_object.emplace("softness", make_number_value(edit.softness));
        constraint_object.emplace("compress", make_boolean_value(edit.compress));
        constraint_object.emplace("stretch", make_boolean_value(edit.stretch));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_path_constraint_edits_value(const std::vector<PathConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const PathConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        constraint_object.emplace("slot", make_string_value(edit.slot_name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("position", make_number_value(edit.position));
        constraint_object.emplace("spacing", make_number_value(edit.spacing));
        constraint_object.emplace(
            "spacingMode",
            make_string_value(std::string(path_spacing_mode_json_key(edit.spacing_mode))));
        constraint_object.emplace("rotateMix", make_number_value(edit.rotate_mix));
        constraint_object.emplace("translateMix", make_number_value(edit.translate_mix));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_transform_constraint_edits_value(const std::vector<TransformConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const TransformConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        constraint_object.emplace("source", make_string_value(edit.source_bone_name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("rotateMix", make_number_value(edit.rotate_mix));
        constraint_object.emplace("translateMix", make_number_value(edit.translate_mix));
        constraint_object.emplace("scaleMix", make_number_value(edit.scale_mix));
        constraint_object.emplace("shearMix", make_number_value(edit.shear_mix));
        Value::Object offset_object;
        offset_object.emplace("rotation", make_number_value(edit.offsets.rotation));
        offset_object.emplace("x", make_number_value(edit.offsets.x));
        offset_object.emplace("y", make_number_value(edit.offsets.y));
        offset_object.emplace("scaleX", make_number_value(edit.offsets.scale_x));
        offset_object.emplace("scaleY", make_number_value(edit.offsets.scale_y));
        offset_object.emplace("shearX", make_number_value(edit.offsets.shear_x));
        offset_object.emplace("shearY", make_number_value(edit.offsets.shear_y));
        constraint_object.emplace("offset", make_object_value(std::move(offset_object)));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_physics_constraint_edits_value(const std::vector<PhysicsConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const PhysicsConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("step", make_number_value(edit.step));
        constraint_object.emplace("x", make_number_value(edit.x));
        constraint_object.emplace("y", make_number_value(edit.y));
        constraint_object.emplace("rotate", make_number_value(edit.rotate));
        constraint_object.emplace("scaleX", make_number_value(edit.scale_x));
        constraint_object.emplace("shearX", make_number_value(edit.shear_x));
        constraint_object.emplace("limit", make_number_value(edit.limit));
        constraint_object.emplace("inertia", make_number_value(edit.inertia));
        constraint_object.emplace("damping", make_number_value(edit.damping));
        constraint_object.emplace("strength", make_number_value(edit.strength));
        constraint_object.emplace("massInverse", make_number_value(edit.mass_inverse));
        Value::Object gravity_object;
        gravity_object.emplace("x", make_number_value(edit.gravity.x));
        gravity_object.emplace("y", make_number_value(edit.gravity.y));
        constraint_object.emplace("gravity", make_object_value(std::move(gravity_object)));
        Value::Object wind_object;
        wind_object.emplace("x", make_number_value(edit.wind.x));
        wind_object.emplace("y", make_number_value(edit.wind.y));
        constraint_object.emplace("wind", make_object_value(std::move(wind_object)));
        constraint_object.emplace("mix", make_number_value(edit.mix));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_timeline_edits_value(
    const std::vector<TransformTimelineEdit>& transform_edits,
    const std::vector<MeshDeformTimelineEdit>& mesh_deform_edits,
    const std::vector<DrawOrderTimelineEdit>& draw_order_edits,
    const std::vector<EventTimelineEdit>& event_edits) {
    Value::Object animations_object;
    for (const TransformTimelineEdit& edit : transform_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        Value* bones_value = ensure_object_member(&animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()[std::string(transform_channel_json_key(edit.channel))] =
                build_transform_keyframes_value(edit);
        }
    }

    for (const MeshDeformTimelineEdit& edit : mesh_deform_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        Value* deform_value = ensure_object_member(&animation_value, "deform");
        Value* slot_value = ensure_object_member(deform_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()[edit.attachment_name] =
                build_deform_keyframes_value(edit);
        }
    }

    for (const DrawOrderTimelineEdit& edit : draw_order_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        animation_value.as_object()["drawOrder"] = build_draw_order_keyframes_value(edit);
    }

    for (const EventTimelineEdit& edit : event_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        animation_value.as_object()["events"] = build_event_keyframes_value(edit);
    }

    Value::Object timeline_edits;
    timeline_edits.emplace("animations", make_object_value(std::move(animations_object)));
    return make_object_value(std::move(timeline_edits));
}

Value build_constraint_edits_value(
    const std::vector<IkConstraintEdit>& ik_edits,
    const std::vector<PathConstraintEdit>& path_edits,
    const std::vector<TransformConstraintEdit>& transform_edits,
    const std::vector<PhysicsConstraintEdit>& physics_edits) {
    Value::Object constraint_edits;
    if (!ik_edits.empty()) {
        constraint_edits.emplace("ik", build_ik_constraint_edits_value(ik_edits));
    }
    if (!path_edits.empty()) {
        constraint_edits.emplace("path", build_path_constraint_edits_value(path_edits));
    }
    if (!transform_edits.empty()) {
        constraint_edits.emplace(
            "transform",
            build_transform_constraint_edits_value(transform_edits));
    }
    if (!physics_edits.empty()) {
        constraint_edits.emplace("physics", build_physics_constraint_edits_value(physics_edits));
    }
    return make_object_value(std::move(constraint_edits));
}

Value build_atlas_pack_definitions_value(
    const std::vector<AtlasPackDefinition>& atlas_pack_definitions) {
    Value::Array definitions;
    definitions.reserve(atlas_pack_definitions.size());
    for (const AtlasPackDefinition& definition : atlas_pack_definitions) {
        Value::Object definition_object;
        definition_object.emplace(
            "atlas",
            make_string_value(definition.atlas_path.generic_string()));
        if (!definition.atlas_name.empty()) {
            definition_object.emplace("atlas_name", make_string_value(definition.atlas_name));
        }
        definition_object.emplace("filter_min", make_string_value(definition.filter_min));
        definition_object.emplace("filter_mag", make_string_value(definition.filter_mag));
        definition_object.emplace("wrap_x", make_string_value(definition.wrap_x));
        definition_object.emplace("wrap_y", make_string_value(definition.wrap_y));
        definition_object.emplace(
            "premultiplied_alpha",
            make_boolean_value(definition.premultiplied_alpha));
        definition_object.emplace("padding", make_number_value(definition.padding));
        definition_object.emplace("trim", make_boolean_value(definition.trim));
        definition_object.emplace("bleed", make_number_value(definition.bleed));

        Value::Array sprites;
        sprites.reserve(definition.sprites.size());
        for (const AtlasPackSprite& sprite : definition.sprites) {
            Value::Object sprite_object;
            sprite_object.emplace("name", make_string_value(sprite.region_name));
            sprite_object.emplace("image", make_string_value(sprite.image_path.generic_string()));
            if (sprite.origin_x.has_value()) {
                sprite_object.emplace("origin_x", make_number_value(*sprite.origin_x));
            }
            if (sprite.origin_y.has_value()) {
                sprite_object.emplace("origin_y", make_number_value(*sprite.origin_y));
            }
            sprites.push_back(make_object_value(std::move(sprite_object)));
        }
        definition_object.emplace("sprites", make_array_value(std::move(sprites)));
        definitions.push_back(make_object_value(std::move(definition_object)));
    }

    return make_array_value(std::move(definitions));
}

Value build_mesh_weight_edits_value(
    const std::vector<MeshWeightAttachmentEdit>& mesh_weight_attachment_edits) {
    Value::Array weights;
    weights.reserve(mesh_weight_attachment_edits.size());
    for (const MeshWeightAttachmentEdit& edit : mesh_weight_attachment_edits) {
        Value::Object edit_object;
        edit_object.emplace("skin", make_string_value(edit.skin_name));
        edit_object.emplace("slot", make_string_value(edit.slot_name));
        edit_object.emplace("attachment", make_string_value(edit.attachment_name));
        edit_object.emplace("weights", build_mesh_weight_vertices_value(edit));
        weights.push_back(make_object_value(std::move(edit_object)));
    }

    Value::Object mesh_edits;
    mesh_edits.emplace("weights", make_array_value(std::move(weights)));
    return make_object_value(std::move(mesh_edits));
}

Value build_project_value(const ProjectData& project) {
    Value::Object root;
    root.emplace("marrow", make_string_value(project.marrow_version));

    Value::Object runtime_object;
    runtime_object.emplace(
        "skeleton",
        make_string_value(project.runtime_assets.skeleton_path.generic_string()));
    Value::Array atlas_paths;
    atlas_paths.reserve(project.runtime_assets.atlas_paths.size());
    for (const auto& atlas_path : project.runtime_assets.atlas_paths) {
        atlas_paths.push_back(make_string_value(atlas_path.generic_string()));
    }
    runtime_object.emplace("atlases", make_array_value(std::move(atlas_paths)));
    root.emplace("runtime", make_object_value(std::move(runtime_object)));

    Value::Object editor_object;
    editor_object.emplace("name", make_string_value(project.editor_metadata.name));
    editor_object.emplace(
        "active_animation",
        make_string_value(project.editor_metadata.active_animation));
    Value::Array preview_skins;
    preview_skins.reserve(project.editor_metadata.preview_skins.size());
    for (const std::string& preview_skin : project.editor_metadata.preview_skins) {
        preview_skins.push_back(make_string_value(preview_skin));
    }
    editor_object.emplace("preview_skins", make_array_value(std::move(preview_skins)));
    editor_object.emplace(
        "export_directory",
        make_string_value(project.editor_metadata.export_directory.generic_string()));
    editor_object.emplace("notes", make_string_value(project.editor_metadata.notes));
    Value::Object viewport_object;
    viewport_object.emplace("pan_x", make_number_value(project.editor_metadata.viewport.pan_x));
    viewport_object.emplace("pan_y", make_number_value(project.editor_metadata.viewport.pan_y));
    viewport_object.emplace("zoom", make_number_value(project.editor_metadata.viewport.zoom));
    Value::Object onion_skin_object;
    onion_skin_object.emplace(
        "enabled",
        make_boolean_value(project.editor_metadata.viewport.onion_skin.enabled));
    onion_skin_object.emplace(
        "mode",
        make_string_value(
            std::string(onion_skin_mode_json_key(project.editor_metadata.viewport.onion_skin.mode))));
    onion_skin_object.emplace(
        "anchor",
        make_boolean_value(project.editor_metadata.viewport.onion_skin.anchor_to_zero));
    onion_skin_object.emplace(
        "before",
        make_number_value(project.editor_metadata.viewport.onion_skin.before_count));
    onion_skin_object.emplace(
        "after",
        make_number_value(project.editor_metadata.viewport.onion_skin.after_count));
    onion_skin_object.emplace(
        "step",
        make_number_value(project.editor_metadata.viewport.onion_skin.step));
    viewport_object.emplace("onion_skin", make_object_value(std::move(onion_skin_object)));
    Value::Object debug_overlay_object;
    debug_overlay_object.emplace(
        "bones",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.bones));
    debug_overlay_object.emplace(
        "ik",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.ik_constraints));
    debug_overlay_object.emplace(
        "path",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.path_constraints));
    debug_overlay_object.emplace(
        "physics",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.physics_constraints));
    debug_overlay_object.emplace(
        "meshes",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.mesh_wireframes));
    debug_overlay_object.emplace(
        "bounds",
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.bounding_boxes));
    viewport_object.emplace(
        "debug_overlay",
        make_object_value(std::move(debug_overlay_object)));
    editor_object.emplace("viewport", make_object_value(std::move(viewport_object)));
    root.emplace("editor", make_object_value(std::move(editor_object)));

    if (!project.transform_timeline_edits.empty() ||
        !project.mesh_deform_timeline_edits.empty() ||
        !project.draw_order_timeline_edits.empty() ||
        !project.event_timeline_edits.empty()) {
        root.emplace(
            "timeline_edits",
            build_timeline_edits_value(
                project.transform_timeline_edits,
                project.mesh_deform_timeline_edits,
                project.draw_order_timeline_edits,
                project.event_timeline_edits));
    }
    if (!project.mesh_weight_attachment_edits.empty()) {
        root.emplace(
            "mesh_edits",
            build_mesh_weight_edits_value(project.mesh_weight_attachment_edits));
    }
    if (!project.ik_constraint_edits.empty() ||
        !project.path_constraint_edits.empty() ||
        !project.transform_constraint_edits.empty() ||
        !project.physics_constraint_edits.empty()) {
        root.emplace(
            "constraint_edits",
            build_constraint_edits_value(
                project.ik_constraint_edits,
                project.path_constraint_edits,
                project.transform_constraint_edits,
                project.physics_constraint_edits));
    }
    if (!project.atlas_pack_definitions.empty()) {
        root.emplace(
            "atlas_packs",
            build_atlas_pack_definitions_value(project.atlas_pack_definitions));
    }

    return make_object_value(std::move(root));
}

void merge_named_object_array_member(
    Value* root,
    std::string_view key,
    Value edits_value) {
    if (root == nullptr || !edits_value.is_array() || edits_value.as_array().empty()) {
        return;
    }
    if (!root->is_object()) {
        *root = make_object_value();
    }

    Value* existing_member = marrow::runtime::json::find_member(*root, key);
    if (existing_member == nullptr) {
        root->as_object().emplace(std::string(key), std::move(edits_value));
        return;
    }
    if (!existing_member->is_array()) {
        *existing_member = std::move(edits_value);
        return;
    }

    for (Value& edit_value : edits_value.as_array()) {
        if (!edit_value.is_object()) {
            existing_member->as_array().push_back(std::move(edit_value));
            continue;
        }

        const Value* edit_name = find_optional_member(edit_value, "name");
        if (edit_name == nullptr || !edit_name->is_string()) {
            existing_member->as_array().push_back(std::move(edit_value));
            continue;
        }

        const auto existing = std::find_if(
            existing_member->as_array().begin(),
            existing_member->as_array().end(),
            [&](const Value& existing_value) {
                const Value* existing_name = find_optional_member(existing_value, "name");
                return existing_name != nullptr &&
                    existing_name->is_string() &&
                    existing_name->as_string() == edit_name->as_string();
            });
        if (existing != existing_member->as_array().end()) {
            *existing = std::move(edit_value);
        } else {
            existing_member->as_array().push_back(std::move(edit_value));
        }
    }
}

Value* find_skin_attachment_value(
    Value* root,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    if (root == nullptr || !root->is_object()) {
        return nullptr;
    }

    Value* skins_value = marrow::runtime::json::find_member(*root, "skins");
    if (skins_value == nullptr || !skins_value->is_object()) {
        return nullptr;
    }

    Value* skin_value = marrow::runtime::json::find_member(*skins_value, skin_name);
    if (skin_value == nullptr || !skin_value->is_object()) {
        return nullptr;
    }

    Value* attachments_root = marrow::runtime::json::find_member(*skin_value, "attachments");
    Value* slot_source =
        attachments_root != nullptr && attachments_root->is_object() ? attachments_root : skin_value;
    if (slot_source == nullptr || !slot_source->is_object()) {
        return nullptr;
    }

    Value* slot_value = marrow::runtime::json::find_member(*slot_source, slot_name);
    if (slot_value == nullptr || !slot_value->is_object()) {
        return nullptr;
    }

    if (attachments_root != nullptr && attachments_root->is_object()) {
        Value* nested_attachment = marrow::runtime::json::find_member(*slot_value, attachment_name);
        if (nested_attachment != nullptr && nested_attachment->is_object()) {
            return nested_attachment;
        }

        for (auto& [candidate_name, candidate_value] : slot_value->as_object()) {
            if (!candidate_value.is_object()) {
                continue;
            }

            const Value* authored_name = find_optional_member(candidate_value, "attachment");
            if (candidate_name == attachment_name ||
                (authored_name != nullptr &&
                 authored_name->is_string() &&
                 authored_name->as_string() == attachment_name)) {
                return &candidate_value;
            }
        }
        return nullptr;
    }

    const Value* authored_name = find_optional_member(*slot_value, "attachment");
    if (authored_name != nullptr &&
        authored_name->is_string() &&
        authored_name->as_string() == attachment_name) {
        return slot_value;
    }

    return attachment_name == slot_name ? slot_value : nullptr;
}

Document build_runtime_document(
    const ProjectData& project,
    const Document& base_skeleton_document) {
    Document document = base_skeleton_document;
    if (!document.root.is_object()) {
        return document;
    }

    for (const MeshWeightAttachmentEdit& edit : project.mesh_weight_attachment_edits) {
        Value* attachment_value = find_skin_attachment_value(
            &document.root,
            edit.skin_name,
            edit.slot_name,
            edit.attachment_name);
        if (attachment_value == nullptr || !attachment_value->is_object()) {
            continue;
        }

        attachment_value->as_object()["weights"] = build_mesh_weight_vertices_value(edit);
    }

    Value* animations = marrow::runtime::json::find_member(document.root, "animations");
    if (animations == nullptr) {
        document.root.as_object().emplace("animations", make_object_value());
        animations = marrow::runtime::json::find_member(document.root, "animations");
    }
    if (animations == nullptr) {
        return document;
    }
    if (!animations->is_object()) {
        *animations = make_object_value();
    }

    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* bones_value = ensure_object_member(animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()[std::string(transform_channel_json_key(edit.channel))] =
                build_transform_keyframes_value(edit);
        }
    }

    for (const MeshDeformTimelineEdit& edit : project.mesh_deform_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* deform_value = ensure_object_member(animation_value, "deform");
        Value* slot_value = ensure_object_member(deform_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()[edit.attachment_name] =
                build_deform_keyframes_value(edit);
        }
    }

    for (const DrawOrderTimelineEdit& edit : project.draw_order_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        if (animation_value != nullptr) {
            animation_value->as_object()["drawOrder"] = build_draw_order_keyframes_value(edit);
        }
    }

    for (const EventTimelineEdit& edit : project.event_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        if (animation_value != nullptr) {
            animation_value->as_object()["events"] = build_event_keyframes_value(edit);
        }
    }

    merge_named_object_array_member(
        &document.root,
        "ik",
        build_ik_constraint_edits_value(project.ik_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "path",
        build_path_constraint_edits_value(project.path_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "transform",
        build_transform_constraint_edits_value(project.transform_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "physics",
        build_physics_constraint_edits_value(project.physics_constraint_edits));

    return document;
}

std::string serialize_project_snapshot(const ProjectData& project) {
    return marrow::runtime::json::serialize_pretty(build_project_value(project));
}

bool validate_project_for_save(const ProjectData& project, ProjectSaveError* error_out) {
    if (project.marrow_version.empty()) {
        error_out->message = "project version must not be empty";
        return false;
    }
    if (project.runtime_assets.skeleton_path.empty()) {
        error_out->message = "runtime skeleton path must not be empty";
        return false;
    }
    if (project.runtime_assets.atlas_paths.empty()) {
        error_out->message = "at least one atlas path is required";
        return false;
    }
    if (project.editor_metadata.name.empty()) {
        error_out->message = "editor metadata name must not be empty";
        return false;
    }
    if (project.editor_metadata.viewport.zoom <= 0.0) {
        error_out->message = "editor viewport zoom must be greater than zero";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.before_count < 0 ||
        project.editor_metadata.viewport.onion_skin.before_count > 6) {
        error_out->message = "editor onion-skin before count must stay within [0, 6]";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.after_count < 0 ||
        project.editor_metadata.viewport.onion_skin.after_count > 6) {
        error_out->message = "editor onion-skin after count must stay within [0, 6]";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.step <= 0) {
        error_out->message = "editor onion-skin step must be greater than zero";
        return false;
    }

    for (const auto& atlas_path : project.runtime_assets.atlas_paths) {
        if (atlas_path.empty()) {
            error_out->message = "atlas paths must not be empty";
            return false;
        }
    }
    for (const std::string& preview_skin : project.editor_metadata.preview_skins) {
        if (preview_skin.empty()) {
            error_out->message = "preview skin names must not be empty";
            return false;
        }
    }

    std::vector<std::tuple<std::string, std::string, TransformTimelineChannel>> seen_tracks;
    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        if (edit.animation_name.empty() || edit.bone_name.empty()) {
            error_out->message = "timeline edits require animation and bone names";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "timeline edits must contain at least one keyframe";
            return false;
        }
        const auto duplicate = std::find(
            seen_tracks.begin(),
            seen_tracks.end(),
            std::make_tuple(edit.animation_name, edit.bone_name, edit.channel));
        if (duplicate != seen_tracks.end()) {
            error_out->message = "duplicate transform timeline edits are not allowed";
            return false;
        }
        seen_tracks.push_back(
            std::make_tuple(edit.animation_name, edit.bone_name, edit.channel));

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message = "timeline edit keyframe times must be strictly increasing";
                return false;
            }
            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::tuple<std::string, std::string, std::string>> seen_deform_tracks;
    for (const MeshDeformTimelineEdit& edit : project.mesh_deform_timeline_edits) {
        if (edit.animation_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            error_out->message =
                "mesh deform timeline edits require animation, slot, and attachment names";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "mesh deform timeline edits must contain at least one keyframe";
            return false;
        }
        const auto duplicate = std::find(
            seen_deform_tracks.begin(),
            seen_deform_tracks.end(),
            std::make_tuple(edit.animation_name, edit.slot_name, edit.attachment_name));
        if (duplicate != seen_deform_tracks.end()) {
            error_out->message = "duplicate mesh deform timeline edits are not allowed";
            return false;
        }
        seen_deform_tracks.push_back(
            std::make_tuple(edit.animation_name, edit.slot_name, edit.attachment_name));

        double previous_time = 0.0;
        bool has_previous_time = false;
        std::size_t expected_vertex_components = 0;
        for (const DeformKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.vertex_offsets.empty() ||
                (keyframe.vertex_offsets.size() % 2U) != 0U) {
                error_out->message =
                    "mesh deform timeline edit keyframes must contain x/y vertex offsets";
                return false;
            }
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message =
                    "mesh deform timeline edit keyframe times must be strictly increasing";
                return false;
            }
            if (expected_vertex_components == 0U) {
                expected_vertex_components = keyframe.vertex_offsets.size();
            } else if (keyframe.vertex_offsets.size() != expected_vertex_components) {
                error_out->message =
                    "mesh deform timeline edit keyframes must use a consistent vertex count";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::tuple<std::string, std::string, std::string>> seen_mesh_weight_edits;
    for (const MeshWeightAttachmentEdit& edit : project.mesh_weight_attachment_edits) {
        if (edit.skin_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            error_out->message =
                "mesh weight edits require skin, slot, and attachment names";
            return false;
        }
        if (edit.vertices.empty()) {
            error_out->message = "mesh weight edits must contain at least one vertex";
            return false;
        }
        const auto duplicate = std::find(
            seen_mesh_weight_edits.begin(),
            seen_mesh_weight_edits.end(),
            std::make_tuple(edit.skin_name, edit.slot_name, edit.attachment_name));
        if (duplicate != seen_mesh_weight_edits.end()) {
            error_out->message = "duplicate mesh weight edits are not allowed";
            return false;
        }
        seen_mesh_weight_edits.push_back(
            std::make_tuple(edit.skin_name, edit.slot_name, edit.attachment_name));

        for (const MeshWeightVertexEdit& vertex : edit.vertices) {
            if (vertex.influences.empty()) {
                error_out->message =
                    "mesh weight edit vertices must contain at least one influence";
                return false;
            }
            if (vertex.influences.size() > 4U) {
                error_out->message =
                    "mesh weight edit vertices must not exceed four influences";
                return false;
            }

            std::vector<std::string> seen_bones;
            double total_weight = 0.0;
            for (const MeshWeightInfluenceEdit& influence : vertex.influences) {
                if (influence.bone_name.empty()) {
                    error_out->message =
                        "mesh weight edit influences require non-empty bone names";
                    return false;
                }
                if (influence.weight <= 0.0) {
                    error_out->message =
                        "mesh weight edit influences must preserve positive weights";
                    return false;
                }
                if (std::find(seen_bones.begin(), seen_bones.end(), influence.bone_name) !=
                    seen_bones.end()) {
                    error_out->message =
                        "mesh weight edit vertices must not repeat the same bone";
                    return false;
                }

                seen_bones.push_back(influence.bone_name);
                total_weight += influence.weight;
            }

            if (total_weight <= 0.0) {
                error_out->message =
                    "mesh weight edit vertices must sum to a positive weight";
                return false;
            }
        }
    }

    std::vector<std::string> seen_draw_order_animations;
    for (const DrawOrderTimelineEdit& edit : project.draw_order_timeline_edits) {
        if (edit.animation_name.empty()) {
            error_out->message = "draw order timeline edits require an animation name";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "draw order timeline edits must contain at least one keyframe";
            return false;
        }
        if (std::find(
                seen_draw_order_animations.begin(),
                seen_draw_order_animations.end(),
                edit.animation_name) != seen_draw_order_animations.end()) {
            error_out->message = "duplicate draw order timeline edits are not allowed";
            return false;
        }
        seen_draw_order_animations.push_back(edit.animation_name);

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const DrawOrderKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.slot_names.empty()) {
                error_out->message =
                    "draw order timeline edit keyframes must list at least one slot";
                return false;
            }
            for (const std::string& slot_name : keyframe.slot_names) {
                if (slot_name.empty()) {
                    error_out->message = "draw order timeline edit slot names must not be empty";
                    return false;
                }
            }
            std::vector<std::string> sorted_slot_names = keyframe.slot_names;
            std::sort(sorted_slot_names.begin(), sorted_slot_names.end());
            if (std::adjacent_find(sorted_slot_names.begin(), sorted_slot_names.end()) !=
                sorted_slot_names.end()) {
                error_out->message = "draw order timeline edit slot names must be unique";
                return false;
            }
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message =
                    "draw order timeline edit keyframe times must be strictly increasing";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::string> seen_event_animations;
    for (const EventTimelineEdit& edit : project.event_timeline_edits) {
        if (edit.animation_name.empty()) {
            error_out->message = "event timeline edits require an animation name";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "event timeline edits must contain at least one keyframe";
            return false;
        }
        if (std::find(
                seen_event_animations.begin(),
                seen_event_animations.end(),
                edit.animation_name) != seen_event_animations.end()) {
            error_out->message = "duplicate event timeline edits are not allowed";
            return false;
        }
        seen_event_animations.push_back(edit.animation_name);

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const EventKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.event_name.empty()) {
                error_out->message = "event timeline edit keyframes require an event name";
                return false;
            }
            if (has_previous_time && keyframe.time < previous_time) {
                error_out->message =
                    "event timeline edit keyframe times must be non-decreasing";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::string> seen_ik_names;
    for (const IkConstraintEdit& edit : project.ik_constraint_edits) {
        if (edit.name.empty() || edit.target_bone_name.empty()) {
            error_out->message = "ik constraint edits require a name and target bone";
            return false;
        }
        if (edit.bone_names.empty() || edit.bone_names.size() > 2U) {
            error_out->message = "ik constraint edits require one or two bones";
            return false;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            error_out->message = "ik constraint edit mix must stay within [0, 1]";
            return false;
        }
        if (std::find(seen_ik_names.begin(), seen_ik_names.end(), edit.name) !=
            seen_ik_names.end()) {
            error_out->message = "duplicate ik constraint edit names are not allowed";
            return false;
        }
        seen_ik_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "ik constraint edit bones must be unique";
            return false;
        }
    }

    std::vector<std::string> seen_path_names;
    for (const PathConstraintEdit& edit : project.path_constraint_edits) {
        if (edit.name.empty() || edit.slot_name.empty() || edit.bone_names.empty()) {
            error_out->message = "path constraint edits require a name, slot, and bone chain";
            return false;
        }
        if (edit.position < 0.0 || edit.position > 1.0 ||
            edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0 ||
            edit.translate_mix < 0.0 || edit.translate_mix > 1.0 ||
            edit.spacing < 0.0) {
            error_out->message =
                "path constraint edit numeric values must stay within their valid ranges";
            return false;
        }
        if (std::find(seen_path_names.begin(), seen_path_names.end(), edit.name) !=
            seen_path_names.end()) {
            error_out->message = "duplicate path constraint edit names are not allowed";
            return false;
        }
        seen_path_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "path constraint edit bones must be unique";
            return false;
        }
    }

    std::vector<std::string> seen_transform_names;
    for (const TransformConstraintEdit& edit : project.transform_constraint_edits) {
        if (edit.name.empty() || edit.source_bone_name.empty() || edit.bone_names.empty()) {
            error_out->message =
                "transform constraint edits require a name, source bone, and targets";
            return false;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0 ||
            edit.translate_mix < 0.0 || edit.translate_mix > 1.0 ||
            edit.scale_mix < 0.0 || edit.scale_mix > 1.0 ||
            edit.shear_mix < 0.0 || edit.shear_mix > 1.0) {
            error_out->message =
                "transform constraint edit mix values must stay within [0, 1]";
            return false;
        }
        if (std::find(seen_transform_names.begin(), seen_transform_names.end(), edit.name) !=
            seen_transform_names.end()) {
            error_out->message = "duplicate transform constraint edit names are not allowed";
            return false;
        }
        seen_transform_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "transform constraint edit target bones must be unique";
            return false;
        }
        if (std::find(edit.bone_names.begin(), edit.bone_names.end(), edit.source_bone_name) !=
            edit.bone_names.end()) {
            error_out->message = "transform constraint source bone must not also be a target";
            return false;
        }
    }

    std::vector<std::string> seen_physics_names;
    for (const PhysicsConstraintEdit& edit : project.physics_constraint_edits) {
        if (edit.name.empty() || edit.bone_names.empty()) {
            error_out->message = "physics constraint edits require a name and bone chain";
            return false;
        }
        if (edit.step <= 0.0 ||
            edit.x < 0.0 || edit.y < 0.0 ||
            edit.rotate < 0.0 || edit.scale_x < 0.0 ||
            edit.shear_x < 0.0 || edit.limit < 0.0 ||
            edit.inertia < 0.0 || edit.inertia > 1.0 ||
            edit.damping < 0.0 || edit.strength < 0.0 ||
            edit.mass_inverse < 0.0 ||
            edit.mix < 0.0 || edit.mix > 1.0) {
            error_out->message =
                "physics constraint edit numeric values must stay within their valid ranges";
            return false;
        }
        if (std::find(seen_physics_names.begin(), seen_physics_names.end(), edit.name) !=
            seen_physics_names.end()) {
            error_out->message = "duplicate physics constraint edit names are not allowed";
            return false;
        }
        seen_physics_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "physics constraint edit bones must be unique";
            return false;
        }
    }

    std::vector<std::filesystem::path> seen_packed_atlas_paths;
    for (const AtlasPackDefinition& definition : project.atlas_pack_definitions) {
        if (definition.atlas_path.empty()) {
            error_out->message = "atlas pack output paths must not be empty";
            return false;
        }
        const std::filesystem::path resolved_definition_path =
            project.resolve_path(definition.atlas_path);
        const auto referenced_atlas = std::find_if(
            project.runtime_assets.atlas_paths.begin(),
            project.runtime_assets.atlas_paths.end(),
            [&](const std::filesystem::path& atlas_path) {
                return project.resolve_path(atlas_path) == resolved_definition_path;
            });
        if (referenced_atlas == project.runtime_assets.atlas_paths.end()) {
            error_out->message =
                "atlas pack outputs must also appear in runtime atlas paths";
            return false;
        }
        const auto duplicate_atlas = std::find(
            seen_packed_atlas_paths.begin(),
            seen_packed_atlas_paths.end(),
            resolved_definition_path);
        if (duplicate_atlas != seen_packed_atlas_paths.end()) {
            error_out->message = "atlas pack output paths must be unique";
            return false;
        }
        seen_packed_atlas_paths.push_back(resolved_definition_path);

        if (definition.filter_min.empty() ||
            definition.filter_mag.empty() ||
            definition.wrap_x.empty() ||
            definition.wrap_y.empty()) {
            error_out->message = "atlas pack filter and wrap settings must not be empty";
            return false;
        }
        if (definition.padding < 0) {
            error_out->message = "atlas pack padding must be zero or greater";
            return false;
        }
        if (definition.bleed < 0) {
            error_out->message = "atlas pack bleed must be zero or greater";
            return false;
        }
        if (definition.bleed > definition.padding) {
            error_out->message = "atlas pack bleed must not exceed padding";
            return false;
        }
        if (definition.sprites.empty()) {
            error_out->message = "atlas pack definitions require at least one sprite";
            return false;
        }

        std::vector<std::string> seen_region_names;
        for (const AtlasPackSprite& sprite : definition.sprites) {
            if (sprite.region_name.empty() || sprite.image_path.empty()) {
                error_out->message =
                    "atlas pack sprites require a region name and image path";
                return false;
            }
            if (std::find(
                    seen_region_names.begin(),
                    seen_region_names.end(),
                    sprite.region_name) != seen_region_names.end()) {
                error_out->message = "atlas pack sprite region names must be unique";
                return false;
            }
            seen_region_names.push_back(sprite.region_name);
        }
    }

    return true;
}

std::filesystem::path default_export_filename(const ProjectData& project) {
    if (!project.runtime_assets.skeleton_path.filename().empty()) {
        return project.runtime_assets.skeleton_path.filename();
    }
    if (!project.source_path.stem().empty()) {
        return project.source_path.stem().string() + ".mskl";
    }
    return "exported_skeleton.mskl";
}

std::filesystem::path default_export_binary_path(const std::filesystem::path& skeleton_path) {
    std::filesystem::path binary_path = skeleton_path;
    binary_path.replace_extension(marrow::runtime::skeleton_binary_extension());
    return binary_path;
}

bool ensure_output_directory(
    const std::filesystem::path& path,
    ProjectExportError* error_out) {
    const std::filesystem::path parent_path = path.parent_path();
    if (parent_path.empty()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(parent_path, error);
    if (!error) {
        return true;
    }

    error_out->path = path;
    error_out->message = error.message();
    return false;
}

bool write_text_file(
    const std::filesystem::path& path,
    std::string_view contents,
    ProjectExportError* error_out) {
    if (!ensure_output_directory(path, error_out)) {
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to open the output file";
        return false;
    }

    output << contents;
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to write the output file";
        return false;
    }

    return true;
}

bool write_binary_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& contents,
    ProjectExportError* error_out) {
    if (!ensure_output_directory(path, error_out)) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to open the output file";
        return false;
    }

    if (!contents.empty()) {
        output.write(
            reinterpret_cast<const char*>(contents.data()),
            static_cast<std::streamsize>(contents.size()));
    }
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to write the output file";
        return false;
    }

    return true;
}

bool copy_file_if_needed(
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination_path,
    bool* copied_out,
    ProjectExportError* error_out) {
    *copied_out = false;
    std::error_code error;
    const bool source_exists = std::filesystem::exists(source_path, error);
    if (error) {
        error_out->path = source_path;
        error_out->message = error.message();
        return false;
    }
    if (!source_exists) {
        return true;
    }

    const bool same_target =
        source_path.lexically_normal() == destination_path.lexically_normal();
    if (same_target) {
        *copied_out = true;
        return true;
    }

    if (!ensure_output_directory(destination_path, error_out)) {
        return false;
    }

    std::filesystem::copy_file(
        source_path,
        destination_path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (!error) {
        *copied_out = true;
        return true;
    }

    error_out->path = destination_path;
    error_out->message = error.message();
    return false;
}

bool export_atlas_asset(
    const std::filesystem::path& source_atlas_path,
    const std::filesystem::path& output_directory,
    std::filesystem::path* exported_atlas_path_out,
    std::vector<std::filesystem::path>* exported_texture_paths_out,
    ProjectExportError* error_out) {
    const auto atlas_document_result = marrow::runtime::json::load_document(source_atlas_path);
    if (!atlas_document_result) {
        error_out->path = source_atlas_path;
        error_out->message = atlas_document_result.error->format();
        return false;
    }

    const auto atlas_result = AtlasLoader::load(*atlas_document_result.document);
    if (!atlas_result) {
        error_out->path = source_atlas_path;
        error_out->message = atlas_result.error->format();
        return false;
    }

    Document atlas_document = *atlas_document_result.document;
    const std::filesystem::path exported_atlas_path =
        (output_directory / source_atlas_path.filename()).lexically_normal();

    if (Value* atlas_object = marrow::runtime::json::find_member(atlas_document.root, "atlas");
        atlas_object != nullptr && atlas_object->is_object()) {
        if (Value* image_value = marrow::runtime::json::find_member(*atlas_object, "image");
            image_value != nullptr && image_value->is_string() &&
            !image_value->as_string().empty()) {
            const std::filesystem::path declared_image_path = image_value->as_string();
            const std::filesystem::path resolved_image_path =
                declared_image_path.is_absolute()
                    ? declared_image_path.lexically_normal()
                    : (source_atlas_path.parent_path() / declared_image_path).lexically_normal();
            const std::filesystem::path exported_image_path =
                (output_directory / resolved_image_path.filename()).lexically_normal();

            bool copied_texture = false;
            ProjectExportError texture_error = *error_out;
            if (!copy_file_if_needed(
                    resolved_image_path,
                    exported_image_path,
                    &copied_texture,
                    &texture_error)) {
                *error_out = std::move(texture_error);
                return false;
            }
            if (copied_texture) {
                image_value->as_string() = exported_image_path.filename().generic_string();
                exported_texture_paths_out->push_back(exported_image_path);
            }
        }
    }

    if (!write_text_file(
            exported_atlas_path,
            marrow::runtime::json::serialize_pretty(atlas_document.root),
            error_out)) {
        return false;
    }

    *exported_atlas_path_out = exported_atlas_path;
    return true;
}

bool export_packed_atlas_asset(
    const ProjectData& project,
    const AtlasPackDefinition& definition,
    const std::filesystem::path& output_directory,
    std::filesystem::path* exported_atlas_path_out,
    std::vector<std::filesystem::path>* exported_texture_paths_out,
    ProjectExportError* error_out) {
    const std::filesystem::path exported_atlas_path =
        (output_directory / definition.atlas_path.filename()).lexically_normal();
    const auto artifact_result = detail::build_packed_atlas_artifact(
        project,
        definition,
        exported_atlas_path);
    if (!artifact_result) {
        error_out->path = exported_atlas_path;
        error_out->message = artifact_result.error_message;
        return false;
    }

    if (!write_binary_file(
            artifact_result.artifact->image_path,
            artifact_result.artifact->image_bytes,
            error_out)) {
        return false;
    }
    if (!write_text_file(
            artifact_result.artifact->atlas_path,
            artifact_result.artifact->atlas_text,
            error_out)) {
        return false;
    }

    exported_texture_paths_out->push_back(artifact_result.artifact->image_path);
    *exported_atlas_path_out = artifact_result.artifact->atlas_path;
    return true;
}

} // namespace

std::string serialize_project(const ProjectData& project) {
    return serialize_project_snapshot(project);
}

std::optional<ProjectCommand> make_project_command(
    std::string label,
    const ProjectData& before_project,
    const ProjectData& after_project) {
    const std::string before_serialized = serialize_project_snapshot(before_project);
    const std::string after_serialized = serialize_project_snapshot(after_project);
    if (before_serialized == after_serialized) {
        return std::nullopt;
    }

    ProjectCommand command;
    command.label = std::move(label);
    command.before_project = before_project;
    command.after_project = after_project;
    command.before_serialized = before_serialized;
    command.after_serialized = after_serialized;
    return command;
}

bool ProjectCommandStack::can_undo() const {
    return !undo_commands_.empty();
}

bool ProjectCommandStack::can_redo() const {
    return !redo_commands_.empty();
}

std::size_t ProjectCommandStack::undo_count() const {
    return undo_commands_.size();
}

std::size_t ProjectCommandStack::redo_count() const {
    return redo_commands_.size();
}

void ProjectCommandStack::clear() {
    undo_commands_.clear();
    redo_commands_.clear();
}

const ProjectCommand* ProjectCommandStack::peek_undo() const {
    return undo_commands_.empty() ? nullptr : &undo_commands_.back();
}

const ProjectCommand* ProjectCommandStack::peek_redo() const {
    return redo_commands_.empty() ? nullptr : &redo_commands_.back();
}

void ProjectCommandStack::push(ProjectCommand command) {
    undo_commands_.push_back(std::move(command));
    redo_commands_.clear();
}

void ProjectCommandStack::commit_undo() {
    if (undo_commands_.empty()) {
        return;
    }

    redo_commands_.push_back(std::move(undo_commands_.back()));
    undo_commands_.pop_back();
}

void ProjectCommandStack::commit_redo() {
    if (redo_commands_.empty()) {
        return;
    }

    undo_commands_.push_back(std::move(redo_commands_.back()));
    redo_commands_.pop_back();
}

std::filesystem::path ProjectData::resolve_path(const std::filesystem::path& referenced_path) const {
    if (referenced_path.empty()) {
        return referenced_path;
    }
    if (referenced_path.is_absolute() || source_path.empty()) {
        return referenced_path.lexically_normal();
    }

    return (source_path.parent_path() / referenced_path).lexically_normal();
}

std::filesystem::path ProjectData::resolved_skeleton_path() const {
    return resolve_path(runtime_assets.skeleton_path);
}

std::vector<std::filesystem::path> ProjectData::resolved_atlas_paths() const {
    std::vector<std::filesystem::path> resolved_paths;
    resolved_paths.reserve(runtime_assets.atlas_paths.size());
    for (const auto& atlas_path : runtime_assets.atlas_paths) {
        resolved_paths.push_back(resolve_path(atlas_path));
    }
    return resolved_paths;
}

std::filesystem::path ProjectData::resolved_export_skeleton_path() const {
    const std::filesystem::path export_path =
        editor_metadata.export_directory / default_export_filename(*this);
    return resolve_path(export_path);
}

std::filesystem::path ProjectData::resolved_export_binary_path() const {
    return default_export_binary_path(resolved_export_skeleton_path());
}

const TransformTimelineEdit* ProjectData::find_transform_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel) const {
    const auto iterator = std::find_if(
        transform_timeline_edits.begin(),
        transform_timeline_edits.end(),
        [&](const TransformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == channel;
        });
    return iterator == transform_timeline_edits.end() ? nullptr : &(*iterator);
}

TransformTimelineEdit* ProjectData::find_transform_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel) {
    const auto iterator = std::find_if(
        transform_timeline_edits.begin(),
        transform_timeline_edits.end(),
        [&](const TransformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == channel;
        });
    return iterator == transform_timeline_edits.end() ? nullptr : &(*iterator);
}

const MeshDeformTimelineEdit* ProjectData::find_mesh_deform_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name) const {
    const auto iterator = std::find_if(
        mesh_deform_timeline_edits.begin(),
        mesh_deform_timeline_edits.end(),
        [&](const MeshDeformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_deform_timeline_edits.end() ? nullptr : &(*iterator);
}

MeshDeformTimelineEdit* ProjectData::find_mesh_deform_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        mesh_deform_timeline_edits.begin(),
        mesh_deform_timeline_edits.end(),
        [&](const MeshDeformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
    });
    return iterator == mesh_deform_timeline_edits.end() ? nullptr : &(*iterator);
}

const MeshWeightAttachmentEdit* ProjectData::find_mesh_weight_attachment_edit(
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) const {
    const auto iterator = std::find_if(
        mesh_weight_attachment_edits.begin(),
        mesh_weight_attachment_edits.end(),
        [&](const MeshWeightAttachmentEdit& edit) {
            return edit.skin_name == skin_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_weight_attachment_edits.end() ? nullptr : &(*iterator);
}

MeshWeightAttachmentEdit* ProjectData::find_mesh_weight_attachment_edit(
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        mesh_weight_attachment_edits.begin(),
        mesh_weight_attachment_edits.end(),
        [&](const MeshWeightAttachmentEdit& edit) {
            return edit.skin_name == skin_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_weight_attachment_edits.end() ? nullptr : &(*iterator);
}

const DrawOrderTimelineEdit* ProjectData::find_draw_order_timeline_edit(
    std::string_view animation_name) const {
    const auto iterator = std::find_if(
        draw_order_timeline_edits.begin(),
        draw_order_timeline_edits.end(),
        [&](const DrawOrderTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == draw_order_timeline_edits.end() ? nullptr : &(*iterator);
}

DrawOrderTimelineEdit* ProjectData::find_draw_order_timeline_edit(
    std::string_view animation_name) {
    const auto iterator = std::find_if(
        draw_order_timeline_edits.begin(),
        draw_order_timeline_edits.end(),
        [&](const DrawOrderTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == draw_order_timeline_edits.end() ? nullptr : &(*iterator);
}

const EventTimelineEdit* ProjectData::find_event_timeline_edit(
    std::string_view animation_name) const {
    const auto iterator = std::find_if(
        event_timeline_edits.begin(),
        event_timeline_edits.end(),
        [&](const EventTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == event_timeline_edits.end() ? nullptr : &(*iterator);
}

EventTimelineEdit* ProjectData::find_event_timeline_edit(
    std::string_view animation_name) {
    const auto iterator = std::find_if(
        event_timeline_edits.begin(),
        event_timeline_edits.end(),
        [&](const EventTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == event_timeline_edits.end() ? nullptr : &(*iterator);
}

const IkConstraintEdit* ProjectData::find_ik_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        ik_constraint_edits.begin(),
        ik_constraint_edits.end(),
        [&](const IkConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == ik_constraint_edits.end() ? nullptr : &(*iterator);
}

IkConstraintEdit* ProjectData::find_ik_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        ik_constraint_edits.begin(),
        ik_constraint_edits.end(),
        [&](const IkConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == ik_constraint_edits.end() ? nullptr : &(*iterator);
}

const PathConstraintEdit* ProjectData::find_path_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        path_constraint_edits.begin(),
        path_constraint_edits.end(),
        [&](const PathConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == path_constraint_edits.end() ? nullptr : &(*iterator);
}

PathConstraintEdit* ProjectData::find_path_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        path_constraint_edits.begin(),
        path_constraint_edits.end(),
        [&](const PathConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == path_constraint_edits.end() ? nullptr : &(*iterator);
}

const TransformConstraintEdit* ProjectData::find_transform_constraint_edit(
    std::string_view name) const {
    const auto iterator = std::find_if(
        transform_constraint_edits.begin(),
        transform_constraint_edits.end(),
        [&](const TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == transform_constraint_edits.end() ? nullptr : &(*iterator);
}

TransformConstraintEdit* ProjectData::find_transform_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        transform_constraint_edits.begin(),
        transform_constraint_edits.end(),
        [&](const TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == transform_constraint_edits.end() ? nullptr : &(*iterator);
}

const PhysicsConstraintEdit* ProjectData::find_physics_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        physics_constraint_edits.begin(),
        physics_constraint_edits.end(),
        [&](const PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == physics_constraint_edits.end() ? nullptr : &(*iterator);
}

PhysicsConstraintEdit* ProjectData::find_physics_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        physics_constraint_edits.begin(),
        physics_constraint_edits.end(),
        [&](const PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == physics_constraint_edits.end() ? nullptr : &(*iterator);
}

const AtlasPackDefinition* ProjectData::find_atlas_pack_definition(
    const std::filesystem::path& atlas_path) const {
    const std::filesystem::path resolved_path = resolve_path(atlas_path);
    const auto iterator = std::find_if(
        atlas_pack_definitions.begin(),
        atlas_pack_definitions.end(),
        [&](const AtlasPackDefinition& definition) {
            return resolve_path(definition.atlas_path) == resolved_path;
        });
    return iterator == atlas_pack_definitions.end() ? nullptr : &(*iterator);
}

AtlasPackDefinition* ProjectData::find_atlas_pack_definition(
    const std::filesystem::path& atlas_path) {
    const std::filesystem::path resolved_path = resolve_path(atlas_path);
    const auto iterator = std::find_if(
        atlas_pack_definitions.begin(),
        atlas_pack_definitions.end(),
        [&](const AtlasPackDefinition& definition) {
            return resolve_path(definition.atlas_path) == resolved_path;
        });
    return iterator == atlas_pack_definitions.end() ? nullptr : &(*iterator);
}

std::string ProjectSaveError::format() const {
    if (path.empty()) {
        return "Failed to save project: " + message;
    }

    return "Failed to save project '" + path.string() + "': " + message;
}

std::string ProjectExportError::format() const {
    if (path.empty()) {
        return "Failed to export runtime assets: " + message;
    }

    return "Failed to export runtime assets '" + path.string() + "': " + message;
}

ProjectData create_minimal_project(const MinimalProjectOptions& options) {
    ProjectData project;
    project.source_path = options.project_path;
    project.runtime_assets.skeleton_path =
        make_project_relative_path(options.project_path, options.skeleton_path);
    project.runtime_assets.atlas_paths.reserve(options.atlas_paths.size());
    for (const auto& atlas_path : options.atlas_paths) {
        project.runtime_assets.atlas_paths.push_back(
            make_project_relative_path(options.project_path, atlas_path));
    }

    project.editor_metadata.name =
        options.name.empty() ? default_project_name(options.project_path, options.skeleton_path)
                             : options.name;
    project.editor_metadata.active_animation = options.active_animation;
    project.editor_metadata.preview_skins = options.preview_skins;
    project.editor_metadata.export_directory = options.export_directory;
    if (project.editor_metadata.preview_skins.empty()) {
        project.editor_metadata.preview_skins.push_back("default");
    }
    if (project.editor_metadata.active_animation.empty()) {
        project.editor_metadata.active_animation = "idle";
    }
    project.editor_metadata.notes =
        options.notes.empty()
            ? "Minimal Marrow editor project referencing the canonical runtime fixtures."
            : options.notes;
    return project;
}

ProjectLoadResult load_project(const Document& document) {
    ProjectLoadResult result;
    if (const auto error = marrow::runtime::json::require_type(
            document, document.root, Value::Type::Object, "$")) {
        result.error = error;
        return result;
    }

    ProjectData project;
    project.source_path = document.source_path;
    if (const auto error = read_required_string(
            document, document.root, "marrow", "$", &project.marrow_version)) {
        result.error = error;
        return result;
    }
    if (project.marrow_version.empty()) {
        result.error = validation_error(
            document,
            document.root.location(),
            "$.marrow",
            "version must not be empty");
        return result;
    }

    if (const auto error = parse_runtime_assets(document, document.root, &project.runtime_assets)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_editor_metadata(document, document.root, &project.editor_metadata)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_transform_timeline_edits(
            document, document.root, &project.transform_timeline_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_mesh_deform_timeline_edits(
            document, document.root, &project.mesh_deform_timeline_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_mesh_weight_attachment_edits(
            document, document.root, &project.mesh_weight_attachment_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_draw_order_timeline_edits(
            document, document.root, &project.draw_order_timeline_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_event_timeline_edits(
            document, document.root, &project.event_timeline_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_ik_constraint_edits(
            document, document.root, &project.ik_constraint_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_path_constraint_edits(
            document, document.root, &project.path_constraint_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_transform_constraint_edits(
            document, document.root, &project.transform_constraint_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_physics_constraint_edits(
            document, document.root, &project.physics_constraint_edits)) {
        result.error = error;
        return result;
    }
    if (const auto error = parse_atlas_pack_definitions(
            document, document.root, &project.atlas_pack_definitions)) {
        result.error = error;
        return result;
    }

    for (std::size_t definition_index = 0;
         definition_index < project.atlas_pack_definitions.size();
         ++definition_index) {
        const AtlasPackDefinition& definition = project.atlas_pack_definitions[definition_index];
        const std::filesystem::path resolved_definition_path =
            project.resolve_path(definition.atlas_path);
        const auto matching_atlas = std::find_if(
            project.runtime_assets.atlas_paths.begin(),
            project.runtime_assets.atlas_paths.end(),
            [&](const std::filesystem::path& atlas_path) {
                return project.resolve_path(atlas_path) == resolved_definition_path;
            });
        if (matching_atlas == project.runtime_assets.atlas_paths.end()) {
            result.error = validation_error(
                document,
                document.root.location(),
                "$.atlas_packs[" + std::to_string(definition_index) + "].atlas",
                "atlas pack outputs must also appear in runtime.atlases");
            return result;
        }
    }

    auto project_ptr = std::make_shared<ProjectData>(std::move(project));
    const auto document_result =
        marrow::runtime::load_skeleton_document(project_ptr->resolved_skeleton_path());
    if (!document_result) {
        result.error = document_result.error;
        return result;
    }

    result.base_skeleton_document =
        marrow::allocate_shared<Document>(std::move(*document_result.document));
    const ProjectRuntimeResult runtime_result =
        build_project_runtime(*project_ptr, *result.base_skeleton_document);
    if (!runtime_result) {
        result.error = runtime_result.error;
        return result;
    }

    std::vector<std::shared_ptr<const marrow::runtime::AtlasData>> atlas_data;
    for (const auto& atlas_path : project_ptr->resolved_atlas_paths()) {
        const auto atlas_result = AtlasLoader::load(atlas_path);
        if (!atlas_result) {
            result.error = atlas_result.error;
            return result;
        }
        atlas_data.push_back(atlas_result.atlas_data);
    }

    result.project = std::move(project_ptr);
    result.skeleton_data = runtime_result.skeleton_data;
    result.atlas_data = std::move(atlas_data);
    return result;
}

ProjectLoadResult load_project(const std::filesystem::path& path) {
    const auto document_result = marrow::runtime::json::load_document(path);
    if (!document_result) {
        ProjectLoadResult result;
        result.error = document_result.error;
        return result;
    }

    return load_project(*document_result.document);
}

ProjectRuntimeResult build_project_runtime(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document) {
    ProjectRuntimeResult result;
    const Document runtime_document = build_runtime_document(project, base_skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(runtime_document);
    if (!skeleton_result) {
        result.error = skeleton_result.error;
        return result;
    }

    result.skeleton_data = skeleton_result.skeleton_data;
    return result;
}

ProjectSaveResult save_project(const ProjectData& project, const std::filesystem::path& path) {
    ProjectSaveResult result;
    ProjectSaveError save_error;
    save_error.path = path;
    if (!validate_project_for_save(project, &save_error)) {
        result.error = std::move(save_error);
        return result;
    }

    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent_path, error);
        if (error) {
            save_error.message = error.message();
            result.error = std::move(save_error);
            return result;
        }
    }

    std::ofstream output(path);
    if (!output) {
        save_error.message = "failed to open the output file";
        result.error = std::move(save_error);
        return result;
    }

    output << serialize_project(project);
    if (!output) {
        save_error.message = "failed to write the serialized project";
        result.error = std::move(save_error);
        return result;
    }

    ProjectData saved_project = project;
    saved_project.source_path = path;
    result.project = std::make_shared<ProjectData>(std::move(saved_project));
    return result;
}

ProjectExportResult export_runtime_assets(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const ProjectExportOptions& options) {
    ProjectExportResult result;
    result.path = options.skeleton_output_path.empty() ? project.resolved_export_skeleton_path()
                                                       : options.skeleton_output_path.lexically_normal();

    ProjectExportError export_error;
    export_error.path = result.path;

    const Document runtime_document = build_runtime_document(project, base_skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(runtime_document);
    if (!skeleton_result) {
        export_error.message = skeleton_result.error->format();
        result.error = std::move(export_error);
        return result;
    }

    if (!write_text_file(
            result.path,
            marrow::runtime::json::serialize_pretty(runtime_document.root),
            &export_error)) {
        result.error = std::move(export_error);
        return result;
    }

    const std::filesystem::path output_directory =
        result.path.has_parent_path() ? result.path.parent_path() : std::filesystem::path(".");
    for (const auto& atlas_path : project.resolved_atlas_paths()) {
        std::filesystem::path exported_atlas_path;
        const AtlasPackDefinition* atlas_pack_definition =
            project.find_atlas_pack_definition(atlas_path);
        const bool exported =
            atlas_pack_definition != nullptr
                ? export_packed_atlas_asset(
                      project,
                      *atlas_pack_definition,
                      output_directory,
                      &exported_atlas_path,
                      &result.texture_paths,
                      &export_error)
                : export_atlas_asset(
                      atlas_path,
                      output_directory,
                      &exported_atlas_path,
                      &result.texture_paths,
                      &export_error);
        if (!exported) {
            result.error = std::move(export_error);
            return result;
        }
        result.atlas_paths.push_back(std::move(exported_atlas_path));
    }

    if (options.binary_output_path.has_value()) {
        result.binary_path = options.binary_output_path->empty()
                                 ? default_export_binary_path(result.path)
                                 : options.binary_output_path->lexically_normal();
        if (!ensure_output_directory(*result.binary_path, &export_error)) {
            result.error = std::move(export_error);
            return result;
        }
        if (const auto binary_error = marrow::runtime::write_skeleton_binary_document(
                runtime_document,
                *result.binary_path)) {
            export_error.path = *result.binary_path;
            export_error.message = binary_error->format();
            result.error = std::move(export_error);
            return result;
        }
    }

    return result;
}

ProjectExportResult export_runtime_skeleton(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const std::filesystem::path& output_path) {
    ProjectExportOptions options;
    options.skeleton_output_path = output_path;
    return export_runtime_assets(project, base_skeleton_document, options);
}

} // namespace marrow::editor
