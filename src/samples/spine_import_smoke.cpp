#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "marrow/runtime/spine_import.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool expect_near(double actual, double expected, double tolerance, std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual
                  << ")\n";
        return false;
    }
    return true;
}

bool expect_not_contains(
    std::string_view actual,
    std::string_view unexpected_fragment,
    std::string_view message) {
    if (actual.find(unexpected_fragment) != std::string_view::npos) {
        std::cerr << "FAIL: " << message << " (found unexpected '" << unexpected_fragment
                  << "' in '" << actual << "')\n";
        return false;
    }
    return true;
}

bool warnings_include_fragments(
    const std::vector<std::string>& warnings,
    std::initializer_list<std::string_view> fragments) {
    return std::any_of(
        warnings.begin(),
        warnings.end(),
        [&](const std::string& warning) {
            return std::all_of(
                fragments.begin(),
                fragments.end(),
                [&](std::string_view fragment) {
                    return warning.find(fragment) != std::string::npos;
                });
        });
}

bool expect_optional_near(
    const std::optional<double>& actual,
    double expected,
    double tolerance,
    std::string_view message) {
    if (!actual.has_value()) {
        std::cerr << "FAIL: " << message << " (missing sample)\n";
        return false;
    }
    return expect_near(*actual, expected, tolerance, message);
}

bool expect_vector_sample(
    const std::optional<marrow::runtime::VectorSample>& actual,
    double expected_x,
    double expected_y,
    double tolerance,
    std::string_view message) {
    if (!actual.has_value()) {
        std::cerr << "FAIL: " << message << " (missing sample)\n";
        return false;
    }

    return expect_near(actual->x, expected_x, tolerance, std::string(message) + " x") &&
        expect_near(actual->y, expected_y, tolerance, std::string(message) + " y");
}

bool expect_curve_points(
    const marrow::runtime::Interpolation& interpolation,
    double cx1,
    double cy1,
    double cx2,
    double cy2,
    double tolerance,
    std::string_view message) {
    if (interpolation.kind() != marrow::runtime::InterpolationKind::CubicBezier) {
        std::cerr << "FAIL: " << message << " (expected cubic bezier interpolation)\n";
        return false;
    }

    const auto& bezier = interpolation.cubic_bezier();
    return expect_near(bezier.cx1, cx1, tolerance, std::string(message) + " cx1") &&
        expect_near(bezier.cy1, cy1, tolerance, std::string(message) + " cy1") &&
        expect_near(bezier.cx2, cx2, tolerance, std::string(message) + " cx2") &&
        expect_near(bezier.cy2, cy2, tolerance, std::string(message) + " cy2");
}

marrow::runtime::MeshWorldVertex transform_mesh_vertex(
    const marrow::runtime::BoneWorldTransform& transform,
    double x,
    double y) {
    return {
        transform.a * x + transform.b * y + transform.world_x,
        transform.c * x + transform.d * y + transform.world_y};
}

marrow::runtime::MeshWorldVertex blend_weighted_mesh_vertex(
    const std::vector<marrow::runtime::BoneWorldTransform>& bone_world_transforms,
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights) {
    marrow::runtime::MeshWorldVertex world_vertex;
    for (const marrow::runtime::MeshGeometry::VertexWeight& influence : vertex_weights.influences) {
        const marrow::runtime::MeshWorldVertex transformed = transform_mesh_vertex(
            bone_world_transforms[influence.bone_index],
            influence.x,
            influence.y);
        world_vertex.x += transformed.x * influence.weight;
        world_vertex.y += transformed.y * influence.weight;
    }
    return world_vertex;
}

const marrow::runtime::json::Value* find_nested_member(
    const marrow::runtime::json::Value& object,
    std::initializer_list<std::string_view> path) {
    const marrow::runtime::json::Value* current = &object;
    for (const std::string_view key : path) {
        current = marrow::runtime::json::find_member(*current, key);
        if (current == nullptr) {
            return nullptr;
        }
    }
    return current;
}

std::filesystem::path resolve_json_fixture_path(int argc, char** argv) {
    if (argc >= 2) {
        return std::filesystem::path(argv[1]);
    }
    return std::filesystem::path("assets/fixtures/spine_import_sample.json");
}

std::filesystem::path resolve_atlas_fixture_path(int argc, char** argv) {
    if (argc >= 3) {
        return std::filesystem::path(argv[2]);
    }
    return std::filesystem::path("assets/fixtures/spine_import_sample.atlas");
}

bool write_document(
    const marrow::runtime::json::Document& document,
    const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Failed to open '" << path.string() << "' for writing.\n";
        return false;
    }

    output << marrow::runtime::json::serialize_pretty(document.root);
    return output.good();
}

std::string sanitize_output_component(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' ||
            character == '-') {
            sanitized.push_back(character);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? std::string("page") : sanitized;
}

std::string atlas_page_suffix(
    const marrow::runtime::json::Document& document,
    std::size_t page_index) {
    const marrow::runtime::json::Value* atlas =
        marrow::runtime::json::find_member(document.root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        return "page_" + std::to_string(page_index);
    }
    const marrow::runtime::json::Value* image =
        marrow::runtime::json::find_member(*atlas, "image");
    if (image == nullptr || !image->is_string() || image->as_string().empty()) {
        return "page_" + std::to_string(page_index);
    }

    const std::filesystem::path image_path(image->as_string());
    const std::string stem = image_path.stem().string();
    return sanitize_output_component(stem.empty() ? image->as_string() : stem);
}

std::filesystem::path atlas_output_path_for_page(
    std::filesystem::path base_output_path,
    const marrow::runtime::json::Document& document,
    std::size_t page_count,
    std::size_t page_index) {
    if (base_output_path.extension().empty()) {
        base_output_path.replace_extension(".matl");
    }
    if (page_count <= 1) {
        return base_output_path;
    }

    const std::string suffix = atlas_page_suffix(document, page_index);
    return base_output_path.parent_path() /
        (base_output_path.stem().string() + "_" + suffix + base_output_path.extension().string());
}

bool load_atlas_model(
    const std::filesystem::path& path,
    std::shared_ptr<const marrow::runtime::AtlasData>* atlas_out) {
    const auto atlas_result = marrow::runtime::AtlasLoader::load(path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return false;
    }

    *atlas_out = atlas_result.atlas_data;
    return true;
}

bool compare_atlas_models(
    const marrow::runtime::AtlasData& actual,
    const marrow::runtime::AtlasData& expected,
    std::string_view label) {
    bool ok = true;
    ok &= expect(
        actual.info().name == expected.info().name,
        std::string(label) + ": atlas name");
    ok &= expect(
        actual.info().image == expected.info().image,
        std::string(label) + ": atlas image");
    ok &= expect_near(
        actual.info().width,
        expected.info().width,
        1e-6,
        std::string(label) + ": atlas width");
    ok &= expect_near(
        actual.info().height,
        expected.info().height,
        1e-6,
        std::string(label) + ": atlas height");
    ok &= expect(
        actual.info().filter_min == expected.info().filter_min,
        std::string(label) + ": atlas filter_min");
    ok &= expect(
        actual.info().filter_mag == expected.info().filter_mag,
        std::string(label) + ": atlas filter_mag");
    ok &= expect(
        actual.info().wrap_x == expected.info().wrap_x,
        std::string(label) + ": atlas wrap_x");
    ok &= expect(
        actual.info().wrap_y == expected.info().wrap_y,
        std::string(label) + ": atlas wrap_y");
    ok &= expect(
        actual.info().premultiplied_alpha == expected.info().premultiplied_alpha,
        std::string(label) + ": atlas premultiplied_alpha");
    ok &= expect(
        actual.regions().size() == expected.regions().size(),
        std::string(label) + ": atlas region count");
    if (!ok) {
        return false;
    }

    for (const auto& expected_region : expected.regions()) {
        const auto* actual_region = actual.find_region(expected_region.name);
        ok &= expect(
            actual_region != nullptr,
            std::string(label) + ": missing atlas region " + expected_region.name);
        if (actual_region == nullptr) {
            continue;
        }

        ok &= expect_near(
            actual_region->x,
            expected_region.x,
            1e-6,
            std::string(label) + ": region x");
        ok &= expect_near(
            actual_region->y,
            expected_region.y,
            1e-6,
            std::string(label) + ": region y");
        ok &= expect_near(
            actual_region->width,
            expected_region.width,
            1e-6,
            std::string(label) + ": region width");
        ok &= expect_near(
            actual_region->height,
            expected_region.height,
            1e-6,
            std::string(label) + ": region height");
        ok &= expect_near(
            actual_region->origin_x,
            expected_region.origin_x,
            1e-6,
            std::string(label) + ": region origin_x");
        ok &= expect_near(
            actual_region->origin_y,
            expected_region.origin_y,
            1e-6,
            std::string(label) + ": region origin_y");
        ok &= expect_near(
            actual_region->rotate_degrees,
            expected_region.rotate_degrees,
            1e-6,
            std::string(label) + ": region rotate");
    }

    return ok;
}

