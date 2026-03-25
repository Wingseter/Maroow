#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

using marrow::runtime::AtlasData;
using marrow::runtime::AtlasLoader;
using marrow::runtime::AnimationState;
using marrow::runtime::AnimationStateEventType;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::Value;
using marrow::runtime::Skeleton;
using marrow::runtime::SkeletonData;
using marrow::runtime::TrackEntry;

struct StateEventRecord {
    AnimationStateEventType type;
    std::size_t track_index{0};
    std::string animation_name;
    std::optional<std::string> event_name;
};

int count_state_events(
    const std::vector<StateEventRecord>& records,
    AnimationStateEventType type,
    std::size_t track_index,
    std::string_view animation_name,
    std::optional<std::string_view> event_name = std::nullopt) {
    int count = 0;
    for (const StateEventRecord& record : records) {
        if (record.type != type ||
            record.track_index != track_index ||
            record.animation_name != animation_name) {
            continue;
        }
        if (event_name.has_value()) {
            if (!record.event_name.has_value() || *record.event_name != *event_name) {
                continue;
            }
        }

        ++count;
    }

    return count;
}

bool print_error(const LoadError& error) {
    std::cerr << error.format() << '\n';
    return false;
}

bool require_near(double actual, double expected, std::string_view label) {
    constexpr double kTolerance = 1e-6;
    if (std::abs(actual - expected) <= kTolerance) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

bool require_slot_color(
    const marrow::runtime::SlotColor& actual,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    return require_near(actual.r, expected_r, std::string(label) + " r") &&
        require_near(actual.g, expected_g, std::string(label) + " g") &&
        require_near(actual.b, expected_b, std::string(label) + " b") &&
        require_near(actual.a, expected_a, std::string(label) + " a");
}

bool require_optional_slot_color(
    const std::optional<marrow::runtime::SlotColor>& actual,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    if (!actual.has_value()) {
        std::cerr << label << " expected a color but none was present.\n";
        return false;
    }

    return require_slot_color(*actual, expected_r, expected_g, expected_b, expected_a, label);
}

bool require_missing_slot_color(
    const std::optional<marrow::runtime::SlotColor>& actual,
    std::string_view label) {
    if (!actual.has_value()) {
        return true;
    }

    std::cerr << label << " expected no color but one was present.\n";
    return false;
}

bool require_mesh_vertex(
    const marrow::runtime::MeshWorldVertex& actual,
    double expected_x,
    double expected_y,
    std::string_view label) {
    return require_near(actual.x, expected_x, std::string(label) + " x") &&
        require_near(actual.y, expected_y, std::string(label) + " y");
}

std::vector<std::size_t> sequential_draw_order(std::size_t slot_count) {
    std::vector<std::size_t> draw_order(slot_count);
    for (std::size_t index = 0; index < slot_count; ++index) {
        draw_order[index] = index;
    }
    return draw_order;
}

std::vector<std::size_t> swapped_front_draw_order(std::size_t slot_count) {
    std::vector<std::size_t> draw_order = sequential_draw_order(slot_count);
    if (draw_order.size() >= 2U) {
        std::swap(draw_order[0], draw_order[1]);
    }
    return draw_order;
}

bool require_attachment_vertex(
    const marrow::runtime::AttachmentVertex& actual,
    double expected_x,
    double expected_y,
    std::string_view label) {
    return require_near(actual.x, expected_x, std::string(label) + " x") &&
        require_near(actual.y, expected_y, std::string(label) + " y");
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
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights,
    double offset_x,
    double offset_y) {
    marrow::runtime::MeshWorldVertex world_vertex;
    for (const marrow::runtime::MeshGeometry::VertexWeight& influence : vertex_weights.influences) {
        const marrow::runtime::MeshWorldVertex transformed = transform_mesh_vertex(
            bone_world_transforms[influence.bone_index],
            influence.x + offset_x,
            influence.y + offset_y);
        world_vertex.x += transformed.x * influence.weight;
        world_vertex.y += transformed.y * influence.weight;
    }
    return world_vertex;
}

std::string object_path(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

bool require_non_empty_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path) {
    if (const auto error = marrow::runtime::json::require_type(
            document, array_value, Value::Type::Array, json_path)) {
        return print_error(*error);
    }

    if (array_value.as_array().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            array_value.location(),
            std::string(json_path),
            "array must not be empty"));
    }

    return true;
}

bool validate_fixture_skeleton(const Document& document) {
    const Value& root = document.root;
    if (const auto error = marrow::runtime::json::require_type(
            document, root, Value::Type::Object, "$")) {
        return print_error(*error);
    }

    const Value* skeleton = nullptr;
    const Value* bones = nullptr;
    const Value* slots = nullptr;
    const Value* skins = nullptr;
    const Value* animations = nullptr;
    const Value* events = nullptr;
    const Value* mixing = nullptr;

    if (const auto error = marrow::runtime::json::require_member(
            document, root, "marrow", Value::Type::String, "$")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "skeleton", Value::Type::Object, "$", &skeleton)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "bones", Value::Type::Array, "$", &bones)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "slots", Value::Type::Array, "$", &slots)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "skins", Value::Type::Object, "$", &skins)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "animations", Value::Type::Object, "$", &animations)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "events", Value::Type::Object, "$", &events)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "mixing", Value::Type::Object, "$", &mixing)) {
        return print_error(*error);
    }

    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "name", Value::Type::String, "$.skeleton")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "width", Value::Type::Number, "$.skeleton")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "height", Value::Type::Number, "$.skeleton")) {
        return print_error(*error);
    }

    if (!require_non_empty_array(document, *bones, "$.bones")) {
        return false;
    }
    for (std::size_t index = 0; index < bones->as_array().size(); ++index) {
        const Value& bone = bones->as_array()[index];
        const std::string path = object_path("$.bones", index);
        if (const auto error = marrow::runtime::json::require_type(
                document, bone, Value::Type::Object, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, bone, "name", Value::Type::String, path)) {
            return print_error(*error);
        }
    }

    if (!require_non_empty_array(document, *slots, "$.slots")) {
        return false;
    }
    for (std::size_t index = 0; index < slots->as_array().size(); ++index) {
        const Value& slot = slots->as_array()[index];
        const std::string path = object_path("$.slots", index);
        if (const auto error = marrow::runtime::json::require_type(
                document, slot, Value::Type::Object, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "name", Value::Type::String, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "bone", Value::Type::String, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "attachment", Value::Type::String, path)) {
            return print_error(*error);
        }
    }

    if (animations->as_object().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            animations->location(),
            "$.animations",
            "object must not be empty"));
    }
    if (skins->as_object().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            skins->location(),
            "$.skins",
            "object must not be empty"));
    }
    if (events->as_object().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            events->location(),
            "$.events",
            "object must not be empty"));
    }

    bool has_point_attachment = false;
    bool has_bounding_box_attachment = false;
    bool has_clipping_attachment = false;
    bool has_sequence_attachment = false;
    for (const auto& [skin_name, skin_value] : skins->as_object()) {
        for (const auto& [slot_name, attachment_value] : skin_value.as_object()) {
            (void)skin_name;
            (void)slot_name;
            if (!attachment_value.is_object()) {
                continue;
            }

            const Value* type_value = marrow::runtime::json::find_member(attachment_value, "type");
            if (type_value == nullptr || !type_value->is_string()) {
                continue;
            }

            if (type_value->as_string() == "point") {
                has_point_attachment = true;
            } else if (type_value->as_string() == "bounding_box") {
                has_bounding_box_attachment = true;
            } else if (type_value->as_string() == "clipping") {
                has_clipping_attachment = true;
            } else if (type_value->as_string() == "sequence") {
                has_sequence_attachment = true;
            }
        }
    }
    if (!has_point_attachment || !has_bounding_box_attachment ||
        !has_clipping_attachment || !has_sequence_attachment) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            skins->location(),
            "$.skins",
            "fixture must include point, bounding-box, clipping, and sequence attachments"));
    }

    if (const auto error = marrow::runtime::json::require_member(
            document, *mixing, "default_mix", Value::Type::Number, "$.mixing")) {
        return print_error(*error);
    }
    const Value* mix_entries = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, *mixing, "entries", Value::Type::Array, "$.mixing", &mix_entries)) {
        return print_error(*error);
    }
    if (!require_non_empty_array(document, *mix_entries, "$.mixing.entries")) {
        return false;
    }

    for (const auto& [event_name, event_value] : events->as_object()) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                event_value,
                Value::Type::Object,
                std::string("$.events.") + event_name)) {
            return print_error(*error);
        }
    }

    bool found_event_timeline = false;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                animation_value,
                Value::Type::Object,
                std::string("$.animations.") + animation_name)) {
            return print_error(*error);
        }

        if (const Value* animation_events =
                marrow::runtime::json::find_member(animation_value, "events")) {
            if (!require_non_empty_array(
                    document,
                    *animation_events,
                    std::string("$.animations.") + animation_name + ".events")) {
                return false;
            }
            found_event_timeline = true;
        }
    }
    if (!found_event_timeline) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            animations->location(),
            "$.animations",
            "at least one animation must define an events timeline"));
    }

    std::cout << "Loaded skeleton fixture: " << document.source_path.string() << '\n'
              << "  bones=" << bones->as_array().size() << ", slots=" << slots->as_array().size()
              << ", skins=" << skins->as_object().size()
              << ", events=" << events->as_object().size()
              << ", animations=" << animations->as_object().size()
              << ", mixEntries=" << mix_entries->as_array().size() << '\n';
    return true;
}

std::optional<Document> load_document_or_print(const std::filesystem::path& path) {
    const auto result = marrow::runtime::load_skeleton_document(path);
    if (!result) {
        print_error(*result.error);
        return std::nullopt;
    }
    return *result.document;
}

bool validate_runtime_slot_presentation_model(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto body_slot_index = skeleton_data->find_slot_index("body");
    const auto arm_slot_index = skeleton_data->find_slot_index("arm_l");
    if (!body_slot_index.has_value() || !arm_slot_index.has_value()) {
        std::cerr << "Fixture lost the slot indices needed for presentation validation.\n";
        return false;
    }

    const auto& body_slot = skeleton_data->slots()[*body_slot_index];
    const auto& arm_slot = skeleton_data->slots()[*arm_slot_index];
    if (body_slot.blend_mode != marrow::runtime::BlendMode::Screen ||
        !require_slot_color(body_slot.color, 1.0, 0.8, 0.6, 1.0, "body setup light color") ||
        !require_optional_slot_color(
            body_slot.dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "body setup dark color") ||
        arm_slot.blend_mode != marrow::runtime::BlendMode::Normal ||
        !require_slot_color(arm_slot.color, 1.0, 1.0, 1.0, 1.0, "arm setup light color") ||
        !require_missing_slot_color(arm_slot.dark_color, "arm setup dark color")) {
        std::cerr << "SkeletonData did not preserve slot blend mode or two-color tint defaults.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    skeleton.set_to_setup_pose();
    if (!require_slot_color(
            skeleton.slot_states()[*body_slot_index].color,
            1.0,
            0.8,
            0.6,
            1.0,
            "body state light color") ||
        !require_optional_slot_color(
            skeleton.slot_states()[*body_slot_index].dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "body state dark color") ||
        !require_slot_color(
            skeleton.slot_states()[*arm_slot_index].color,
            1.0,
            1.0,
            1.0,
            1.0,
            "arm state light color") ||
        !require_missing_slot_color(
            skeleton.slot_states()[*arm_slot_index].dark_color,
            "arm state dark color")) {
        std::cerr << "Skeleton setup pose did not apply slot presentation defaults.\n";
        return false;
    }

    std::cout << "Loaded runtime slot presentation defaults: blend="
              << "screen"
              << ", light=(" << body_slot.color.r << ", " << body_slot.color.g << ", "
              << body_slot.color.b << ", " << body_slot.color.a << ")"
              << ", dark=(" << body_slot.dark_color->r << ", " << body_slot.dark_color->g
              << ", " << body_slot.dark_color->b << ", " << body_slot.dark_color->a << ")\n";
    return true;
}

bool validate_runtime_slot_blend_modes() {
    constexpr std::string_view kBlendModeFixture = R"({
  "marrow": "1.0",
  "skeleton": {
    "name": "blend_modes",
    "width": 64,
    "height": 64
  },
  "bones": [
    { "name": "root" }
  ],
  "slots": [
    { "name": "normal", "bone": "root", "attachment": "body", "blend": "normal" },
    { "name": "add", "bone": "root", "attachment": "body", "blend": "additive" },
    { "name": "multiply", "bone": "root", "attachment": "body", "blend": "multiply" },
    {
      "name": "screen",
      "bone": "root",
      "attachment": "body",
      "blend": "screen",
      "color": "ffcc99ff",
      "dark": "336699ff"
    }
  ],
  "animations": {
    "idle": {}
  }
})";

    const auto document_result =
        marrow::runtime::json::parse_document(kBlendModeFixture, "<blend-mode-fixture>");
    if (!document_result) {
        return print_error(*document_result.error);
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(*document_result.document);
    if (!skeleton_result) {
        return print_error(*skeleton_result.error);
    }

    const auto& slots = skeleton_result.skeleton_data->slots();
    if (slots.size() != 4 ||
        slots[0].blend_mode != marrow::runtime::BlendMode::Normal ||
        slots[1].blend_mode != marrow::runtime::BlendMode::Additive ||
        slots[2].blend_mode != marrow::runtime::BlendMode::Multiply ||
        slots[3].blend_mode != marrow::runtime::BlendMode::Screen ||
        !require_slot_color(slots[3].color, 1.0, 0.8, 0.6, 1.0, "inline screen light color") ||
        !require_optional_slot_color(
            slots[3].dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "inline screen dark color")) {
        std::cerr << "Inline slot presentation fixture did not preserve all supported blend modes.\n";
        return false;
    }

    std::cout << "Loaded supported slot blend modes: normal, additive, multiply, screen.\n";
    return true;
}

