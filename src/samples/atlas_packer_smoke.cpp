#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "atlas_packer.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"

namespace {

struct SpriteSpec {
    std::string region_name;
    std::filesystem::path image_path;
    int outer_width{0};
    int outer_height{0};
    int border_left{0};
    int border_top{0};
    int border_right{0};
    int border_bottom{0};
    std::array<std::uint8_t, 4> color{};
};

bool expect(bool condition, std::string_view message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

bool expect_near(double actual, double expected, double epsilon, std::string_view label) {
    if (std::abs(actual - expected) <= epsilon) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but was " << actual << '\n';
    return false;
}

std::optional<std::string> write_text_file(
    const std::filesystem::path& path,
    std::string_view text) {
    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(parent_path, directory_error);
        if (directory_error) {
            return directory_error.message();
        }
    }

    std::ofstream output(path);
    if (!output) {
        return "failed to open " + path.string();
    }

    output << text;
    if (!output) {
        return "failed to write " + path.string();
    }

    return std::nullopt;
}

std::vector<SpriteSpec> build_sprite_specs(const std::filesystem::path& sprites_directory) {
    constexpr int kSpriteCount = 24;
    std::vector<SpriteSpec> sprites;
    sprites.reserve(kSpriteCount);
    for (int index = 0; index < kSpriteCount; ++index) {
        SpriteSpec spec;
        spec.region_name = "sprite_" + std::to_string(index);
        spec.image_path = sprites_directory / (spec.region_name + ".png");
        spec.outer_width = 18 + ((index * 7) % 19) + (index % 2);
        spec.outer_height = 16 + ((index * 5) % 17) + ((index / 3) % 2);
        spec.border_left = 1 + (index % 3);
        spec.border_top = 1 + ((index + 1) % 3);
        spec.border_right = 1 + ((index + 2) % 3);
        spec.border_bottom = 1 + ((index + 3) % 3);
        spec.color = {
            static_cast<std::uint8_t>(40 + (index * 7) % 180),
            static_cast<std::uint8_t>(60 + (index * 11) % 160),
            static_cast<std::uint8_t>(80 + (index * 13) % 140),
            255U,
        };
        sprites.push_back(std::move(spec));
    }
    return sprites;
}

std::vector<std::uint8_t> make_sprite_pixels(const SpriteSpec& spec) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(spec.outer_width * spec.outer_height * 4),
        0U);
    const int opaque_min_x = spec.border_left;
    const int opaque_min_y = spec.border_top;
    const int opaque_max_x = spec.outer_width - spec.border_right;
    const int opaque_max_y = spec.outer_height - spec.border_bottom;
    for (int y = opaque_min_y; y < opaque_max_y; ++y) {
        for (int x = opaque_min_x; x < opaque_max_x; ++x) {
            const std::size_t pixel_index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(spec.outer_width) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[pixel_index + 0U] = spec.color[0];
            pixels[pixel_index + 1U] = spec.color[1];
            pixels[pixel_index + 2U] = spec.color[2];
            pixels[pixel_index + 3U] = spec.color[3];
        }
    }
    return pixels;
}

std::optional<std::string> write_source_sprites(const std::vector<SpriteSpec>& sprites) {
    for (const SpriteSpec& spec : sprites) {
        const std::vector<std::uint8_t> pixels = make_sprite_pixels(spec);
        if (const auto error = marrow::editor::detail::write_rgba_png(
                spec.image_path,
                spec.outer_width,
                spec.outer_height,
                pixels)) {
            return *error;
        }
    }

    return std::nullopt;
}

std::string build_base_skeleton_json(const std::vector<SpriteSpec>& sprites) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"marrow\": \"1.0\",\n"
           << "  \"version\": 1,\n"
           << "  \"skeleton\": {\n"
           << "    \"name\": \"atlas_pack_fixture\",\n"
           << "    \"width\": 512,\n"
           << "    \"height\": 512\n"
           << "  },\n"
           << "  \"bones\": [\n"
           << "    { \"name\": \"root\" }\n"
           << "  ],\n"
           << "  \"slots\": [\n";
    for (std::size_t index = 0; index < sprites.size(); ++index) {
        stream << "    { \"name\": \"slot_" << index << "\", \"bone\": \"root\", \"attachment\": \""
               << sprites[index].region_name << "\" }";
        stream << (index + 1U == sprites.size() ? "\n" : ",\n");
    }
    stream << "  ],\n"
           << "  \"animations\": {\n"
           << "    \"idle\": {}\n"
           << "  }\n"
           << "}\n";
    return stream.str();
}

