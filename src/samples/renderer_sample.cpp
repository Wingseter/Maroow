#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

#include "marrow/renderer/module.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/profiler.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

bool require_near(double actual, double expected, std::string_view label) {
    constexpr double kTolerance = 1e-6;
    if (std::abs(actual - expected) <= kTolerance) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

bool require_near_with_tolerance(
    double actual,
    double expected,
    double tolerance,
    std::string_view label) {
    if (std::abs(actual - expected) <= tolerance) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

bool validate_attachment(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    std::string_view expected_slot_name,
    std::string_view expected_attachment_name,
    std::string_view expected_region_name,
    std::size_t expected_bone_index,
    double expected_min_x,
    double expected_min_y,
    double expected_max_x,
    double expected_max_y,
    double expected_u_min,
    double expected_v_min,
    double expected_u_max,
    double expected_v_max) {
    if (attachment.slot_name != expected_slot_name ||
        attachment.attachment_name != expected_attachment_name ||
        attachment.atlas_region_name != expected_region_name ||
        attachment.bone_index != expected_bone_index) {
        std::cerr << "Attachment identity mismatch for slot " << expected_slot_name << ".\n";
        return false;
    }

    return require_near(attachment.vertices[0].position.x, expected_min_x, "quad min x") &&
        require_near(attachment.vertices[0].position.y, expected_min_y, "quad min y") &&
        require_near(attachment.vertices[2].position.x, expected_max_x, "quad max x") &&
        require_near(attachment.vertices[2].position.y, expected_max_y, "quad max y") &&
        require_near(attachment.vertices[0].uv.x, expected_u_min, "uv min u") &&
        require_near(attachment.vertices[0].uv.y, expected_v_min, "uv min v") &&
        require_near(attachment.vertices[2].uv.x, expected_u_max, "uv max u") &&
        require_near(attachment.vertices[2].uv.y, expected_v_max, "uv max v");
}

bool require_color(
    const marrow::runtime::SlotColor& color,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    return require_near(color.r, expected_r, std::string(label) + " r") &&
        require_near(color.g, expected_g, std::string(label) + " g") &&
        require_near(color.b, expected_b, std::string(label) + " b") &&
        require_near(color.a, expected_a, std::string(label) + " a");
}

bool require_optional_color(
    const std::optional<marrow::runtime::SlotColor>& color,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    if (!color.has_value()) {
        std::cerr << label << " expected a color but none was provided.\n";
        return false;
    }

    return require_color(*color, expected_r, expected_g, expected_b, expected_a, label);
}

bool require_no_color(
    const std::optional<marrow::runtime::SlotColor>& color,
    std::string_view label) {
    if (!color.has_value()) {
        return true;
    }

    std::cerr << label << " expected no color but one was provided.\n";
    return false;
}

bool validate_skinning_influence(
    const marrow::renderer::GpuSkinningVertexPayload& payload,
    std::size_t influence_index,
    std::size_t expected_bone_index,
    double expected_x,
    double expected_y,
    double expected_weight,
    std::string_view label) {
    if (influence_index >= payload.influence_count) {
        std::cerr << label << " missing expected influence " << influence_index << ".\n";
        return false;
    }

    if (payload.bone_indices[influence_index] != expected_bone_index) {
        std::cerr << label << " expected bone " << expected_bone_index << " but got "
                  << payload.bone_indices[influence_index] << ".\n";
        return false;
    }

    return
        require_near(
            payload.bone_local_positions[influence_index].x,
            expected_x,
            std::string(label) + " x") &&
        require_near(
            payload.bone_local_positions[influence_index].y,
            expected_y,
            std::string(label) + " y") &&
        require_near(
            payload.bone_weights[influence_index],
            expected_weight,
            std::string(label) + " weight");
}

bool validate_skinned_vertex(
    const marrow::renderer::SkinnedMeshVertex& vertex,
    double expected_x,
    double expected_y,
    double expected_u,
    double expected_v,
    std::string_view label) {
    constexpr double kFloatSkinningTolerance = 1e-5;
    return
        require_near_with_tolerance(
            vertex.position.x,
            expected_x,
            kFloatSkinningTolerance,
            std::string(label) + " x") &&
        require_near_with_tolerance(
            vertex.position.y,
            expected_y,
            kFloatSkinningTolerance,
            std::string(label) + " y") &&
        require_near(vertex.uv.x, expected_u, std::string(label) + " u") &&
        require_near(vertex.uv.y, expected_v, std::string(label) + " v");
}

bool require_skinned_vertices_differ(
    const std::vector<marrow::renderer::SkinnedMeshVertex>& lhs,
    const std::vector<marrow::renderer::SkinnedMeshVertex>& rhs,
    std::string_view label) {
    if (lhs.size() != rhs.size()) {
        std::cerr << label << " expected matching vertex counts but compared " << lhs.size()
                  << " against " << rhs.size() << ".\n";
        return false;
    }

    constexpr double kTolerance = 1e-6;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::abs(lhs[index].position.x - rhs[index].position.x) > kTolerance ||
            std::abs(lhs[index].position.y - rhs[index].position.y) > kTolerance) {
            return true;
        }
    }

    std::cerr << label << " expected the animated mesh pose to move at least one vertex.\n";
    return false;
}

const marrow::renderer::RegionAttachmentDrawCommand* find_region_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::region_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }

    return nullptr;
}

const marrow::renderer::DynamicMeshDrawCommand* find_dynamic_mesh_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::dynamic_mesh_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }

    return nullptr;
}

const marrow::renderer::ClipAttachmentDrawCommand* find_clip_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& attachment : scene.clip_attachments) {
        if (attachment.slot_name == slot_name) {
            return &attachment;
        }
    }

    return nullptr;
}

bool require_masked_region_bounds(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    double expected_min_x,
    double expected_min_y,
    double expected_max_x,
    double expected_max_y,
    std::string_view label) {
    if (attachment.masked_vertices.empty()) {
        std::cerr << label << " expected masked vertices but none were generated.\n";
        return false;
    }

    double min_x = attachment.masked_vertices.front().position.x;
    double min_y = attachment.masked_vertices.front().position.y;
    double max_x = min_x;
    double max_y = min_y;
    for (const auto& vertex : attachment.masked_vertices) {
        min_x = std::min(min_x, vertex.position.x);
        min_y = std::min(min_y, vertex.position.y);
        max_x = std::max(max_x, vertex.position.x);
        max_y = std::max(max_y, vertex.position.y);
    }

    return require_near(min_x, expected_min_x, std::string(label) + " min x") &&
        require_near(min_y, expected_min_y, std::string(label) + " min y") &&
        require_near(max_x, expected_max_x, std::string(label) + " max x") &&
        require_near(max_y, expected_max_y, std::string(label) + " max y");
}

bool require_mask_matches_clip(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    const marrow::renderer::ClipAttachmentDrawCommand& clip_attachment,
    std::string_view label) {
    if (clip_attachment.polygon.empty()) {
        std::cerr << label << " expected a clip polygon but none was provided.\n";
        return false;
    }

    double min_x = clip_attachment.polygon.front().x;
    double min_y = clip_attachment.polygon.front().y;
    double max_x = min_x;
    double max_y = min_y;
    for (const auto& vertex : clip_attachment.polygon) {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }

    return require_masked_region_bounds(attachment, min_x, min_y, max_x, max_y, label);
}

struct DrawCommandCounts {
    std::size_t region_attachments{0};
    std::size_t dynamic_mesh_attachments{0};
};

