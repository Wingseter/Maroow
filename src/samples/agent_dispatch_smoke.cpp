// Headless smoke test for the AI agent dispatch pipeline.
//
// Exercises the *real* supported operations through the stable C ABI and
// asserts the dispatcher's `ok` flag (the C status code is OK as long as the
// JSON parses, so success must be read from the result payload). Covers a
// mutation, an inspection, an export round-trip, and undo — mirroring the
// plan's headless verification method.

#include <cstring>
#include <iostream>
#include <string>

#include "marrow/marrow_c.h"

namespace {

bool result_ok(const MarrowStringView& result) {
    const std::string json(result.data ? result.data : "", result.size);
    return json.find("\"ok\": true") != std::string::npos ||
           json.find("\"ok\":true") != std::string::npos;
}

bool result_contains(const MarrowStringView& result, const char* needle) {
    const std::string json(result.data ? result.data : "", result.size);
    return json.find(needle) != std::string::npos;
}

bool dispatch(MarrowProject* project, const char* label, const char* cmd) {
    MarrowStringView result{};
    const MarrowStatusCode status =
        marrow_editor_agent_dispatch(project, cmd, &result);
    if (status != MARROW_STATUS_OK) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        std::cerr << "[FAIL] " << label << ": status "
                  << static_cast<int>(status) << " - "
                  << std::string(error.data ? error.data : "", error.size)
                  << std::endl;
        return false;
    }
    const std::string payload(result.data ? result.data : "", result.size);
    if (!result_ok(result)) {
        std::cerr << "[FAIL] " << label << ": dispatcher returned ok=false -> "
                  << payload << std::endl;
        return false;
    }
    std::cout << "[ OK ] " << label << std::endl;
    return true;
}

bool dispatch_contains(
    MarrowProject* project,
    const char* label,
    const char* cmd,
    const char* needle) {
    MarrowStringView result{};
    const MarrowStatusCode status =
        marrow_editor_agent_dispatch(project, cmd, &result);
    if (status != MARROW_STATUS_OK) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        std::cerr << "[FAIL] " << label << ": status "
                  << static_cast<int>(status) << " - "
                  << std::string(error.data ? error.data : "", error.size)
                  << std::endl;
        return false;
    }
    const std::string payload(result.data ? result.data : "", result.size);
    if (!result_ok(result) || payload.find(needle) == std::string::npos) {
        std::cerr << "[FAIL] " << label << ": expected successful payload containing "
                  << needle << " -> " << payload << std::endl;
        return false;
    }
    std::cout << "[ OK ] " << label << std::endl;
    return true;
}