const std::uint8_t* image_pixel(
    const marrow::renderer::TextureImage& image,
    int x,
    int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) *
        4U;
    return image.rgba8.data() + static_cast<std::ptrdiff_t>(offset);
}

bool validate_generated_regions(
    const std::vector<SpriteSpec>& sprites,
    const marrow::runtime::AtlasData& atlas,
    const marrow::renderer::TextureImage& image) {
    bool ok = true;
    for (const SpriteSpec& spec : sprites) {
        const auto* region = atlas.find_region(spec.region_name);
        ok &= expect(region != nullptr, "missing generated atlas region");
        if (region == nullptr) {
            continue;
        }

        const int trimmed_width =
            spec.outer_width - spec.border_left - spec.border_right;
        const int trimmed_height =
            spec.outer_height - spec.border_top - spec.border_bottom;
        ok &= expect_near(region->width, trimmed_width, 1e-6, "trimmed width");
        ok &= expect_near(region->height, trimmed_height, 1e-6, "trimmed height");
        ok &= expect_near(
            region->origin_x,
            (static_cast<double>(spec.outer_width) * 0.5) - spec.border_left,
            1e-6,
            "trimmed origin_x");
        ok &= expect_near(
            region->origin_y,
            (static_cast<double>(spec.outer_height) * 0.5) - spec.border_top,
            1e-6,
            "trimmed origin_y");

        const int region_x = static_cast<int>(region->x);
        const int region_y = static_cast<int>(region->y);
        const int sample_x = region_x + (trimmed_width / 2);
        const int sample_y = region_y + (trimmed_height / 2);
        const std::uint8_t* sample = image_pixel(image, sample_x, sample_y);
        ok &= expect(
            sample[0] == spec.color[0] &&
                sample[1] == spec.color[1] &&
                sample[2] == spec.color[2] &&
                sample[3] == spec.color[3],
            "packed atlas body pixel did not preserve source color");

        const std::uint8_t* left_bleed = image_pixel(image, region_x - 1, sample_y);
        const std::uint8_t* right_bleed =
            image_pixel(image, region_x + trimmed_width, sample_y);
        const std::uint8_t* top_bleed = image_pixel(image, sample_x, region_y - 1);
        const std::uint8_t* bottom_bleed =
            image_pixel(image, sample_x, region_y + trimmed_height);
        ok &= expect(
            left_bleed[0] == spec.color[0] &&
                right_bleed[1] == spec.color[1] &&
                top_bleed[2] == spec.color[2] &&
                bottom_bleed[3] == spec.color[3],
            "atlas bleed pixels did not extrude the sprite edge color");

        const std::uint8_t* left_padding = image_pixel(image, region_x - 2, sample_y);
        ok &= expect(
            left_padding[3] == 0U,
            "atlas padding pixel should stay transparent outside the 1px bleed");
    }

    for (std::size_t left_index = 0; left_index < sprites.size(); ++left_index) {
        const auto* left = atlas.find_region(sprites[left_index].region_name);
        if (left == nullptr) {
            continue;
        }
        const double left_min_x = left->x - 2.0;
        const double left_min_y = left->y - 2.0;
        const double left_max_x = left->x + left->width + 2.0;
        const double left_max_y = left->y + left->height + 2.0;
        for (std::size_t right_index = left_index + 1U; right_index < sprites.size(); ++right_index) {
            const auto* right = atlas.find_region(sprites[right_index].region_name);
            if (right == nullptr) {
                continue;
            }
            const double right_min_x = right->x - 2.0;
            const double right_min_y = right->y - 2.0;
            const double right_max_x = right->x + right->width + 2.0;
            const double right_max_y = right->y + right->height + 2.0;
            const bool intersects =
                left_min_x < right_max_x &&
                left_max_x > right_min_x &&
                left_min_y < right_max_y &&
                left_max_y > right_min_y;
            ok &= expect(!intersects, "atlas padding boxes overlapped");
        }
    }

    return ok;
}

} // namespace