bool validate_runtime_skeleton_model(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    Skeleton skeleton(skeleton_data);
    const auto* idle = skeleton_data->find_animation("idle");
    const auto* attack = skeleton_data->find_animation("attack");
    const auto* aim = skeleton_data->find_animation("aim");

    if (skeleton_data->bones().size() != 16 || skeleton_data->slots().size() != 7 ||
        skeleton_data->animations().size() != 3 || idle == nullptr || attack == nullptr || aim == nullptr) {
        std::cerr << "SkeletonData did not preserve fixture counts.\n";
        return false;
    }

    if (skeleton_data->bones()[1].parent_index != std::optional<std::size_t>{0} ||
        skeleton_data->slots()[0].bone_index != 1 ||
        idle->targeted_bone_indices != std::vector<std::size_t>{0, 1} ||
        attack->targeted_bone_indices != std::vector<std::size_t>{2} ||
        aim->targeted_bone_indices != std::vector<std::size_t>{2}) {
        std::cerr << "SkeletonData relationships do not match the fixture.\n";
        return false;
    }

    if (!require_near(skeleton_data->default_mix_duration(), 0.2, "default mix duration") ||
        skeleton_data->mix_definitions().size() != 1 ||
        !require_near(skeleton_data->mix_duration("attack", "aim"), 0.1, "attack->aim mix") ||
        !require_near(skeleton_data->mix_duration("idle", "aim"), 0.1, "wildcard -> aim mix")) {
        std::cerr << "SkeletonData mixing metadata did not preserve the fixture defaults.\n";
        return false;
    }

    if (skeleton.bone_poses().size() != skeleton_data->bones().size() ||
        skeleton.bone_world_transforms().size() != skeleton_data->bones().size() ||
        skeleton.slot_states().size() != skeleton_data->slots().size() ||
        skeleton.draw_order().size() != skeleton_data->slots().size()) {
        std::cerr << "Skeleton instance state does not match SkeletonData sizes.\n";
        return false;
    }

    if (!require_near(skeleton.bone_world_transforms()[1].world_x, 0.0, "spine setup world_x") ||
        !require_near(skeleton.bone_world_transforms()[1].world_y, 50.0, "spine setup world_y") ||
        !require_near(skeleton.bone_world_transforms()[2].world_x, -30.0, "arm_l setup world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 60.0, "arm_l setup world_y")) {
        std::cerr << "Setup-pose hierarchy evaluation did not match the fixture chain.\n";
        return false;
    }

    skeleton.bone_poses()[0].local_pose.x = 10.0;
    skeleton.bone_poses()[1].local_pose.y = 72.0;
    skeleton.slot_states()[0].attachment_name = "debug_attachment";
    skeleton.draw_order()[0] = 1;
    skeleton.draw_order()[1] = 0;
    skeleton.update_world_transforms();

    if (!require_near(skeleton.bone_world_transforms()[2].world_x, -20.0, "arm_l mutated world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 82.0, "arm_l mutated world_y")) {
        std::cerr << "Mutated hierarchy evaluation did not propagate parent transforms.\n";
        return false;
    }

    skeleton.set_to_setup_pose();

    const std::vector<std::size_t> default_draw_order =
        sequential_draw_order(skeleton_data->slots().size());
    if (skeleton.bone_poses()[0].local_pose.x != 0.0 ||
        skeleton.bone_poses()[1].local_pose.y != 50.0 ||
        skeleton.slot_states()[0].attachment_name != "body" ||
        skeleton.draw_order() != default_draw_order) {
        std::cerr << "Skeleton did not reset to setup pose.\n";
        return false;
    }

    if (skeleton_data->bones()[0].setup_pose.x != 0.0 ||
        skeleton_data->bones()[1].setup_pose.y != 50.0 ||
        skeleton_data->slots()[0].setup_attachment != "body") {
        std::cerr << "Mutable instance state leaked back into SkeletonData.\n";
        return false;
    }

    if (!require_near(skeleton.bone_world_transforms()[2].world_x, -30.0, "arm_l reset world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 60.0, "arm_l reset world_y")) {
        std::cerr << "Reset setup pose did not restore hierarchy world transforms.\n";
        return false;
    }

    std::cout << "Loaded runtime skeleton model: " << skeleton_data->info().name << '\n'
              << "  bones=" << skeleton_data->bones().size()
              << ", slots=" << skeleton_data->slots().size()
              << ", animations=" << skeleton_data->animations().size() << '\n'
              << "  resetPoseY=" << skeleton.bone_poses()[1].local_pose.y
              << ", armWorld=(" << skeleton.bone_world_transforms()[2].world_x << ", "
              << skeleton.bone_world_transforms()[2].world_y << ")\n";
    return true;
}

bool validate_runtime_skin_model(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto body_slot_index = skeleton_data->find_slot_index("body");
    const auto arm_slot_index = skeleton_data->find_slot_index("arm_l");
    const auto default_skin_index = skeleton_data->find_skin_index("default");
    const auto warrior_skin_index = skeleton_data->find_skin_index("warrior");
    const auto mage_skin_index = skeleton_data->find_skin_index("mage");
    const auto mage_arm_skin_index = skeleton_data->find_skin_index("mage_arm");
    const auto mesh_base_skin_index = skeleton_data->find_skin_index("mesh_base");

    if (!body_slot_index.has_value() || !arm_slot_index.has_value() ||
        !default_skin_index.has_value() || !warrior_skin_index.has_value() ||
        !mage_skin_index.has_value() || !mage_arm_skin_index.has_value() ||
        !mesh_base_skin_index.has_value()) {
        std::cerr << "Skin fixture lost required slot or skin lookups.\n";
        return false;
    }

    if (skeleton_data->skins().size() != 5 ||
        skeleton_data->default_skin_index() != default_skin_index) {
        std::cerr << "SkeletonData did not preserve the fixture skin registry.\n";
        return false;
    }

    const marrow::runtime::AttachmentData* default_body =
        skeleton_data->find_attachment("default", *body_slot_index);
    const marrow::runtime::AttachmentData* default_arm =
        skeleton_data->find_attachment("default", *arm_slot_index);
    const marrow::runtime::AttachmentData* mesh_body =
        skeleton_data->find_attachment("mesh_base", *body_slot_index);
    const marrow::runtime::AttachmentData* warrior_body =
        skeleton_data->find_attachment("warrior", *body_slot_index);
    const marrow::runtime::AttachmentData* mage_body =
        skeleton_data->find_attachment("mage", *body_slot_index);
    const marrow::runtime::AttachmentData* mage_arm =
        skeleton_data->find_attachment("mage", *arm_slot_index);
    const marrow::runtime::AttachmentData* mage_arm_partial =
        skeleton_data->find_attachment("mage_arm", *arm_slot_index);

    if (default_body == nullptr || default_arm == nullptr || mesh_body == nullptr ||
        warrior_body == nullptr || mage_body == nullptr || mage_arm == nullptr ||
        mage_arm_partial == nullptr) {
        std::cerr << "SkeletonData skin attachment lookup failed for fixture skins.\n";
        return false;
    }

    if (default_body->kind != marrow::runtime::AttachmentKind::Region ||
        default_body->region_name != "body" ||
        default_arm->kind != marrow::runtime::AttachmentKind::Region ||
        default_arm->region_name != "arm_l") {
        std::cerr << "Default skin region attachments did not round-trip.\n";
        return false;
    }

    if (mesh_body->kind != marrow::runtime::AttachmentKind::Mesh ||
        mesh_body->mesh_geometry == nullptr ||
        mesh_body->mesh_geometry->vertices.size() != 8 ||
        mesh_body->mesh_geometry->triangles.size() != 6 ||
        mesh_body->mesh_geometry->uvs.size() != 8 ||
        mesh_body->mesh_geometry->weights.size() != 4) {
        std::cerr << "Mesh source attachment did not preserve its geometry metadata.\n";
        return false;
    }

    if (warrior_body->kind != marrow::runtime::AttachmentKind::LinkedMesh ||
        !warrior_body->linked_mesh.has_value() ||
        warrior_body->linked_mesh->parent_attachment != "body_mesh" ||
        warrior_body->linked_mesh->parent_skin_index != mesh_base_skin_index ||
        !warrior_body->linked_mesh->deform ||
        warrior_body->mesh_geometry == nullptr ||
        warrior_body->mesh_geometry.get() != mesh_body->mesh_geometry.get()) {
        std::cerr << "Warrior linked mesh did not preserve deform inheritance from its shared parent geometry.\n";
        return false;
    }

    if (mage_body->kind != marrow::runtime::AttachmentKind::LinkedMesh ||
        !mage_body->linked_mesh.has_value() ||
        !mage_body->linked_mesh->deform ||
        mage_body->mesh_geometry == nullptr ||
        mage_body->mesh_geometry.get() != mesh_body->mesh_geometry.get() ||
        mage_arm->kind != marrow::runtime::AttachmentKind::Region ||
        mage_arm->region_name != "mage_arm_l" ||
        mage_arm_partial->name != "mage_arm_l") {
        std::cerr << "Mage skin attachments did not preserve linked mesh and partial skin metadata.\n";
        return false;
    }

    std::optional<std::size_t> setup_body_skin_index;
    const marrow::runtime::AttachmentData* setup_body_source =
        skeleton_data->find_attachment_source(*body_slot_index, "body", &setup_body_skin_index);
    if (setup_body_source != default_body || setup_body_skin_index != default_skin_index) {
        std::cerr << "Setup attachment lookup did not resolve the default skin source.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    if (skeleton.current_attachment(*body_slot_index) != default_body ||
        skeleton.current_attachment(*arm_slot_index) != default_arm) {
        std::cerr << "Skeleton setup pose did not start on the default skin attachments.\n";
        return false;
    }

    if (!skeleton.set_skin("warrior") ||
        skeleton.slot_states()[*body_slot_index].attachment_name != "warrior_body" ||
        skeleton.slot_states()[*body_slot_index].attachment_skin_index != warrior_skin_index ||
        skeleton.slot_states()[*arm_slot_index].attachment_name != "arm_l" ||
        skeleton.slot_states()[*arm_slot_index].attachment_skin_index != default_skin_index ||
        skeleton.current_attachment(*body_slot_index) != warrior_body) {
        std::cerr << "Whole-skin swap did not update the active runtime attachments.\n";
        return false;
    }

    if (!skeleton.set_skin("mage") ||
        skeleton.slot_states()[*body_slot_index].attachment_name != "mage_body" ||
        skeleton.slot_states()[*arm_slot_index].attachment_name != "mage_arm_l" ||
        skeleton.current_attachment(*body_slot_index) != mage_body ||
        skeleton.current_attachment(*arm_slot_index) != mage_arm) {
        std::cerr << "Mage skin swap did not activate its linked mesh and region attachments.\n";
        return false;
    }

    if (!skeleton.set_skin_composition(std::vector<std::string_view>{"warrior", "mage_arm"}) ||
        skeleton.slot_states()[*body_slot_index].attachment_name != "warrior_body" ||
        skeleton.slot_states()[*arm_slot_index].attachment_name != "mage_arm_l" ||
        skeleton.current_attachment(*body_slot_index) != warrior_body ||
        skeleton.current_attachment(*arm_slot_index) != mage_arm_partial) {
        std::cerr << "Partial skin composition did not layer fixture skins in order.\n";
        return false;
    }

    if (skeleton.set_skin("missing") ||
        skeleton.set_skin_composition(std::vector<std::string_view>{"warrior", "missing"})) {
        std::cerr << "Skin API should reject unknown skin names.\n";
        return false;
    }

    skeleton.set_to_setup_pose();
    if (skeleton.slot_states()[*body_slot_index].attachment_name != "warrior_body" ||
        skeleton.slot_states()[*arm_slot_index].attachment_name != "mage_arm_l" ||
        skeleton.current_attachment(*body_slot_index) != warrior_body ||
        skeleton.current_attachment(*arm_slot_index) != mage_arm_partial) {
        std::cerr << "Setup-pose reset did not preserve the active skin composition.\n";
        return false;
    }

    if (!skeleton.set_skin("default") ||
        skeleton.slot_states()[*body_slot_index].attachment_name != "body" ||
        skeleton.slot_states()[*arm_slot_index].attachment_name != "arm_l" ||
        skeleton.slot_states()[*body_slot_index].attachment_skin_index != default_skin_index ||
        skeleton.current_attachment(*body_slot_index) != default_body ||
        skeleton.current_attachment(*arm_slot_index) != default_arm) {
        std::cerr << "Switching back to the default skin did not restore the setup attachments.\n";
        return false;
    }

    std::cout << "Loaded runtime skin model: skins=" << skeleton_data->skins().size() << '\n'
              << "  warriorBody=" << warrior_body->name
              << ", mageArm=" << mage_arm->name
              << ", sharedMeshVertices=" << mesh_body->mesh_geometry->vertices.size() / 2 << '\n';
    return true;
}

bool validate_runtime_inherit_timeline_and_skin_constraints(
    const std::filesystem::path& fixture_path) {
    const std::optional<Document> document = load_document_or_print(fixture_path);
    if (!document.has_value()) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(*document);
    if (!skeleton_result) {
        return print_error(*skeleton_result.error);
    }

    const auto child_index = skeleton_result.skeleton_data->find_bone_index("child");
    const auto constrained_index = skeleton_result.skeleton_data->find_bone_index("constrained");
    const auto cape_target_index = skeleton_result.skeleton_data->find_bone_index("cape_target");
    const auto cape_skin_index = skeleton_result.skeleton_data->find_skin_index("cape");
    const auto* cape_skin = skeleton_result.skeleton_data->find_skin("cape");
    const auto* toggle_inherit =
        skeleton_result.skeleton_data->find_animation("toggle_inherit");
    if (!child_index.has_value() || !constrained_index.has_value() ||
        !cape_target_index.has_value() || !cape_skin_index.has_value() ||
        cape_skin == nullptr || toggle_inherit == nullptr) {
        std::cerr << "Inherit/skin constraint fixture lost required lookups.\n";
        return false;
    }

    if (cape_skin->bone_indices != std::vector<std::size_t>{*cape_target_index} ||
        cape_skin->transform_constraint_indices != std::vector<std::size_t>{0}) {
        std::cerr << "Skin-scoped bone or transform constraint metadata did not resolve onto the cape skin.\n";
        return false;
    }

    Skeleton skin_skeleton(skeleton_result.skeleton_data);
    if (skin_skeleton.is_bone_active(*cape_target_index) ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_x,
            50.0,
            "default constrained x") ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_y,
            0.0,
            "default constrained y")) {
        std::cerr << "Setup pose should keep the skin-scoped bone inactive and leave the transform constraint off.\n";
        return false;
    }

    if (!skin_skeleton.set_skin("cape") ||
        !skin_skeleton.is_bone_active(*cape_target_index)) {
        std::cerr << "Skin swap did not activate the cape-only bone set.\n";
        return false;
    }
    skin_skeleton.apply_animation(*toggle_inherit, 0.5);

    if (!skin_skeleton.is_bone_active(*cape_target_index) ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_x,
            200.0,
            "cape constrained x") ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_y,
            0.0,
            "cape constrained y")) {
        std::cerr << "Skin-scoped transform constraint did not stay active through animation playback.\n";
        return false;
    }

    skin_skeleton.set_to_setup_pose();
    if (!skin_skeleton.is_bone_active(*cape_target_index) ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_x,
            200.0,
            "reset constrained x")) {
        std::cerr << "Setup-pose reset did not preserve the cape skin activation state.\n";
        return false;
    }

    if (!skin_skeleton.set_skin("default") ||
        skin_skeleton.is_bone_active(*cape_target_index)) {
        std::cerr << "Switching back to the default skin did not deactivate the cape-only bone.\n";
        return false;
    }
    skin_skeleton.update_world_transforms();

    if (!require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_x,
            50.0,
            "default constrained x after reset") ||
        !require_near(
            skin_skeleton.bone_world_transforms()[*constrained_index].world_y,
            0.0,
            "default constrained y after reset")) {
        std::cerr << "Default skin should leave the transform constraint disabled.\n";
        return false;
    }

    Skeleton direct_skeleton(skeleton_result.skeleton_data);
    direct_skeleton.apply_animation(*toggle_inherit, 0.0);
    const auto inherited_world = direct_skeleton.bone_world_transforms()[*child_index];
    if (!direct_skeleton.bone_poses()[*child_index].inherit.inherit_rotation ||
        !direct_skeleton.bone_poses()[*child_index].inherit.inherit_scale ||
        !direct_skeleton.bone_poses()[*child_index].inherit.inherit_reflection ||
        !require_near(inherited_world.world_x, 0.0, "inherit on world x") ||
        !require_near(inherited_world.world_y, 20.0, "inherit on world y") ||
        !require_near(inherited_world.a, 0.0, "inherit on axis a") ||
        !require_near(inherited_world.b, 0.5, "inherit on axis b") ||
        !require_near(inherited_world.c, 2.0, "inherit on axis c") ||
        !require_near(inherited_world.d, 0.0, "inherit on axis d")) {
        std::cerr << "Inherit timeline setup sample did not keep full parent inheritance enabled.\n";
        return false;
    }

    direct_skeleton.apply_animation(*toggle_inherit, 0.25);
    const auto reflection_disabled_world = direct_skeleton.bone_world_transforms()[*child_index];
    if (!direct_skeleton.bone_poses()[*child_index].inherit.inherit_rotation ||
        !direct_skeleton.bone_poses()[*child_index].inherit.inherit_scale ||
        direct_skeleton.bone_poses()[*child_index].inherit.inherit_reflection ||
        !require_near(reflection_disabled_world.world_x, 0.0, "inherit reflection-off world x") ||
        !require_near(reflection_disabled_world.world_y, 20.0, "inherit reflection-off world y") ||
        !require_near(reflection_disabled_world.a, 0.0, "inherit reflection-off axis a") ||
        !require_near(reflection_disabled_world.b, -0.5, "inherit reflection-off axis b") ||
        !require_near(reflection_disabled_world.c, 2.0, "inherit reflection-off axis c") ||
        !require_near(reflection_disabled_world.d, 0.0, "inherit reflection-off axis d")) {
        std::cerr << "Inherit timeline did not remove reflected parent axes while keeping rotation and scale.\n";
        return false;
    }

    direct_skeleton.apply_animation(*toggle_inherit, 0.5);
    const auto disabled_world = direct_skeleton.bone_world_transforms()[*child_index];
    if (direct_skeleton.bone_poses()[*child_index].inherit.inherit_rotation ||
        direct_skeleton.bone_poses()[*child_index].inherit.inherit_scale ||
        direct_skeleton.bone_poses()[*child_index].inherit.inherit_reflection ||
        !require_near(disabled_world.world_x, 0.0, "inherit off world x") ||
        !require_near(disabled_world.world_y, 20.0, "inherit off world y") ||
        !require_near(disabled_world.a, 1.0, "inherit off axis a") ||
        !require_near(disabled_world.b, 0.0, "inherit off axis b") ||
        !require_near(disabled_world.c, 0.0, "inherit off axis c") ||
        !require_near(disabled_world.d, 1.0, "inherit off axis d")) {
        std::cerr << "Inherit timeline did not disable parent rotation and scale inheritance at t=0.5.\n";
        return false;
    }

    direct_skeleton.apply_animation(*toggle_inherit, 1.0);
    const auto restored_world = direct_skeleton.bone_world_transforms()[*child_index];
    if (!direct_skeleton.bone_poses()[*child_index].inherit.inherit_rotation ||
        !direct_skeleton.bone_poses()[*child_index].inherit.inherit_scale ||
        !direct_skeleton.bone_poses()[*child_index].inherit.inherit_reflection ||
        !require_near(restored_world.a, 0.0, "inherit restored axis a") ||
        !require_near(restored_world.b, 0.5, "inherit restored axis b") ||
        !require_near(restored_world.c, 2.0, "inherit restored axis c") ||
        !require_near(restored_world.d, 0.0, "inherit restored axis d")) {
        std::cerr << "Inherit timeline did not restore full parent inheritance at t=1.0.\n";
        return false;
    }

    Skeleton state_skeleton(skeleton_result.skeleton_data);
    AnimationState state(skeleton_result.skeleton_data);
    state.set_animation(0, "toggle_inherit", true);
    state.update(0.5);
    state.apply(state_skeleton);
    const auto state_world = state_skeleton.bone_world_transforms()[*child_index];
    if (!require_near(state_world.a, 1.0, "state inherit off axis a") ||
        !require_near(state_world.b, 0.0, "state inherit off axis b") ||
        !require_near(state_world.c, 0.0, "state inherit off axis c") ||
        !require_near(state_world.d, 1.0, "state inherit off axis d")) {
        std::cerr << "AnimationState did not apply the inherit timeline through discrete track playback.\n";
        return false;
    }

    std::cout << "Loaded inherit + skin-scoped constraint fixture: skins="
              << skeleton_result.skeleton_data->skins().size() << '\n'
              << "  capeConstraintX="
              << skin_skeleton.bone_world_transforms()[*constrained_index].world_x
              << ", reflectionOffAxes=(" << reflection_disabled_world.a << ", "
              << reflection_disabled_world.b << ", " << reflection_disabled_world.c << ", "
              << reflection_disabled_world.d << ")"
              << ", inheritOffAxes=(" << disabled_world.a << ", " << disabled_world.b << ", "
              << disabled_world.c << ", " << disabled_world.d << ")\n";
    return true;
}

