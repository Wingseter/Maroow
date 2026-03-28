#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
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