DrawCommandCounts count_draw_commands(const marrow::renderer::PreparedScene& scene) {
    DrawCommandCounts counts;
    for (const auto& command : scene.draw_commands) {
        if (marrow::renderer::region_attachment_command(command) != nullptr) {
            ++counts.region_attachments;
            continue;
        }
        if (marrow::renderer::dynamic_mesh_attachment_command(command) != nullptr) {
            ++counts.dynamic_mesh_attachments;
        }
    }
    return counts;
}

bool require_event_sequence(
    const marrow::renderer::PreparedScene& scene,
    const std::vector<marrow::renderer::PreparedSceneEventKind>& expected_kinds,
    std::string_view label) {
    if (scene.ordered_events.size() != expected_kinds.size()) {
        std::cerr << label << " expected " << expected_kinds.size()
                  << " ordered events but got " << scene.ordered_events.size() << ".\n";
        return false;
    }

    for (std::size_t index = 0; index < expected_kinds.size(); ++index) {
        if (scene.ordered_events[index].kind != expected_kinds[index]) {
            std::cerr << label << " event[" << index << "] did not match the expected type.\n";
            return false;
        }
    }

    return true;
}

bool require_render_command_event_sequence(
    const marrow::renderer::RenderCommandList& command_list,
    const std::vector<marrow::renderer::RenderCommandEventKind>& expected_kinds,
    std::string_view label) {
    if (command_list.ordered_events.size() != expected_kinds.size()) {
        std::cerr << label << " expected " << expected_kinds.size()
                  << " ordered events but got " << command_list.ordered_events.size() << ".\n";
        return false;
    }

    for (std::size_t index = 0; index < expected_kinds.size(); ++index) {
        if (command_list.ordered_events[index].kind != expected_kinds[index]) {
            std::cerr << label << " event[" << index << "] did not match the expected type.\n";
            return false;
        }
    }

    return true;
}

bool require_primary_bone_index(
    const std::vector<marrow::renderer::RenderCommandVertex>& vertices,
    std::size_t expected_bone_index,
    std::string_view label) {
    if (vertices.empty()) {
        std::cerr << label << " expected at least one streamed vertex.\n";
        return false;
    }

    const std::size_t actual_bone_index =
        static_cast<std::size_t>(std::lround(vertices.front().bone_indices[0]));
    if (actual_bone_index != expected_bone_index) {
        std::cerr << label << " expected primary bone index " << expected_bone_index
                  << " but got " << actual_bone_index << ".\n";
        return false;
    }
    if (std::abs(vertices.front().bone_weights[0] - 1.0f) > 1e-6f) {
        std::cerr << label << " expected a rigid primary bone weight of 1.0.\n";
        return false;
    }

    return true;
}

void translate_prepared_scene(
    marrow::renderer::PreparedScene* scene,
    double offset_x,
    double offset_y) {
    for (auto& bone : scene->bone_palette) {
        bone.world_x += offset_x;
        bone.world_y += offset_y;
    }
    for (auto& clip_attachment : scene->clip_attachments) {
        for (auto& point : clip_attachment.polygon) {
            point.x += offset_x;
            point.y += offset_y;
        }
    }
    for (auto& command : scene->draw_commands) {
        if (auto* attachment = std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(&command)) {
            for (auto& vertex : attachment->vertices) {
                vertex.position.x += offset_x;
                vertex.position.y += offset_y;
            }
            for (auto& vertex : attachment->masked_vertices) {
                vertex.position.x += offset_x;
                vertex.position.y += offset_y;
            }
            continue;
        }

        auto* attachment = std::get_if<marrow::renderer::DynamicMeshDrawCommand>(&command);
        for (auto& vertex : attachment->masked_vertices) {
            vertex.position.x += offset_x;
            vertex.position.y += offset_y;
        }
    }
}

bool append_scene_instance(
    const marrow::renderer::PreparedScene& source,
    marrow::renderer::PreparedScene* destination) {
    if (destination->atlas_name.empty()) {
        destination->atlas_name = source.atlas_name;
        destination->atlas_image = source.atlas_image;
        destination->atlas_filter_min = source.atlas_filter_min;
        destination->atlas_filter_mag = source.atlas_filter_mag;
        destination->atlas_wrap_x = source.atlas_wrap_x;
        destination->atlas_wrap_y = source.atlas_wrap_y;
        destination->premultiplied_alpha = source.premultiplied_alpha;
        destination->skeleton_name = "batch_demo";
        destination->skeleton_count = 0U;
    } else if (destination->atlas_image != source.atlas_image) {
        std::cerr << "Cross-skeleton batching demo requires a shared atlas texture.\n";
        return false;
    } else if (destination->premultiplied_alpha != source.premultiplied_alpha) {
        std::cerr << "Cross-skeleton batching demo requires a shared PMA mode.\n";
        return false;
    }

    const std::size_t bone_offset = destination->bone_palette.size();
    const std::size_t clip_offset = destination->clip_attachments.size();
    const std::size_t draw_offset = destination->draw_commands.size();
    const std::size_t event_offset = destination->ordered_events.size();
    destination->bone_palette.insert(
        destination->bone_palette.end(),
        source.bone_palette.begin(),
        source.bone_palette.end());
    destination->skeleton_count += source.skeleton_count;

    for (const auto& source_clip : source.clip_attachments) {
        marrow::renderer::ClipAttachmentDrawCommand adjusted = source_clip;
        adjusted.draw_order_index += event_offset;
        destination->clip_attachments.push_back(std::move(adjusted));
    }

    for (const auto& source_command : source.draw_commands) {
        if (const auto* region = marrow::renderer::region_attachment_command(source_command)) {
            marrow::renderer::RegionAttachmentDrawCommand adjusted = *region;
            adjusted.bone_index += bone_offset;
            adjusted.draw_order_index += event_offset;
            destination->draw_commands.push_back(std::move(adjusted));
            continue;
        }

        const auto* mesh = marrow::renderer::dynamic_mesh_attachment_command(source_command);
        marrow::renderer::DynamicMeshDrawCommand adjusted = *mesh;
        adjusted.draw_order_index += event_offset;
        for (auto& payload : adjusted.vertex_payloads) {
            for (std::size_t influence_index = 0; influence_index < payload.influence_count;
                 ++influence_index) {
                payload.bone_indices[influence_index] += bone_offset;
            }
        }
        destination->draw_commands.push_back(std::move(adjusted));
    }

    for (const auto& event : source.ordered_events) {
        marrow::renderer::PreparedSceneEventRef adjusted = event;
        if (event.kind == marrow::renderer::PreparedSceneEventKind::Draw) {
            adjusted.index += draw_offset;
        } else {
            adjusted.index += clip_offset;
        }
        destination->ordered_events.push_back(adjusted);
    }

    return true;
}

bool require_batch_summary(
    const marrow::renderer::PreparedSceneBatchSummary& summary,
    std::size_t expected_draw_commands,
    std::size_t expected_draw_calls,
    std::size_t expected_merged_draw_calls,
    std::string_view label) {
    if (!summary) {
        std::cerr << label << " failed to summarize batches: "
                  << summary.error_message.value_or("unknown error") << ".\n";
        return false;
    }

    if (summary.draw_command_count != expected_draw_commands ||
        summary.draw_call_count != expected_draw_calls ||
        summary.merged_draw_calls != expected_merged_draw_calls) {
        std::cerr << label << " expected drawCommands=" << expected_draw_commands
                  << ", drawCalls=" << expected_draw_calls
                  << ", merged=" << expected_merged_draw_calls
                  << " but got drawCommands=" << summary.draw_command_count
                  << ", drawCalls=" << summary.draw_call_count
                  << ", merged=" << summary.merged_draw_calls << ".\n";
        return false;
    }

    return true;
}