bool validate_runtime_attachment_extensions(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto spawn_slot_index = skeleton_data->find_slot_index("spawn_anchor");
    const auto hurtbox_slot_index = skeleton_data->find_slot_index("hurtbox");
    const auto default_skin_index = skeleton_data->find_skin_index("default");
    const auto* attack = skeleton_data->find_animation("attack");
    if (!spawn_slot_index.has_value() || !hurtbox_slot_index.has_value() ||
        !default_skin_index.has_value() || attack == nullptr) {
        std::cerr << "Attachment extension validation requires the default spawn/hurtbox slots and attack animation.\n";
        return false;
    }

    const marrow::runtime::AttachmentData* spawn_attachment =
        skeleton_data->find_attachment("default", *spawn_slot_index);
    const marrow::runtime::AttachmentData* hurtbox_attachment =
        skeleton_data->find_attachment("default", *hurtbox_slot_index);
    if (spawn_attachment == nullptr || hurtbox_attachment == nullptr) {
        std::cerr << "Default skin lookup did not preserve the point and bounding-box attachments.\n";
        return false;
    }

    if (spawn_attachment->kind != marrow::runtime::AttachmentKind::Point ||
        !spawn_attachment->point_attachment.has_value() ||
        !require_near(
            spawn_attachment->point_attachment->local_position.x,
            40.0,
            "spawn anchor x") ||
        !require_near(
            spawn_attachment->point_attachment->local_position.y,
            0.0,
            "spawn anchor y") ||
        !require_near(
            spawn_attachment->point_attachment->rotation,
            15.0,
            "spawn anchor rotation") ||
        hurtbox_attachment->kind != marrow::runtime::AttachmentKind::BoundingBox ||
        !hurtbox_attachment->bounding_box.has_value() ||
        hurtbox_attachment->bounding_box->polygon.size() != 4) {
        std::cerr << "Point or bounding-box attachment parsing did not preserve the fixture metadata.\n";
        return false;
    }

    if (!require_attachment_vertex(
            hurtbox_attachment->bounding_box->polygon[0],
            0.0,
            0.0,
            "hurtbox local v0") ||
        !require_attachment_vertex(
            hurtbox_attachment->bounding_box->polygon[2],
            30.0,
            20.0,
            "hurtbox local v2")) {
        std::cerr << "Bounding-box local polygon vertices did not round-trip.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    if (skeleton.current_attachment(*spawn_slot_index) != spawn_attachment ||
        skeleton.current_attachment(*hurtbox_slot_index) != hurtbox_attachment ||
        skeleton.slot_states()[*spawn_slot_index].attachment_skin_index != default_skin_index ||
        skeleton.slot_states()[*hurtbox_slot_index].attachment_skin_index != default_skin_index) {
        std::cerr << "Setup pose did not activate the default point and bounding-box attachments.\n";
        return false;
    }

    skeleton.apply_animation(*attack, 0.2);

    const std::optional<marrow::runtime::PointAttachmentPose> point_pose =
        skeleton.evaluate_current_point_attachment(*spawn_slot_index);
    const std::optional<marrow::runtime::BoundingBoxAttachmentPose> hurtbox_pose =
        skeleton.evaluate_current_bounding_box_attachment(*hurtbox_slot_index);
    if (!point_pose.has_value() || !hurtbox_pose.has_value() ||
        point_pose->attachment_name != "spawn_anchor" ||
        hurtbox_pose->attachment_name != "hurtbox" ||
        hurtbox_pose->polygon.size() != 4) {
        std::cerr << "Runtime attachment evaluators did not expose the current point or bounding-box pose.\n";
        return false;
    }

    if (!require_attachment_vertex(
            point_pose->position,
            -10.0,
            94.64101615137754,
            "spawn anchor world") ||
        !require_near(point_pose->rotation, 75.0, "spawn anchor world rotation") ||
        !require_attachment_vertex(hurtbox_pose->polygon[0], -30.0, 60.0, "hurtbox world v0") ||
        !require_attachment_vertex(
            hurtbox_pose->polygon[1],
            -15.0,
            85.98076211353316,
            "hurtbox world v1") ||
        !require_attachment_vertex(
            hurtbox_pose->polygon[2],
            -32.32050807568877,
            95.98076211353316,
            "hurtbox world v2") ||
        !require_attachment_vertex(
            hurtbox_pose->polygon[3],
            -47.32050807568877,
            70.0,
            "hurtbox world v3")) {
        std::cerr << "Point or bounding-box world-space evaluation did not match the animated pose.\n";
        return false;
    }

    std::cout << "Loaded runtime attachment extensions: point=" << point_pose->attachment_name
              << ", polygonVertices=" << hurtbox_pose->polygon.size() << '\n'
              << "  pointWorld=(" << point_pose->position.x << ", " << point_pose->position.y
              << "), rotation=" << point_pose->rotation << '\n'
              << "  hurtboxV2=(" << hurtbox_pose->polygon[2].x << ", "
              << hurtbox_pose->polygon[2].y << ")\n";
    return true;
}

