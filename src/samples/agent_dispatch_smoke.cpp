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
    pass &= dispatch(project, "scene.describe", "{\"op\":\"scene.describe\"}");
    pass &= dispatch(project, "bones.list", "{\"op\":\"bones.list\"}");
    pass &= dispatch(project, "animation.list", "{\"op\":\"animation.list\"}");

    // 2. Real mutation: add a rotate keyframe to idle/spine.
    pass &= dispatch(
        project, "set_transform",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.75,\"angle\":30}}");

    // 3. Export round-trip with the mutation applied.
    pass &= dispatch(project, "export_runtime",
                     "{\"op\":\"export_runtime\",\"args\":{\"binary\":true}}");

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