std::filesystem::path resolve_atlas_image_path(
    const std::filesystem::path& atlas_path,
    const marrow::runtime::AtlasData& atlas) {
    const std::filesystem::path declared_image_path(atlas.info().image);
    return declared_image_path.is_absolute()
        ? declared_image_path.lexically_normal()
        : (atlas_path.parent_path() / declared_image_path).lexically_normal();
}

std::string format_pixel(const std::array<std::uint8_t, 4>& pixel) {
    return "(" + std::to_string(pixel[0]) + ", " + std::to_string(pixel[1]) + ", " +
        std::to_string(pixel[2]) + ", " + std::to_string(pixel[3]) + ")";
}

bool require_pixel_equals(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& expected,
    std::string_view label) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected " << format_pixel(expected) << " but got "
              << format_pixel(actual) << ".\n";
    return false;
}

bool require_pixels_differ(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& unexpected,
    std::string_view label) {
    if (actual != unexpected) {
        return true;
    }

    std::cerr << label << " expected different framebuffer results but both were "
              << format_pixel(actual) << ".\n";
    return false;
}

bool require_rgb_equals(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& expected,
    std::string_view label) {
    if (actual[0] == expected[0] && actual[1] == expected[1] && actual[2] == expected[2]) {
        return true;
    }

    std::cerr << label << " expected RGB "
              << "(" << static_cast<int>(expected[0]) << ", "
              << static_cast<int>(expected[1]) << ", "
              << static_cast<int>(expected[2]) << ")"
              << " but got "
              << "(" << static_cast<int>(actual[0]) << ", "
              << static_cast<int>(actual[1]) << ", "
              << static_cast<int>(actual[2]) << ").\n";
    return false;
}

double clamp_unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::array<double, 4> normalized_pixel(const std::array<std::uint8_t, 4>& pixel) {
    return {
        pixel[0] / 255.0,
        pixel[1] / 255.0,
        pixel[2] / 255.0,
        pixel[3] / 255.0,
    };
}

std::array<std::uint8_t, 4> sample_texture(
    const marrow::renderer::TextureImage& image,
    double u,
    double v) {
    if (image.width <= 0 || image.height <= 0 || image.rgba8.empty()) {
        return {0, 0, 0, 0};
    }

    const std::size_t x = static_cast<std::size_t>(std::lround(
        clamp_unit(u) * static_cast<double>(image.width - 1)));
    const std::size_t y = static_cast<std::size_t>(std::lround(
        clamp_unit(v) * static_cast<double>(image.height - 1)));
    const std::size_t offset =
        ((y * static_cast<std::size_t>(image.width)) + x) * 4U;
    return {
        image.rgba8[offset + 0],
        image.rgba8[offset + 1],
        image.rgba8[offset + 2],
        image.rgba8[offset + 3],
    };
}

std::array<std::uint8_t, 4> sample_region_attachment_center(
    const marrow::renderer::TextureImage& image,
    const marrow::renderer::RegionAttachmentDrawCommand& attachment) {
    const double center_u = (attachment.vertices[0].uv.x + attachment.vertices[2].uv.x) * 0.5;
    const double center_v = (attachment.vertices[0].uv.y + attachment.vertices[2].uv.y) * 0.5;
    return sample_texture(image, center_u, center_v);
}

std::array<std::uint8_t, 4> sample_dynamic_mesh_center(
    const marrow::renderer::TextureImage& image,
    const marrow::renderer::DynamicMeshDrawCommand& attachment) {
    if (attachment.vertex_payloads.empty()) {
        return {0, 0, 0, 0};
    }

    double sum_u = 0.0;
    double sum_v = 0.0;
    for (const auto& vertex : attachment.vertex_payloads) {
        sum_u += vertex.uv.x;
        sum_v += vertex.uv.y;
    }
    const double divisor = static_cast<double>(attachment.vertex_payloads.size());
    return sample_texture(image, sum_u / divisor, sum_v / divisor);
}

std::array<std::uint8_t, 4> premultiply_texel(const std::array<std::uint8_t, 4>& texel) {
    const double alpha = texel[3] / 255.0;
    return {
        static_cast<std::uint8_t>(std::lround((texel[0] / 255.0) * alpha * 255.0)),
        static_cast<std::uint8_t>(std::lround((texel[1] / 255.0) * alpha * 255.0)),
        static_cast<std::uint8_t>(std::lround((texel[2] / 255.0) * alpha * 255.0)),
        texel[3],
    };
}

std::array<double, 4> shader_output(
    const std::array<std::uint8_t, 4>& texel,
    const marrow::runtime::SlotColor& light_color,
    std::optional<marrow::runtime::SlotColor> dark_color,
    bool premultiplied_alpha) {
    const std::array<double, 4> normalized_texel = normalized_pixel(texel);
    const double alpha = normalized_texel[3] * light_color.a;
    std::array<double, 4> output{
        normalized_texel[0] * light_color.r,
        normalized_texel[1] * light_color.g,
        normalized_texel[2] * light_color.b,
        alpha,
    };
    if (!dark_color.has_value()) {
        if (premultiplied_alpha) {
            output[0] *= light_color.a;
            output[1] *= light_color.a;
            output[2] *= light_color.a;
        }
        return output;
    }

    const double u_pma = premultiplied_alpha ? 0.0 : 1.0;
    output[0] =
        ((u_pma + ((1.0 - u_pma) * normalized_texel[3])) - normalized_texel[0]) *
            dark_color->r +
        (normalized_texel[0] * light_color.r);
    output[1] =
        ((u_pma + ((1.0 - u_pma) * normalized_texel[3])) - normalized_texel[1]) *
            dark_color->g +
        (normalized_texel[1] * light_color.g);
    output[2] =
        ((u_pma + ((1.0 - u_pma) * normalized_texel[3])) - normalized_texel[2]) *
            dark_color->b +
        (normalized_texel[2] * light_color.b);
    return output;
}

double blend_factor_value(
    marrow::renderer::BlendFactor factor,
    const std::array<double, 4>& source,
    const std::array<double, 4>& destination,
    std::size_t component_index) {
    switch (factor) {
    case marrow::renderer::BlendFactor::Zero:
        return 0.0;
    case marrow::renderer::BlendFactor::One:
        return 1.0;
    case marrow::renderer::BlendFactor::SrcAlpha:
        return source[3];
    case marrow::renderer::BlendFactor::OneMinusSrcAlpha:
        return 1.0 - source[3];
    case marrow::renderer::BlendFactor::DstColor:
        return destination[component_index];
    case marrow::renderer::BlendFactor::OneMinusSrcColor:
        return 1.0 - source[component_index];
    }

    return 0.0;
}

std::array<std::uint8_t, 4> simulated_framebuffer_pixel(
    const std::array<std::uint8_t, 4>& texel,
    marrow::runtime::BlendMode blend_mode,
    std::optional<marrow::runtime::SlotColor> dark_color,
    bool premultiplied_alpha) {
    constexpr marrow::runtime::SlotColor kLightColor{0.6, 0.4, 0.2, 1.0};
    constexpr std::array<double, 4> kClearColor{{0.08, 0.09, 0.12, 1.0}};

    const std::array<double, 4> source =
        shader_output(texel, kLightColor, dark_color, premultiplied_alpha);
    const marrow::renderer::BlendState blend_state =
        marrow::renderer::blend_state_for(blend_mode, premultiplied_alpha);
    std::array<double, 4> blended{};
    for (std::size_t index = 0; index < blended.size(); ++index) {
        blended[index] =
            (source[index] * blend_factor_value(blend_state.src_factor, source, kClearColor, index)) +
            (kClearColor[index] *
             blend_factor_value(blend_state.dst_factor, source, kClearColor, index));
    }

    std::array<std::uint8_t, 4> pixel{};
    for (std::size_t index = 0; index < pixel.size(); ++index) {
        pixel[index] = static_cast<std::uint8_t>(std::lround(clamp_unit(blended[index]) * 255.0));
    }
    return pixel;
}