bool validate_runtime_clipping_and_sequence_attachments(
    const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto clip_slot_index = skeleton_data->find_slot_index("fx_mask");
    const auto spark_slot_index = skeleton_data->find_slot_index("spark_fx");
    if (!clip_slot_index.has_value() || !spark_slot_index.has_value()) {
        std::cerr << "Clipping and sequence validation requires the fx_mask and spark_fx slots.\n";
        return false;
    }

    const marrow::runtime::AttachmentData* clip_attachment =
        skeleton_data->find_attachment("default", *clip_slot_index);
    const marrow::runtime::AttachmentData* spark_attachment =
        skeleton_data->find_attachment("default", *spark_slot_index);
    if (clip_attachment == nullptr || spark_attachment == nullptr) {
        std::cerr << "Default skin lookup did not preserve the clipping or sequence attachments.\n";
        return false;
    }

    if (clip_attachment->kind != marrow::runtime::AttachmentKind::Clipping ||
        !clip_attachment->clipping_attachment.has_value() ||
        clip_attachment->clipping_attachment->end_slot_index != spark_slot_index ||
        clip_attachment->clipping_attachment->polygon.size() != 4 ||
        spark_attachment->kind != marrow::runtime::AttachmentKind::Region ||
        !spark_attachment->sequence.has_value() ||
        spark_attachment->sequence->frame_regions !=
            std::vector<std::string>{"spark_fx_0", "spark_fx_1", "spark_fx_2", "spark_fx_3"} ||
        !require_near(spark_attachment->sequence->fps, 8.0, "spark sequence fps") ||
        spark_attachment->sequence->playback_mode !=
            marrow::runtime::SequencePlaybackMode::Loop ||
        spark_attachment->sequence->setup_frame != 0) {
        std::cerr << "Clipping or sequence metadata parsing did not preserve the fixture data.\n";
        return false;
    }

    const marrow::runtime::AttachmentSequenceData hold_sequence{
        {"hold_0", "hold_1", "hold_2"},
        12.0,
        marrow::runtime::SequencePlaybackMode::Hold,
        1};
    const marrow::runtime::AttachmentSequenceData once_sequence{
        {"once_0", "once_1", "once_2", "once_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::Once,
        1};
    const marrow::runtime::AttachmentSequenceData loop_sequence{
        {"loop_0", "loop_1", "loop_2", "loop_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::Loop,
        0};
    const marrow::runtime::AttachmentSequenceData pingpong_sequence{
        {"pingpong_0", "pingpong_1", "pingpong_2", "pingpong_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::PingPong,
        0};
    const marrow::runtime::AttachmentSequenceData once_reverse_sequence{
        {"reverse_0", "reverse_1", "reverse_2", "reverse_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::OnceReverse,
        3};
    const marrow::runtime::AttachmentSequenceData loop_reverse_sequence{
        {"loop_reverse_0", "loop_reverse_1", "loop_reverse_2", "loop_reverse_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::LoopReverse,
        0};
    const marrow::runtime::AttachmentSequenceData pingpong_reverse_sequence{
        {"pingpong_reverse_0", "pingpong_reverse_1", "pingpong_reverse_2", "pingpong_reverse_3"},
        4.0,
        marrow::runtime::SequencePlaybackMode::PingPongReverse,
        3};

    if (sample_sequence_frame(hold_sequence, 2.0) != std::optional<std::size_t>{2} ||
        sample_sequence_frame(once_sequence, 1.0) != std::optional<std::size_t>{3} ||
        sample_sequence_frame(loop_sequence, 1.25) != std::optional<std::size_t>{1} ||
        sample_sequence_frame(pingpong_sequence, 1.25) != std::optional<std::size_t>{1} ||
        sample_sequence_frame(once_reverse_sequence, 1.0) != std::optional<std::size_t>{0} ||
        sample_sequence_frame(loop_reverse_sequence, 0.25) != std::optional<std::size_t>{3} ||
        sample_sequence_frame(pingpong_reverse_sequence, 0.25) != std::optional<std::size_t>{2}) {
        std::cerr << "Sequence frame sampling did not honor all playback modes.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    if (skeleton.current_attachment(*clip_slot_index) != clip_attachment ||
        skeleton.current_attachment(*spark_slot_index) != spark_attachment ||
        skeleton.current_sequence_frame(*spark_slot_index) != std::optional<std::size_t>{0} ||
        skeleton.current_region_name(*spark_slot_index) != "spark_fx_0") {
        std::cerr << "Setup pose did not expose the clipping or sequence attachments.\n";
        return false;
    }

    const std::optional<marrow::runtime::ClippingAttachmentPose> clip_pose =
        skeleton.evaluate_current_clipping_attachment(*clip_slot_index);
    if (!clip_pose.has_value() ||
        clip_pose->end_slot_index != spark_slot_index ||
        clip_pose->polygon.size() != 4 ||
        !require_attachment_vertex(clip_pose->polygon[0], -6.0, 40.0, "fx_mask world v0") ||
        !require_attachment_vertex(clip_pose->polygon[2], 14.0, 60.0, "fx_mask world v2")) {
        std::cerr << "Runtime clipping attachment evaluation did not expose the world-space mask polygon.\n";
        return false;
    }

    skeleton.advance_attachment_playback(0.375);
    if (!require_near(skeleton.attachment_playback_time(), 0.375, "sequence playback time") ||
        skeleton.current_sequence_frame(*spark_slot_index) != std::optional<std::size_t>{3} ||
        skeleton.current_region_name(*spark_slot_index) != "spark_fx_3") {
        std::cerr << "Sequence playback advance did not resolve the expected loop frame.\n";
        return false;
    }
    const std::string frame_at_0375 = std::string(skeleton.current_region_name(*spark_slot_index));

    skeleton.set_attachment_playback_time(0.625);
    if (skeleton.current_sequence_frame(*spark_slot_index) != std::optional<std::size_t>{1} ||
        skeleton.current_region_name(*spark_slot_index) != "spark_fx_1") {
        std::cerr << "Sequence playback time override did not resolve the expected wrapped frame.\n";
        return false;
    }
    const std::string frame_at_0625 = std::string(skeleton.current_region_name(*spark_slot_index));

    std::cout << "Loaded runtime clipping and sequence attachments: clip=" << clip_pose->attachment_name
              << ", sequenceFrames=" << spark_attachment->sequence->frame_regions.size() << '\n'
              << "  clipEnd=" << skeleton_data->slots()[*spark_slot_index].name
              << ", frame@0.375=" << frame_at_0375
              << ", frame@0.625=" << frame_at_0625 << '\n';
    return true;
}

bool validate_runtime_skeleton_bounds(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto hurtbox_slot_index = skeleton_data->find_slot_index("hurtbox");
    const auto* attack = skeleton_data->find_animation("attack");
    if (!hurtbox_slot_index.has_value() || attack == nullptr) {
        std::cerr << "SkeletonBounds validation requires the hurtbox slot and attack animation.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    skeleton.apply_animation(*attack, 0.2);

    marrow::runtime::SkeletonBounds bounds;
    bounds.update(skeleton, true);

    const std::optional<marrow::runtime::BoundingBoxAttachmentPose> hurtbox_pose =
        skeleton.evaluate_current_bounding_box_attachment(*hurtbox_slot_index);
    const std::vector<marrow::runtime::AttachmentVertex>* hurtbox_polygon =
        bounds.get_polygon("hurtbox");
    if (!hurtbox_pose.has_value() ||
        hurtbox_polygon == nullptr ||
        hurtbox_polygon->size() != hurtbox_pose->polygon.size() ||
        bounds.bounding_boxes().size() != 1 ||
        !bounds.has_aabb()) {
        std::cerr << "SkeletonBounds did not capture the transformed hurtbox polygon.\n";
        return false;
    }

    if (!require_attachment_vertex(
            (*hurtbox_polygon)[0],
            hurtbox_pose->polygon[0].x,
            hurtbox_pose->polygon[0].y,
            "bounds hurtbox v0") ||
        !require_attachment_vertex(
            (*hurtbox_polygon)[2],
            hurtbox_pose->polygon[2].x,
            hurtbox_pose->polygon[2].y,
            "bounds hurtbox v2")) {
        std::cerr << "SkeletonBounds polygon retrieval did not match the active pose.\n";
        return false;
    }

    const double inside_x = ((*hurtbox_polygon)[0].x + (*hurtbox_polygon)[2].x) * 0.5;
    const double inside_y = ((*hurtbox_polygon)[0].y + (*hurtbox_polygon)[2].y) * 0.5;

    const marrow::runtime::BoundingBoxAttachmentPose* point_hit = nullptr;
    if (!bounds.contains_point(inside_x, inside_y, &point_hit) ||
        point_hit == nullptr ||
        point_hit != bounds.get_bounding_box() ||
        point_hit->attachment_name != "hurtbox") {
        std::cerr << "SkeletonBounds point queries did not report the hurtbox hit.\n";
        return false;
    }

    const marrow::runtime::BoundingBoxAttachmentPose* segment_hit = nullptr;
    if (!bounds.intersects_segment(
            bounds.min_x() - 10.0,
            inside_y,
            bounds.max_x() + 10.0,
            inside_y,
            &segment_hit) ||
        segment_hit == nullptr ||
        segment_hit != bounds.get_bounding_box() ||
        segment_hit->attachment_name != "hurtbox") {
        std::cerr << "SkeletonBounds segment queries did not report the hurtbox hit.\n";
        return false;
    }

    std::cout << "SkeletonBounds positive hits: point=" << point_hit->attachment_name
              << ", segment=" << segment_hit->attachment_name << '\n'
              << "  boundsMin=(" << bounds.min_x() << ", " << bounds.min_y() << ")"
              << ", boundsMax=(" << bounds.max_x() << ", " << bounds.max_y() << ")\n";
    return true;
}

bool validate_runtime_weighted_mesh_model(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto body_slot_index = skeleton_data->find_slot_index("body");
    const auto spine_index = skeleton_data->find_bone_index("spine");
    const auto arm_index = skeleton_data->find_bone_index("arm_l");
    if (!body_slot_index.has_value() || !spine_index.has_value() || !arm_index.has_value()) {
        std::cerr << "Fixture lost the indices needed for weighted mesh validation.\n";
        return false;
    }

    const marrow::runtime::AttachmentData* mesh_body =
        skeleton_data->find_attachment("mesh_base", *body_slot_index);
    if (mesh_body == nullptr || mesh_body->mesh_geometry == nullptr) {
        std::cerr << "Mesh fixture did not preserve the source body mesh.\n";
        return false;
    }

    const auto& geometry = *mesh_body->mesh_geometry;
    if (geometry.vertices.size() != 8 || geometry.triangles.size() != 6 ||
        geometry.uvs.size() != 8 || geometry.weights.size() != 4) {
        std::cerr << "Weighted mesh geometry did not preserve the expected array sizes.\n";
        return false;
    }

    if (geometry.weights[0].influences.size() != 1 ||
        geometry.weights[1].influences.size() != 2 ||
        geometry.weights[2].influences.size() != 2 ||
        geometry.weights[3].influences.size() != 2) {
        std::cerr << "Weighted mesh did not preserve per-vertex influence counts.\n";
        return false;
    }

    const auto& top_left = geometry.weights[0].influences[0];
    const auto& top_right_spine = geometry.weights[1].influences[0];
    const auto& top_right_arm = geometry.weights[1].influences[1];
    const auto& bottom_right_spine = geometry.weights[2].influences[0];
    const auto& bottom_right_arm = geometry.weights[2].influences[1];
    if (top_left.bone_index != *spine_index ||
        !require_near(top_left.weight, 1.0, "top-left mesh weight") ||
        top_right_spine.bone_index != *spine_index ||
        top_right_arm.bone_index != *arm_index ||
        !require_near(top_right_spine.weight, 0.75, "top-right spine weight") ||
        !require_near(top_right_arm.weight, 0.25, "top-right arm weight") ||
        bottom_right_spine.bone_index != *spine_index ||
        bottom_right_arm.bone_index != *arm_index ||
        !require_near(bottom_right_spine.weight, 0.25, "bottom-right spine weight") ||
        !require_near(bottom_right_arm.weight, 0.75, "bottom-right arm weight")) {
        std::cerr << "Weighted mesh influences did not preserve bone bindings.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    if (!skeleton.set_skin("warrior")) {
        std::cerr << "Weighted mesh validation could not activate the linked warrior mesh.\n";
        return false;
    }

    const std::optional<marrow::runtime::MeshAttachmentPose> setup_mesh =
        skeleton.evaluate_current_mesh_attachment(*body_slot_index);
    if (!setup_mesh.has_value() ||
        setup_mesh->attachment_name != "warrior_body" ||
        setup_mesh->region_name != "warrior_body" ||
        setup_mesh->triangles != geometry.triangles ||
        setup_mesh->uvs != geometry.uvs ||
        setup_mesh->vertices.size() != 4) {
        std::cerr << "Linked mesh setup-pose evaluation did not expose the shared mesh geometry.\n";
        return false;
    }

    if (!require_mesh_vertex(setup_mesh->vertices[0], -64.0, -30.0, "setup mesh v0") ||
        !require_mesh_vertex(setup_mesh->vertices[1], 64.0, -30.0, "setup mesh v1") ||
        !require_mesh_vertex(setup_mesh->vertices[2], 64.0, 130.0, "setup mesh v2") ||
        !require_mesh_vertex(setup_mesh->vertices[3], -64.0, 130.0, "setup mesh v3")) {
        std::cerr << "Weighted mesh setup-pose deformation did not match the authored quad.\n";
        return false;
    }

    skeleton.bone_poses()[*arm_index].local_pose.rotation = 30.0;
    skeleton.update_world_transforms();

    const std::optional<marrow::runtime::MeshAttachmentPose> posed_mesh =
        skeleton.evaluate_current_mesh_attachment(*body_slot_index);
    if (!posed_mesh.has_value() ||
        posed_mesh->attachment_name != "warrior_body" ||
        posed_mesh->vertices.size() != 4) {
        std::cerr << "Weighted mesh pose sample did not keep the linked mesh attachment active.\n";
        return false;
    }

    const auto posed_rigid_top_left = transform_mesh_vertex(
        skeleton.bone_world_transforms()[*spine_index],
        geometry.vertices[0],
        geometry.vertices[1]);
    const auto posed_rigid_bottom_right = transform_mesh_vertex(
        skeleton.bone_world_transforms()[*spine_index],
        geometry.vertices[4],
        geometry.vertices[5]);
    if (!require_mesh_vertex(
            posed_mesh->vertices[0],
            posed_rigid_top_left.x,
            posed_rigid_top_left.y,
            "posed mesh v0")) {
        std::cerr << "Single-bone weighted vertex did not follow the bound spine transform.\n";
        return false;
    }

    const double bottom_right_rigid_delta_x =
        std::abs(posed_mesh->vertices[2].x - posed_rigid_bottom_right.x);
    const double bottom_right_rigid_delta_y =
        std::abs(posed_mesh->vertices[2].y - posed_rigid_bottom_right.y);
    if (std::hypot(bottom_right_rigid_delta_x, bottom_right_rigid_delta_y) <= 5.0) {
        std::cerr << "Multi-bone weighted vertex did not diverge from the rigid spine transform.\n";
        return false;
    }

    std::cout << "Loaded runtime weighted mesh: " << posed_mesh->attachment_name << '\n'
              << "  vertices=" << posed_mesh->vertices.size()
              << ", triangles=" << posed_mesh->triangles.size() / 3
              << ", topRightInfluences=" << geometry.weights[1].influences.size() << '\n'
              << "  posedV2=(" << posed_mesh->vertices[2].x << ", "
              << posed_mesh->vertices[2].y << ")"
              << ", rigidDelta=(" << bottom_right_rigid_delta_x << ", "
              << bottom_right_rigid_delta_y << ")\n";
    return true;
}

bool validate_runtime_mesh_deform_timelines(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto body_slot_index = skeleton_data->find_slot_index("body");
    const marrow::runtime::AnimationData* idle_animation =
        skeleton_data->find_animation("idle");
    if (!body_slot_index.has_value() || idle_animation == nullptr) {
        std::cerr << "Mesh deform validation requires the idle animation and body slot.\n";
        return false;
    }

    const marrow::runtime::MeshDeformTimeline* deform_timeline =
        idle_animation->find_deform_timeline(*body_slot_index, "body_mesh");
    if (deform_timeline == nullptr || deform_timeline->keyframes.size() != 3) {
        std::cerr << "Mesh deform timeline parsing did not preserve the keyed attachment data.\n";
        return false;
    }

    const std::optional<std::vector<double>> start_offsets =
        idle_animation->sample_slot_deform(*body_slot_index, "body_mesh", 0.0);
    const std::optional<std::vector<double>> midpoint_offsets =
        idle_animation->sample_slot_deform(*body_slot_index, "body_mesh", 0.75);
    const std::optional<std::vector<double>> end_offsets =
        idle_animation->sample_slot_deform(*body_slot_index, "body_mesh", 1.0);
    const std::vector<double> expected_midpoint_offsets{0.0, 0.0, 6.0, -4.0, 8.0, 10.0, -2.0, 5.0};
    const std::vector<double> zero_offsets(8, 0.0);
    if (!start_offsets.has_value() || !midpoint_offsets.has_value() || !end_offsets.has_value() ||
        *start_offsets != zero_offsets ||
        *midpoint_offsets != expected_midpoint_offsets ||
        *end_offsets != zero_offsets) {
        std::cerr << "Mesh deform timeline sampling did not reproduce the authored vertex offsets.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    skeleton.apply_animation(*idle_animation, 0.75);

    const marrow::runtime::AttachmentData* current_attachment =
        skeleton.current_attachment(*body_slot_index);
    if (current_attachment == nullptr ||
        current_attachment->name != "warrior_body" ||
        !current_attachment->linked_mesh.has_value() ||
        !current_attachment->linked_mesh->deform) {
        std::cerr << "Mesh deform playback did not activate the deformable linked mesh attachment.\n";
        return false;
    }

    const std::optional<marrow::runtime::MeshAttachmentPose> posed_mesh =
        skeleton.evaluate_current_mesh_attachment(*body_slot_index);
    if (!posed_mesh.has_value() ||
        posed_mesh->attachment_name != "warrior_body" ||
        posed_mesh->vertices.size() != expected_midpoint_offsets.size() / 2) {
        std::cerr << "Mesh deform playback did not produce a posed mesh for the active linked mesh.\n";
        return false;
    }

    const auto body_mesh_source =
        skeleton_data->find_attachment_source(*body_slot_index, "body_mesh");
    if (body_mesh_source == nullptr || body_mesh_source->mesh_geometry == nullptr) {
        std::cerr << "Mesh deform validation could not resolve the source mesh geometry.\n";
        return false;
    }

    const auto& geometry = *body_mesh_source->mesh_geometry;
    for (std::size_t vertex_index = 0; vertex_index < posed_mesh->vertices.size(); ++vertex_index) {
        const marrow::runtime::MeshWorldVertex expected_vertex = blend_weighted_mesh_vertex(
            skeleton.bone_world_transforms(),
            geometry.weights[vertex_index],
            expected_midpoint_offsets[vertex_index * 2],
            expected_midpoint_offsets[(vertex_index * 2) + 1]);
        if (!require_mesh_vertex(
                posed_mesh->vertices[vertex_index],
                expected_vertex.x,
                expected_vertex.y,
                "deformed mesh vertex")) {
            std::cerr << "Runtime mesh deform playback did not apply offsets on top of weighted skinning.\n";
            return false;
        }
    }

    std::cout << "Loaded runtime mesh deform timeline: " << idle_animation->name << '\n'
              << "  attachment=" << deform_timeline->attachment_name
              << ", keys=" << deform_timeline->keyframes.size() << '\n'
              << "  offsets@0.75=(" << expected_midpoint_offsets[2] << ", "
              << expected_midpoint_offsets[3] << ") -> v1=("
              << posed_mesh->vertices[1].x << ", " << posed_mesh->vertices[1].y << ")\n";
    return true;
}

bool validate_runtime_animation_curves(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto* animation_ptr = skeleton_data->find_animation("idle");
    if (animation_ptr == nullptr) {
        std::cerr << "Fixture did not load any animations.\n";
        return false;
    }

    const auto spine_index = skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Fixture lost the spine bone index.\n";
        return false;
    }

    const auto& animation = *animation_ptr;
    const marrow::runtime::BoneRotateTimeline* rotate_timeline =
        animation.find_rotate_timeline(*spine_index);
    const marrow::runtime::BoneTranslateTimeline* translate_timeline =
        animation.find_translate_timeline(*spine_index);
    const marrow::runtime::BoneScaleTimeline* scale_timeline =
        animation.find_scale_timeline(*spine_index);
    const marrow::runtime::BoneShearTimeline* shear_timeline =
        animation.find_shear_timeline(*spine_index);
    if (rotate_timeline == nullptr || rotate_timeline->keyframes.size() != 3 ||
        translate_timeline == nullptr || translate_timeline->keyframes.size() != 3 ||
        scale_timeline == nullptr || scale_timeline->keyframes.size() != 2 ||
        shear_timeline == nullptr || shear_timeline->keyframes.size() != 2) {
        std::cerr << "Bone transform timeline parsing did not preserve the fixture keyframes.\n";
        return false;
    }

    if (rotate_timeline->keyframes[0].interpolation.kind() !=
            marrow::runtime::InterpolationKind::Linear ||
        rotate_timeline->keyframes[1].interpolation.kind() !=
            marrow::runtime::InterpolationKind::CubicBezier ||
        rotate_timeline->keyframes[2].interpolation.kind() !=
            marrow::runtime::InterpolationKind::Stepped) {
        std::cerr << "Rotate timeline interpolation kinds do not match the fixture curves.\n";
        return false;
    }

    const marrow::runtime::CubicBezierControlPoints& bezier =
        rotate_timeline->keyframes[1].interpolation.cubic_bezier();
    if (!require_near(bezier.cx1, 0.25, "rotate bezier cx1") ||
        !require_near(bezier.cy1, 0.1, "rotate bezier cy1") ||
        !require_near(bezier.cx2, 0.75, "rotate bezier cx2") ||
        !require_near(bezier.cy2, 0.9, "rotate bezier cy2")) {
        std::cerr << "Rotate timeline bezier control points did not round-trip.\n";
        return false;
    }

    const std::optional<double> linear_sample = animation.sample_bone_rotation(*spine_index, 0.25);
    const std::optional<double> bezier_sample = animation.sample_bone_rotation(*spine_index, 0.625);
    const std::optional<double> start_sample = animation.sample_bone_rotation(*spine_index, 0.0);
    const std::optional<double> midpoint_sample = animation.sample_bone_rotation(*spine_index, 0.5);
    const std::optional<double> end_sample = animation.sample_bone_rotation(*spine_index, 1.0);
    const std::optional<marrow::runtime::VectorSample> translate_linear_sample =
        animation.sample_bone_translation(*spine_index, 0.25);
    const std::optional<marrow::runtime::VectorSample> translate_stepped_sample =
        animation.sample_bone_translation(*spine_index, 0.625);
    const std::optional<marrow::runtime::VectorSample> scale_sample =
        animation.sample_bone_scale(*spine_index, 0.625);
    const std::optional<marrow::runtime::VectorSample> shear_sample =
        animation.sample_bone_shear(*spine_index, 0.625);
    const double stepped_mid = marrow::runtime::interpolate_value(
        5.0,
        10.0,
        rotate_timeline->keyframes[2].interpolation,
        0.5);
    const double stepped_end = marrow::runtime::interpolate_value(
        5.0,
        10.0,
        rotate_timeline->keyframes[2].interpolation,
        1.0);

    if (!linear_sample.has_value() || !bezier_sample.has_value() ||
        !start_sample.has_value() || !midpoint_sample.has_value() || !end_sample.has_value() ||
        !translate_linear_sample.has_value() || !translate_stepped_sample.has_value() ||
        !scale_sample.has_value() || !shear_sample.has_value()) {
        std::cerr << "Animation sampling did not find all bone transform timelines.\n";
        return false;
    }

    if (!require_near(*linear_sample, 2.5, "linear rotate sample") ||
        !require_near(*bezier_sample, 3.9529315894941443, "bezier rotate sample") ||
        !require_near(stepped_mid, 5.0, "stepped rotate sample mid") ||
        !require_near(stepped_end, 10.0, "stepped rotate sample end") ||
        !require_near(*start_sample, 0.0, "rotate start sample") ||
        !require_near(*midpoint_sample, 5.0, "rotate midpoint sample") ||
        !require_near(*end_sample, 0.0, "rotate final sample") ||
        !require_near(translate_linear_sample->x, 5.0, "translate linear sample x") ||
        !require_near(translate_linear_sample->y, 55.0, "translate linear sample y") ||
        !require_near(translate_stepped_sample->x, 10.0, "translate stepped sample x") ||
        !require_near(translate_stepped_sample->y, 60.0, "translate stepped sample y") ||
        !require_near(scale_sample->x, 1.125, "scale sample x") ||
        !require_near(scale_sample->y, 0.875, "scale sample y") ||
        !require_near(shear_sample->x, 6.25, "shear sample x") ||
        !require_near(shear_sample->y, -3.125, "shear sample y")) {
        std::cerr << "Bone transform timeline evaluators did not reproduce the fixture data.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    skeleton.apply_animation(animation, 0.0);
    if (!require_near(skeleton.bone_poses()[*spine_index].local_pose.rotation, 0.0, "start pose rotation") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.x, 0.0, "start pose x") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.y, 50.0, "start pose y") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.scale_x, 1.0, "start pose scale x") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.scale_y, 1.0, "start pose scale y") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.shear_x, 0.0, "start pose shear x") ||
        !require_near(skeleton.bone_poses()[*spine_index].local_pose.shear_y, 0.0, "start pose shear y")) {
        std::cerr << "Animation sampling did not preserve the first transform keyframe at time zero.\n";
        return false;
    }

    skeleton.apply_animation(animation, 0.625);
    const auto& sampled_pose = skeleton.bone_poses()[*spine_index].local_pose;
    if (!require_near(sampled_pose.rotation, 3.9529315894941443, "sampled pose rotation") ||
        !require_near(sampled_pose.x, 10.0, "sampled pose x") ||
        !require_near(sampled_pose.y, 60.0, "sampled pose y") ||
        !require_near(sampled_pose.scale_x, 1.125, "sampled pose scale x") ||
        !require_near(sampled_pose.scale_y, 0.875, "sampled pose scale y") ||
        !require_near(sampled_pose.shear_x, 6.25, "sampled pose shear x") ||
        !require_near(sampled_pose.shear_y, -3.125, "sampled pose shear y")) {
        std::cerr << "Applying the animation sample did not update the bone pose over time.\n";
        return false;
    }

    if (!require_near(skeleton.bone_world_transforms()[2].world_x, 1.65726981632622, "arm world x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 70.27077695332018, "arm world y")) {
        std::cerr << "Animated world transforms did not match the sampled pose.\n";
        return false;
    }

    std::cout << "Loaded runtime animation curves: " << animation.name << '\n'
              << "  rotateKeys=" << rotate_timeline->keyframes.size()
              << ", translateKeys=" << translate_timeline->keyframes.size()
              << ", scaleKeys=" << scale_timeline->keyframes.size()
              << ", shearKeys=" << shear_timeline->keyframes.size() << '\n'
              << "  rotate@0.625=" << *bezier_sample
              << ", translate@0.625=(" << translate_stepped_sample->x << ", "
              << translate_stepped_sample->y << ")"
              << ", armWorld=(" << skeleton.bone_world_transforms()[2].world_x << ", "
              << skeleton.bone_world_transforms()[2].world_y << ")\n";
    return true;
}

bool validate_runtime_animation_events(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto* animation_ptr = skeleton_data->find_animation("idle");
    if (animation_ptr == nullptr) {
        std::cerr << "Fixture did not load any animations.\n";
        return false;
    }

    const auto find_event_definition = [&](std::string_view name)
        -> const marrow::runtime::EventDefinition* {
        for (const auto& event_definition : skeleton_data->events()) {
            if (event_definition.name == name) {
                return &event_definition;
            }
        }
        return nullptr;
    };

    const marrow::runtime::EventDefinition* footstep =
        find_event_definition("footstep");
    const marrow::runtime::EventDefinition* dust_vfx =
        find_event_definition("dust_vfx");
    if (skeleton_data->events().size() != 2 || footstep == nullptr || dust_vfx == nullptr) {
        std::cerr << "Event definition parsing did not preserve the fixture registry.\n";
        return false;
    }

    if (footstep->int_value != 0 ||
        !require_near(footstep->float_value, 0.35, "footstep default float") ||
        footstep->string_value != "heel" ||
        !footstep->audio_path.has_value() ||
        *footstep->audio_path != "sfx/footstep.wav" ||
        !require_near(footstep->volume, 0.8, "footstep default volume") ||
        !require_near(footstep->balance, 0.0, "footstep default balance") ||
        dust_vfx->int_value != 2 ||
        !require_near(dust_vfx->float_value, 0.5, "dust_vfx default float") ||
        dust_vfx->string_value != "dust_ring" ||
        dust_vfx->audio_path.has_value() ||
        !require_near(dust_vfx->volume, 1.0, "dust_vfx default volume") ||
        !require_near(dust_vfx->balance, 0.0, "dust_vfx default balance")) {
        std::cerr << "Event definition defaults did not round-trip from the fixture.\n";
        return false;
    }

    const auto& animation = *animation_ptr;
    const marrow::runtime::EventTimeline* event_timeline = animation.find_event_timeline();
    if (event_timeline == nullptr || event_timeline->keyframes.size() != 3) {
        std::cerr << "Animation event timeline parsing did not preserve the fixture keyframes.\n";
        return false;
    }

    if (!require_near(event_timeline->keyframes[0].time, 0.3, "event key 0 time") ||
        !require_near(event_timeline->keyframes[1].time, 0.3, "event key 1 time") ||
        !require_near(event_timeline->keyframes[2].time, 0.6, "event key 2 time") ||
        skeleton_data->events()[event_timeline->keyframes[0].event_index].name != "footstep" ||
        skeleton_data->events()[event_timeline->keyframes[1].event_index].name != "dust_vfx" ||
        skeleton_data->events()[event_timeline->keyframes[2].event_index].name != "footstep") {
        std::cerr << "Animation event keys did not preserve fixture order or event references.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    std::vector<marrow::runtime::AnimationEvent> emitted_events;
    skeleton.apply_animation(
        animation,
        0.0,
        0.2,
        [&](const marrow::runtime::AnimationEvent& event) {
            emitted_events.push_back(event);
        });
    if (!emitted_events.empty()) {
        std::cerr << "Event callbacks fired before the first keyed event time.\n";
        return false;
    }

    skeleton.apply_animation(
        animation,
        0.2,
        0.35,
        [&](const marrow::runtime::AnimationEvent& event) {
            emitted_events.push_back(event);
        });
    skeleton.apply_animation(
        animation,
        0.35,
        0.65,
        [&](const marrow::runtime::AnimationEvent& event) {
            emitted_events.push_back(event);
        });

    if (emitted_events.size() != 3) {
        std::cerr << "Runtime playback did not emit the expected number of animation events.\n";
        return false;
    }

    const auto& first_event = emitted_events[0];
    const auto& second_event = emitted_events[1];
    const auto& third_event = emitted_events[2];
    if (first_event.name != "footstep" ||
        !require_near(first_event.time, 0.3, "first event time") ||
        first_event.int_value != 0 ||
        !require_near(first_event.float_value, 0.35, "first event float") ||
        first_event.string_value != "left" ||
        !first_event.audio_path.has_value() ||
        *first_event.audio_path != "sfx/footstep.wav" ||
        !require_near(first_event.volume, 0.8, "first event volume") ||
        !require_near(first_event.balance, 0.0, "first event balance") ||
        second_event.name != "dust_vfx" ||
        !require_near(second_event.time, 0.3, "second event time") ||
        second_event.int_value != 2 ||
        !require_near(second_event.float_value, 0.65, "second event float") ||
        second_event.string_value != "landing_dust" ||
        second_event.audio_path.has_value() ||
        !require_near(second_event.volume, 1.0, "second event volume") ||
        !require_near(second_event.balance, 0.0, "second event balance") ||
        third_event.name != "footstep" ||
        !require_near(third_event.time, 0.6, "third event time") ||
        third_event.int_value != 1 ||
        !require_near(third_event.float_value, 0.35, "third event float") ||
        third_event.string_value != "right" ||
        !third_event.audio_path.has_value() ||
        *third_event.audio_path != "sfx/footstep.wav" ||
        !require_near(third_event.volume, 0.8, "third event volume") ||
        !require_near(third_event.balance, 0.25, "third event balance")) {
        std::cerr << "Animation event callbacks did not surface the expected resolved event data.\n";
        return false;
    }

    std::cout << "Loaded runtime animation events: " << animation.name << '\n'
              << "  eventDefs=" << skeleton_data->events().size()
              << ", eventKeys=" << event_timeline->keyframes.size() << '\n'
              << "  callbacks="
              << first_event.name << ":" << first_event.string_value << "@"
              << first_event.time << ", "
              << second_event.name << ":" << second_event.string_value << "@"
              << second_event.time << ", "
              << third_event.name << ":" << third_event.string_value << "@"
              << third_event.time << '\n';
    return true;
}

bool validate_runtime_slot_timelines(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto* animation_ptr = skeleton_data->find_animation("idle");
    if (animation_ptr == nullptr) {
        std::cerr << "Fixture did not load any animations.\n";
        return false;
    }

    const auto body_slot_index = skeleton_data->find_slot_index("body");
    const auto arm_slot_index = skeleton_data->find_slot_index("arm_l");
    if (!body_slot_index.has_value() || !arm_slot_index.has_value()) {
        std::cerr << "Fixture lost the expected slot indices.\n";
        return false;
    }

    const auto& animation = *animation_ptr;
    const marrow::runtime::SlotAttachmentTimeline* attachment_timeline =
        animation.find_attachment_timeline(*body_slot_index);
    const marrow::runtime::SlotColorTimeline* color_timeline =
        animation.find_color_timeline(*body_slot_index);
    const marrow::runtime::DrawOrderTimeline* draw_order_timeline =
        animation.find_draw_order_timeline();
    if (attachment_timeline == nullptr || attachment_timeline->keyframes.size() != 3 ||
        color_timeline == nullptr || color_timeline->keyframes.size() != 3 ||
        draw_order_timeline == nullptr || draw_order_timeline->keyframes.size() != 3) {
        std::cerr << "Slot timeline parsing did not preserve the fixture keyframes.\n";
        return false;
    }

    const marrow::runtime::AttachmentKeyframe* attachment_start =
        animation.sample_slot_attachment(*body_slot_index, 0.0);
    const marrow::runtime::AttachmentKeyframe* attachment_swap =
        animation.sample_slot_attachment(*body_slot_index, 0.625);
    const marrow::runtime::AttachmentKeyframe* attachment_end =
        animation.sample_slot_attachment(*body_slot_index, 1.0);
    const std::optional<marrow::runtime::SlotColor> color_linear =
        animation.sample_slot_color(*body_slot_index, 0.25);
    const std::optional<marrow::runtime::SlotColor> color_stepped =
        animation.sample_slot_color(*body_slot_index, 0.625);
    const std::optional<marrow::runtime::SlotColor> color_end =
        animation.sample_slot_color(*body_slot_index, 1.0);
    const marrow::runtime::DrawOrderKeyframe* draw_order_start = animation.sample_draw_order(0.0);
    const marrow::runtime::DrawOrderKeyframe* draw_order_swap = animation.sample_draw_order(0.625);
    const marrow::runtime::DrawOrderKeyframe* draw_order_end = animation.sample_draw_order(1.0);

    if (attachment_start == nullptr || attachment_swap == nullptr || attachment_end == nullptr ||
        !color_linear.has_value() || !color_stepped.has_value() || !color_end.has_value() ||
        draw_order_start == nullptr || draw_order_swap == nullptr || draw_order_end == nullptr) {
        std::cerr << "Slot timeline sampling did not find all timeline values.\n";
        return false;
    }

    if (!attachment_start->attachment_name.has_value() ||
        *attachment_start->attachment_name != "body" ||
        !attachment_swap->attachment_name.has_value() ||
        *attachment_swap->attachment_name != "warrior_body" ||
        !attachment_end->attachment_name.has_value() ||
        *attachment_end->attachment_name != "body") {
        std::cerr << "Attachment timeline sampling did not preserve the fixture swaps.\n";
        return false;
    }

    if (!require_slot_color(*color_linear, 0.8, 0.9, 1.0, 0.75, "slot color linear") ||
        !require_slot_color(*color_stepped, 0.6, 0.8, 1.0, 0.5, "slot color stepped") ||
        !require_slot_color(*color_end, 1.0, 1.0, 1.0, 1.0, "slot color end")) {
        std::cerr << "Slot color timeline evaluators did not reproduce the fixture data.\n";
        return false;
    }

    const std::vector<std::size_t> expected_default_draw_order =
        sequential_draw_order(skeleton_data->slots().size());
    const std::vector<std::size_t> expected_swapped_draw_order =
        swapped_front_draw_order(skeleton_data->slots().size());
    if (draw_order_start->slot_indices != expected_default_draw_order ||
        draw_order_swap->slot_indices != expected_swapped_draw_order ||
        draw_order_end->slot_indices != expected_default_draw_order) {
        std::cerr << "Draw order timeline sampling did not preserve the fixture order.\n";
        return false;
    }

    Skeleton skeleton(skeleton_data);
    const auto warrior_skin_index = skeleton_data->find_skin_index("warrior");
    if (!warrior_skin_index.has_value()) {
        std::cerr << "Fixture lost the warrior skin index needed by attachment swaps.\n";
        return false;
    }

    skeleton.apply_animation(animation, 0.625);
    if (skeleton.slot_states()[*body_slot_index].attachment_name != "warrior_body" ||
        skeleton.slot_states()[*body_slot_index].attachment_skin_index != warrior_skin_index ||
        skeleton.draw_order() != expected_swapped_draw_order ||
        !require_slot_color(
            skeleton.slot_states()[*body_slot_index].color,
            0.6,
            0.8,
            1.0,
            0.5,
            "applied slot color") ||
        !require_optional_slot_color(
            skeleton.slot_states()[*body_slot_index].dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "applied slot dark color") ||
        !require_slot_color(
            skeleton.slot_states()[*arm_slot_index].color,
            1.0,
            1.0,
            1.0,
            1.0,
            "untouched slot color") ||
        !require_missing_slot_color(
            skeleton.slot_states()[*arm_slot_index].dark_color,
            "untouched slot dark color")) {
        std::cerr << "Applying slot timelines did not update slot presentation state over time.\n";
        return false;
    }

    const marrow::runtime::AttachmentData* current_attachment =
        skeleton.current_attachment(*body_slot_index);
    if (current_attachment == nullptr ||
        current_attachment->name != "warrior_body" ||
        current_attachment->region_name != "warrior_body") {
        std::cerr << "Current attachment resolution did not follow the sampled slot swap.\n";
        return false;
    }

    skeleton.apply_animation(animation, 1.0);
    if (skeleton.slot_states()[*body_slot_index].attachment_name != "body" ||
        skeleton.draw_order() != expected_default_draw_order ||
        !require_slot_color(
            skeleton.slot_states()[*body_slot_index].color,
            1.0,
            1.0,
            1.0,
            1.0,
            "restored slot color") ||
        !require_optional_slot_color(
            skeleton.slot_states()[*body_slot_index].dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "restored slot dark color")) {
        std::cerr << "Slot timelines did not restore the final keyed presentation state.\n";
        return false;
    }

    std::cout << "Loaded runtime slot timelines: " << animation.name << '\n'
              << "  attachmentKeys=" << attachment_timeline->keyframes.size()
              << ", colorKeys=" << color_timeline->keyframes.size()
              << ", drawOrderKeys=" << draw_order_timeline->keyframes.size() << '\n'
              << "  attachment@0.625=" << *attachment_swap->attachment_name
              << ", color@0.625=(" << color_stepped->r << ", " << color_stepped->g << ", "
              << color_stepped->b << ", " << color_stepped->a << ")"
              << ", drawOrder@0.625=[" << draw_order_swap->slot_indices[0] << ", "
              << draw_order_swap->slot_indices[1] << "]\n";
    return true;
}

bool validate_runtime_animation_state(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto* idle = skeleton_data->find_animation("idle");
    const auto* attack = skeleton_data->find_animation("attack");
    const auto* aim = skeleton_data->find_animation("aim");
    const auto spine_index = skeleton_data->find_bone_index("spine");
    const auto arm_index = skeleton_data->find_bone_index("arm_l");
    const auto body_slot_index = skeleton_data->find_slot_index("body");
    if (idle == nullptr || attack == nullptr || aim == nullptr ||
        !spine_index.has_value() || !arm_index.has_value() || !body_slot_index.has_value()) {
        std::cerr << "AnimationState validation requires the idle, attack, and aim fixture data.\n";
        return false;
    }

    if (!require_near(skeleton_data->default_mix_duration(), 0.2, "default mix duration") ||
        !require_near(skeleton_data->mix_duration("attack", "aim"), 0.1, "attack->aim mix duration") ||
        !require_near(skeleton_data->mix_duration("idle", "idle"), 0.2, "default wildcard mix")) {
        std::cerr << "SkeletonData did not preserve the fixture AnimationState mix settings.\n";
        return false;
    }

    auto record_event = [](
                            std::vector<StateEventRecord>* records,
                            AnimationStateEventType type,
                            const std::shared_ptr<TrackEntry>& entry,
                            const marrow::runtime::AnimationEvent* event) {
        StateEventRecord record;
        record.type = type;
        record.track_index = entry != nullptr ? entry->track_index : 0;
        record.animation_name = entry != nullptr ? entry->animation_name : std::string{};
        if (event != nullptr) {
            record.event_name = event->name;
        }
        records->push_back(std::move(record));
    };

    std::vector<StateEventRecord> state_events;
    std::vector<StateEventRecord> idle_entry_events;
    std::vector<StateEventRecord> attack_entry_events;
    std::vector<StateEventRecord> aim_entry_events;
    AnimationState state(skeleton_data);
    state.set_listener(
        [&](AnimationState&,
            AnimationStateEventType type,
            const std::shared_ptr<TrackEntry>& entry,
            const marrow::runtime::AnimationEvent* event) {
            record_event(&state_events, type, entry, event);
        });

    Skeleton skeleton(skeleton_data);
    std::shared_ptr<TrackEntry> idle_track = state.set_animation(0, "idle", true);
    idle_track->set_listener(
        [&](AnimationState&,
            AnimationStateEventType type,
            const std::shared_ptr<TrackEntry>& entry,
            const marrow::runtime::AnimationEvent* event) {
            record_event(&idle_entry_events, type, entry, event);
        });
    state.update(0.35);
    state.apply(skeleton);

    const std::optional<double> idle_spine_035 =
        idle->sample_bone_rotation(*spine_index, 0.35);
    const int idle_start_count =
        count_state_events(state_events, AnimationStateEventType::Start, 0, "idle");
    const int idle_entry_start_count =
        count_state_events(idle_entry_events, AnimationStateEventType::Start, 0, "idle");
    const int idle_footstep_count =
        count_state_events(state_events, AnimationStateEventType::Event, 0, "idle", "footstep");
    const int idle_dust_count =
        count_state_events(state_events, AnimationStateEventType::Event, 0, "idle", "dust_vfx");
    const int idle_entry_footstep_count =
        count_state_events(idle_entry_events, AnimationStateEventType::Event, 0, "idle", "footstep");
    const int idle_entry_dust_count =
        count_state_events(idle_entry_events, AnimationStateEventType::Event, 0, "idle", "dust_vfx");
    if (!idle_spine_035.has_value() ||
        !require_near(
            skeleton.bone_poses()[*spine_index].local_pose.rotation,
            *idle_spine_035,
            "track0 idle spine rotation") ||
        idle_start_count != 1 ||
        idle_entry_start_count != 1 ||
        idle_footstep_count != 1 ||
        idle_dust_count != 1 ||
        idle_entry_footstep_count != 1 ||
        idle_entry_dust_count != 1) {
        std::cerr << "Looping idle playback did not emit the expected start or event callbacks"
                  << " (start=" << idle_start_count
                  << ", localStart=" << idle_entry_start_count
                  << ", footstep=" << idle_footstep_count
                  << ", dust=" << idle_dust_count
                  << ", localFootstep=" << idle_entry_footstep_count
                  << ", localDust=" << idle_entry_dust_count << ").\n";
        return false;
    }

    std::shared_ptr<TrackEntry> attack_track = state.set_animation(1, "attack", false);
    attack_track->set_listener(
        [&](AnimationState&,
            AnimationStateEventType type,
            const std::shared_ptr<TrackEntry>& entry,
            const marrow::runtime::AnimationEvent* event) {
            record_event(&attack_entry_events, type, entry, event);
        });
    std::shared_ptr<TrackEntry> queued_aim =
        state.add_animation(1, "aim", false, 0.0);
    queued_aim->set_listener(
        [&](AnimationState&,
            AnimationStateEventType type,
            const std::shared_ptr<TrackEntry>& entry,
            const marrow::runtime::AnimationEvent* event) {
            record_event(&aim_entry_events, type, entry, event);
        });

    if (!require_near(queued_aim->mix_duration, 0.1, "queued aim mix duration") ||
        count_state_events(attack_entry_events, AnimationStateEventType::Start, 1, "attack") != 1) {
        std::cerr << "TrackEntry listeners did not receive the immediate attack start or the queued aim mix duration.\n";
        return false;
    }

    state.update(0.2);
    state.apply(skeleton);

    const std::optional<double> idle_spine_055 =
        idle->sample_bone_rotation(*spine_index, 0.55);
    const std::optional<double> attack_arm_020 =
        attack->sample_bone_rotation(*arm_index, 0.2);
    const std::optional<marrow::runtime::SlotColor> idle_body_color_055 =
        idle->sample_slot_color(*body_slot_index, 0.55);
    if (!idle_spine_055.has_value() || !attack_arm_020.has_value() ||
        !idle_body_color_055.has_value() ||
        !require_near(
            skeleton.bone_poses()[*spine_index].local_pose.rotation,
            *idle_spine_055,
            "independent track spine rotation") ||
        !require_near(
            skeleton.bone_poses()[*arm_index].local_pose.rotation,
            *attack_arm_020,
            "independent track arm rotation") ||
        !require_slot_color(
            skeleton.slot_states()[*body_slot_index].color,
            idle_body_color_055->r,
            idle_body_color_055->g,
            idle_body_color_055->b,
            idle_body_color_055->a,
            "independent track body color")) {
        std::cerr << "Independent track application did not preserve lower-track and upper-track state.\n";
        return false;
    }

    state.update(0.25);
    state.apply(skeleton);

    const std::shared_ptr<TrackEntry> current_track_one = state.get_current(1);
    if (current_track_one == nullptr ||
        current_track_one->animation_name != "aim" ||
        current_track_one->mixing_from == nullptr ||
        !require_near(current_track_one->mix_duration, 0.1, "active aim mix duration")) {
        std::cerr << "Queued attack->aim transition did not promote the next entry with mixing.\n";
        return false;
    }

    const std::optional<double> attack_arm_mix =
        attack->sample_bone_rotation(*arm_index, current_track_one->mixing_from->animation_time());
    const std::optional<double> aim_arm_mix =
        aim->sample_bone_rotation(*arm_index, current_track_one->animation_time());
    const double mix_alpha = current_track_one->mix_time / current_track_one->mix_duration;
    if (!attack_arm_mix.has_value() || !aim_arm_mix.has_value() ||
        !require_near(
            skeleton.bone_poses()[*arm_index].local_pose.rotation,
            *attack_arm_mix * (1.0 - mix_alpha) + *aim_arm_mix * mix_alpha,
            "crossfade arm rotation") ||
        count_state_events(state_events, AnimationStateEventType::Complete, 1, "attack") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Interrupt, 1, "attack") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Start, 1, "aim") != 1 ||
        count_state_events(aim_entry_events, AnimationStateEventType::Start, 1, "aim") != 1) {
        std::cerr << "Queued crossfade did not emit the expected transition callbacks.\n";
        return false;
    }

    state.set_empty_animation(1, 0.3);
    state.update(0.15);
    state.apply(skeleton);

    const std::shared_ptr<TrackEntry> empty_track_one = state.get_current(1);
    if (empty_track_one == nullptr ||
        empty_track_one->animation_name != "<empty>" ||
        empty_track_one->mixing_from == nullptr ||
        empty_track_one->mixing_from->animation == nullptr) {
        std::cerr << "Empty-animation fade-out did not keep the outgoing animation chain alive.\n";
        return false;
    }

    const std::optional<double> aim_arm_fade =
        aim->sample_bone_rotation(*arm_index, empty_track_one->mixing_from->animation_time());
    const double fade_alpha = 1.0 - (empty_track_one->mix_time / empty_track_one->mix_duration);
    if (!aim_arm_fade.has_value() ||
        !require_near(
            skeleton.bone_poses()[*arm_index].local_pose.rotation,
            *aim_arm_fade * fade_alpha,
            "empty animation fade arm rotation") ||
        count_state_events(state_events, AnimationStateEventType::Interrupt, 1, "aim") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Start, 1, "<empty>") != 1) {
        std::cerr << "Empty-animation fade-out did not preserve the outgoing pose or callbacks.\n";
        return false;
    }

    state.update(0.2);
    state.apply(skeleton);

    const std::optional<double> idle_spine_015 =
        idle->sample_bone_rotation(*spine_index, 0.15);
    if (state.get_current(1) != nullptr ||
        !idle_spine_015.has_value() ||
        !require_near(
            skeleton.bone_poses()[*spine_index].local_pose.rotation,
            *idle_spine_015,
            "post-fade idle spine rotation") ||
        !require_near(
            skeleton.bone_poses()[*arm_index].local_pose.rotation,
            0.0,
            "post-fade arm rotation") ||
        count_state_events(state_events, AnimationStateEventType::Complete, 0, "idle") != 1 ||
        count_state_events(state_events, AnimationStateEventType::End, 1, "attack") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Dispose, 1, "attack") != 1 ||
        count_state_events(state_events, AnimationStateEventType::End, 1, "aim") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Dispose, 1, "aim") != 1 ||
        count_state_events(state_events, AnimationStateEventType::End, 1, "<empty>") != 1 ||
        count_state_events(state_events, AnimationStateEventType::Dispose, 1, "<empty>") != 1 ||
        count_state_events(idle_entry_events, AnimationStateEventType::Complete, 0, "idle") != 1 ||
        count_state_events(idle_entry_events, AnimationStateEventType::Event, 0, "idle", "footstep") != 2 ||
        count_state_events(idle_entry_events, AnimationStateEventType::Event, 0, "idle", "dust_vfx") != 1 ||
        count_state_events(attack_entry_events, AnimationStateEventType::Complete, 1, "attack") != 1 ||
        count_state_events(attack_entry_events, AnimationStateEventType::Interrupt, 1, "attack") != 1 ||
        count_state_events(attack_entry_events, AnimationStateEventType::End, 1, "attack") != 1 ||
        count_state_events(attack_entry_events, AnimationStateEventType::Dispose, 1, "attack") != 1 ||
        count_state_events(aim_entry_events, AnimationStateEventType::Interrupt, 1, "aim") != 1 ||
        count_state_events(aim_entry_events, AnimationStateEventType::End, 1, "aim") != 1 ||
        count_state_events(aim_entry_events, AnimationStateEventType::Dispose, 1, "aim") != 1) {
        std::cerr << "AnimationState callbacks did not cover completion, end, and dispose semantics.\n";
        return false;
    }

    std::cout << "Loaded runtime AnimationState: tracks=2\n"
              << "  defaultMix=" << skeleton_data->default_mix_duration()
              << ", attack->aim=" << skeleton_data->mix_duration("attack", "aim") << '\n'
              << "  callbacks=start/interrupt/complete/end/dispose/event verified"
              << ", localAimEvents=" << aim_entry_events.size() << '\n';
    return true;
}