int main() {
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_atlas_packer_smoke";
    std::error_code filesystem_error;
    std::filesystem::remove_all(temp_root, filesystem_error);
    filesystem_error.clear();
    std::filesystem::create_directories(temp_root / "sprites", filesystem_error);
    if (filesystem_error) {
        std::cerr << "failed to create " << temp_root.string() << '\n';
        return 1;
    }

    const std::vector<SpriteSpec> sprites = build_sprite_specs(temp_root / "sprites");
    if (const auto error = write_source_sprites(sprites)) {
        std::cerr << *error << '\n';
        return 1;
    }

    const std::filesystem::path source_skeleton_path = temp_root / "atlas_pack_source.mskl";
    if (const auto error =
            write_text_file(source_skeleton_path, build_base_skeleton_json(sprites))) {
        std::cerr << *error << '\n';
        return 1;
    }

    const std::filesystem::path project_path = temp_root / "atlas_pack_project.marrow";
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = project_path;
    project_options.skeleton_path = source_skeleton_path;
    project_options.atlas_paths = {temp_root / "packed_sprites.matl"};
    project_options.name = "Atlas Pack Smoke";
    project_options.notes =
        "Generated by marrow_atlas_packer_smoke for atlas export validation.";

    marrow::editor::ProjectData project = marrow::editor::create_minimal_project(project_options);
    marrow::editor::AtlasPackDefinition definition;
    definition.atlas_path = project.runtime_assets.atlas_paths.front();
    definition.atlas_name = "packed_sprites";
    for (const SpriteSpec& spec : sprites) {
        marrow::editor::AtlasPackSprite sprite;
        sprite.region_name = spec.region_name;
        sprite.image_path = std::filesystem::relative(spec.image_path, temp_root);
        definition.sprites.push_back(std::move(sprite));
    }
    project.atlas_pack_definitions.push_back(std::move(definition));

    const auto save_result = marrow::editor::save_project(project, project_path);
    if (!save_result) {
        std::cerr << save_result.error->format() << '\n';
        return 1;
    }

    const auto source_document_result = marrow::runtime::load_skeleton_document(source_skeleton_path);
    if (!source_document_result) {
        std::cerr << source_document_result.error->format() << '\n';
        return 1;
    }

    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = temp_root / "packed_export.mskl";
    const auto export_result = marrow::editor::export_runtime_assets(
        *save_result.project,
        *source_document_result.document,
        export_options);
    if (!export_result) {
        std::cerr << export_result.error->format() << '\n';
        return 1;
    }

    if (!expect(
            export_result.atlas_paths.size() == 1U &&
                export_result.texture_paths.size() == 1U,
            "atlas export did not emit exactly one .matl and one .png")) {
        return 1;
    }

    const auto atlas_result =
        marrow::runtime::AtlasLoader::load(export_result.atlas_paths.front());
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return 1;
    }

    const auto texture_result =
        marrow::renderer::load_png_texture_or_white(export_result.texture_paths.front());
    if (!expect(
            texture_result.loaded_from_file,
            "renderer fallback path was used instead of loading the generated atlas PNG")) {
        return 1;
    }

    if (!expect(
            atlas_result.atlas_data->regions().size() == sprites.size(),
            "generated atlas did not contain every packed region")) {
        return 1;
    }
    if (!expect(
            static_cast<int>(atlas_result.atlas_data->info().width) == texture_result.image.width &&
                static_cast<int>(atlas_result.atlas_data->info().height) == texture_result.image.height,
            "generated atlas metadata dimensions did not match the packed PNG")) {
        return 1;
    }

    if (!validate_generated_regions(
            sprites,
            *atlas_result.atlas_data,
            texture_result.image)) {
        return 1;
    }

    const auto exported_runtime_result =
        marrow::runtime::load_skeleton_data(export_result.path);
    if (!exported_runtime_result) {
        std::cerr << exported_runtime_result.error->format() << '\n';
        return 1;
    }

    marrow::runtime::Skeleton preview(exported_runtime_result.skeleton_data);
    preview.set_to_setup_pose();
    preview.update_world_transforms();
    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(preview, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return 1;
    }
    if (!expect(
            scene_result.scene->draw_commands.size() == sprites.size(),
            "renderer scene preparation did not include every packed region attachment")) {
        return 1;
    }
    const auto batch_summary = marrow::renderer::summarize_prepared_scene_batches(*scene_result.scene);
    if (!batch_summary) {
        std::cerr << *batch_summary.error_message << '\n';
        return 1;
    }

    const auto reloaded_project = marrow::editor::load_project(project_path);
    if (!reloaded_project) {
        std::cerr << reloaded_project.error->format() << '\n';
        return 1;
    }

    std::cout << "Packed " << sprites.size()
              << " sprite images into "
              << export_result.atlas_paths.front().string()
              << " and "
              << export_result.texture_paths.front().string()
              << '\n';
    std::cout << "Generated atlas size: "
              << atlas_result.atlas_data->info().width
              << " x "
              << atlas_result.atlas_data->info().height
              << '\n';
    std::cout << "Renderer draw commands: "
              << scene_result.scene->draw_commands.size()
              << '\n';
    return 0;
}