const marrow::runtime::AtlasRegion* find_region_across_pages(
    const std::vector<std::shared_ptr<const marrow::runtime::AtlasData>>& atlas_pages,
    std::string_view region_name) {
    for (const auto& atlas : atlas_pages) {
        if (const auto* region = atlas->find_region(region_name)) {
            return region;
        }
    }
    return nullptr;
}

struct SourceMeshInfluence {
    std::size_t source_index{0};
    std::size_t bone_index{0};
    double x{0.0};
    double y{0.0};
    double weight{0.0};
};

using SourceWeightedVertex = std::vector<SourceMeshInfluence>;

struct PruneRegressionExpectation {
    std::filesystem::path source_path;
    std::string skin_name;
    std::string slot_name;
    std::string attachment_name;
    std::size_t expected_affected_vertex_count{0};
    double max_discarded_weight{0.0};
};

const marrow::runtime::json::Value* find_spine_source_attachment(
    const marrow::runtime::json::Document& document,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    const auto* skins = marrow::runtime::json::find_member(document.root, "skins");
    if (skins == nullptr) {
        return nullptr;
    }

    if (skins->is_array()) {
        for (const auto& skin : skins->as_array()) {
            if (!skin.is_object()) {
                continue;
            }

            const auto* name = marrow::runtime::json::find_member(skin, "name");
            const std::string_view current_skin =
                name != nullptr && name->is_string() ? std::string_view(name->as_string())
                                                     : std::string_view("default");
            if (current_skin != skin_name) {
                continue;
            }

            const auto* attachments = marrow::runtime::json::find_member(skin, "attachments");
            if (attachments == nullptr || !attachments->is_object()) {
                return nullptr;
            }
            const auto* slot = marrow::runtime::json::find_member(*attachments, slot_name);
            if (slot == nullptr || !slot->is_object()) {
                return nullptr;
            }
            return marrow::runtime::json::find_member(*slot, attachment_name);
        }
        return nullptr;
    }

    if (!skins->is_object()) {
        return nullptr;
    }

    const auto* skin = marrow::runtime::json::find_member(*skins, skin_name);
    if (skin == nullptr || !skin->is_object()) {
        return nullptr;
    }
    const auto* attachments = marrow::runtime::json::find_member(*skin, "attachments");
    const auto* attachment_root =
        attachments != nullptr && attachments->is_object() ? attachments : skin;
    const auto* slot = marrow::runtime::json::find_member(*attachment_root, slot_name);
    if (slot == nullptr || !slot->is_object()) {
        return nullptr;
    }
    return marrow::runtime::json::find_member(*slot, attachment_name);
}

std::optional<std::vector<SourceWeightedVertex>> decode_source_weighted_mesh(
    const marrow::runtime::json::Value& attachment) {
    const auto* vertices = marrow::runtime::json::find_member(attachment, "vertices");
    const auto* uvs = marrow::runtime::json::find_member(attachment, "uvs");
    if (vertices == nullptr || !vertices->is_array() || uvs == nullptr || !uvs->is_array()) {
        return std::nullopt;
    }
    if ((uvs->as_array().size() % 2U) != 0U) {
        return std::nullopt;
    }

    std::vector<double> spine_vertices;
    spine_vertices.reserve(vertices->as_array().size());
    for (const auto& value : vertices->as_array()) {
        if (!value.is_number()) {
            return std::nullopt;
        }
        spine_vertices.push_back(value.as_number());
    }
    if (spine_vertices.size() <= uvs->as_array().size()) {
        return std::nullopt;
    }

    const std::size_t vertex_count = uvs->as_array().size() / 2U;
    std::vector<SourceWeightedVertex> decoded(vertex_count);
    std::size_t cursor = 0;
    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (cursor >= spine_vertices.size()) {
            return std::nullopt;
        }
        const double bone_count_value = spine_vertices[cursor++];
        if (bone_count_value < 1.0 || std::floor(bone_count_value) != bone_count_value) {
            return std::nullopt;
        }

        const std::size_t influence_count = static_cast<std::size_t>(bone_count_value);
        if (cursor + influence_count * 4U > spine_vertices.size()) {
            return std::nullopt;
        }

        SourceWeightedVertex influences;
        influences.reserve(influence_count);
        for (std::size_t influence_index = 0; influence_index < influence_count; ++influence_index) {
            const double bone_index_value = spine_vertices[cursor++];
            if (bone_index_value < 0.0 || std::floor(bone_index_value) != bone_index_value) {
                return std::nullopt;
            }
            influences.push_back(SourceMeshInfluence{
                influence_index,
                static_cast<std::size_t>(bone_index_value),
                spine_vertices[cursor++],
                spine_vertices[cursor++],
                spine_vertices[cursor++]});
        }
        decoded[vertex_index] = std::move(influences);
    }

    if (cursor != spine_vertices.size()) {
        return std::nullopt;
    }
    return decoded;
}

std::optional<std::vector<SourceWeightedVertex>> decode_source_weighted_polygon(
    const marrow::runtime::json::Value& attachment) {
    const auto* vertex_count_value =
        marrow::runtime::json::find_member(attachment, "vertexCount");
    const auto* vertices = marrow::runtime::json::find_member(attachment, "vertices");
    if (vertex_count_value == nullptr || !vertex_count_value->is_number() || vertices == nullptr ||
        !vertices->is_array()) {
        return std::nullopt;
    }

    const double raw_vertex_count = vertex_count_value->as_number();
    if (raw_vertex_count < 0.0 || std::floor(raw_vertex_count) != raw_vertex_count) {
        return std::nullopt;
    }
    const std::size_t vertex_count = static_cast<std::size_t>(raw_vertex_count);

    std::vector<double> spine_vertices;
    spine_vertices.reserve(vertices->as_array().size());
    for (const auto& value : vertices->as_array()) {
        if (!value.is_number()) {
            return std::nullopt;
        }
        spine_vertices.push_back(value.as_number());
    }
    if (spine_vertices.size() <= vertex_count * 2U) {
        return std::nullopt;
    }

    std::vector<SourceWeightedVertex> decoded(vertex_count);
    std::size_t cursor = 0;
    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (cursor >= spine_vertices.size()) {
            return std::nullopt;
        }

        const double bone_count_value = spine_vertices[cursor++];
        if (bone_count_value < 1.0 || std::floor(bone_count_value) != bone_count_value) {
            return std::nullopt;
        }

        const std::size_t influence_count = static_cast<std::size_t>(bone_count_value);
        if (cursor + influence_count * 4U > spine_vertices.size()) {
            return std::nullopt;
        }

        SourceWeightedVertex influences;
        influences.reserve(influence_count);
        for (std::size_t influence_index = 0; influence_index < influence_count; ++influence_index) {
            const double bone_index_value = spine_vertices[cursor++];
            if (bone_index_value < 0.0 || std::floor(bone_index_value) != bone_index_value) {
                return std::nullopt;
            }
            influences.push_back(SourceMeshInfluence{
                influence_index,
                static_cast<std::size_t>(bone_index_value),
                spine_vertices[cursor++],
                spine_vertices[cursor++],
                spine_vertices[cursor++]});
        }
        decoded[vertex_index] = std::move(influences);
    }

    if (cursor != spine_vertices.size()) {
        return std::nullopt;
    }
    return decoded;
}