bool validate_runtime_reverse_playback_and_root_motion(
    const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto* idle = skeleton_data->find_animation("idle");
    const auto root_index = skeleton_data->find_bone_index("root");
    const auto spine_index = skeleton_data->find_bone_index("spine");
    if (idle == nullptr || !root_index.has_value() || !spine_index.has_value()) {
        std::cerr << "Reverse playback validation requires the idle animation plus root and spine bones.\n";
        return false;
    }

    Skeleton forward_skeleton(skeleton_data);
    AnimationState forward_state(skeleton_data);
    std::shared_ptr<TrackEntry> forward_track = forward_state.set_animation(0, "idle", true);
    forward_state.update(0.35);
    forward_state.apply(forward_skeleton);

    const std::optional<marrow::runtime::VectorSample> forward_root_sample =
        idle->sample_bone_translation(*root_index, 0.35);
    const std::optional<double> forward_spine_sample =
        idle->sample_bone_rotation(*spine_index, 0.35);
    const marrow::runtime::RootMotionDelta forward_root_motion_a =
        forward_state.extract_root_motion(0, *root_index);
    if (forward_track == nullptr || !forward_root_sample.has_value() ||
        !forward_spine_sample.has_value() ||
        !require_near(forward_track->animation_time(), 0.35, "forward animation time @0.35") ||
        !require_near(
            forward_skeleton.bone_poses()[*root_index].local_pose.x,
            forward_root_sample->x,
            "forward root x @0.35") ||
        !require_near(
            forward_skeleton.bone_poses()[*root_index].local_pose.y,
            forward_root_sample->y,
            "forward root y @0.35") ||
        !require_near(
            forward_skeleton.bone_poses()[*spine_index].local_pose.rotation,
            *forward_spine_sample,
            "forward spine rotation @0.35") ||
        !require_near(forward_root_motion_a.x, 14.0, "forward root motion x @0.35") ||
        !require_near(forward_root_motion_a.y, 7.0, "forward root motion y @0.35")) {
        std::cerr << "Forward playback did not expose the expected root pose or root-motion delta.\n";
        return false;
    }

    forward_state.update(0.2);
    forward_state.apply(forward_skeleton);
    const marrow::runtime::RootMotionDelta forward_root_motion_b =
        forward_state.extract_root_motion(0, *root_index);
    if (!require_near(forward_track->animation_time(), 0.55, "forward animation time @0.55") ||
        !require_near(forward_root_motion_b.x, 8.0, "forward root motion x @0.55") ||
        !require_near(forward_root_motion_b.y, 2.0, "forward root motion y @0.55") ||
        (std::abs(forward_root_motion_a.x - forward_root_motion_b.x) <= 1e-6 &&
         std::abs(forward_root_motion_a.y - forward_root_motion_b.y) <= 1e-6)) {
        std::cerr << "Forward root motion did not change over playback time.\n";
        return false;
    }

    Skeleton reverse_skeleton(skeleton_data);
    AnimationState reverse_state(skeleton_data);
    std::shared_ptr<TrackEntry> reverse_track = reverse_state.set_animation(0, "idle", true);
    if (reverse_track == nullptr) {
        std::cerr << "Reverse playback validation could not create the idle track.\n";
        return false;
    }
    reverse_track->reverse = true;
    reverse_state.update(0.35);
    reverse_state.apply(reverse_skeleton);

    const std::optional<marrow::runtime::VectorSample> reverse_root_sample =
        idle->sample_bone_translation(*root_index, 0.65);
    const std::optional<double> reverse_spine_sample =
        idle->sample_bone_rotation(*spine_index, 0.65);
    const marrow::runtime::RootMotionDelta reverse_root_motion_a =
        reverse_state.extract_root_motion(0, *root_index);
    if (!reverse_root_sample.has_value() || !reverse_spine_sample.has_value() ||
        !require_near(reverse_track->animation_time(), 0.65, "reverse animation time @0.35") ||
        !require_near(
            reverse_skeleton.bone_poses()[*root_index].local_pose.x,
            reverse_root_sample->x,
            "reverse root x @0.35") ||
        !require_near(
            reverse_skeleton.bone_poses()[*root_index].local_pose.y,
            reverse_root_sample->y,
            "reverse root y @0.35") ||
        !require_near(
            reverse_skeleton.bone_poses()[*spine_index].local_pose.rotation,
            *reverse_spine_sample,
            "reverse spine rotation @0.35") ||
        !require_near(reverse_root_motion_a.x, -14.0, "reverse root motion x @0.35") ||
        !require_near(reverse_root_motion_a.y, 7.0, "reverse root motion y @0.35")) {
        std::cerr << "Reverse playback did not sample the idle timelines backward or expose the reverse root-motion delta.\n";
        return false;
    }

    reverse_state.update(0.2);
    reverse_state.apply(reverse_skeleton);
    const marrow::runtime::RootMotionDelta reverse_root_motion_b =
        reverse_state.extract_root_motion(0, *root_index);
    if (!require_near(reverse_track->animation_time(), 0.45, "reverse animation time @0.55") ||
        !require_near(reverse_root_motion_b.x, -8.0, "reverse root motion x @0.55") ||
        !require_near(reverse_root_motion_b.y, 2.0, "reverse root motion y @0.55")) {
        std::cerr << "Reverse playback did not continue sampling backward across subsequent updates.\n";
        return false;
    }

    std::cout << "Loaded reverse playback and root motion: " << idle->name << '\n'
              << "  forwardDelta@0.35=(" << forward_root_motion_a.x << ", "
              << forward_root_motion_a.y << ")"
              << ", forwardDelta@0.55=(" << forward_root_motion_b.x << ", "
              << forward_root_motion_b.y << ")\n"
              << "  reverseTime@0.55=" << reverse_track->animation_time()
              << ", reverseDelta@0.35=(" << reverse_root_motion_a.x << ", "
              << reverse_root_motion_a.y << ")"
              << ", reverseDelta@0.55=(" << reverse_root_motion_b.x << ", "
              << reverse_root_motion_b.y << ")\n";
    return true;
}