bool validate_framebuffer_blend_modes(const std::array<std::uint8_t, 4>& texel) {
    const marrow::runtime::SlotColor dark_color{0.1, 0.3, 0.5, 1.0};
    const auto pma_texel = premultiply_texel(texel);
    const auto normal_pixel =
        simulated_framebuffer_pixel(
            texel,
            marrow::runtime::BlendMode::Normal,
            std::nullopt,
            false);
    const auto additive_pixel =
        simulated_framebuffer_pixel(
            texel,
            marrow::runtime::BlendMode::Additive,
            std::nullopt,
            false);
    const auto two_color_normal_pixel =
        simulated_framebuffer_pixel(
            texel,
            marrow::runtime::BlendMode::Normal,
            dark_color,
            false);
    const auto screen_pixel =
        simulated_framebuffer_pixel(
            texel,
            marrow::runtime::BlendMode::Screen,
            dark_color,
            false);
    const auto pma_two_color_normal_pixel =
        simulated_framebuffer_pixel(
            pma_texel,
            marrow::runtime::BlendMode::Normal,
            dark_color,
            true);
    const marrow::renderer::BlendState straight_normal_blend =
        marrow::renderer::blend_state_for(marrow::runtime::BlendMode::Normal, false);
    const marrow::renderer::BlendState pma_normal_blend =
        marrow::renderer::blend_state_for(marrow::runtime::BlendMode::Normal, true);

    if (!require_pixels_differ(
            two_color_normal_pixel,
            normal_pixel,
            "two-color tint shader path") ||
        !require_pixels_differ(
            additive_pixel,
            normal_pixel,
            "additive blend mode smoke") ||
        !require_pixels_differ(
            screen_pixel,
            two_color_normal_pixel,
            "screen blend mode smoke") ||
        !require_rgb_equals(
            pma_two_color_normal_pixel,
            two_color_normal_pixel,
            "PMA vs straight-alpha two-color tint") ||
        straight_normal_blend.src_factor != marrow::renderer::BlendFactor::SrcAlpha ||
        straight_normal_blend.dst_factor != marrow::renderer::BlendFactor::OneMinusSrcAlpha ||
        pma_normal_blend.src_factor != marrow::renderer::BlendFactor::One ||
        pma_normal_blend.dst_factor != marrow::renderer::BlendFactor::OneMinusSrcAlpha) {
        if (straight_normal_blend.src_factor != marrow::renderer::BlendFactor::SrcAlpha ||
            straight_normal_blend.dst_factor != marrow::renderer::BlendFactor::OneMinusSrcAlpha ||
            pma_normal_blend.src_factor != marrow::renderer::BlendFactor::One ||
            pma_normal_blend.dst_factor != marrow::renderer::BlendFactor::OneMinusSrcAlpha) {
            std::cerr << "Renderer PMA blend-state selection did not match the expected GL factors.\n";
        }
        return false;
    }

    std::cout << "Framebuffer blend/two-color smoke passed: normal="
              << format_pixel(normal_pixel)
              << ", additive=" << format_pixel(additive_pixel)
              << ", normal+dark=" << format_pixel(two_color_normal_pixel)
              << ", screen+dark=" << format_pixel(screen_pixel)
              << ", pmaNormal+dark=" << format_pixel(pma_two_color_normal_pixel) << ".\n";
    return true;
}

constexpr std::array<std::uint8_t, 4> kBodyAtlasTexel{{224, 160, 96, 180}};
constexpr std::array<std::uint8_t, 4> kArmAtlasTexel{{96, 192, 240, 255}};
constexpr std::array<std::uint8_t, 4> kWarriorBodyAtlasTexel{{112, 208, 120, 196}};
constexpr std::array<std::uint8_t, 4> kSpark0AtlasTexel{{255, 224, 64, 200}};
constexpr std::array<std::uint8_t, 4> kSpark3AtlasTexel{{255, 96, 160, 96}};
constexpr std::array<std::uint8_t, 4> kWhiteTexel{{255, 255, 255, 255}};

struct Options {
    std::filesystem::path skeleton_path{"assets/fixtures/player_idle.mskl"};
    std::filesystem::path atlas_path{"assets/fixtures/player_idle.matl"};
    std::optional<int> auto_close_frames;
    bool skip_render{false};
    bool hud_overlay{false};
};

std::optional<Options> parse_options(int argc, char** argv) {
    Options options;
    std::size_t positional_index = 0;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--skip-render") {
            options.skip_render = true;
            continue;
        }
        if (argument == "--hud") {
            options.hud_overlay = true;
            continue;
        }
        if (argument == "--no-hud") {
            options.hud_overlay = false;
            continue;
        }
        if (argument == "--auto-close") {
            if (index + 1 >= argc) {
                std::cerr << "--auto-close requires a frame count.\n";
                return std::nullopt;
            }

            try {
                const int frame_count = std::stoi(argv[++index]);
                if (frame_count <= 0) {
                    std::cerr << "--auto-close frame count must be positive.\n";
                    return std::nullopt;
                }
                options.auto_close_frames = frame_count;
            } catch (const std::exception&) {
                std::cerr << "--auto-close requires a valid integer frame count.\n";
                return std::nullopt;
            }
            continue;
        }

        if (positional_index == 0) {
            options.skeleton_path = argument;
        } else if (positional_index == 1) {
            options.atlas_path = argument;
        } else {
            std::cerr << "Unexpected argument: " << argument << '\n';
            return std::nullopt;
        }
        ++positional_index;
    }

    return options;
}

bool filename_matches_any(
    const std::filesystem::path& path,
    std::initializer_list<std::string_view> expected_names) {
    const std::string filename = path.filename().string();
    for (const std::string_view expected_name : expected_names) {
        if (filename == expected_name) {
            return true;
        }
    }
    return false;
}

bool uses_checked_in_fixture_renderer_validation(
    const std::filesystem::path& skeleton_path,
    const std::filesystem::path& atlas_path) {
    return filename_matches_any(skeleton_path, {"player_idle.mskl", "player_idle.mbin"}) &&
        filename_matches_any(atlas_path, {"player_idle.matl"});
}