std::vector<SourceMeshInfluence> select_best_four_source_influences(
    const SourceWeightedVertex& source_vertex) {
    std::vector<SourceMeshInfluence> selected = source_vertex;
    selected.erase(
        std::remove_if(
            selected.begin(),
            selected.end(),
            [](const SourceMeshInfluence& influence) {
                return influence.weight <= 0.0;
            }),
        selected.end());
    if (selected.size() > 4U) {
        std::stable_sort(
            selected.begin(),
            selected.end(),
            [](const SourceMeshInfluence& lhs, const SourceMeshInfluence& rhs) {
                return lhs.weight > rhs.weight;
            });
        selected.resize(4U);
        std::sort(
            selected.begin(),
            selected.end(),
            [](const SourceMeshInfluence& lhs, const SourceMeshInfluence& rhs) {
                return lhs.source_index < rhs.source_index;
            });
    }
    return selected;
}

double sum_source_weights(const SourceWeightedVertex& source_vertex) {
    double total = 0.0;
    for (const SourceMeshInfluence& influence : source_vertex) {
        total += influence.weight;
    }
    return total;
}

double sum_imported_weights(const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights) {
    double total = 0.0;
    for (const auto& influence : vertex_weights.influences) {
        total += influence.weight;
    }
    return total;
}

bool warnings_include_pruned_vertex_count(
    const std::vector<std::string>& warnings,
    std::string_view slot_name,
    std::string_view attachment_name,
    std::size_t affected_vertex_count) {
    const std::string path_fragment =
        std::string(slot_name) + "." + std::string(attachment_name) + ".vertices";
    const std::string count_fragment =
        std::to_string(affected_vertex_count) + " " +
        (affected_vertex_count == 1U ? std::string("vertex") : std::string("vertices"));
    return std::any_of(
        warnings.begin(),
        warnings.end(),
        [&](const std::string& warning) {
            return warning.find(path_fragment) != std::string::npos &&
                warning.find(count_fragment) != std::string::npos;
        });
}

bool verify_best_n_pruning_asset(const PruneRegressionExpectation& expectation) {
    const auto source_load_result = marrow::runtime::json::load_document(expectation.source_path);
    if (!source_load_result) {
        std::cerr << source_load_result.error->format() << '\n';
        return false;
    }

    const auto* source_attachment = find_spine_source_attachment(
        *source_load_result.document,
        expectation.skin_name,
        expectation.slot_name,
        expectation.attachment_name);
    if (!expect(source_attachment != nullptr, "expected real Spine mesh attachment")) {
        return false;
    }

    const auto source_weights = decode_source_weighted_mesh(*source_attachment);
    if (!expect(source_weights.has_value(), "expected weighted mesh data in real Spine asset")) {
        return false;
    }

    const auto raw_import_result =
        marrow::runtime::import_spine_json_file(expectation.source_path);
    bool ok = true;
    const std::string label = expectation.source_path.filename().string();
    ok &= expect(
        warnings_include_pruned_vertex_count(
            raw_import_result.warnings,
            expectation.slot_name,
            expectation.attachment_name,
            expectation.expected_affected_vertex_count),
        label + " raw import should log the pruned vertex count");
    if (raw_import_result.error.has_value()) {
        ok &= expect_not_contains(
            raw_import_result.error->message,
            "mesh vertices support at most 4 bone influences",
            label + " should not fail on the runtime 4-influence cap");
    }
    if (!ok) {
        return false;
    }

    marrow::runtime::json::Document importable_document = *source_load_result.document;
    marrow::runtime::json::Value::Object stub_animations;
    stub_animations.emplace(
        "import_stub",
        marrow::runtime::json::Value(marrow::runtime::json::Value::Object{}, {}));
    importable_document.root.as_object()["animations"] =
        marrow::runtime::json::Value(std::move(stub_animations), {});
    const auto stripped_import_result =
        marrow::runtime::import_spine_json_document(importable_document);
    if (!stripped_import_result) {
        std::cerr << stripped_import_result.error->format() << '\n';
        return false;
    }

    ok &= expect(
        warnings_include_pruned_vertex_count(
            stripped_import_result.warnings,
            expectation.slot_name,
            expectation.attachment_name,
            expectation.expected_affected_vertex_count),
        label + " stripped import should log the pruned vertex count");

    const auto load_result = marrow::runtime::load_skeleton_data(*stripped_import_result.document);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    const auto slot_index = load_result.skeleton_data->find_slot_index(expectation.slot_name);
    const auto* imported_attachment = slot_index.has_value()
        ? load_result.skeleton_data->find_attachment(
              expectation.skin_name,
              *slot_index,
              expectation.attachment_name)
        : nullptr;
    ok &= expect(slot_index.has_value(), label + " slot should import");
    ok &= expect(imported_attachment != nullptr, label + " attachment should import");
    ok &= expect(
        imported_attachment != nullptr && imported_attachment->mesh_geometry != nullptr,
        label + " attachment should keep mesh geometry");
    if (!ok) {
        return false;
    }

    const auto& geometry = *imported_attachment->mesh_geometry;
    ok &= expect(
        geometry.weights.size() == source_weights->size(),
        label + " imported mesh should preserve vertex count");
    if (!ok) {
        return false;
    }

    std::size_t affected_vertex_count = 0;
    double observed_max_discarded_weight = 0.0;
    for (std::size_t vertex_index = 0; vertex_index < source_weights->size(); ++vertex_index) {
        const SourceWeightedVertex& original_vertex = (*source_weights)[vertex_index];
        std::size_t positive_influence_count = 0;
        for (const SourceMeshInfluence& influence : original_vertex) {
            if (influence.weight > 0.0) {
                ++positive_influence_count;
            }
        }
        if (positive_influence_count <= 4U) {
            continue;
        }

        ++affected_vertex_count;
        const std::vector<SourceMeshInfluence> selected =
            select_best_four_source_influences(original_vertex);
        const double original_total = sum_source_weights(original_vertex);
        const double selected_total = sum_source_weights(selected);
        const double discarded_weight = original_total - selected_total;
        observed_max_discarded_weight =
            std::max(observed_max_discarded_weight, discarded_weight);

        const auto& imported_vertex = geometry.weights[vertex_index];
        ok &= expect(
            imported_vertex.influences.size() == 4U,
            label + " affected vertex should keep the strongest four influences");
        ok &= expect_near(
            sum_imported_weights(imported_vertex),
            1.0,
            1e-5,
            label + " affected vertex should renormalize to 1.0");
        if (selected_total <= 0.0 || imported_vertex.influences.size() != 4U) {
            continue;
        }

        for (std::size_t influence_index = 0; influence_index < 4U; ++influence_index) {
            const auto& expected = selected[influence_index];
            const auto& imported = imported_vertex.influences[influence_index];
            const std::string influence_label = label + " vertex " + std::to_string(vertex_index) +
                " influence " + std::to_string(influence_index);
            ok &= expect(
                imported.bone_index == expected.bone_index,
                influence_label + " should keep the expected bone");
            ok &= expect_near(
                imported.x,
                expected.x,
                1e-5,
                influence_label + " should keep the expected local x");
            ok &= expect_near(
                imported.y,
                expected.y,
                1e-5,
                influence_label + " should keep the expected local y");
            ok &= expect_near(
                imported.weight,
                expected.weight / selected_total,
                1e-5,
                influence_label + " should renormalize the retained weight");
        }
    }

    ok &= expect(
        affected_vertex_count == expectation.expected_affected_vertex_count,
        label + " should prune the expected number of vertices");
    ok &= expect(
        observed_max_discarded_weight <= expectation.max_discarded_weight,
        label + " should only discard visually small weights");
    return ok;
}