bool validate_runtime_ik_constraints(const std::filesystem::path& fixture_path) {
    const std::optional<Document> document = load_document_or_print(fixture_path);
    if (!document.has_value()) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(*document);
    if (!skeleton_result) {
        return print_error(*skeleton_result.error);
    }

    const auto find_constraint = [&](std::string_view name)
        -> const marrow::runtime::IkConstraintData* {
        for (const marrow::runtime::IkConstraintData& constraint :
             skeleton_result.skeleton_data->ik_constraints()) {
            if (constraint.name == name) {
                return &constraint;
            }
        }
        return nullptr;
    };

    const auto upper_arm_pos_index = skeleton_result.skeleton_data->find_bone_index("upper_arm_pos");
    const auto lower_arm_pos_index = skeleton_result.skeleton_data->find_bone_index("lower_arm_pos");
    const auto hand_pos_index = skeleton_result.skeleton_data->find_bone_index("hand_pos");
    const auto target_pos_index = skeleton_result.skeleton_data->find_bone_index("target_pos");
    const auto upper_arm_neg_index = skeleton_result.skeleton_data->find_bone_index("upper_arm_neg");
    const auto lower_arm_neg_index = skeleton_result.skeleton_data->find_bone_index("lower_arm_neg");
    const auto hand_neg_index = skeleton_result.skeleton_data->find_bone_index("hand_neg");
    const auto target_neg_index = skeleton_result.skeleton_data->find_bone_index("target_neg");
    const auto turret_full_index = skeleton_result.skeleton_data->find_bone_index("turret_full");
    const auto muzzle_full_index = skeleton_result.skeleton_data->find_bone_index("muzzle_full");
    const auto target_full_index = skeleton_result.skeleton_data->find_bone_index("target_full");
    const auto turret_half_index = skeleton_result.skeleton_data->find_bone_index("turret_half");
    const auto muzzle_half_index = skeleton_result.skeleton_data->find_bone_index("muzzle_half");
    const auto target_half_index = skeleton_result.skeleton_data->find_bone_index("target_half");
    if (!upper_arm_pos_index.has_value() || !lower_arm_pos_index.has_value() ||
        !hand_pos_index.has_value() || !target_pos_index.has_value() ||
        !upper_arm_neg_index.has_value() || !lower_arm_neg_index.has_value() ||
        !hand_neg_index.has_value() || !target_neg_index.has_value() ||
        !turret_full_index.has_value() || !muzzle_full_index.has_value() ||
        !target_full_index.has_value() || !turret_half_index.has_value() ||
        !muzzle_half_index.has_value() || !target_half_index.has_value()) {
        std::cerr << "IK fixture lost required bone lookups.\n";
        return false;
    }

    const marrow::runtime::IkConstraintData* arm_positive = find_constraint("arm_positive");
    const marrow::runtime::IkConstraintData* arm_negative = find_constraint("arm_negative");
    const marrow::runtime::IkConstraintData* turret_reach = find_constraint("turret_reach");
    const marrow::runtime::IkConstraintData* turret_half_mix = find_constraint("turret_half_mix");
    if (skeleton_result.skeleton_data->ik_constraints().size() != 4 ||
        arm_positive == nullptr || arm_negative == nullptr ||
        turret_reach == nullptr || turret_half_mix == nullptr) {
        std::cerr << "IK fixture did not preserve the expected constraint registry.\n";
        return false;
    }

    if (arm_positive->bone_indices !=
            std::vector<std::size_t>{*upper_arm_pos_index, *lower_arm_pos_index} ||
        arm_positive->target_bone_index != *target_pos_index ||
        !require_near(arm_positive->mix, 1.0, "arm_positive mix") ||
        !arm_positive->bend_positive ||
        arm_negative->bone_indices !=
            std::vector<std::size_t>{*upper_arm_neg_index, *lower_arm_neg_index} ||
        arm_negative->target_bone_index != *target_neg_index ||
        !require_near(arm_negative->mix, 1.0, "arm_negative mix") ||
        arm_negative->bend_positive ||
        turret_reach->bone_indices != std::vector<std::size_t>{*turret_full_index} ||
        turret_reach->target_bone_index != *target_full_index ||
        !require_near(turret_reach->mix, 1.0, "turret_reach mix") ||
        turret_half_mix->bone_indices != std::vector<std::size_t>{*turret_half_index} ||
        turret_half_mix->target_bone_index != *target_half_index ||
        !require_near(turret_half_mix->mix, 0.5, "turret_half_mix mix")) {
        std::cerr << "Parsed IK constraints did not preserve bones, targets, mix, or bend metadata.\n";
        return false;
    }

    Skeleton skeleton(skeleton_result.skeleton_data);
    if (!require_near(
            skeleton.bone_world_transforms()[*muzzle_full_index].world_x,
            skeleton.bone_world_transforms()[*target_full_index].world_x,
            "turret full mix muzzle x") ||
        !require_near(
            skeleton.bone_world_transforms()[*muzzle_full_index].world_y,
            skeleton.bone_world_transforms()[*target_full_index].world_y,
            "turret full mix muzzle y") ||
        !require_near(
            skeleton.bone_world_transforms()[*muzzle_half_index].world_x,
            340.0 + (60.0 * std::sqrt(0.5)),
            "turret half mix muzzle x") ||
        !require_near(
            skeleton.bone_world_transforms()[*muzzle_half_index].world_y,
            60.0 * std::sqrt(0.5),
            "turret half mix muzzle y")) {
        std::cerr << "One-bone IK solving did not preserve full-mix reach and partial-mix aiming.\n";
        return false;
    }

    if (!require_near(
            skeleton.bone_world_transforms()[*hand_pos_index].world_x,
            skeleton.bone_world_transforms()[*target_pos_index].world_x,
            "positive bend hand x") ||
        !require_near(
            skeleton.bone_world_transforms()[*hand_pos_index].world_y,
            skeleton.bone_world_transforms()[*target_pos_index].world_y,
            "positive bend hand y") ||
        !require_near(
            skeleton.bone_world_transforms()[*hand_neg_index].world_x,
            skeleton.bone_world_transforms()[*target_neg_index].world_x,
            "negative bend hand x") ||
        !require_near(
            skeleton.bone_world_transforms()[*hand_neg_index].world_y,
            skeleton.bone_world_transforms()[*target_neg_index].world_y,
            "negative bend hand y") ||
        skeleton.bone_world_transforms()[*lower_arm_pos_index].world_y <=
            skeleton.bone_world_transforms()[*upper_arm_pos_index].world_y ||
        skeleton.bone_world_transforms()[*lower_arm_neg_index].world_y >=
            skeleton.bone_world_transforms()[*upper_arm_neg_index].world_y) {
        std::cerr << "Two-bone IK solving did not reach the targets or respect bend direction.\n";
        return false;
    }

    skeleton.bone_poses()[*target_full_index].local_pose.y = -60.0;
    skeleton.update_world_transforms();
    if (!require_near(
            skeleton.bone_world_transforms()[*muzzle_full_index].world_x,
            220.0,
            "moved turret muzzle x") ||
        !require_near(
            skeleton.bone_world_transforms()[*muzzle_full_index].world_y,
            -60.0,
            "moved turret muzzle y")) {
        std::cerr << "IK solving did not update when the target bone moved.\n";
        return false;
    }

    std::cout << "Loaded runtime IK constraints: "
              << skeleton_result.skeleton_data->ik_constraints().size() << '\n'
              << "  oneBoneFull=("
              << skeleton.bone_world_transforms()[*muzzle_full_index].world_x << ", "
              << skeleton.bone_world_transforms()[*muzzle_full_index].world_y << ")"
              << ", oneBoneHalf=("
              << skeleton.bone_world_transforms()[*muzzle_half_index].world_x << ", "
              << skeleton.bone_world_transforms()[*muzzle_half_index].world_y << ")\n"
              << "  bendPositiveElbowY="
              << skeleton.bone_world_transforms()[*lower_arm_pos_index].world_y
              << ", bendNegativeElbowY="
              << skeleton.bone_world_transforms()[*lower_arm_neg_index].world_y << '\n';
    return true;
}