bool validate_generic_renderer_sample(
    const Options& options,
    const std::shared_ptr<const marrow::runtime::SkeletonData>& skeleton_data,
    const std::shared_ptr<const marrow::runtime::AtlasData>& atlas_data,
    const std::filesystem::path& atlas_image_path) {
    marrow::runtime::Skeleton skeleton(skeleton_data);
    skeleton.set_to_setup_pose();

    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return false;
    }

    const marrow::renderer::PreparedScene& scene = *scene_result.scene;
    if (scene.draw_commands.empty()) {
        std::cerr << "Prepared scene does not contain any setup-pose attachments to render.\n";
        return false;
    }
    if (scene.bone_palette.size() != skeleton_data->bones().size()) {
        std::cerr << "Prepared scene did not preserve the imported bone palette.\n";
        return false;
    }

    constexpr std::array<float, 16> kIdentityProjection{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    const marrow::renderer::RenderCommandListResult command_list_result =
        marrow::renderer::build_render_command_list(scene, kIdentityProjection);
    if (!command_list_result) {
        std::cerr << command_list_result.error_message << '\n';
        return false;
    }
    if (command_list_result.command_list->commands.empty()) {
        std::cerr << "Render command list did not contain any setup-pose draw calls.\n";
        return false;
    }

    const DrawCommandCounts draw_counts = count_draw_commands(scene);
    const marrow::renderer::PreparedSceneBatchSummary batch_summary =
        marrow::renderer::summarize_prepared_scene_batches(scene);
    if (!batch_summary) {
        std::cerr << batch_summary.error_message.value_or("Failed to summarize prepared scene batches.")
                  << '\n';
        return false;
    }

    std::cout << "Generic renderer sample prepared setup pose: skeleton="
              << scene.skeleton_name
              << ", bones=" << scene.bone_palette.size()
              << ", regions=" << draw_counts.region_attachments
              << ", meshes=" << draw_counts.dynamic_mesh_attachments
              << ", clips=" << scene.clip_attachments.size()
              << ", drawCommands=" << scene.draw_commands.size()
              << ", drawCalls=" << batch_summary.draw_call_count
              << ", atlas=" << scene.atlas_name << '\n';

    marrow::renderer::SampleAppWindow window;
    window.title = "Marrow Render Validation";
    window.width = 1280;
    window.height = 720;

    const marrow::renderer::DemoShell shell(
        window,
        scene,
        atlas_image_path,
        options.hud_overlay);

    if (options.skip_render) {
        std::cout << "Renderer startup skipped after generic setup-pose validation.\n";
        return true;
    }

    if (options.auto_close_frames.has_value()) {
        if (const std::optional<std::string> render_error =
                shell.run(options.auto_close_frames)) {
            std::cerr << *render_error << '\n';
            return false;
        }
        std::cout << "Auto-close smoke mode completed after presenting the atlas through sokol_gfx.\n";
        return true;
    }

    if (const std::optional<std::string> render_error = shell.run()) {
        std::cerr << *render_error << '\n';
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> options = parse_options(argc, argv);
    if (!options.has_value()) {
        return 1;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(options->skeleton_path);
    if (!skeleton_result) {
        std::cerr << skeleton_result.error->format() << '\n';
        return 1;
    }

    const auto atlas_result = marrow::runtime::AtlasLoader::load(options->atlas_path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return 1;
    }

    const std::filesystem::path atlas_image_path =
        resolve_atlas_image_path(options->atlas_path, *atlas_result.atlas_data);
    if (!uses_checked_in_fixture_renderer_validation(
            options->skeleton_path,
            options->atlas_path)) {
        return validate_generic_renderer_sample(
                   *options,
                   skeleton_result.skeleton_data,
                   atlas_result.atlas_data,
                   atlas_image_path)
            ? 0
            : 1;
    }

    if (atlas_result.atlas_data->info().premultiplied_alpha) {
        std::cerr << "The checked-in fixture atlas should stay in straight-alpha mode for MAR-062 coverage.\n";
        return 1;
    }

    const marrow::renderer::TextureImageLoadResult atlas_texture =
        marrow::renderer::load_png_texture_or_white(atlas_image_path);
    if (!atlas_texture.loaded_from_file) {
        std::cerr << atlas_texture.message << '\n';
        return 1;
    }
    if (atlas_texture.image.width != 256 || atlas_texture.image.height != 256) {
        std::cerr << "Atlas texture dimensions did not match the fixture metadata.\n";
        return 1;
    }
    std::cout << atlas_texture.message << '\n';

    const auto missing_texture = marrow::renderer::load_png_texture_or_white(
        atlas_image_path.parent_path() / "missing_fixture_texture.png");
    if (missing_texture.loaded_from_file ||
        missing_texture.image.width != 1 ||
        missing_texture.image.height != 1 ||
        !require_pixel_equals(
            sample_texture(missing_texture.image, 0.5, 0.5),
            kWhiteTexel,
            "missing atlas fallback texel")) {
        std::cerr << "Missing atlas texture did not fall back to a white texel.\n";
        return 1;
    }
    std::cout << "Missing atlas texture fallback validation passed.\n";

    const auto body_slot_index = skeleton_result.skeleton_data->find_slot_index("body");
    const auto spine_index = skeleton_result.skeleton_data->find_bone_index("spine");
    const auto arm_index = skeleton_result.skeleton_data->find_bone_index("arm_l");
    if (!body_slot_index.has_value() || !spine_index.has_value() || !arm_index.has_value()) {
        std::cerr << "Fixture did not preserve the indices needed for renderer mesh validation.\n";
        return 1;
    }
    // Sample after the slot swap so the renderer has to honor linked-mesh deform offsets
    // and weighted GPU skinning in the same draw command.
    constexpr double kAnimatedSampleTime = 0.75;

    marrow::runtime::Skeleton skeleton(skeleton_result.skeleton_data);
    skeleton.set_to_setup_pose();

    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& scene = *scene_result.scene;
    const DrawCommandCounts setup_counts = count_draw_commands(scene);
    if (scene.clip_attachments.size() != 1 ||
        setup_counts.region_attachments != 3 ||
        setup_counts.dynamic_mesh_attachments != 0) {
        std::cerr << "Expected 1 setup-pose clip attachment, 3 region attachments, and 0 dynamic meshes"
                  << " but prepared clips=" << scene.clip_attachments.size()
                  << ", regions=" << setup_counts.region_attachments
                  << ", meshes=" << setup_counts.dynamic_mesh_attachments << ".\n";
        return 1;
    }
    if (!require_event_sequence(
            scene,
            {
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::ClipStart,
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::ClipEnd,
            },
            "setup scene clip event ordering")) {
        return 1;
    }

    const auto* setup_body = find_region_attachment(scene, "body");
    const auto* setup_arm = find_region_attachment(scene, "arm_l");
    const auto* setup_spark = find_region_attachment(scene, "spark_fx");
    const auto* setup_clip = find_clip_attachment(scene, "fx_mask");
    if (setup_body == nullptr || setup_arm == nullptr ||
        setup_spark == nullptr || setup_clip == nullptr) {
        std::cerr << "Setup-pose scene did not preserve the expected body, arm, spark, and clip draw commands.\n";
        return 1;
    }

    const bool body_ok = validate_attachment(
        *setup_body,
        "body",
        "body",
        "body",
        1,
        -63.999995192,
        -30.0,
        63.999990821,
        130.0,
        0.0,
        0.0,
        0.5,
        0.625);
    const bool arm_ok = validate_attachment(
        *setup_arm,
        "arm_l",
        "arm_l",
        "arm_l",
        2,
        -45.999992466,
        -12.0,
        17.999994945,
        84.0,
        0.5,
        0.0,
        0.75,
        0.375);
    const bool spark_ok = validate_attachment(
        *setup_spark,
        "spark_fx",
        "spark_fx",
        "spark_fx_0",
        1,
        -16.000000787,
        34.0,
        15.999996416,
        66.0,
        0.5,
        0.75,
        0.625,
        0.875);
    if (!body_ok || !arm_ok || !spark_ok) {
        std::cerr << "Setup-pose region attachment placement did not match the fixture transforms.\n";
        return 1;
    }
    if (setup_body->blend_mode != marrow::runtime::BlendMode::Screen ||
        !require_color(
            setup_body->color,
            1.0,
            0.8,
            0.6,
            1.0,
            "setup body light color") ||
        !require_optional_color(
            setup_body->dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "setup body dark color") ||
        setup_arm->blend_mode != marrow::runtime::BlendMode::Normal ||
        !require_color(
            setup_arm->color,
            1.0,
            1.0,
            1.0,
            1.0,
            "setup arm light color") ||
        !require_no_color(setup_arm->dark_color, "setup arm dark color") ||
        setup_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        !require_mask_matches_clip(*setup_spark, *setup_clip, "setup spark clip") ||
        setup_spark->masked_indices.size() != 6 ||
        setup_clip->end_slot_name != "spark_fx" ||
        setup_clip->polygon.size() != 4 ||
        !require_near(setup_clip->polygon[0].x, -6.000001431, "clip polygon min x") ||
        !require_near(setup_clip->polygon[0].y, 40.0, "clip polygon min y") ||
        !require_near(setup_clip->polygon[2].x, 13.999997139, "clip polygon max x") ||
        !require_near(setup_clip->polygon[2].y, 60.0, "clip polygon max y")) {
        std::cerr << "Setup-pose slot blend mode or two-color tint did not propagate.\n";
        return 1;
    }

    const auto setup_body_texel = sample_region_attachment_center(atlas_texture.image, *setup_body);
    const auto setup_arm_texel = sample_region_attachment_center(atlas_texture.image, *setup_arm);
    const auto setup_spark_texel = sample_region_attachment_center(atlas_texture.image, *setup_spark);
    if (!require_pixel_equals(setup_body_texel, kBodyAtlasTexel, "setup body atlas sampling") ||
        !require_pixel_equals(setup_arm_texel, kArmAtlasTexel, "setup arm atlas sampling") ||
        !require_pixel_equals(setup_spark_texel, kSpark0AtlasTexel, "setup spark atlas sampling")) {
        std::cerr << "Setup-pose UVs did not sample the expected atlas regions.\n";
        return 1;
    }
    std::cout << "Setup-pose atlas texture sampling validation passed.\n";

    constexpr std::array<float, 16> kIdentityProjection{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    const marrow::renderer::RenderCommandListResult command_list_result =
        marrow::renderer::build_render_command_list(scene, kIdentityProjection);
    if (!command_list_result) {
        std::cerr << command_list_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::RenderCommandList& command_list =
        *command_list_result.command_list;
    if (command_list.commands.size() != 3 ||
        command_list.clip_commands.size() != 1 ||
        !require_render_command_event_sequence(
            command_list,
            {
                marrow::renderer::RenderCommandEventKind::Draw,
                marrow::renderer::RenderCommandEventKind::Draw,
                marrow::renderer::RenderCommandEventKind::ClipStart,
                marrow::renderer::RenderCommandEventKind::Draw,
                marrow::renderer::RenderCommandEventKind::ClipEnd,
            },
            "setup render command ordering") ||
        command_list.commands[0].blend_mode != marrow::runtime::BlendMode::Screen ||
        command_list.commands[0].shader_variant !=
            marrow::renderer::ColorShaderVariant::TwoColorTint ||
        command_list.commands[0].source_draw_command_offset != 0 ||
        command_list.commands[0].source_draw_command_count != 1 ||
        command_list.commands[1].blend_mode != marrow::runtime::BlendMode::Normal ||
        command_list.commands[1].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        command_list.commands[1].source_draw_command_offset != 1 ||
        command_list.commands[1].source_draw_command_count != 1 ||
        command_list.commands[2].blend_mode != marrow::runtime::BlendMode::Normal ||
        command_list.commands[2].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        command_list.commands[2].source_draw_command_offset != 2 ||
        command_list.commands[2].source_draw_command_count != 1 ||
        command_list.clip_commands[0].attachment_name != "fx_mask" ||
        command_list.clip_commands[0].source_clip_attachment_index != 0 ||
        !require_primary_bone_index(
            command_list.commands[2].vertices,
            setup_spark->bone_index,
            "setup spark render batch bone") ||
        !require_primary_bone_index(
            command_list.clip_commands[0].vertices,
            scene.bone_palette.size(),
            "setup clip render batch identity bone")) {
        std::cerr << "Render command list batching did not preserve the GPU clip event stream.\n";
        return 1;
    }
    std::cout << "Render command list clip batching validation passed.\n";

    marrow::renderer::SampleAppWindow window;
    window.title = "Marrow Render Validation";
    window.width = 1280;
    window.height = 720;

    const marrow::renderer::DemoShell shell(
        window,
        scene,
        atlas_image_path,
        options->hud_overlay);

    std::cout << shell.launch_report() << '\n';
    std::cout << "Setup-pose region attachment validation passed.\n";
    std::cout << "Interactive renderer sample includes a screen-blended body slot for visual blend-mode validation.\n";
    const marrow::renderer::PreparedSceneBatchSummary setup_batch_summary =
        marrow::renderer::summarize_prepared_scene_batches(scene);
    if (!require_batch_summary(setup_batch_summary, 3, 3, 0, "setup scene batching") ||
        setup_batch_summary.skeleton_count != 1U ||
        setup_batch_summary.batches.size() != 3 ||
        setup_batch_summary.batches[0].blend_mode != marrow::runtime::BlendMode::Screen ||
        setup_batch_summary.batches[0].shader_variant !=
            marrow::renderer::ColorShaderVariant::TwoColorTint ||
        setup_batch_summary.batches[0].draw_command_count != 1 ||
        setup_batch_summary.batches[1].blend_mode != marrow::runtime::BlendMode::Normal ||
        setup_batch_summary.batches[1].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        setup_batch_summary.batches[1].draw_command_count != 1 ||
        setup_batch_summary.break_reasons.texture_changes != 0U ||
        setup_batch_summary.break_reasons.blend_changes != 1U ||
        setup_batch_summary.break_reasons.clip_changes != 2U ||
        setup_batch_summary.batches[2].blend_mode != marrow::runtime::BlendMode::Normal ||
        setup_batch_summary.batches[2].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        setup_batch_summary.batches[2].draw_command_count != 1 ||
        setup_batch_summary.bone_uniform_count != scene.bone_palette.size() + 1) {
        std::cerr << "Setup-pose batch summary did not match the expected clip-aware draw-call layout.\n";
        return 1;
    }
    std::cout << "Setup-pose clip-aware batch validation passed: 3 draw commands -> "
              << setup_batch_summary.draw_call_count << " draw calls.\n";
    marrow::renderer::PreparedScene same_blend_variant_scene = scene;
    for (auto& command : same_blend_variant_scene.draw_commands) {
        if (auto* attachment = std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(&command);
            attachment != nullptr && attachment->slot_name == "body") {
            attachment->blend_mode = marrow::runtime::BlendMode::Normal;
        }
    }
    const marrow::renderer::PreparedSceneBatchSummary same_blend_variant_summary =
        marrow::renderer::summarize_prepared_scene_batches(same_blend_variant_scene);
    if (!require_batch_summary(
            same_blend_variant_summary,
            3,
            3,
            0,
            "same-blend shader variant batching") ||
        same_blend_variant_summary.batches.size() != 3 ||
        same_blend_variant_summary.batches[0].blend_mode != marrow::runtime::BlendMode::Normal ||
        same_blend_variant_summary.batches[0].shader_variant !=
            marrow::renderer::ColorShaderVariant::TwoColorTint ||
        same_blend_variant_summary.batches[0].draw_command_count != 1 ||
        same_blend_variant_summary.batches[1].blend_mode != marrow::runtime::BlendMode::Normal ||
        same_blend_variant_summary.batches[1].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        same_blend_variant_summary.batches[1].draw_command_count != 1 ||
        same_blend_variant_summary.batches[2].blend_mode != marrow::runtime::BlendMode::Normal ||
        same_blend_variant_summary.batches[2].shader_variant !=
            marrow::renderer::ColorShaderVariant::SingleColor ||
        same_blend_variant_summary.batches[2].draw_command_count != 1) {
        std::cerr << "Shared-blend batching did not preserve the single-color/two-color shader split.\n";
        return 1;
    }
    std::cout << "Shared normal-blend shader split validation passed: dark-tinted body stayed isolated from untinted slots.\n";
    if (!validate_framebuffer_blend_modes(setup_body_texel)) {
        std::cerr << "Framebuffer blend-mode smoke validation failed.\n";
        return 1;
    }

    skeleton.advance_attachment_playback(0.375);
    const auto sequence_scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!sequence_scene_result) {
        std::cerr << sequence_scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& sequence_scene = *sequence_scene_result.scene;
    const auto* sequence_spark = find_region_attachment(sequence_scene, "spark_fx");
    const auto* sequence_clip = find_clip_attachment(sequence_scene, "fx_mask");
    if (sequence_scene.clip_attachments.size() != 1 ||
        sequence_clip == nullptr ||
        sequence_spark == nullptr ||
        sequence_spark->atlas_region_name != "spark_fx_3" ||
        sequence_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        !require_mask_matches_clip(*sequence_spark, *sequence_clip, "sequence spark clip")) {
        std::cerr << "Sequence playback did not advance the clipped spark region through the renderer.\n";
        return 1;
    }

    const auto sequence_spark_texel =
        sample_region_attachment_center(atlas_texture.image, *sequence_spark);
    if (!require_pixel_equals(
            sequence_spark_texel,
            kSpark3AtlasTexel,
            "sequence spark atlas sampling")) {
        std::cerr << "Sequence playback did not sample the expected atlas frame.\n";
        return 1;
    }

    const marrow::renderer::DemoShell sequence_shell(
        window,
        sequence_scene,
        atlas_image_path,
        options->hud_overlay);
    std::cout << sequence_shell.launch_report() << '\n';
    std::cout << "Sequence attachment playback validation passed at t=0.375.\n";
    skeleton.set_attachment_playback_time(0.0);

    const marrow::runtime::AnimationData* idle_animation =
        skeleton_result.skeleton_data->find_animation("idle");
    if (idle_animation == nullptr) {
        std::cerr << "Fixture did not load the idle animation for renderer validation.\n";
        return 1;
    }

    constexpr double kAnimatedComparisonTime = 0.625;
    skeleton.apply_animation(*idle_animation, kAnimatedComparisonTime);
    const auto comparison_scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!comparison_scene_result) {
        std::cerr << comparison_scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& comparison_scene = *comparison_scene_result.scene;
    const auto* comparison_body = find_dynamic_mesh_attachment(comparison_scene, "body");
    if (comparison_body == nullptr) {
        std::cerr << "Comparison pose did not preserve the weighted mesh draw command.\n";
        return 1;
    }

    skeleton.apply_animation(*idle_animation, kAnimatedSampleTime);
    const marrow::runtime::AttachmentData* animated_body_attachment =
        skeleton.current_attachment(*body_slot_index);
    const auto animated_scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!animated_scene_result) {
        std::cerr << animated_scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& animated_scene = *animated_scene_result.scene;
    const DrawCommandCounts animated_counts = count_draw_commands(animated_scene);
    if (animated_scene.clip_attachments.size() != 1 ||
        animated_counts.region_attachments != 2 ||
        animated_counts.dynamic_mesh_attachments != 1) {
        std::cerr << "Animated body attachment was "
                  << (animated_body_attachment != nullptr
                          ? animated_body_attachment->name
                          : std::string("<null>"))
                  << " with mesh="
                  << (animated_body_attachment != nullptr &&
                              animated_body_attachment->mesh_geometry != nullptr
                          ? "yes"
                          : "no")
                  << ".\n";
        std::cerr << "Expected 1 animated clip attachment, 2 animated region attachments, and 1 dynamic mesh attachment but prepared "
                  << animated_scene.clip_attachments.size() << " clip attachments, "
                  << animated_counts.region_attachments << " region attachments and "
                  << animated_counts.dynamic_mesh_attachments << " dynamic mesh attachments.\n";
        return 1;
    }
    if (!require_event_sequence(
            animated_scene,
            {
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::ClipStart,
                marrow::renderer::PreparedSceneEventKind::Draw,
                marrow::renderer::PreparedSceneEventKind::ClipEnd,
            },
            "animated scene clip event ordering")) {
        return 1;
    }
    if (animated_scene.draw_commands.size() != 3 ||
        marrow::renderer::region_attachment_command(animated_scene.draw_commands[0]) == nullptr ||
        marrow::renderer::dynamic_mesh_attachment_command(animated_scene.draw_commands[1]) == nullptr ||
        marrow::renderer::region_attachment_command(animated_scene.draw_commands[2]) == nullptr) {
        std::cerr << "Animated prepared scene did not preserve a single interleaved draw-command list.\n";
        return 1;
    }

    const auto* draw_back = find_region_attachment(animated_scene, "arm_l");
    const auto* draw_spark = find_region_attachment(animated_scene, "spark_fx");
    const auto* draw_front = find_dynamic_mesh_attachment(animated_scene, "body");
    const auto* animated_clip = find_clip_attachment(animated_scene, "fx_mask");
    if (draw_back == nullptr || draw_spark == nullptr || draw_front == nullptr ||
        animated_clip == nullptr) {
        std::cerr << "Animated scene did not preserve the expected arm, spark, and body draw commands.\n";
        return 1;
    }

    if (draw_back->attachment_name != "arm_l" ||
        draw_back->atlas_region_name != "arm_l" ||
        draw_back->slot_index != 1 ||
        draw_back->bone_index != 2) {
        std::cerr << "Animated draw order did not place the arm slot first.\n";
        return 1;
    }
    if (!(draw_back->draw_order_index < draw_front->draw_order_index &&
          draw_front->draw_order_index < draw_spark->draw_order_index)) {
        std::cerr << "Animated draw commands did not preserve region/mesh interleaving order.\n";
        return 1;
    }
    if (draw_front->attachment_name != "warrior_body" ||
        draw_front->atlas_region_name != "warrior_body" ||
        draw_front->slot_index != *body_slot_index ||
        draw_front->blend_mode != marrow::runtime::BlendMode::Screen ||
        draw_front->vertex_buffer_usage != marrow::renderer::MeshBufferUsage::Static ||
        draw_front->deform_buffer_usage != marrow::renderer::MeshBufferUsage::Dynamic ||
        draw_front->shader_path != marrow::renderer::MeshShaderPath::GpuSkinning ||
        animated_body_attachment == nullptr ||
        !animated_body_attachment->linked_mesh.has_value() ||
        !animated_body_attachment->linked_mesh->deform) {
        std::cerr << "Animated weighted mesh did not use the GPU skinning draw path.\n";
        return 1;
    }
    if (draw_spark->atlas_region_name != "spark_fx_0" ||
        draw_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        draw_spark->masked_vertices.empty() ||
        draw_spark->masked_indices.empty()) {
        std::cerr << "Animated scene did not preserve the clipped spark sequence attachment.\n";
        return 1;
    }
    if (animated_scene.bone_palette.size() != skeleton.bone_world_transforms().size()) {
        std::cerr << "Prepared scene did not upload the current skeleton bone palette.\n";
        return 1;
    }
    if (draw_back->blend_mode != marrow::runtime::BlendMode::Normal ||
        !require_color(draw_back->color, 1.0, 1.0, 1.0, 1.0, "arm slot color") ||
        !require_no_color(draw_back->dark_color, "arm slot dark color") ||
        !require_color(draw_front->color, 0.6, 0.8, 1.0, 0.5, "body slot color") ||
        !require_optional_color(
            draw_front->dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "body slot dark color")) {
        std::cerr << "Animated slot blend mode or two-color tint did not propagate into renderer draw commands.\n";
        return 1;
    }
    if (draw_front->vertex_payloads.size() != 4 || draw_front->indices.size() != 6) {
        std::cerr << "Animated weighted mesh did not upload the expected dynamic mesh buffers.\n";
        return 1;
    }

    if (!require_pixel_equals(
            sample_dynamic_mesh_center(atlas_texture.image, *draw_front),
            kWarriorBodyAtlasTexel,
            "animated mesh atlas sampling")) {
        std::cerr << "Animated weighted mesh UVs did not sample the expected atlas region.\n";
        return 1;
    }

    const auto& top_left = draw_front->vertex_payloads[0];
    const auto& top_right = draw_front->vertex_payloads[1];
    const auto& bottom_right = draw_front->vertex_payloads[2];
    if (top_left.influence_count != 1 || top_right.influence_count != 2 ||
        bottom_right.influence_count != 2 ||
        !validate_skinning_influence(
            top_left,
            0,
            *spine_index,
            -64.0,
            -80.0,
            1.0,
            "top-left upload") ||
        !validate_skinning_influence(
            top_right,
            0,
            *spine_index,
            64.0,
            -80.0,
            0.75,
            "top-right upload spine") ||
        !validate_skinning_influence(
            top_right,
            1,
            *arm_index,
            94.0,
            -90.0,
            0.25,
            "top-right upload arm") ||
        !validate_skinning_influence(
            bottom_right,
            0,
            *spine_index,
            64.0,
            80.0,
            0.25,
            "bottom-right upload spine") ||
        !validate_skinning_influence(
            bottom_right,
            1,
            *arm_index,
            94.0,
            70.0,
            0.75,
            "bottom-right upload arm")) {
        std::cerr << "Renderer did not upload the authored weighted mesh influences.\n";
        return 1;
    }

    const marrow::renderer::GpuSkinningEvaluationResult skinned_result =
        marrow::renderer::evaluate_gpu_skinned_vertices(
            *draw_front,
            animated_scene.bone_palette);
    const marrow::renderer::GpuSkinningEvaluationResult comparison_skinned_result =
        marrow::renderer::evaluate_gpu_skinned_vertices(
            *comparison_body,
            comparison_scene.bone_palette);
    if (!skinned_result || !comparison_skinned_result) {
        std::cerr << (skinned_result.error_message.has_value()
                         ? *skinned_result.error_message
                         : *comparison_skinned_result.error_message)
                  << '\n';
        return 1;
    }

    const std::vector<marrow::renderer::SkinnedMeshVertex>& skinned_vertices =
        skinned_result.vertices;
    const std::vector<marrow::renderer::SkinnedMeshVertex>& comparison_skinned_vertices =
        comparison_skinned_result.vertices;
    const std::optional<marrow::runtime::MeshAttachmentPose> runtime_mesh =
        skeleton.evaluate_current_mesh_attachment(*body_slot_index);
    if (!runtime_mesh.has_value() || skinned_vertices.size() != runtime_mesh->vertices.size()) {
        std::cerr << "GPU skinning validation could not compare against the runtime mesh pose.\n";
        return 1;
    }
    if (!validate_skinned_vertex(
            skinned_vertices[0],
            runtime_mesh->vertices[0].x,
            runtime_mesh->vertices[0].y,
            0.0,
            0.625,
            "skinned vertex 0") ||
        !validate_skinned_vertex(
            skinned_vertices[1],
            runtime_mesh->vertices[1].x,
            runtime_mesh->vertices[1].y,
            0.5,
            0.625,
            "skinned vertex 1") ||
        !validate_skinned_vertex(
            skinned_vertices[2],
            runtime_mesh->vertices[2].x,
            runtime_mesh->vertices[2].y,
            0.5,
            1.0,
            "skinned vertex 2") ||
        !validate_skinned_vertex(
            skinned_vertices[3],
            runtime_mesh->vertices[3].x,
            runtime_mesh->vertices[3].y,
            0.0,
            1.0,
            "skinned vertex 3")) {
        std::cerr << "GPU skinning vertex evaluation did not match the runtime weighted mesh pose.\n";
        return 1;
    }
    if (!require_skinned_vertices_differ(
            comparison_skinned_vertices,
            skinned_vertices,
            "animated pose change")) {
        return 1;
    }

    const marrow::renderer::DemoShell animated_shell(
        window,
        animated_scene,
        atlas_image_path,
        options->hud_overlay);
    std::cout << animated_shell.launch_report() << '\n';
    const marrow::renderer::PreparedSceneBatchSummary animated_batch_summary =
        marrow::renderer::summarize_prepared_scene_batches(animated_scene);
    if (!require_batch_summary(animated_batch_summary, 3, 3, 0, "animated scene batching") ||
        animated_batch_summary.skeleton_count != 1U) {
        return 1;
    }
    std::cout << "Animated slot timeline presentation validation passed.\n";
    std::cout << "GPU-skinned weighted mesh validation passed.\n";
    std::cout << "GPU-skinned FFD deform timeline validation passed at t="
              << kAnimatedSampleTime << ".\n";

    marrow::renderer::PreparedScene batched_scene;
    constexpr std::size_t kBatchSkeletonCount = 3;
    for (std::size_t skeleton_index = 0; skeleton_index < kBatchSkeletonCount; ++skeleton_index) {
        marrow::runtime::Skeleton batch_skeleton(skeleton_result.skeleton_data);
        batch_skeleton.set_to_setup_pose();
        batch_skeleton.slot_states()[*body_slot_index].attachment_name.clear();
        const auto batched_scene_result =
            marrow::renderer::prepare_setup_pose_scene(batch_skeleton, *atlas_result.atlas_data);
        if (!batched_scene_result) {
            std::cerr << batched_scene_result.error_message << '\n';
            return 1;
        }

        marrow::renderer::PreparedScene translated_scene = *batched_scene_result.scene;
        translate_prepared_scene(
            &translated_scene,
            (static_cast<double>(skeleton_index) - 1.0) * 160.0,
            0.0);
        if (!append_scene_instance(translated_scene, &batched_scene)) {
            return 1;
        }
    }

    const marrow::renderer::PreparedSceneBatchSummary cross_skeleton_batch_summary =
        marrow::renderer::summarize_prepared_scene_batches(batched_scene);
    if (!require_batch_summary(
            cross_skeleton_batch_summary,
            6,
            6,
            0,
            "cross-skeleton batching") ||
        cross_skeleton_batch_summary.skeleton_count != kBatchSkeletonCount ||
        cross_skeleton_batch_summary.batches.size() != 6 ||
        cross_skeleton_batch_summary.batches.front().blend_mode !=
            marrow::runtime::BlendMode::Normal ||
        cross_skeleton_batch_summary.batches.front().draw_command_count != 1 ||
        cross_skeleton_batch_summary.batches.back().blend_mode !=
            marrow::runtime::BlendMode::Normal ||
        cross_skeleton_batch_summary.batches.back().draw_command_count != 1 ||
        cross_skeleton_batch_summary.vertex_buffer_bytes == 0 ||
        cross_skeleton_batch_summary.index_buffer_bytes == 0) {
        std::cerr << "Cross-skeleton batch summary did not preserve clip boundaries across instances.\n";
        return 1;
    }

    const marrow::renderer::DemoShell batching_shell(
        window,
        batched_scene,
        atlas_image_path,
        options->hud_overlay);
    std::cout << batching_shell.launch_report() << '\n';
    std::cout << "Cross-skeleton clip-aware batching validation passed: 6 draw commands from 3 skeletons stayed isolated across clip boundaries.\n";

    if (options->skip_render) {
        std::cout << "Renderer startup skipped after atlas texture validation.\n";
        return 0;
    }

    if (options->auto_close_frames.has_value()) {
        if (const std::optional<std::string> render_error =
                batching_shell.run(options->auto_close_frames)) {
            std::cerr << *render_error << '\n';
            return 1;
        }
        std::cout << "Auto-close smoke mode completed after presenting the atlas through sokol_gfx.\n";
        return 0;
    }

    if (const std::optional<std::string> render_error = batching_shell.run()) {
        std::cerr << *render_error << '\n';
        return 1;
    }

    return 0;
}