bool verify_curve_regression_fixture() {
    constexpr std::string_view kBezierRegressionFixture = R"json(
{
  "skeleton": {
    "spine": "4.2.22"
  },
  "bones": [
    { "name": "root" },
    { "name": "legacy", "parent": "root" },
    { "name": "spine_scalar", "parent": "root" },
    { "name": "vector", "parent": "root" }
  ],
  "slots": [
    { "name": "body", "bone": "root" }
  ],
  "skins": {
    "default": {}
  },
  "animations": {
    "curve_test": {
      "bones": {
        "legacy": {
          "rotate": [
            { "time": 0.0, "angle": 0.0 },
            { "time": 0.5, "angle": 10.0, "curve": [0.25, 0.1, 0.75, 0.9] },
            { "time": 1.0, "angle": 20.0 }
          ]
        },
        "spine_scalar": {
          "rotate": [
            { "time": 0.0, "value": 0.0 },
            { "time": 0.5, "value": 10.0, "curve": [0.625, 12.0, 0.875, 19.0] },
            { "time": 1.0, "value": 20.0 }
          ]
        },
        "vector": {
          "translate": [
            { "time": 0.0, "x": 0.0, "y": 0.0 },
            {
              "time": 0.5,
              "x": 0.0,
              "y": 10.0,
              "curve": [0.625, 0.0, 0.875, 0.0, 0.625, 12.0, 0.875, 19.0]
            },
            { "time": 1.0, "x": 0.0, "y": 20.0 }
          ]
        }
      }
    }
  }
}
)json";

    const auto document_result = marrow::runtime::json::parse_document(
        kBezierRegressionFixture,
        "<spine-bezier-regression>");
    if (!document_result) {
        std::cerr << document_result.error->format() << '\n';
        return false;
    }

    const auto import_result =
        marrow::runtime::import_spine_json_document(*document_result.document);
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return false;
    }

    const auto load_result = marrow::runtime::load_skeleton_data(*import_result.document);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    const auto* animation = load_result.skeleton_data->find_animation("curve_test");
    const auto legacy_index = load_result.skeleton_data->find_bone_index("legacy");
    const auto spine_scalar_index =
        load_result.skeleton_data->find_bone_index("spine_scalar");
    const auto vector_index = load_result.skeleton_data->find_bone_index("vector");
    if (!expect(animation != nullptr, "curve regression animation should load") ||
        !expect(legacy_index.has_value(), "legacy regression bone should load") ||
        !expect(spine_scalar_index.has_value(), "spine scalar regression bone should load") ||
        !expect(vector_index.has_value(), "vector regression bone should load")) {
        return false;
    }

    const auto* legacy_timeline = animation->find_rotate_timeline(*legacy_index);
    const auto* scalar_timeline = animation->find_rotate_timeline(*spine_scalar_index);
    const auto* vector_timeline = animation->find_translate_timeline(*vector_index);
    if (!expect(legacy_timeline != nullptr, "legacy rotate timeline should import") ||
        !expect(scalar_timeline != nullptr, "Spine scalar rotate timeline should import") ||
        !expect(vector_timeline != nullptr, "Spine vector timeline should import")) {
        return false;
    }
    if (!expect(legacy_timeline->keyframes.size() == 3, "legacy rotate keyframe count") ||
        !expect(scalar_timeline->keyframes.size() == 3, "Spine scalar rotate keyframe count") ||
        !expect(vector_timeline->keyframes.size() == 3, "Spine vector keyframe count")) {
        return false;
    }

    const marrow::runtime::Interpolation expected_legacy_curve =
        marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9);
    const marrow::runtime::Interpolation expected_spine_curve =
        marrow::runtime::Interpolation::cubic_bezier(0.25, 0.2, 0.75, 0.9);
    bool ok = true;
    ok &= expect_curve_points(
        legacy_timeline->keyframes[1].interpolation,
        0.25,
        0.1,
        0.75,
        0.9,
        1e-6,
        "legacy bezier curve");
    ok &= expect_curve_points(
        scalar_timeline->keyframes[1].interpolation,
        0.25,
        0.2,
        0.75,
        0.9,
        1e-6,
        "Spine scalar bezier curve");
    ok &= expect_curve_points(
        vector_timeline->keyframes[1].interpolation,
        0.25,
        0.2,
        0.75,
        0.9,
        1e-6,
        "Spine vector bezier curve");

    const double expected_legacy_rotation =
        marrow::runtime::interpolate_value(10.0, 20.0, expected_legacy_curve, 0.5);
    const double expected_scalar_rotation =
        marrow::runtime::interpolate_value(10.0, 20.0, expected_spine_curve, 0.5);
    const double expected_vector_y =
        marrow::runtime::interpolate_value(10.0, 20.0, expected_spine_curve, 0.5);
    ok &= expect_optional_near(
        animation->sample_bone_rotation(*legacy_index, 0.75),
        expected_legacy_rotation,
        1e-5,
        "legacy sampled rotation");
    ok &= expect_optional_near(
        animation->sample_bone_rotation(*spine_scalar_index, 0.75),
        expected_scalar_rotation,
        1e-5,
        "Spine scalar sampled rotation");
    ok &= expect_vector_sample(
        animation->sample_bone_translation(*vector_index, 0.75),
        0.0,
        expected_vector_y,
        1e-5,
        "Spine vector sampled translation");

    return ok;
}

bool verify_goblins_curve_import() {
    const auto import_result = marrow::runtime::import_spine_json_file(
        "assets/spine-examples/goblins/goblins-pro.json");
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return false;
    }

    const auto load_result = marrow::runtime::load_skeleton_data(*import_result.document);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    const auto* walk = load_result.skeleton_data->find_animation("walk");
    const auto hip_index = load_result.skeleton_data->find_bone_index("hip");
    const auto right_upper_leg_index =
        load_result.skeleton_data->find_bone_index("right-upper-leg");
    if (!expect(walk != nullptr, "goblins walk animation should import") ||
        !expect(hip_index.has_value(), "goblins hip bone should import") ||
        !expect(
            right_upper_leg_index.has_value(),
            "goblins right-upper-leg bone should import")) {
        return false;
    }

    const auto* hip_timeline = walk->find_translate_timeline(*hip_index);
    const auto* leg_timeline = walk->find_rotate_timeline(*right_upper_leg_index);
    if (!expect(hip_timeline != nullptr, "goblins hip translate timeline should import") ||
        !expect(
            leg_timeline != nullptr,
            "goblins right-upper-leg rotate timeline should import")) {
        return false;
    }
    if (!expect(hip_timeline->keyframes.size() >= 3, "goblins hip keyframe count") ||
        !expect(leg_timeline->keyframes.size() >= 3, "goblins right-upper-leg keyframe count")) {
        return false;
    }

    const double hip_start_time = 0.1333;
    const double hip_end_time = 0.2333;
    const double goblins_sample_time = 0.1833;
    const double hip_alpha = (goblins_sample_time - hip_start_time) /
        (hip_end_time - hip_start_time);
    const marrow::runtime::Interpolation hip_curve =
        marrow::runtime::Interpolation::cubic_bezier(
            (0.166 - hip_start_time) / (hip_end_time - hip_start_time),
            (-8.91 - -9.35) / (-0.59 - -9.35),
            (0.201 - hip_start_time) / (hip_end_time - hip_start_time),
            (-1.14 - -9.35) / (-0.59 - -9.35));

    const double leg_start_time = 0.1333;
    const double leg_end_time = 0.2333;
    const double leg_alpha = (goblins_sample_time - leg_start_time) /
        (leg_end_time - leg_start_time);
    const marrow::runtime::Interpolation leg_curve =
        marrow::runtime::Interpolation::cubic_bezier(
            (0.175 - leg_start_time) / (leg_end_time - leg_start_time),
            (49.86 - 49.86) / (22.51 - 49.86),
            (0.204 - leg_start_time) / (leg_end_time - leg_start_time),
            (22.69 - 49.86) / (22.51 - 49.86));
    const double expected_leg_rotation = leg_timeline->setup_rotation +
        marrow::runtime::interpolate_value(
            leg_timeline->keyframes[1].angle,
            leg_timeline->keyframes[2].angle,
            leg_curve,
            leg_alpha);

    bool ok = true;
    ok &= expect_curve_points(
        hip_timeline->keyframes[1].interpolation,
        (0.166 - hip_start_time) / (hip_end_time - hip_start_time),
        (-8.91 - -9.35) / (-0.59 - -9.35),
        (0.201 - hip_start_time) / (hip_end_time - hip_start_time),
        (-1.14 - -9.35) / (-0.59 - -9.35),
        1e-5,
        "goblins hip bezier");
    ok &= expect_curve_points(
        leg_timeline->keyframes[1].interpolation,
        (0.175 - leg_start_time) / (leg_end_time - leg_start_time),
        (49.86 - 49.86) / (22.51 - 49.86),
        (0.204 - leg_start_time) / (leg_end_time - leg_start_time),
        (22.69 - 49.86) / (22.51 - 49.86),
        1e-5,
        "goblins right-upper-leg bezier");
    ok &= expect_vector_sample(
        walk->sample_bone_translation(*hip_index, goblins_sample_time),
        marrow::runtime::interpolate_value(
            hip_timeline->keyframes[1].x,
            hip_timeline->keyframes[2].x,
            hip_curve,
            hip_alpha),
        marrow::runtime::interpolate_value(
            hip_timeline->keyframes[1].y,
            hip_timeline->keyframes[2].y,
            hip_curve,
            hip_alpha),
        1e-5,
        "goblins hip translation");
    ok &= expect_optional_near(
        walk->sample_bone_rotation(*right_upper_leg_index, goblins_sample_time),
        expected_leg_rotation,
        1e-5,
        "goblins right-upper-leg rotation");
    return ok;
}