bool validate_runtime_path_transform_constraints(const std::filesystem::path& fixture_path) {
    const std::optional<Document> document = load_document_or_print(fixture_path);
    if (!document.has_value()) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(*document);
    if (!skeleton_result) {
        return print_error(*skeleton_result.error);
    }

    const auto find_path_constraint = [&](std::string_view name)
        -> const marrow::runtime::PathConstraintData* {
        for (const marrow::runtime::PathConstraintData& constraint :
             skeleton_result.skeleton_data->path_constraints()) {
            if (constraint.name == name) {
                return &constraint;
            }
        }
        return nullptr;
    };
    const auto find_transform_constraint = [&](std::string_view name)
        -> const marrow::runtime::TransformConstraintData* {
        for (const marrow::runtime::TransformConstraintData& constraint :
             skeleton_result.skeleton_data->transform_constraints()) {
            if (constraint.name == name) {
                return &constraint;
            }
        }
        return nullptr;
    };

    const auto guide_slot_index = skeleton_result.skeleton_data->find_slot_index("guide");
    const auto path_a_index = skeleton_result.skeleton_data->find_bone_index("path_a");
    const auto path_b_index = skeleton_result.skeleton_data->find_bone_index("path_b");
    const auto path_c_index = skeleton_result.skeleton_data->find_bone_index("path_c");
    const auto source_index = skeleton_result.skeleton_data->find_bone_index("source");
    const auto target_index = skeleton_result.skeleton_data->find_bone_index("target");
    if (!guide_slot_index.has_value() || !path_a_index.has_value() ||
        !path_b_index.has_value() || !path_c_index.has_value() ||
        !source_index.has_value() || !target_index.has_value()) {
        std::cerr << "Path/transform fixture lost required bone or slot lookups.\n";
        return false;
    }

    const marrow::runtime::PathConstraintData* rope_follow =
        find_path_constraint("rope_follow");
    const marrow::runtime::TransformConstraintData* mirror_source =
        find_transform_constraint("mirror_source");
    if (skeleton_result.skeleton_data->path_constraints().size() != 1 ||
        skeleton_result.skeleton_data->transform_constraints().size() != 1 ||
        rope_follow == nullptr || mirror_source == nullptr) {
        std::cerr << "Path/transform fixture did not preserve the expected constraint registry.\n";
        return false;
    }

    if (rope_follow->slot_index != *guide_slot_index ||
        rope_follow->bone_indices !=
            std::vector<std::size_t>{*path_a_index, *path_b_index, *path_c_index} ||
        !require_near(rope_follow->position, 0.1, "rope_follow position") ||
        !require_near(rope_follow->spacing, 0.3, "rope_follow spacing") ||
        rope_follow->spacing_mode != marrow::runtime::PathConstraintSpacingMode::Percent ||
        !require_near(rope_follow->rotate_mix, 1.0, "rope_follow rotate mix") ||
        !require_near(rope_follow->translate_mix, 1.0, "rope_follow translate mix") ||
        mirror_source->source_bone_index != *source_index ||
        mirror_source->target_bone_indices != std::vector<std::size_t>{*target_index} ||
        !require_near(mirror_source->rotate_mix, 0.5, "mirror_source rotate mix") ||
        !require_near(mirror_source->translate_mix, 0.25, "mirror_source translate mix") ||
        !require_near(mirror_source->scale_mix, 1.0, "mirror_source scale mix") ||
        !require_near(mirror_source->shear_mix, 0.75, "mirror_source shear mix") ||
        !require_near(mirror_source->offsets.rotation, 15.0, "mirror_source offset rotation") ||
        !require_near(mirror_source->offsets.x, -10.0, "mirror_source offset x") ||
        !require_near(mirror_source->offsets.y, 20.0, "mirror_source offset y") ||
        !require_near(mirror_source->offsets.scale_x, 0.2, "mirror_source offset scaleX") ||
        !require_near(mirror_source->offsets.scale_y, -0.1, "mirror_source offset scaleY") ||
        !require_near(mirror_source->offsets.shear_x, 5.0, "mirror_source offset shearX") ||
        !require_near(mirror_source->offsets.shear_y, -2.0, "mirror_source offset shearY")) {
        std::cerr << "Parsed path or transform constraint metadata did not match the fixture.\n";
        return false;
    }

    Skeleton skeleton(skeleton_result.skeleton_data);
    const marrow::runtime::AttachmentData* guide_attachment =
        skeleton.current_attachment(*guide_slot_index);
    if (guide_attachment == nullptr || !guide_attachment->path_attachment.has_value() ||
        guide_attachment->path_attachment->control_points.size() != 7) {
        std::cerr << "Path constraint slot did not resolve to the authored path attachment.\n";
        return false;
    }

    const auto& path_a_world = skeleton.bone_world_transforms()[*path_a_index];
    const auto& path_b_world = skeleton.bone_world_transforms()[*path_b_index];
    const auto& path_c_world = skeleton.bone_world_transforms()[*path_c_index];
    if (!require_near(path_a_world.world_x, 20.0, "path_a world x") ||
        !require_near(path_a_world.world_y, 0.0, "path_a world y") ||
        !require_near(path_b_world.world_x, 80.0, "path_b world x") ||
        !require_near(path_b_world.world_y, 0.0, "path_b world y") ||
        !require_near(path_c_world.world_x, 100.0, "path_c world x") ||
        !require_near(path_c_world.world_y, 40.0, "path_c world y") ||
        !require_near(path_a_world.a, 1.0, "path_a axis a") ||
        !require_near(path_a_world.c, 0.0, "path_a axis c") ||
        !require_near(path_c_world.a, 0.0, "path_c axis a") ||
        !require_near(path_c_world.b, -1.0, "path_c axis b") ||
        !require_near(path_c_world.c, 1.0, "path_c axis c") ||
        !require_near(path_c_world.d, 0.0, "path_c axis d")) {
        std::cerr << "Path constraint solving did not place the chain on the authored guide.\n";
        return false;
    }

    const double expected_rotation = 12.5;
    const double expected_x = 287.5;
    const double expected_y = 0.0;
    const double expected_scale_x = 1.7;
    const double expected_scale_y = 0.65;
    const double expected_shear_x = 11.25;
    const double expected_shear_y = -5.25;
    const double rotation_x_radians =
        (expected_rotation + expected_shear_x) * 3.14159265358979323846 / 180.0;
    const double rotation_y_radians =
        (expected_rotation + 90.0 + expected_shear_y) * 3.14159265358979323846 / 180.0;
    const auto& target_world = skeleton.bone_world_transforms()[*target_index];
    if (!require_near(target_world.world_x, expected_x, "transform target world x") ||
        !require_near(target_world.world_y, expected_y, "transform target world y") ||
        !require_near(
            target_world.a,
            std::cos(rotation_x_radians) * expected_scale_x,
            "transform target axis a") ||
        !require_near(
            target_world.b,
            std::cos(rotation_y_radians) * expected_scale_y,
            "transform target axis b") ||
        !require_near(
            target_world.c,
            std::sin(rotation_x_radians) * expected_scale_x,
            "transform target axis c") ||
        !require_near(
            target_world.d,
            std::sin(rotation_y_radians) * expected_scale_y,
            "transform target axis d")) {
        std::cerr << "Transform constraint solving did not copy translation, rotation, scale, or shear as expected.\n";
        return false;
    }

    std::cout << "Loaded runtime path/transform constraints: path="
              << skeleton_result.skeleton_data->path_constraints().size()
              << ", transform="
              << skeleton_result.skeleton_data->transform_constraints().size() << '\n'
              << "  pathEnd=(" << path_c_world.world_x << ", " << path_c_world.world_y << ")"
              << ", target=(" << target_world.world_x << ", " << target_world.world_y << ")\n"
              << "  targetAxes=(" << target_world.a << ", " << target_world.b << ", "
              << target_world.c << ", " << target_world.d << ")\n";
    return true;
}