bool dispatch_rejected_contains(
    MarrowProject* project,
    const char* label,
    const char* cmd,
    const char* needle) {
    MarrowStringView result{};
    const MarrowStatusCode status =
        marrow_editor_agent_dispatch(project, cmd, &result);
    if (status != MARROW_STATUS_OK) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        std::cerr << "[FAIL] " << label << ": status "
                  << static_cast<int>(status) << " - "
                  << std::string(error.data ? error.data : "", error.size)
                  << std::endl;
        return false;
    }
    const std::string payload(result.data ? result.data : "", result.size);
    if (result_ok(result) || payload.find(needle) == std::string::npos) {
        std::cerr << "[FAIL] " << label
                  << ": expected rejected payload containing " << needle
                  << " -> " << payload << std::endl;
        return false;
    }
    std::cout << "[ OK ] " << label << std::endl;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* project_path = "assets/fixtures/player_idle.marrow";
    if (argc > 1) {
        project_path = argv[1];
    }

    MarrowProject* project = nullptr;
    MarrowStatusCode status = marrow_editor_project_load(project_path, &project);
    if (status != MARROW_STATUS_OK) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        std::cerr << "Failed to load project: "
                  << std::string(error.data ? error.data : "", error.size)
                  << std::endl;
        return 1;
    }
    std::cout << "Loaded project: " << project_path << std::endl;

    bool pass = true;

    // 1. Inspection round-trip.
    pass &= dispatch_contains(
        project,
        "operations.list",
        "{\"op\":\"operations.list\"}",
        "set_slot_color_keyframe");
    pass &= dispatch_contains(
        project,
        "operations.list write metadata",
        "{\"op\":\"operations.list\"}",
        "\"dry_run_supported\"");
    pass &= dispatch_contains(project, "scene.describe", "{\"op\":\"scene.describe\"}", "slot_count");
    pass &= dispatch(project, "bones.list", "{\"op\":\"bones.list\"}");
    pass &= dispatch(project, "animation.list", "{\"op\":\"animation.list\"}");
    pass &= dispatch_contains(project, "slots.list", "{\"op\":\"slots.list\"}", "spark_fx");
    pass &= dispatch_contains(project, "skins.list", "{\"op\":\"skins.list\"}", "mesh_base");
    pass &= dispatch_contains(project, "attachments.list", "{\"op\":\"attachments.list\"}", "body_mesh");
    pass &= dispatch_contains(project, "constraints.list", "{\"op\":\"constraints.list\"}", "editor_arm_reach");
    pass &= dispatch_contains(
        project,
        "timeline.describe",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"idle\"}}",
        "draw_order_keyframes");
    pass &= dispatch_contains(
        project,
        "mesh.describe",
        "{\"op\":\"mesh.describe\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}",
        "vertex_count");
    pass &= dispatch_contains(
        project,
        "project.diagnostics",
        "{\"op\":\"project.diagnostics\"}",
        "error_count");

    // 2. Real mutation: add a rotate keyframe to idle/spine.
    pass &= dispatch_contains(
        project,
        "set_transform dry-run",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.625,\"angle\":12,"
        "\"dry_run\":true}}",
        "\"dry_run\": true");
    pass &= dispatch(
        project, "set_transform",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.75,\"angle\":30}}");
    pass &= dispatch_contains(
        project,
        "edit_ik_constraint dry-run",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"mix\":0.5,\"dry_run\":true}}",
        "\"dry_run\": true");
    pass &= dispatch_contains(
        project,
        "edit_path_constraint dry-run",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.2,\"dry_run\":true}}",
        "\"dry_run\": true");
    pass &= dispatch(
        project,
        "edit_path_constraint",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.25,\"rotate_mix\":0.75}}");
    pass &= dispatch_contains(
        project,
        "edit_transform_constraint dry-run",
        "{\"op\":\"edit_transform_constraint\",\"args\":{\"name\":\"editor_transform_follow\","
        "\"translate_mix\":0.5,\"offset\":{\"x\":-8},\"dry_run\":true}}",
        "\"dry_run\": true");
    pass &= dispatch(
        project,
        "edit_transform_constraint",
        "{\"op\":\"edit_transform_constraint\",\"args\":{\"name\":\"editor_transform_follow\","
        "\"translate_mix\":0.5,\"offset\":{\"x\":-8}}}");
    pass &= dispatch_contains(
        project,
        "edit_physics_constraint dry-run",
        "{\"op\":\"edit_physics_constraint\",\"args\":{\"name\":\"editor_ribbon_secondary\","
        "\"mix\":0.8,\"wind\":{\"x\":10},\"dry_run\":true}}",
        "\"dry_run\": true");
    pass &= dispatch(
        project,
        "edit_physics_constraint",
        "{\"op\":\"edit_physics_constraint\",\"args\":{\"name\":\"editor_ribbon_secondary\","
        "\"mix\":0.8,\"wind\":{\"x\":10}}}");
    pass &= dispatch(
        project,
        "set_event_keyframe",
        "{\"op\":\"set_event_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.42,\"event\":\"footstep\",\"int\":7,\"float\":0.5,"
        "\"string\":\"agent\",\"audio_path\":\"sfx/agent.wav\",\"volume\":0.6,"
        "\"balance\":-0.1}}");
    pass &= dispatch(
        project,
        "remove_event_keyframe",
        "{\"op\":\"remove_event_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.42,\"event\":\"footstep\"}}");
    pass &= dispatch(
        project,
        "set_deform_keyframe",
        "{\"op\":\"set_deform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":0.625,"
        "\"offsets\":[0,0,1,0,0,1,0,0]}}");
    pass &= dispatch(
        project,
        "remove_deform_keyframe",
        "{\"op\":\"remove_deform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":0.625}}");
    pass &= dispatch(
        project,
        "set_vertex_weights",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":1,\"influences\":["
        "{\"bone\":\"spine\",\"x\":60,\"y\":0,\"weight\":0.5},"
        "{\"bone\":\"arm_l\",\"x\":20,\"y\":0,\"weight\":0.5}]}]}}");
    pass &= dispatch(
        project,
        "normalize_weights",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
    pass &= dispatch(
        project,
        "set_slot_color_keyframe",
        "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"color\":{\"r\":0.5,\"g\":0.75,"
        "\"b\":1.0,\"a\":0.8}}}");
    pass &= dispatch(
        project,
        "remove_slot_color_keyframe",
        "{\"op\":\"remove_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625}}");
    pass &= dispatch(
        project,
        "set_attachment_keyframe",
        "{\"op\":\"set_attachment_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"attachment\":\"body\"}}");
    pass &= dispatch(
        project,
        "remove_attachment_keyframe",
        "{\"op\":\"remove_attachment_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625}}");
    pass &= dispatch(
        project,
        "set_draw_order_keyframe",
        "{\"op\":\"set_draw_order_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.75,\"slots\":[\"arm_l\",\"body\",\"fx_mask\",\"spark_fx\","
        "\"spawn_anchor\",\"hurtbox\",\"guide\"]}}");
    pass &= dispatch(
        project,
        "remove_draw_order_keyframe",
        "{\"op\":\"remove_draw_order_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.75}}");

    // 3. Export is now review-gated for agents and must not write immediately.
    pass &= dispatch_contains(
        project,
        "export_runtime review",
        "{\"op\":\"export_runtime\",\"args\":{\"binary\":true}}",
        "\"required\": true");
    pass &= dispatch_contains(
        project,
        "export.preview",
        "{\"op\":\"export.preview\",\"args\":{\"binary\":true}}",
        "\"binary\": true");
    pass &= dispatch_contains(
        project,
        "runtime.validate",
        "{\"op\":\"runtime.validate\"}",
        "\"diagnostics\"");
    pass &= dispatch_contains(
        project,
        "compare_runtime_export",
        "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}",
        "rotation_error_degrees");
    pass &= dispatch_contains(
        project,
        "import.spine_json dry-run",
        "{\"op\":\"import.spine_json\",\"args\":{\"input\":\"assets/fixtures/spine_import_sample.json\","
        "\"output\":\"/tmp/agent_spine_import_sample.mskl\"}}",
        "\"dry_run\": true");
    pass &= dispatch_contains(
        project,
        "import.spine_json review",
        "{\"op\":\"import.spine_json\",\"args\":{\"input\":\"assets/fixtures/spine_import_sample.json\","
        "\"output\":\"/tmp/agent_spine_import_sample.mskl\",\"dry_run\":false}}",
        "\"kind\": \"import_or_pack\"");
    pass &= dispatch_contains(
        project,
        "agent.permissions.describe",
        "{\"op\":\"agent.permissions.describe\"}",
        "\"paused\": false");
    pass &= dispatch(project, "agent.pause", "{\"op\":\"agent.pause\"}");
    pass &= dispatch_rejected_contains(
        project,
        "paused mutation blocked",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.8,\"angle\":5}}",
        "\"blocked\"");
    pass &= dispatch(project, "agent.resume", "{\"op\":\"agent.resume\"}");

    // 4. Undo the mutation.
    pass &= dispatch(project, "undo", "{\"op\":\"undo\"}");

    // 5. Negative case: an unknown op MUST report ok=false.
    {
        MarrowStringView result{};
        marrow_editor_agent_dispatch(
            project, "{\"op\":\"definitely_not_a_real_op\"}", &result);
        if (result_ok(result)) {
            std::cerr << "[FAIL] unknown op was reported as ok=true" << std::endl;
            pass = false;
        } else {
            std::cout << "[ OK ] unknown op rejected" << std::endl;
        }
    }

    marrow_editor_project_destroy(project);

    if (!pass) {
        std::cerr << "agent_dispatch_smoke: FAILED" << std::endl;
        return 1;
    }
    std::cout << "agent_dispatch_smoke: PASSED" << std::endl;
    return 0;
}