bool verify_owl_zero_weight_mesh_import() {
    const auto import_result = marrow::runtime::import_spine_json_file(
        "assets/spine-examples/owl/owl-pro.json");
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return false;
    }

    const auto* imported_weights = find_nested_member(
        import_result.document->root,
        {"skins", "default", "attachments", "wood", "wood", "weights"});
    if (!expect(
            imported_weights != nullptr && imported_weights->is_array(),
            "owl wood mesh weights should import")) {
        return false;
    }

    bool ok = true;
    ok &= expect(
        imported_weights->as_array().size() > 20,
        "owl wood mesh should preserve its weighted vertices");
    for (std::size_t vertex_index = 0; vertex_index < imported_weights->as_array().size();
         ++vertex_index) {
        const auto& vertex_weights = imported_weights->as_array()[vertex_index];
        const std::string vertex_label =
            "owl wood vertex " + std::to_string(vertex_index) + " imported weights";
        ok &= expect(vertex_weights.is_array(), vertex_label + " should stay arrays");
        if (!vertex_weights.is_array()) {
            continue;
        }

        double total_weight = 0.0;
        for (std::size_t influence_index = 0; influence_index < vertex_weights.as_array().size();
             ++influence_index) {
            const auto& influence = vertex_weights.as_array()[influence_index];
            const std::string influence_label =
                vertex_label + "[" + std::to_string(influence_index) + "]";
            ok &= expect(influence.is_object(), influence_label + " should stay objects");
            if (!influence.is_object()) {
                continue;
            }

            const auto* weight_value = marrow::runtime::json::find_member(influence, "weight");
            ok &= expect(
                weight_value != nullptr && weight_value->is_number(),
                influence_label + " should keep a numeric weight");
            if (weight_value == nullptr || !weight_value->is_number()) {
                continue;
            }

            ok &= expect(
                weight_value->as_number() > 0.0,
                influence_label + " should prune zero weights");
            total_weight += weight_value->as_number();
        }
        ok &= expect_near(total_weight, 1.0, 1e-5, vertex_label + " should normalize to 1.0");
    }
    if (!ok) {
        return false;
    }

    const auto& vertex15 = imported_weights->as_array()[15].as_array();
    const auto& vertex16 = imported_weights->as_array()[16].as_array();
    const auto& vertex20 = imported_weights->as_array()[20].as_array();
    ok &= expect(
        vertex15.size() == 2,
        "owl wood vertex 15 should prune its zero-weight third influence");
    ok &= expect(
        vertex16.size() == 2,
        "owl wood vertex 16 should keep the near-zero influence after pruning zero");
    ok &= expect(
        vertex20.size() == 1,
        "owl wood vertex 20 should collapse to its remaining positive influence");
    if (!ok) {
        return false;
    }

    const auto* vertex15_weight0 = marrow::runtime::json::find_member(vertex15[0], "weight");
    const auto* vertex15_weight1 = marrow::runtime::json::find_member(vertex15[1], "weight");
    const auto* vertex16_weight0 = marrow::runtime::json::find_member(vertex16[0], "weight");
    const auto* vertex16_weight1 = marrow::runtime::json::find_member(vertex16[1], "weight");
    const auto* vertex20_weight0 = marrow::runtime::json::find_member(vertex20[0], "weight");
    if (!expect(
            vertex15_weight0 != nullptr && vertex15_weight1 != nullptr &&
                vertex16_weight0 != nullptr && vertex16_weight1 != nullptr &&
                vertex20_weight0 != nullptr,
            "owl targeted imported weights should expose numeric weight fields")) {
        return false;
    }

    const double vertex15_min_weight =
        std::min(vertex15_weight0->as_number(), vertex15_weight1->as_number());
    const double vertex15_max_weight =
        std::max(vertex15_weight0->as_number(), vertex15_weight1->as_number());
    const double vertex16_min_weight =
        std::min(vertex16_weight0->as_number(), vertex16_weight1->as_number());
    const double vertex16_max_weight =
        std::max(vertex16_weight0->as_number(), vertex16_weight1->as_number());
    ok &= expect_near(
        vertex15_min_weight,
        0.14373,
        1e-5,
        "owl wood vertex 15 should preserve the lighter positive influence");
    ok &= expect_near(
        vertex15_max_weight,
        0.85627,
        1e-5,
        "owl wood vertex 15 should preserve the dominant positive influence");
    ok &= expect_near(
        vertex16_min_weight,
        2.0e-5,
        1e-8,
        "owl wood vertex 16 should keep the near-zero positive influence");
    ok &= expect_near(
        vertex16_max_weight,
        0.99998,
        1e-5,
        "owl wood vertex 16 should preserve the dominant normalized weight");
    ok &= expect_near(
        vertex20_weight0->as_number(),
        1.0,
        1e-6,
        "owl wood vertex 20 should normalize to one rigid influence");
    if (!ok) {
        return false;
    }

    const auto load_result = marrow::runtime::load_skeleton_data(*import_result.document);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    const auto wood_slot_index = load_result.skeleton_data->find_slot_index("wood");
    const auto* wood_attachment =
        wood_slot_index.has_value()
        ? load_result.skeleton_data->find_attachment("default", *wood_slot_index, "wood")
        : nullptr;
    if (!expect(wood_slot_index.has_value(), "owl wood slot should load") ||
        !expect(wood_attachment != nullptr, "owl wood attachment should load") ||
        !expect(
            wood_attachment != nullptr && wood_attachment->mesh_geometry != nullptr,
            "owl wood attachment should keep mesh geometry")) {
        return false;
    }

    const auto& geometry = *wood_attachment->mesh_geometry;
    ok &= expect(
        geometry.weights.size() == imported_weights->as_array().size(),
        "owl wood runtime geometry should match the imported weight count");
    ok &= expect(
        geometry.weights[15].influences.size() == 2,
        "owl wood runtime vertex 15 should keep two influences");
    ok &= expect(
        geometry.weights[16].influences.size() == 2,
        "owl wood runtime vertex 16 should keep the near-zero influence");
    ok &= expect(
        geometry.weights[20].influences.size() == 1,
        "owl wood runtime vertex 20 should reduce to one influence");
    if (!ok) {
        return false;
    }

    marrow::runtime::Skeleton skeleton(load_result.skeleton_data);
    skeleton.bone_poses()[geometry.weights[15].influences[0].bone_index].local_pose.rotation += 18.0f;
    skeleton.bone_poses()[geometry.weights[15].influences[1].bone_index].local_pose.rotation -= 12.0f;
    skeleton.bone_poses()[geometry.weights[16].influences[1].bone_index].local_pose.rotation += 6.0f;
    skeleton.update_world_transforms();

    const auto posed_mesh = skeleton.evaluate_current_mesh_attachment(*wood_slot_index);
    if (!expect(
            posed_mesh.has_value() && posed_mesh->attachment_name == "wood",
            "owl wood mesh should pose after import") ||
        !expect(
            posed_mesh.has_value() && posed_mesh->vertices.size() == geometry.weights.size(),
            "owl wood posed mesh should keep the imported vertex count")) {
        return false;
    }

    const std::vector<marrow::runtime::BoneWorldTransform> world_transforms =
        skeleton.bone_world_transforms().materialize();
    const auto expected_vertex15 = blend_weighted_mesh_vertex(world_transforms, geometry.weights[15]);
    const auto expected_vertex16 = blend_weighted_mesh_vertex(world_transforms, geometry.weights[16]);
    const auto expected_vertex20 = blend_weighted_mesh_vertex(world_transforms, geometry.weights[20]);
    ok &= expect_near(
        posed_mesh->vertices[15].x,
        expected_vertex15.x,
        1e-5,
        "owl wood posed vertex 15 x");
    ok &= expect_near(
        posed_mesh->vertices[15].y,
        expected_vertex15.y,
        1e-5,
        "owl wood posed vertex 15 y");
    ok &= expect_near(
        posed_mesh->vertices[16].x,
        expected_vertex16.x,
        1e-5,
        "owl wood posed vertex 16 x");
    ok &= expect_near(
        posed_mesh->vertices[16].y,
        expected_vertex16.y,
        1e-5,
        "owl wood posed vertex 16 y");
    ok &= expect_near(
        posed_mesh->vertices[20].x,
        expected_vertex20.x,
        1e-5,
        "owl wood posed vertex 20 x");
    ok &= expect_near(
        posed_mesh->vertices[20].y,
        expected_vertex20.y,
        1e-5,
        "owl wood posed vertex 20 y");

    return ok;
}