bool validate_runtime_physics_constraints(const std::filesystem::path& fixture_path) {
    const std::optional<Document> document = load_document_or_print(fixture_path);
    if (!document.has_value()) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(*document);
    if (!skeleton_result) {
        return print_error(*skeleton_result.error);
    }

    const auto find_constraint = [&](std::string_view name)
        -> const marrow::runtime::PhysicsConstraintData* {
        for (const marrow::runtime::PhysicsConstraintData& constraint :
             skeleton_result.skeleton_data->physics_constraints()) {
            if (constraint.name == name) {
                return &constraint;
            }
        }
        return nullptr;
    };

    const auto pivot_index = skeleton_result.skeleton_data->find_bone_index("pivot");
    const auto ribbon_01_index = skeleton_result.skeleton_data->find_bone_index("ribbon_01");
    const auto ribbon_02_index = skeleton_result.skeleton_data->find_bone_index("ribbon_02");
    const auto ribbon_tip_index = skeleton_result.skeleton_data->find_bone_index("ribbon_tip");
    if (!pivot_index.has_value() || !ribbon_01_index.has_value() ||
        !ribbon_02_index.has_value() || !ribbon_tip_index.has_value()) {
        std::cerr << "Physics fixture lost required bone lookups.\n";
        return false;
    }

    const marrow::runtime::PhysicsConstraintData* ribbon_secondary =
        find_constraint("ribbon_secondary");
    if (skeleton_result.skeleton_data->physics_constraints().size() != 1 ||
        ribbon_secondary == nullptr) {
        std::cerr << "Physics fixture did not preserve the expected constraint registry.\n";
        return false;
    }

    if (ribbon_secondary->bone_indices !=
            std::vector<std::size_t>{*ribbon_01_index, *ribbon_02_index} ||
        !require_near(ribbon_secondary->inertia, 0.85, "ribbon_secondary inertia") ||
        !require_near(ribbon_secondary->damping, 4.0, "ribbon_secondary damping") ||
        !require_near(ribbon_secondary->strength, 18.0, "ribbon_secondary strength") ||
        !require_near(ribbon_secondary->gravity.x, 0.0, "ribbon_secondary gravity.x") ||
        !require_near(ribbon_secondary->gravity.y, -24.0, "ribbon_secondary gravity.y") ||
        !require_near(ribbon_secondary->wind.x, 12.0, "ribbon_secondary wind.x") ||
        !require_near(ribbon_secondary->wind.y, 0.0, "ribbon_secondary wind.y") ||
        !require_near(ribbon_secondary->mix, 1.0, "ribbon_secondary mix")) {
        std::cerr << "Parsed physics constraint metadata did not match the fixture.\n";
        return false;
    }

    Skeleton skeleton(skeleton_result.skeleton_data);
    if (!require_near(
            skeleton.bone_world_transforms()[*ribbon_tip_index].world_x,
            210.0,
            "setup ribbon tip x") ||
        !require_near(
            skeleton.bone_world_transforms()[*ribbon_tip_index].world_y,
            0.0,
            "setup ribbon tip y")) {
        std::cerr << "Physics fixture setup pose did not preserve the authored chain lengths.\n";
        return false;
    }

    skeleton.bone_poses()[*pivot_index].local_pose.rotation = 90.0;
    skeleton.update_world_transforms();

    const auto lagged_tip = skeleton.bone_world_transforms()[*ribbon_tip_index];
    const double target_x = 0.0;
    const double target_y = 210.0;
    const double lagged_distance =
        std::hypot(lagged_tip.world_x - target_x, lagged_tip.world_y - target_y);
    if (lagged_distance <= 20.0) {
        std::cerr << "Physics constraint did not preserve visible lag after the driving bone rotated.\n";
        return false;
    }

    skeleton.update_physics(1.0 / 60.0);
    const auto first_step_tip = skeleton.bone_world_transforms()[*ribbon_tip_index];
    const double first_step_motion = std::hypot(
        first_step_tip.world_x - lagged_tip.world_x,
        first_step_tip.world_y - lagged_tip.world_y);
    if (first_step_motion <= 0.1) {
        std::cerr << "Physics step did not advance the constrained chain numerically.\n";
        return false;
    }

    for (int step = 0; step < 180; ++step) {
        skeleton.update_physics(1.0 / 60.0);
    }
    const auto settled_tip = skeleton.bone_world_transforms()[*ribbon_tip_index];
    const double settled_distance =
        std::hypot(settled_tip.world_x - target_x, settled_tip.world_y - target_y);
    if (settled_distance >= 1.0 ||
        settled_distance >= lagged_distance ||
        settled_tip.world_y <= lagged_tip.world_y) {
        std::cerr << "Physics constraint did not converge toward the driven pose after secondary-motion lag."
                  << " lagged=(" << lagged_tip.world_x << ", " << lagged_tip.world_y << ")"
                  << " settled=(" << settled_tip.world_x << ", " << settled_tip.world_y << ")"
                  << " target=(" << target_x << ", " << target_y << ")"
                  << " distances=(" << lagged_distance << ", " << settled_distance << ")\n";
        return false;
    }

    std::cout << "Loaded runtime physics constraints: "
              << skeleton_result.skeleton_data->physics_constraints().size() << '\n'
              << "  laggedTip=(" << lagged_tip.world_x << ", " << lagged_tip.world_y << ")"
              << ", firstStep=(" << first_step_tip.world_x << ", " << first_step_tip.world_y << ")"
              << ", settled=(" << settled_tip.world_x << ", " << settled_tip.world_y << ")\n"
              << "  target=(" << target_x << ", " << target_y << ")"
              << ", distances=(" << lagged_distance << ", " << settled_distance << ")\n";
    return true;
}

bool validate_runtime_atlas_model(
    const Document& atlas_document,
    const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto atlas_result = AtlasLoader::load(atlas_document);
    if (!atlas_result) {
        return print_error(*atlas_result.error);
    }

    const std::shared_ptr<const AtlasData>& atlas_data = atlas_result.atlas_data;
    if (atlas_data->info().name != "player_fixture" ||
        atlas_data->info().image != "player_fixture.png" ||
        !require_near(atlas_data->info().width, 256.0, "atlas width") ||
        !require_near(atlas_data->info().height, 256.0, "atlas height") ||
        atlas_data->info().filter_min != "linear" ||
        atlas_data->info().filter_mag != "linear" ||
        atlas_data->info().wrap_x != "clamp_to_edge" ||
        atlas_data->info().wrap_y != "clamp_to_edge") {
        std::cerr << "AtlasData did not preserve atlas metadata.\n";
        return false;
    }

    if (atlas_data->regions().size() != 9) {
        std::cerr << "AtlasData did not preserve fixture region count.\n";
        return false;
    }

    const auto* body_region = atlas_data->find_region("body");
    const auto* arm_region = atlas_data->find_region("arm_l");
    const auto* mage_arm_region = atlas_data->find_region("mage_arm_l");
    const auto* mage_body_region = atlas_data->find_region("mage_body");
    const auto* warrior_body_region = atlas_data->find_region("warrior_body");
    const auto* spark_region_0 = atlas_data->find_region("spark_fx_0");
    const auto* spark_region_1 = atlas_data->find_region("spark_fx_1");
    const auto* spark_region_2 = atlas_data->find_region("spark_fx_2");
    const auto* spark_region_3 = atlas_data->find_region("spark_fx_3");
    const auto* body_attachment_region =
        atlas_data->find_region_for_attachment(skeleton_data->slots()[0].setup_attachment);
    const auto* arm_attachment_region =
        atlas_data->find_region_for_attachment(skeleton_data->slots()[1].setup_attachment);

    if (body_region == nullptr || arm_region == nullptr ||
        mage_arm_region == nullptr || mage_body_region == nullptr || warrior_body_region == nullptr ||
        spark_region_0 == nullptr || spark_region_1 == nullptr ||
        spark_region_2 == nullptr || spark_region_3 == nullptr ||
        body_attachment_region == nullptr || arm_attachment_region == nullptr) {
        std::cerr << "Atlas region lookup failed for fixture names.\n";
        return false;
    }

    if (body_attachment_region != body_region || arm_attachment_region != arm_region) {
        std::cerr << "Attachment lookup did not resolve the same atlas regions as direct lookup.\n";
        return false;
    }

    if (!require_near(body_region->x, 0.0, "body region x") ||
        !require_near(body_region->y, 0.0, "body region y") ||
        !require_near(body_region->width, 128.0, "body region width") ||
        !require_near(body_region->height, 160.0, "body region height") ||
        !require_near(body_region->origin_x, 64.0, "body region origin_x") ||
        !require_near(body_region->origin_y, 80.0, "body region origin_y") ||
        !require_near(arm_region->x, 128.0, "arm_l region x") ||
        !require_near(arm_region->y, 0.0, "arm_l region y") ||
        !require_near(arm_region->width, 64.0, "arm_l region width") ||
        !require_near(arm_region->height, 96.0, "arm_l region height") ||
        !require_near(arm_region->origin_x, 16.0, "arm_l region origin_x") ||
        !require_near(arm_region->origin_y, 72.0, "arm_l region origin_y") ||
        !require_near(mage_arm_region->x, 192.0, "mage_arm_l region x") ||
        !require_near(mage_arm_region->y, 0.0, "mage_arm_l region y") ||
        !require_near(mage_arm_region->width, 64.0, "mage_arm_l region width") ||
        !require_near(mage_arm_region->height, 96.0, "mage_arm_l region height") ||
        !require_near(mage_body_region->x, 128.0, "mage_body region x") ||
        !require_near(mage_body_region->y, 96.0, "mage_body region y") ||
        !require_near(mage_body_region->width, 96.0, "mage_body region width") ||
        !require_near(mage_body_region->height, 96.0, "mage_body region height") ||
        !require_near(warrior_body_region->x, 0.0, "warrior_body region x") ||
        !require_near(warrior_body_region->y, 160.0, "warrior_body region y") ||
        !require_near(warrior_body_region->width, 128.0, "warrior_body region width") ||
        !require_near(warrior_body_region->height, 96.0, "warrior_body region height") ||
        !require_near(spark_region_0->x, 128.0, "spark_fx_0 region x") ||
        !require_near(spark_region_0->y, 192.0, "spark_fx_0 region y") ||
        !require_near(spark_region_0->width, 32.0, "spark_fx_0 region width") ||
        !require_near(spark_region_0->height, 32.0, "spark_fx_0 region height") ||
        !require_near(spark_region_1->x, 160.0, "spark_fx_1 region x") ||
        !require_near(spark_region_1->y, 192.0, "spark_fx_1 region y") ||
        !require_near(spark_region_2->x, 192.0, "spark_fx_2 region x") ||
        !require_near(spark_region_2->y, 192.0, "spark_fx_2 region y") ||
        !require_near(spark_region_3->x, 224.0, "spark_fx_3 region x") ||
        !require_near(spark_region_3->y, 192.0, "spark_fx_3 region y") ||
        !require_near(spark_region_3->origin_x, 16.0, "spark_fx_3 region origin_x") ||
        !require_near(spark_region_3->origin_y, 16.0, "spark_fx_3 region origin_y")) {
        std::cerr << "AtlasData regions did not preserve fixture geometry.\n";
        return false;
    }

    if (atlas_data->find_region("missing_region") != nullptr ||
        atlas_data->find_region_for_attachment("missing_attachment") != nullptr) {
        std::cerr << "Atlas lookup should return null for unknown regions.\n";
        return false;
    }

    std::cout << "Loaded runtime atlas model: " << atlas_data->info().name << '\n'
              << "  image=" << atlas_data->info().image
              << ", regions=" << atlas_data->regions().size() << '\n'
              << "  bodyOrigin=(" << body_region->origin_x << ", " << body_region->origin_y << ")"
              << ", armAttachment=" << skeleton_data->slots()[1].setup_attachment
              << ", warriorRegion=" << warrior_body_region->name
              << ", sparkRegion=" << spark_region_3->name << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path skeleton_path =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/fixtures/player_idle.mskl");
    const std::filesystem::path atlas_path =
        argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/fixtures/player_idle.matl");

    const std::optional<Document> skeleton_document = load_document_or_print(skeleton_path);
    const std::optional<Document> atlas_document = load_document_or_print(atlas_path);
    if (!skeleton_document.has_value() || !atlas_document.has_value()) {
        return 1;
    }

    const bool skeleton_ok = validate_fixture_skeleton(*skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(*skeleton_document);
    if (!skeleton_result) {
        print_error(*skeleton_result.error);
        return 1;
    }

    const bool runtime_skeleton_ok = validate_runtime_skeleton_model(skeleton_result.skeleton_data);
    const bool runtime_skin_ok = validate_runtime_skin_model(skeleton_result.skeleton_data);
    const bool runtime_inherit_skin_constraint_ok =
        validate_runtime_inherit_timeline_and_skin_constraints(
            "assets/fixtures/skin_inherit_constraints.mskl");
    const bool runtime_attachment_extensions_ok =
        validate_runtime_attachment_extensions(skeleton_result.skeleton_data);
    const bool runtime_clipping_and_sequence_ok =
        validate_runtime_clipping_and_sequence_attachments(skeleton_result.skeleton_data);
    const bool runtime_skeleton_bounds_ok =
        validate_runtime_skeleton_bounds(skeleton_result.skeleton_data);
    const bool runtime_weighted_mesh_ok =
        validate_runtime_weighted_mesh_model(skeleton_result.skeleton_data);
    const bool runtime_mesh_deform_ok =
        validate_runtime_mesh_deform_timelines(skeleton_result.skeleton_data);
    const bool runtime_slot_presentation_ok =
        validate_runtime_slot_presentation_model(skeleton_result.skeleton_data);
    const bool runtime_slot_blend_modes_ok = validate_runtime_slot_blend_modes();
    const bool runtime_animation_ok = validate_runtime_animation_curves(skeleton_result.skeleton_data);
    const bool runtime_event_ok =
        validate_runtime_animation_events(skeleton_result.skeleton_data);
    const bool runtime_slot_timelines_ok =
        validate_runtime_slot_timelines(skeleton_result.skeleton_data);
    const bool runtime_animation_state_ok =
        validate_runtime_animation_state(skeleton_result.skeleton_data);
    const bool runtime_reverse_and_root_motion_ok =
        validate_runtime_reverse_playback_and_root_motion(
            skeleton_result.skeleton_data);
    const bool runtime_ik_ok =
        validate_runtime_ik_constraints("assets/fixtures/ik_constraints.mskl");
    const bool runtime_path_transform_ok =
        validate_runtime_path_transform_constraints(
            "assets/fixtures/path_transform_constraints.mskl");
    const bool runtime_physics_ok =
        validate_runtime_physics_constraints("assets/fixtures/physics_constraints.mskl");
    const bool runtime_atlas_ok =
        validate_runtime_atlas_model(*atlas_document, skeleton_result.skeleton_data);
    if (!skeleton_ok || !runtime_skeleton_ok || !runtime_skin_ok ||
        !runtime_inherit_skin_constraint_ok ||
        !runtime_attachment_extensions_ok || !runtime_clipping_and_sequence_ok ||
        !runtime_skeleton_bounds_ok ||
        !runtime_weighted_mesh_ok ||
        !runtime_mesh_deform_ok ||
        !runtime_slot_presentation_ok || !runtime_slot_blend_modes_ok || !runtime_animation_ok ||
        !runtime_event_ok || !runtime_slot_timelines_ok || !runtime_animation_state_ok ||
        !runtime_reverse_and_root_motion_ok ||
        !runtime_ik_ok || !runtime_path_transform_ok || !runtime_physics_ok ||
        !runtime_atlas_ok) {
        return 1;
    }

    std::cout << "Fixture JSON smoke test passed.\n";
    return 0;
}
