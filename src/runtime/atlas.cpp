#include "marrow/runtime/atlas.hpp"

#include <algorithm>
#include <utility>

namespace marrow::runtime {
namespace {

using json::Document;
using json::LoadError;
using json::SourceLocation;
using json::Value;

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

std::optional<LoadError> parse_atlas_info(
    const Document& document,
    const Value& root,
    AtlasInfo* info_out) {
    const Value* atlas = nullptr;
    if (const auto error = json::require_member(
            document, root, "atlas", Value::Type::Object, "$", &atlas)) {
        return error;
    }

    if (const auto error = read_required_string(
            document, *atlas, "name", "$.atlas", &info_out->name)) {
        return error;
    }
    if (info_out->name.empty()) {
        return validation_error(
            document,
            atlas->location(),
            "$.atlas.name",
            "atlas name must not be empty");
    }
    if (const auto error = read_required_string(
            document, *atlas, "image", "$.atlas", &info_out->image)) {
        return error;
    }
    if (info_out->image.empty()) {
        return validation_error(
            document,
            atlas->location(),
            "$.atlas.image",
            "atlas image path must not be empty");
    }
    if (const auto error = read_required_number(
            document, *atlas, "width", "$.atlas", &info_out->width)) {
        return error;
    }
    if (const auto error = read_required_number(
            document, *atlas, "height", "$.atlas", &info_out->height)) {
        return error;
    }
    if (info_out->width <= 0.0 || info_out->height <= 0.0) {
        return validation_error(
            document,
            atlas->location(),
            "$.atlas",
            "atlas dimensions must be greater than zero");
    }
    if (const auto error = read_required_string(
            document, *atlas, "filter_min", "$.atlas", &info_out->filter_min)) {
        return error;
    }
    if (const auto error = read_required_string(
            document, *atlas, "filter_mag", "$.atlas", &info_out->filter_mag)) {
        return error;
    }
    if (const auto error = read_required_string(
            document, *atlas, "wrap_x", "$.atlas", &info_out->wrap_x)) {
        return error;
    }
    if (const auto error = read_required_string(
            document, *atlas, "wrap_y", "$.atlas", &info_out->wrap_y)) {
        return error;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_regions(
    const Document& document,
    const Value& root,
    std::vector<AtlasRegion>* regions_out) {
    const Value* regions = nullptr;
    if (const auto error = json::require_member(
            document, root, "regions", Value::Type::Array, "$", &regions)) {
        return error;
    }
    if (regions->as_array().empty()) {
        return validation_error(
            document, regions->location(), "$.regions", "array must not be empty");
    }

    std::vector<AtlasRegion> parsed_regions;
    parsed_regions.reserve(regions->as_array().size());

    for (std::size_t index = 0; index < regions->as_array().size(); ++index) {
        const Value& region_value = regions->as_array()[index];
        const std::string path = "$.regions[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, region_value, Value::Type::Object, path)) {
            return error;
        }

        AtlasRegion region;
        if (const auto error = read_required_string(
                document, region_value, "name", path, &region.name)) {
            return error;
        }
        if (region.name.empty()) {
            return validation_error(
                document,
                region_value.location(),
                path + ".name",
                "region name must not be empty");
        }
        if (const auto error = read_required_number(
                document, region_value, "x", path, &region.x)) {
            return error;
        }
        if (const auto error = read_required_number(
                document, region_value, "y", path, &region.y)) {
            return error;
        }
        if (const auto error = read_required_number(
                document, region_value, "width", path, &region.width)) {
            return error;
        }
        if (const auto error = read_required_number(
                document, region_value, "height", path, &region.height)) {
            return error;
        }
        if (region.width <= 0.0 || region.height <= 0.0) {
            return validation_error(
                document,
                region_value.location(),
                path,
                "region dimensions must be greater than zero");
        }
        if (const auto error = read_optional_number(
                document, region_value, "origin_x", path, &region.origin_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, region_value, "origin_y", path, &region.origin_y)) {
            return error;
        }

        const auto duplicate = std::find_if(
            parsed_regions.begin(),
            parsed_regions.end(),
            [&](const AtlasRegion& existing) {
                return existing.name == region.name;
            });
        if (duplicate != parsed_regions.end()) {
            return validation_error(
                document,
                region_value.location(),
                path + ".name",
                "region name must be unique");
        }

        parsed_regions.push_back(std::move(region));
    }

    *regions_out = std::move(parsed_regions);
    return std::nullopt;
}

} // namespace

AtlasData::AtlasData(AtlasInfo info, std::vector<AtlasRegion> regions)
    : info_(std::move(info)),
      regions_(std::move(regions)) {
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        region_indices_.emplace(regions_[index].name, index);
    }
}

const AtlasInfo& AtlasData::info() const {
    return info_;
}

const std::vector<AtlasRegion>& AtlasData::regions() const {
    return regions_;
}

const AtlasRegion* AtlasData::find_region(std::string_view region_name) const {
    const auto it = region_indices_.find(region_name);
    if (it == region_indices_.end()) {
        return nullptr;
    }
    return &regions_[it->second];
}

const AtlasRegion* AtlasData::find_region_for_attachment(std::string_view attachment_name) const {
    return find_region(attachment_name);
}

AtlasDataResult AtlasLoader::load(const json::Document& document) {
    AtlasDataResult result;

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

    AtlasInfo info;
    if (const auto error = parse_atlas_info(document, root, &info)) {
        result.error = *error;
        return result;
    }

    std::vector<AtlasRegion> regions;
    if (const auto error = parse_regions(document, root, &regions)) {
        result.error = *error;
        return result;
    }

    result.atlas_data = std::make_shared<AtlasData>(std::move(info), std::move(regions));
    return result;
}

AtlasDataResult AtlasLoader::load(const std::filesystem::path& path) {
    const auto document_result = json::load_document(path);
    if (!document_result) {
        AtlasDataResult result;
        result.error = *document_result.error;
        return result;
    }

    return load(*document_result.document);
}

} // namespace marrow::runtime