bool verify_best_n_pruning_real_assets() {
    bool ok = true;
    ok &= verify_best_n_pruning_asset(PruneRegressionExpectation{
        "assets/spine-examples/spineboy/spineboy-pro.json",
        "default",
        "head",
        "head",
        1,
        0.06});
    ok &= verify_best_n_pruning_asset(PruneRegressionExpectation{
        "assets/spine-examples/raptor/raptor-pro.json",
        "default",
        "raptor-body",
        "raptor-body",
        12,
        0.02});
    return ok;
}

bool verify_tank_weighted_clipping_import() {
    const auto source_load_result = marrow::runtime::json::load_document(
        "assets/spine-examples/tank/tank-pro.json");
    if (!source_load_result) {
        std::cerr << source_load_result.error->format() << '\n';
        return false;
    }

    const auto* source_attachment = find_spine_source_attachment(
        *source_load_result.document,
        "default",
        "clipping",
        "clipping");
    if (!expect(source_attachment != nullptr, "tank clipping source attachment should exist")) {
        return false;
    }

    const auto source_polygon = decode_source_weighted_polygon(*source_attachment);
    if (!expect(
            source_polygon.has_value(),
            "tank clipping source attachment should decode as a weighted polygon")) {
        return false;
    }

    const auto raw_import_result = marrow::runtime::import_spine_json_file(
        "assets/spine-examples/tank/tank-pro.json");

    bool ok = true;
    ok &= expect(
        warnings_include_fragments(
            raw_import_result.warnings,
            {"clipping.clipping.vertices", "flattened weighted clipping polygon to static"}),
        "tank import should warn when flattening the weighted clipping polygon");
    if (raw_import_result.error.has_value()) {
        ok &= expect_not_contains(
            raw_import_result.error->message,
            "weighted polygon attachments are not supported",
            "tank import should not fail on the weighted clipping polygon");
    }
    if (!ok) {
        return false;
    }

    marrow::runtime::json::Document importable_document = *source_load_result.document;
    importable_document.root.as_object().erase("path");
    marrow::runtime::json::Value* skins =
        marrow::runtime::json::find_member(importable_document.root, "skins");
    bool removed_weighted_path_attachment = false;
    if (skins != nullptr && skins->is_array()) {
        for (auto& skin : skins->as_array()) {
            if (!skin.is_object()) {
                continue;
            }

            const auto* name = marrow::runtime::json::find_member(skin, "name");
            const std::string_view current_skin =
                name != nullptr && name->is_string() ? std::string_view(name->as_string())
                                                     : std::string_view("default");
            if (current_skin != "default") {
                continue;
            }

            marrow::runtime::json::Value* attachments =
                marrow::runtime::json::find_member(skin, "attachments");
            if (attachments == nullptr || !attachments->is_object()) {
                continue;
            }
            marrow::runtime::json::Value* slot_value =
                marrow::runtime::json::find_member(*attachments, "treads-path");
            if (slot_value == nullptr || !slot_value->is_object()) {
                continue;
            }

            removed_weighted_path_attachment =
                slot_value->as_object().erase("treads-path") > 0U;
        }
    }
    ok &= expect(
        removed_weighted_path_attachment,
        "tank regression fixture should remove the unrelated weighted path attachment");
    marrow::runtime::json::Value::Object stub_animations;
    stub_animations.emplace(
        "import_stub",
        marrow::runtime::json::Value(marrow::runtime::json::Value::Object{}, {}));
    importable_document.root.as_object()["animations"] =
        marrow::runtime::json::Value(std::move(stub_animations), {});
    if (!ok) {
        return false;
    }

    const auto import_result =
        marrow::runtime::import_spine_json_document(importable_document);
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return false;
    }

    const auto* imported_vertices = find_nested_member(
        import_result.document->root,
        {"skins", "default", "attachments", "clipping", "clipping", "vertices"});
    ok &= expect(
        imported_vertices != nullptr && imported_vertices->is_array(),
        "tank imported clipping vertices should be emitted");
    ok &= expect(
        imported_vertices != nullptr && imported_vertices->is_array() &&
            imported_vertices->as_array().size() == source_polygon->size() * 2U,
        "tank imported clipping vertices should flatten to static x/y pairs");
    if (!ok) {
        return false;
    }

    const auto load_result = marrow::runtime::load_skeleton_data(*import_result.document);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    const auto slot_index = load_result.skeleton_data->find_slot_index("clipping");
    const auto* imported_attachment = slot_index.has_value()
        ? load_result.skeleton_data->find_attachment("default", *slot_index, "clipping")
        : nullptr;
    ok &= expect(slot_index.has_value(), "tank clipping slot should import");
    ok &= expect(imported_attachment != nullptr, "tank clipping attachment should import");
    ok &= expect(
        imported_attachment != nullptr && imported_attachment->clipping_attachment.has_value(),
        "tank clipping attachment should expose runtime clipping data");
    ok &= expect(
        imported_attachment != nullptr && imported_attachment->clipping_attachment.has_value() &&
            imported_attachment->clipping_attachment->end_slot_name == "tank-glow",
        "tank clipping attachment should preserve the end slot");
    ok &= expect(
        imported_attachment != nullptr && imported_attachment->clipping_attachment.has_value() &&
            imported_attachment->clipping_attachment->polygon.size() == source_polygon->size(),
        "tank clipping attachment should keep the source vertex count");
    if (!ok) {
        return false;
    }

    marrow::runtime::Skeleton skeleton(load_result.skeleton_data);
    skeleton.update_world_transforms();
    const auto clip_pose = skeleton.evaluate_current_clipping_attachment(*slot_index);
    ok &= expect(
        clip_pose.has_value() && clip_pose->polygon.size() == source_polygon->size(),
        "tank clipping pose should evaluate with the flattened polygon");
    if (!ok) {
        return false;
    }

    const std::vector<marrow::runtime::BoneWorldTransform> world_transforms =
        skeleton.bone_world_transforms().materialize();
    for (std::size_t vertex_index = 0; vertex_index < source_polygon->size(); ++vertex_index) {
        double total_weight = 0.0;
        marrow::runtime::MeshWorldVertex expected_world;
        for (const SourceMeshInfluence& influence : (*source_polygon)[vertex_index]) {
            if (influence.weight <= 0.0) {
                continue;
            }

            const marrow::runtime::MeshWorldVertex transformed = transform_mesh_vertex(
                world_transforms[influence.bone_index],
                influence.x,
                influence.y);
            expected_world.x += transformed.x * influence.weight;
            expected_world.y += transformed.y * influence.weight;
            total_weight += influence.weight;
        }

        const std::string vertex_label =
            "tank weighted clipping vertex " + std::to_string(vertex_index);
        ok &= expect(total_weight > 0.0, vertex_label + " should preserve positive weight");
        if (total_weight <= 0.0) {
            continue;
        }

        expected_world.x /= total_weight;
        expected_world.y /= total_weight;
        ok &= expect_near(
            clip_pose->polygon[vertex_index].x,
            expected_world.x,
            0.2,
            vertex_label + " x");
        ok &= expect_near(
            clip_pose->polygon[vertex_index].y,
            expected_world.y,
            0.2,
            vertex_label + " y");
    }

    return ok;
}

bool verify_real_asset_constraint_timeline_warnings() {
    struct ConstraintTimelineWarningExpectation {
        std::filesystem::path source_path;
        std::string animation_path_fragment;
        std::string timeline_types_fragment;
    };

    const std::vector<ConstraintTimelineWarningExpectation> expectations{
        {
            "assets/spine-examples/spineboy/spineboy-pro.json",
            "$.animations.aim",
            "ik, transform",
        },
        {
            "assets/spine-examples/tank/tank-pro.json",
            "$.animations.drive",
            "path",
        },
        {
            "assets/spine-examples/raptor/raptor-pro.json",
            "$.animations.walk",
            "ik",
        },
    };

    bool ok = true;
    for (const auto& expectation : expectations) {
        const auto import_result =
            marrow::runtime::import_spine_json_file(expectation.source_path);
        const std::string label = expectation.source_path.filename().string();
        ok &= expect(import_result.document.has_value(), label + " should still import");
        ok &= expect(
            warnings_include_fragments(
                import_result.warnings,
                {
                    expectation.animation_path_fragment,
                    "skipped unsupported Spine constraint timelines",
                    expectation.timeline_types_fragment,
                }),
            label + " should warn about skipped constraint timelines");
        if (!import_result.document.has_value()) {
            continue;
        }

        const auto load_result = marrow::runtime::load_skeleton_data(*import_result.document);
        ok &= expect(load_result.skeleton_data != nullptr, label + " imported document should load");
        ok &= expect(
            load_result.skeleton_data != nullptr &&
                !load_result.skeleton_data->animations().empty(),
            label + " should keep its animations after skipping constraint timelines");
    }

    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path spine_path = resolve_json_fixture_path(argc, argv);
    const std::filesystem::path atlas_path = resolve_atlas_fixture_path(argc, argv);

    const auto import_result = marrow::runtime::import_spine_json_file(spine_path);
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return 1;
    }

    marrow::runtime::json::Document imported_document = *import_result.document;
    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() / "marrow_spine_import_sample.mskl";
    imported_document.source_path = output_path;
    if (!write_document(imported_document, output_path)) {
        return 1;
    }

    const auto load_result = marrow::runtime::load_skeleton_data(output_path);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return 1;
    }

    const auto& data = *load_result.skeleton_data;
    bool ok = true;

    ok &= expect(data.skins().size() == 1, "expected one imported skin");
    ok &= expect(data.animations().size() == 1, "expected one imported animation");
    ok &= expect(data.ik_constraints().size() == 1, "expected one imported IK constraint");
    ok &= expect(data.path_constraints().size() == 1, "expected one imported path constraint");
    ok &= expect(
        data.transform_constraints().size() == 1,
        "expected one imported transform constraint");

    const auto hip_index = data.find_bone_index("hip");
    const auto chest_index = data.find_bone_index("chest");
    const auto scaler_index = data.find_bone_index("scaler");
    const auto transform_source_index = data.find_bone_index("transform_source");
    const auto body_slot_index = data.find_slot_index("body_slot");
    const auto mesh_slot_index = data.find_slot_index("mesh_slot");
    const auto path_slot_index = data.find_slot_index("path_slot");
    ok &= expect(hip_index.has_value(), "expected hip bone");
    ok &= expect(chest_index.has_value(), "expected chest bone");
    ok &= expect(scaler_index.has_value(), "expected scaler bone");
    ok &= expect(transform_source_index.has_value(), "expected transform_source bone");
    ok &= expect(body_slot_index.has_value(), "expected body_slot");
    ok &= expect(mesh_slot_index.has_value(), "expected mesh_slot");
    ok &= expect(path_slot_index.has_value(), "expected path_slot");
    if (!ok) {
        return 1;
    }

    marrow::runtime::Skeleton skeleton(load_result.skeleton_data);
    skeleton.update_world_transforms();

    const auto& world_transforms = skeleton.bone_world_transforms();
    ok &= expect_near(world_transforms[*hip_index].world_x, 10.0, 1e-6, "hip world x");
    ok &= expect_near(world_transforms[*hip_index].world_y, 20.0, 1e-6, "hip world y");
    ok &= expect_near(world_transforms[*chest_index].world_x, 50.0, 1e-6, "chest world x");
    ok &= expect_near(world_transforms[*chest_index].world_y, 25.0, 1e-6, "chest world y");
    ok &= expect_near(world_transforms[*scaler_index].world_x, 30.0, 1e-6, "scaler world x");
    ok &= expect_near(world_transforms[*scaler_index].world_y, 15.0, 1e-6, "scaler world y");

    const auto* hero_region = data.find_attachment("default", *body_slot_index, "hero_region");
    const auto* weighted_mesh =
        data.find_attachment("default", *mesh_slot_index, "weighted_mesh");
    const auto* plain_mesh = data.find_attachment("default", *mesh_slot_index, "plain_mesh");
    ok &= expect(hero_region != nullptr, "expected imported region attachment");
    ok &= expect(weighted_mesh != nullptr, "expected imported weighted mesh attachment");
    ok &= expect(plain_mesh != nullptr, "expected imported unweighted mesh attachment");
    if (!ok) {
        return 1;
    }

    ok &= expect(
        hero_region->kind == marrow::runtime::AttachmentKind::Mesh,
        "region attachments should import as mesh geometry");
    ok &= expect(hero_region->region_name == "hero_body", "region path should be preserved");
    ok &= expect(
        hero_region->mesh_geometry != nullptr &&
            hero_region->mesh_geometry->vertices.size() == 8 &&
            hero_region->mesh_geometry->weights.size() == 4,
        "region mesh should contain four weighted quad vertices");
    ok &= expect(
        weighted_mesh->mesh_geometry != nullptr &&
            weighted_mesh->mesh_geometry->weights.size() == 4 &&
            weighted_mesh->mesh_geometry->weights.front().influences.size() == 2,
        "weighted mesh should decode two influences per vertex");
    ok &= expect(
        plain_mesh->mesh_geometry != nullptr &&
            plain_mesh->mesh_geometry->weights.size() == 4 &&
            plain_mesh->mesh_geometry->weights.front().influences.size() == 1,
        "unweighted mesh should import as one rigid influence per vertex");

    const auto& ik = data.ik_constraints().front();
    ok &= expect(ik.bone_indices.size() == 2, "IK constraint should target two bones");
    ok &= expect_near(ik.mix, 0.75, 1e-6, "IK mix");
    ok &= expect_near(ik.softness, 2.0, 1e-6, "IK softness");
    ok &= expect(!ik.bend_positive, "IK bendPositive should import");

    const auto& path = data.path_constraints().front();
    ok &= expect(path.slot_index == *path_slot_index, "path constraint target slot");
    ok &= expect(path.bone_indices.size() == 2, "path constraint should target two bones");
    ok &= expect_near(path.position, 0.25, 1e-6, "fixed path position should normalize");
    ok &= expect_near(path.spacing, 0.35, 1e-6, "path spacing");
    ok &= expect(
        path.spacing_mode == marrow::runtime::PathConstraintSpacingMode::Percent,
        "path spacing mode should import");
    ok &= expect_near(path.rotate_mix, 1.0, 1e-6, "path rotate mix");
    ok &= expect_near(path.translate_mix, 0.5, 1e-6, "path translate mix");

    const auto& transform = data.transform_constraints().front();
    ok &= expect(
        transform.source_bone_index == *transform_source_index,
        "transform constraint source bone");
    ok &= expect(
        transform.target_bone_indices.size() == 1,
        "transform constraint target bones");
    ok &= expect_near(transform.rotate_mix, 0.6, 1e-6, "transform rotate mix");
    ok &= expect_near(transform.translate_mix, 0.7, 1e-6, "transform translate mix");
    ok &= expect_near(transform.scale_mix, 0.8, 1e-6, "transform scale mix");
    ok &= expect_near(transform.shear_mix, 0.9, 1e-6, "transform shear mix");
    ok &= expect_near(transform.offsets.rotation, 10.0, 1e-6, "transform offset rotation");
    ok &= expect_near(transform.offsets.x, -5.0, 1e-6, "transform offset x");
    ok &= expect_near(transform.offsets.y, 6.0, 1e-6, "transform offset y");
    ok &= expect_near(transform.offsets.scale_x, 0.2, 1e-6, "transform offset scaleX");
    ok &= expect_near(transform.offsets.scale_y, -0.1, 1e-6, "transform offset scaleY");
    ok &= expect_near(transform.offsets.shear_y, 3.0, 1e-6, "transform offset shearY");

    const auto* animation = data.find_animation("idle");
    ok &= expect(animation != nullptr, "expected idle animation");
    if (!ok) {
        return 1;
    }

    const auto rotation = animation->sample_bone_rotation(*scaler_index, 0.5);
    const auto translation = animation->sample_bone_translation(*scaler_index, 0.5);
    const auto scale = animation->sample_bone_scale(*scaler_index, 0.5);
    const auto color = animation->sample_slot_color(*body_slot_index, 0.5);
    const auto attachment_keyframe = animation->sample_slot_attachment(*mesh_slot_index, 1.0);
    ok &= expect(rotation.has_value(), "expected scaler rotate timeline");
    ok &= expect(translation.has_value(), "expected scaler translate timeline");
    ok &= expect(scale.has_value(), "expected scaler scale timeline");
    ok &= expect(color.has_value(), "expected body slot color timeline");
    ok &= expect(attachment_keyframe != nullptr, "expected mesh slot attachment timeline");
    if (!ok) {
        return 1;
    }

    ok &= expect_near(*rotation, 20.0, 1e-6, "rotate timeline should add setup rotation");
    ok &= expect_near(translation->x, 35.0, 1e-6, "translate timeline should add setup x");
    ok &= expect_near(translation->y, 13.0, 1e-6, "translate timeline should add setup y");
    ok &= expect_near(scale->x, 1.32, 1e-6, "scale timeline should multiply setup scaleX");
    ok &= expect_near(scale->y, 0.72, 1e-6, "scale timeline should multiply setup scaleY");
    ok &= expect_near(color->r, 1.0, 1e-6, "color timeline red");
    ok &= expect_near(color->g, 0.0, 1e-6, "color timeline green");
    ok &= expect_near(color->b, 0.0, 1e-6, "color timeline blue");
    ok &= expect_near(color->a, 1.0, 1e-6, "color timeline alpha");
    ok &= expect(
        attachment_keyframe->attachment_name.has_value() &&
            *attachment_keyframe->attachment_name == "plain_mesh",
        "attachment timeline should preserve attachment names");

    skeleton.apply_animation(*animation, 1.0);
    const auto* current_attachment = skeleton.current_attachment(*mesh_slot_index);
    ok &= expect(current_attachment != nullptr, "runtime should resolve imported attachment");
    ok &= expect(
        current_attachment != nullptr && current_attachment->name == "plain_mesh",
        "runtime should switch to the imported attachment");
    ok &= expect_near(
        skeleton.bone_poses()[*scaler_index].local_pose.rotation,
        20.0,
        1e-6,
        "runtime should apply imported rotate timeline");
    ok &= expect_near(
        skeleton.bone_poses()[*scaler_index].local_pose.x,
        35.0,
        1e-6,
        "runtime should apply imported translate x");
    ok &= expect_near(
        skeleton.bone_poses()[*scaler_index].local_pose.scale_x,
        1.32,
        1e-6,
        "runtime should apply imported scale x");
    ok &= verify_curve_regression_fixture();
    ok &= verify_goblins_curve_import();
    ok &= verify_owl_zero_weight_mesh_import();
    ok &= verify_best_n_pruning_real_assets();
    ok &= verify_tank_weighted_clipping_import();
    ok &= verify_real_asset_constraint_timeline_warnings();
    if (!ok) {
        return 1;
    }

    const auto atlas_import_result = marrow::runtime::import_spine_atlas_file(atlas_path);
    if (!atlas_import_result) {
        std::cerr << atlas_import_result.error->format() << '\n';
        return 1;
    }

    ok &= expect(
        atlas_import_result.documents.size() == 2,
        "expected two imported atlas pages");
    if (!ok) {
        return 1;
    }

    const std::filesystem::path atlas_output_base =
        std::filesystem::temp_directory_path() / "marrow_spine_import_sample.matl";
    std::vector<std::filesystem::path> generated_atlas_paths;
    generated_atlas_paths.reserve(atlas_import_result.documents.size());
    for (std::size_t page_index = 0; page_index < atlas_import_result.documents.size();
         ++page_index) {
        const std::filesystem::path generated_path = atlas_output_path_for_page(
            atlas_output_base,
            atlas_import_result.documents[page_index],
            atlas_import_result.documents.size(),
            page_index);
        if (!write_document(atlas_import_result.documents[page_index], generated_path)) {
            return 1;
        }
        generated_atlas_paths.push_back(generated_path);
    }

    const std::vector<std::filesystem::path> expected_atlas_paths{
        "assets/fixtures/spine_import_sample_hero_page.matl",
        "assets/fixtures/spine_import_sample_fx_page.matl",
    };
    std::vector<std::shared_ptr<const marrow::runtime::AtlasData>> generated_atlas_pages;
    generated_atlas_pages.reserve(generated_atlas_paths.size());
    for (const auto& generated_path : generated_atlas_paths) {
        std::shared_ptr<const marrow::runtime::AtlasData> atlas_page;
        if (!load_atlas_model(generated_path, &atlas_page)) {
            return 1;
        }
        generated_atlas_pages.push_back(std::move(atlas_page));
    }

    for (std::size_t page_index = 0; page_index < expected_atlas_paths.size(); ++page_index) {
        std::shared_ptr<const marrow::runtime::AtlasData> expected_atlas_page;
        if (!load_atlas_model(expected_atlas_paths[page_index], &expected_atlas_page)) {
            return 1;
        }
        ok &= compare_atlas_models(
            *generated_atlas_pages[page_index],
            *expected_atlas_page,
            "atlas page " + std::to_string(page_index));
    }

    const auto* hero_body_region =
        find_region_across_pages(generated_atlas_pages, hero_region->region_name);
    const auto* weighted_region =
        find_region_across_pages(generated_atlas_pages, weighted_mesh->region_name);
    const auto* plain_region =
        find_region_across_pages(generated_atlas_pages, plain_mesh->region_name);
    ok &= expect(hero_body_region != nullptr, "expected hero_body atlas region lookup");
    ok &= expect(weighted_region != nullptr, "expected weighted_region atlas region lookup");
    ok &= expect(plain_region != nullptr, "expected plain_region atlas region lookup");
    if (!ok) {
        return 1;
    }

    ok &= expect_near(hero_body_region->origin_x, 20.0, 1e-6, "hero_body origin_x");
    ok &= expect_near(hero_body_region->origin_y, 12.0, 1e-6, "hero_body origin_y");
    ok &= expect_near(weighted_region->rotate_degrees, 90.0, 1e-6, "weighted_region rotate");
    ok &= expect_near(plain_region->origin_x, 8.0, 1e-6, "plain_region origin_x");
    ok &= expect_near(plain_region->origin_y, 6.0, 1e-6, "plain_region origin_y");
    if (!ok) {
        return 1;
    }

    std::cout << "Spine import smoke passed for " << spine_path.string() << '\n'
              << "Converted atlas pages: " << generated_atlas_paths[0].filename().string()
              << ", " << generated_atlas_paths[1].filename().string() << '\n'
              << "Resolved regions: " << hero_region->region_name << ", "
              << weighted_mesh->region_name << ", " << plain_mesh->region_name << '\n';
    return 0;
}
