#include "marrow/renderer/module.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::array<float, 16> kIdentityProjection{{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
}};

struct Options {
    std::filesystem::path skeleton_path;
    std::filesystem::path atlas_path;
    std::optional<std::filesystem::path> report_path;
    std::optional<std::filesystem::path> import_report_path;
    std::optional<std::string> animation_name;
    std::optional<std::string> skin_name;
    std::optional<double> sample_time;
    std::optional<int> auto_close_frames;
    bool skip_render{false};
    bool hud_overlay{false};
};

struct CheckMetrics {
    marrow::runtime::json::Value::Object values;
};

struct Check {
    std::string name;
    std::string status;
    std::string category;
    std::string message;
    std::optional<CheckMetrics> metrics;
};

struct BoundsSummary {
    std::size_t count{0};
    bool has_aabb{false};
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
};

struct Summary {
    std::size_t bone_count{0};
    std::size_t slot_count{0};
    std::size_t skin_count{0};
    std::size_t animation_count{0};
    std::size_t event_count{0};
    std::size_t setup_attachment_count{0};
    std::size_t draw_command_count{0};
    std::size_t clip_count{0};
    BoundsSummary bounds;
    std::size_t sampled_animation_count{0};
    std::size_t sampled_event_count{0};
    std::size_t import_diagnostic_count{0};
    std::size_t unsupported_import_diagnostic_count{0};
};

struct ValidationReport {
    explicit ValidationReport(const Options& options_in)
        : options(options_in) {}

    void add_passed(std::string name, std::string category, std::string message) {
        checks.push_back(Check{
            std::move(name),
            "passed",
            std::move(category),
            std::move(message),
            std::nullopt,
        });
    }

    void add_passed(
        std::string name,
        std::string category,
        std::string message,
        CheckMetrics metrics) {
        checks.push_back(Check{
            std::move(name),
            "passed",
            std::move(category),
            std::move(message),
            std::move(metrics),
        });
    }

    void add_warning(
        std::string name,
        std::string category,
        std::string message,
        std::optional<CheckMetrics> metrics = std::nullopt) {
        has_warning = true;
        checks.push_back(Check{
            std::move(name),
            "warning",
            std::move(category),
            std::move(message),
            std::move(metrics),
        });
    }

    void add_failed(
        std::string name,
        std::string category,
        std::string message,
        std::optional<CheckMetrics> metrics = std::nullopt) {
        has_failure = true;
        checks.push_back(Check{
            std::move(name),
            "failed",
            std::move(category),
            std::move(message),
            std::move(metrics),
        });
    }

    std::string status() const {
        if (has_failure) {
            return "failed";
        }
        if (has_warning) {
            return "passed_with_warnings";
        }
        return "passed";
    }

    Options options;
    Summary summary;
    std::vector<Check> checks;
    bool has_failure{false};
    bool has_warning{false};
};

marrow::runtime::json::Value json_null() {
    return marrow::runtime::json::Value{};
}

marrow::runtime::json::Value json_bool(bool value) {
    return marrow::runtime::json::Value(value, {});
}

marrow::runtime::json::Value json_number(double value) {
    return marrow::runtime::json::Value(value, {});
}

marrow::runtime::json::Value json_string(std::string value) {
    return marrow::runtime::json::Value(std::move(value), {});
}

marrow::runtime::json::Value json_array(marrow::runtime::json::Value::Array value) {
    return marrow::runtime::json::Value(std::move(value), {});
}

marrow::runtime::json::Value json_object(marrow::runtime::json::Value::Object value) {
    return marrow::runtime::json::Value(std::move(value), {});
}

marrow::runtime::json::Value json_path_or_null(
    const std::optional<std::filesystem::path>& path) {
    if (!path.has_value()) {
        return json_null();
    }
    return json_string(path->generic_string());
}

marrow::runtime::json::Value json_string_or_null(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return json_null();
    }
    return json_string(*value);
}

marrow::runtime::json::Value json_number_or_null(const std::optional<double>& value) {
    if (!value.has_value()) {
        return json_null();
    }
    return json_number(*value);
}

marrow::runtime::json::Value check_to_json(const Check& check) {
    marrow::runtime::json::Value::Object object;
    object.emplace("name", json_string(check.name));
    object.emplace("status", json_string(check.status));
    object.emplace("category", json_string(check.category));
    object.emplace("message", json_string(check.message));
    if (check.metrics.has_value()) {
        object.emplace("metrics", json_object(check.metrics->values));
    }
    return json_object(std::move(object));
}

marrow::runtime::json::Value report_to_json(const ValidationReport& report) {
    marrow::runtime::json::Value::Object inputs;
    inputs.emplace("skeleton", json_string(report.options.skeleton_path.generic_string()));
    inputs.emplace("atlas", json_string(report.options.atlas_path.generic_string()));
    inputs.emplace("animation", json_string_or_null(report.options.animation_name));
    inputs.emplace("skin", json_string_or_null(report.options.skin_name));
    inputs.emplace("time", json_number_or_null(report.options.sample_time));
    inputs.emplace("import_report", json_path_or_null(report.options.import_report_path));

    marrow::runtime::json::Value::Object aabb;
    aabb.emplace("has_aabb", json_bool(report.summary.bounds.has_aabb));
    if (report.summary.bounds.has_aabb) {
        aabb.emplace("min_x", json_number(report.summary.bounds.min_x));
        aabb.emplace("min_y", json_number(report.summary.bounds.min_y));
        aabb.emplace("max_x", json_number(report.summary.bounds.max_x));
        aabb.emplace("max_y", json_number(report.summary.bounds.max_y));
    } else {
        aabb.emplace("min_x", json_null());
        aabb.emplace("min_y", json_null());
        aabb.emplace("max_x", json_null());
        aabb.emplace("max_y", json_null());
    }

    marrow::runtime::json::Value::Object summary;
    summary.emplace("bone_count", json_number(static_cast<double>(report.summary.bone_count)));
    summary.emplace("slot_count", json_number(static_cast<double>(report.summary.slot_count)));
    summary.emplace("skin_count", json_number(static_cast<double>(report.summary.skin_count)));
    summary.emplace(
        "animation_count",
        json_number(static_cast<double>(report.summary.animation_count)));
    summary.emplace("event_count", json_number(static_cast<double>(report.summary.event_count)));
    summary.emplace(
        "setup_attachment_count",
        json_number(static_cast<double>(report.summary.setup_attachment_count)));
    summary.emplace(
        "draw_command_count",
        json_number(static_cast<double>(report.summary.draw_command_count)));
    summary.emplace("clip_count", json_number(static_cast<double>(report.summary.clip_count)));
    summary.emplace("bounds_count", json_number(static_cast<double>(report.summary.bounds.count)));
    summary.emplace("bounds_aabb", json_object(std::move(aabb)));
    summary.emplace(
        "sampled_animation_count",
        json_number(static_cast<double>(report.summary.sampled_animation_count)));
    summary.emplace(
        "sampled_event_count",
        json_number(static_cast<double>(report.summary.sampled_event_count)));
    summary.emplace(
        "import_diagnostic_count",
        json_number(static_cast<double>(report.summary.import_diagnostic_count)));
    summary.emplace(
        "unsupported_import_diagnostic_count",
        json_number(static_cast<double>(report.summary.unsupported_import_diagnostic_count)));

    marrow::runtime::json::Value::Array checks;
    checks.reserve(report.checks.size());
    for (const Check& check : report.checks) {
        checks.push_back(check_to_json(check));
    }

    marrow::runtime::json::Value::Object root;
    root.emplace("status", json_string(report.status()));
    root.emplace("inputs", json_object(std::move(inputs)));
    root.emplace("summary", json_object(std::move(summary)));
    root.emplace("checks", json_array(std::move(checks)));
    return json_object(std::move(root));
}

void print_usage(std::string_view executable) {
    std::cerr << "Usage: " << executable
              << " [options] <skeleton.mskl|skeleton.mbin> <atlas.matl>\n"
              << "Options:\n"
              << "  --report <path>        Write machine-readable JSON report.\n"
              << "  --import-report <path> Merge a spine_to_marrow --report JSON file.\n"
              << "  --animation <name>     Validate one animation; otherwise sample all.\n"
              << "  --skin <name>          Activate a skin before setup and animation checks.\n"
              << "  --time <seconds>       Sample time for --animation; default midpoint.\n"
              << "  --skip-render          CI mode; do not open a preview window.\n"
              << "  --hud                  Enable preview HUD when rendering.\n"
              << "  --auto-close <frames>  Close preview after a frame count.\n";
}

bool parse_double(std::string_view text, double* value_out) {
    try {
        std::size_t parsed = 0;
        const double value = std::stod(std::string(text), &parsed);
        if (parsed != text.size() || value < 0.0) {
            return false;
        }
        *value_out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_positive_int(std::string_view text, int* value_out) {
    try {
        std::size_t parsed = 0;
        const int value = std::stoi(std::string(text), &parsed);
        if (parsed != text.size() || value <= 0) {
            return false;
        }
        *value_out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_options(int argc, char** argv, Options* options_out) {
    Options options;
    std::vector<std::string_view> positional_arguments;
    positional_arguments.reserve(2);

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            return false;
        }
        if (argument == "--report") {
            if (index + 1 >= argc) {
                std::cerr << "--report requires an output JSON path.\n";
                return false;
            }
            options.report_path = std::filesystem::path(argv[++index]);
            continue;
        }
        if (argument == "--import-report") {
            if (index + 1 >= argc) {
                std::cerr << "--import-report requires a JSON path.\n";
                return false;
            }
            options.import_report_path = std::filesystem::path(argv[++index]);
            continue;
        }
        if (argument == "--animation") {
            if (index + 1 >= argc) {
                std::cerr << "--animation requires a name.\n";
                return false;
            }
            options.animation_name = std::string(argv[++index]);
            continue;
        }
        if (argument == "--skin") {
            if (index + 1 >= argc) {
                std::cerr << "--skin requires a name.\n";
                return false;
            }
            options.skin_name = std::string(argv[++index]);
            continue;
        }
        if (argument == "--time") {
            if (index + 1 >= argc) {
                std::cerr << "--time requires a seconds value.\n";
                return false;
            }
            double sample_time = 0.0;
            if (!parse_double(argv[++index], &sample_time)) {
                std::cerr << "--time requires a non-negative number.\n";
                return false;
            }
            options.sample_time = sample_time;
            continue;
        }
        if (argument == "--skip-render") {
            options.skip_render = true;
            continue;
        }
        if (argument == "--hud") {
            options.hud_overlay = true;
            continue;
        }
        if (argument == "--auto-close") {
            if (index + 1 >= argc) {
                std::cerr << "--auto-close requires a frame count.\n";
                return false;
            }
            int frame_count = 0;
            if (!parse_positive_int(argv[++index], &frame_count)) {
                std::cerr << "--auto-close requires a positive integer frame count.\n";
                return false;
            }
            options.auto_close_frames = frame_count;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "Unknown option: " << argument << '\n';
            return false;
        }

        positional_arguments.push_back(argument);
    }

    if (positional_arguments.size() != 2U) {
        print_usage(argv[0]);
        return false;
    }

    options.skeleton_path = std::filesystem::path(positional_arguments[0]);
    options.atlas_path = std::filesystem::path(positional_arguments[1]);
    *options_out = std::move(options);
    return true;
}

bool write_report(
    const std::filesystem::path& report_path,
    const ValidationReport& report) {
    const std::filesystem::path output_directory = report_path.parent_path();
    if (!output_directory.empty()) {
        std::error_code create_error;
        std::filesystem::create_directories(output_directory, create_error);
        if (create_error) {
            std::cerr << "Failed to create report directory '" << output_directory.string()
                      << "': " << create_error.message() << '\n';
            return false;
        }
    }

    std::ofstream output(report_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Failed to open report path '" << report_path.string() << "'.\n";
        return false;
    }

    output << marrow::runtime::json::serialize_pretty(report_to_json(report));
    if (!output.good()) {
        std::cerr << "Failed while writing report path '" << report_path.string() << "'.\n";
        return false;
    }
    return true;
}

int finish(ValidationReport& report) {
    if (report.options.report_path.has_value() &&
        !write_report(*report.options.report_path, report)) {
        return EXIT_FAILURE;
    }

    if (report.has_failure) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

std::filesystem::path resolve_atlas_image_path(
    const std::filesystem::path& atlas_path,
    const marrow::runtime::AtlasData& atlas) {
    const std::filesystem::path declared_image_path(atlas.info().image);
    return declared_image_path.is_absolute()
        ? declared_image_path.lexically_normal()
        : (atlas_path.parent_path() / declared_image_path).lexically_normal();
}

std::optional<std::string> string_member(
    const marrow::runtime::json::Value& object,
    std::string_view key) {
    const marrow::runtime::json::Value* value =
        marrow::runtime::json::find_member(object, key);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return value->as_string();
}

std::string import_report_error_message(const marrow::runtime::json::Value& root) {
    const marrow::runtime::json::Value* error =
        marrow::runtime::json::find_member(root, "error");
    if (error == nullptr || !error->is_object()) {
        return "Phase 1 import report was rejected.";
    }

    const std::optional<std::string> message = string_member(*error, "message");
    if (message.has_value() && !message->empty()) {
        return "Phase 1 import report was rejected: " + *message;
    }
    return "Phase 1 import report was rejected.";
}

bool merge_import_report(ValidationReport* report) {
    if (!report->options.import_report_path.has_value()) {
        report->add_passed(
            "import_report",
            "unsupported_import_field",
            "No Phase 1 import report was provided.");
        return true;
    }

    const std::filesystem::path& import_report_path = *report->options.import_report_path;
    const auto load_result = marrow::runtime::json::load_document(import_report_path);
    if (!load_result) {
        report->add_failed(
            "import_report_load",
            "unsupported_import_field",
            load_result.error.has_value()
                ? load_result.error->format()
                : "Failed to load Phase 1 import report.");
        return false;
    }

    const marrow::runtime::json::Value& root = load_result.document->root;
    if (!root.is_object()) {
        report->add_failed(
            "import_report_shape",
            "unsupported_import_field",
            "Phase 1 import report root must be a JSON object.");
        return false;
    }

    const std::optional<std::string> status = string_member(root, "status");
    if (!status.has_value()) {
        report->add_failed(
            "import_report_status",
            "unsupported_import_field",
            "Phase 1 import report is missing a string status.");
        return false;
    }
    if (*status == "rejected") {
        report->add_failed(
            "import_report_status",
            "unsupported_import_field",
            import_report_error_message(root));
        return false;
    }
    if (*status != "accepted" && *status != "accepted_with_warnings") {
        report->add_failed(
            "import_report_status",
            "unsupported_import_field",
            "Phase 1 import report has unknown status '" + *status + "'.");
        return false;
    }

    const marrow::runtime::json::Value* diagnostics =
        marrow::runtime::json::find_member(root, "diagnostics");
    if (diagnostics == nullptr || diagnostics->is_null()) {
        CheckMetrics metrics;
        metrics.values.emplace("status", json_string(*status));
        report->add_passed(
            "import_report",
            "unsupported_import_field",
            "Phase 1 import report contained no diagnostics.",
            std::move(metrics));
        return true;
    }
    if (!diagnostics->is_array()) {
        report->add_failed(
            "import_report_diagnostics",
            "unsupported_import_field",
            "Phase 1 import report diagnostics must be an array.");
        return false;
    }

    std::size_t unsupported_count = 0;
    std::size_t diagnostic_index = 0;
    for (const marrow::runtime::json::Value& diagnostic : diagnostics->as_array()) {
        ++diagnostic_index;
        if (!diagnostic.is_object()) {
            report->add_warning(
                "import_report_diagnostic",
                "unsupported_import_field",
                "Skipped malformed non-object diagnostic.");
            continue;
        }

        const std::string severity = string_member(diagnostic, "severity").value_or("warning");
        const std::string code = string_member(diagnostic, "code")
                                     .value_or("diagnostic_" + std::to_string(diagnostic_index));
        const std::string path = string_member(diagnostic, "path").value_or("");
        const std::string message = string_member(diagnostic, "message").value_or("");

        CheckMetrics metrics;
        metrics.values.emplace("severity", json_string(severity));
        metrics.values.emplace("code", json_string(code));
        metrics.values.emplace("path", json_string(path));

        ++report->summary.import_diagnostic_count;
        if (severity == "unsupported") {
            ++unsupported_count;
            ++report->summary.unsupported_import_diagnostic_count;
        }

        std::ostringstream check_name;
        check_name << "import_report_diagnostic_" << diagnostic_index;
        std::ostringstream check_message;
        check_message << "Phase 1 import report diagnostic";
        if (!code.empty()) {
            check_message << " " << code;
        }
        if (!path.empty()) {
            check_message << " at " << path;
        }
        if (!message.empty()) {
            check_message << ": " << message;
        }
        report->add_warning(
            check_name.str(),
            "unsupported_import_field",
            check_message.str(),
            std::move(metrics));
    }

    if (report->summary.import_diagnostic_count == 0) {
        CheckMetrics metrics;
        metrics.values.emplace("status", json_string(*status));
        report->add_passed(
            "import_report",
            "unsupported_import_field",
            "Phase 1 import report diagnostics were empty.",
            std::move(metrics));
    } else {
        CheckMetrics metrics;
        metrics.values.emplace(
            "diagnostics",
            json_number(static_cast<double>(report->summary.import_diagnostic_count)));
        metrics.values.emplace("unsupported", json_number(static_cast<double>(unsupported_count)));
        report->add_warning(
            "import_report",
            "unsupported_import_field",
            "Phase 1 import report was accepted with diagnostics.",
            std::move(metrics));
    }
    return true;
}

void update_bounds_summary(
    const marrow::runtime::SkeletonBounds& bounds,
    BoundsSummary* summary) {
    summary->count = std::max(summary->count, bounds.bounding_boxes().size());
    if (!bounds.has_aabb()) {
        return;
    }

    if (!summary->has_aabb) {
        summary->has_aabb = true;
        summary->min_x = bounds.min_x();
        summary->min_y = bounds.min_y();
        summary->max_x = bounds.max_x();
        summary->max_y = bounds.max_y();
        return;
    }

    summary->min_x = std::min(summary->min_x, bounds.min_x());
    summary->min_y = std::min(summary->min_y, bounds.min_y());
    summary->max_x = std::max(summary->max_x, bounds.max_x());
    summary->max_y = std::max(summary->max_y, bounds.max_y());
}

bool apply_requested_skin(
    const Options& options,
    marrow::runtime::Skeleton* skeleton,
    ValidationReport* report,
    bool record_passed_check = true) {
    if (!options.skin_name.has_value()) {
        return true;
    }

    if (!skeleton->set_skin(*options.skin_name)) {
        report->add_failed(
            "skin_selection",
            "runtime_sampling_error",
            "Requested skin '" + *options.skin_name + "' was not found.");
        return false;
    }

    if (record_passed_check) {
        report->add_passed(
            "skin_selection",
            "runtime_sampling_error",
            "Activated skin '" + *options.skin_name + "'.");
    }
    return true;
}

bool prepare_renderer_scene(
    const std::string& check_prefix,
    const marrow::runtime::Skeleton& skeleton,
    const marrow::runtime::AtlasData& atlas,
    marrow::renderer::PreparedScene* scene_out,
    marrow::renderer::RenderCommandList* command_list_out,
    marrow::renderer::PreparedSceneBatchSummary* batch_summary_out,
    ValidationReport* report) {
    const marrow::renderer::PreparedSceneResult scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, atlas);
    if (!scene_result) {
        report->add_failed(
            check_prefix + "_scene",
            "renderer_preparation_error",
            scene_result.error_message.empty()
                ? "Renderer failed to prepare the scene."
                : scene_result.error_message);
        return false;
    }
    if (scene_result.scene->draw_commands.empty()) {
        report->add_failed(
            check_prefix + "_scene",
            "renderer_preparation_error",
            "Prepared scene did not contain renderable draw commands.");
        return false;
    }

    const marrow::renderer::RenderCommandListResult command_list_result =
        marrow::renderer::build_render_command_list(*scene_result.scene, kIdentityProjection);
    if (!command_list_result) {
        report->add_failed(
            check_prefix + "_command_list",
            "renderer_preparation_error",
            command_list_result.error_message.empty()
                ? "Renderer failed to build a command list."
                : command_list_result.error_message);
        return false;
    }
    if (command_list_result.command_list->commands.empty()) {
        report->add_failed(
            check_prefix + "_command_list",
            "renderer_preparation_error",
            "Render command list did not contain draw commands.");
        return false;
    }

    const marrow::renderer::PreparedSceneBatchSummary batch_summary =
        marrow::renderer::summarize_prepared_scene_batches(*scene_result.scene);
    if (!batch_summary) {
        report->add_failed(
            check_prefix + "_batch_summary",
            "renderer_preparation_error",
            batch_summary.error_message.value_or("Failed to summarize prepared scene batches."));
        return false;
    }

    *scene_out = *scene_result.scene;
    *command_list_out = *command_list_result.command_list;
    *batch_summary_out = batch_summary;
    return true;
}

std::vector<const marrow::runtime::AnimationData*> animations_to_sample(
    const Options& options,
    const marrow::runtime::SkeletonData& skeleton_data,
    ValidationReport* report) {
    std::vector<const marrow::runtime::AnimationData*> animations;
    if (options.animation_name.has_value()) {
        const marrow::runtime::AnimationData* animation =
            skeleton_data.find_animation(*options.animation_name);
        if (animation == nullptr) {
            report->add_failed(
                "animation_selection",
                "runtime_sampling_error",
                "Requested animation '" + *options.animation_name + "' was not found.");
            return animations;
        }
        animations.push_back(animation);
        return animations;
    }

    animations.reserve(skeleton_data.animations().size());
    for (const marrow::runtime::AnimationData& animation : skeleton_data.animations()) {
        animations.push_back(&animation);
    }
    return animations;
}

bool sample_animations(
    const Options& options,
    const std::shared_ptr<const marrow::runtime::SkeletonData>& skeleton_data,
    const marrow::runtime::AtlasData& atlas,
    ValidationReport* report,
    std::optional<marrow::renderer::PreparedScene>* selected_animation_scene) {
    const std::vector<const marrow::runtime::AnimationData*> animations =
        animations_to_sample(options, *skeleton_data, report);
    if (report->has_failure) {
        return false;
    }

    if (animations.empty()) {
        report->add_passed(
            "animation_sampling",
            "runtime_sampling_error",
            "Skeleton contains no animations to sample.");
        return true;
    }

    for (const marrow::runtime::AnimationData* animation : animations) {
        const double duration = animation->duration();
        const double sample_time =
            options.sample_time.value_or(duration > 0.0 ? duration * 0.5 : 0.0);

        marrow::runtime::Skeleton skeleton(skeleton_data);
        skeleton.set_to_setup_pose();
        if (!apply_requested_skin(options, &skeleton, report, false)) {
            return false;
        }

        std::size_t sampled_events = 0;
        if (duration > 0.0) {
            skeleton.apply_animation(
                *animation,
                0.0,
                duration,
                [&](const marrow::runtime::AnimationEvent&) {
                    ++sampled_events;
                });
        }

        skeleton.apply_animation(*animation, sample_time);

        marrow::runtime::SkeletonBounds bounds;
        bounds.update(skeleton, true);
        update_bounds_summary(bounds, &report->summary.bounds);

        marrow::renderer::PreparedScene sample_scene;
        marrow::renderer::RenderCommandList sample_command_list;
        marrow::renderer::PreparedSceneBatchSummary sample_batch_summary;
        if (!prepare_renderer_scene(
                "animation_" + animation->name,
                skeleton,
                atlas,
                &sample_scene,
                &sample_command_list,
                &sample_batch_summary,
                report)) {
            return false;
        }

        if (options.animation_name.has_value()) {
            *selected_animation_scene = sample_scene;
        }

        ++report->summary.sampled_animation_count;
        report->summary.sampled_event_count += sampled_events;

        CheckMetrics metrics;
        metrics.values.emplace("duration", json_number(duration));
        metrics.values.emplace("sample_time", json_number(sample_time));
        metrics.values.emplace("events", json_number(static_cast<double>(sampled_events)));
        metrics.values.emplace(
            "draw_commands",
            json_number(static_cast<double>(sample_command_list.commands.size())));
        report->add_passed(
            "animation_" + animation->name,
            "runtime_sampling_error",
            "Sampled animation '" + animation->name + "'.",
            std::move(metrics));
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        return argc >= 2 &&
                (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help")
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    ValidationReport report(options);

    if (!merge_import_report(&report)) {
        return finish(report);
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(options.skeleton_path);
    if (!skeleton_result) {
        report.add_failed(
            "skeleton_load",
            "load_error",
            skeleton_result.error.has_value()
                ? skeleton_result.error->format()
                : "Failed to load skeleton data.");
        return finish(report);
    }
    report.summary.bone_count = skeleton_result.skeleton_data->bones().size();
    report.summary.slot_count = skeleton_result.skeleton_data->slots().size();
    report.summary.skin_count = skeleton_result.skeleton_data->skins().size();
    report.summary.animation_count = skeleton_result.skeleton_data->animations().size();
    report.summary.event_count = skeleton_result.skeleton_data->events().size();
    report.add_passed(
        "skeleton_load",
        "load_error",
        "Loaded skeleton data from " + options.skeleton_path.string() + ".");

    const auto atlas_result = marrow::runtime::AtlasLoader::load(options.atlas_path);
    if (!atlas_result) {
        report.add_failed(
            "atlas_metadata_load",
            "load_error",
            atlas_result.error.has_value()
                ? atlas_result.error->format()
                : "Failed to load atlas metadata.");
        return finish(report);
    }
    report.add_passed(
        "atlas_metadata_load",
        "load_error",
        "Loaded atlas metadata from " + options.atlas_path.string() + ".");

    const std::filesystem::path atlas_image_path =
        resolve_atlas_image_path(options.atlas_path, *atlas_result.atlas_data);
    const marrow::renderer::TextureImageLoadResult texture_result =
        marrow::renderer::load_png_texture_or_white(atlas_image_path);
    if (!texture_result.loaded_from_file) {
        report.add_failed(
            "atlas_page_load",
            "load_error",
            "Atlas page image did not load from file: " + atlas_image_path.string() +
                ". " + texture_result.message);
        return finish(report);
    }
    {
        CheckMetrics metrics;
        metrics.values.emplace("width", json_number(texture_result.image.width));
        metrics.values.emplace("height", json_number(texture_result.image.height));
        report.add_passed(
            "atlas_page_load",
            "load_error",
            "Loaded atlas page image from " + atlas_image_path.string() + ".",
            std::move(metrics));
    }

    marrow::runtime::Skeleton setup_skeleton(skeleton_result.skeleton_data);
    setup_skeleton.set_to_setup_pose();
    if (!apply_requested_skin(options, &setup_skeleton, &report)) {
        return finish(report);
    }
    setup_skeleton.update_world_transforms();
    report.add_passed(
        "setup_pose",
        "runtime_sampling_error",
        "Applied setup pose and resolved world transforms.");

    marrow::runtime::SkeletonBounds setup_bounds;
    setup_bounds.update(setup_skeleton, true);
    update_bounds_summary(setup_bounds, &report.summary.bounds);
    {
        CheckMetrics metrics;
        metrics.values.emplace(
            "bounding_boxes",
            json_number(static_cast<double>(setup_bounds.bounding_boxes().size())));
        metrics.values.emplace("has_aabb", json_bool(setup_bounds.has_aabb()));
        report.add_passed(
            "bounds_scan",
            "runtime_sampling_error",
            "Scanned setup-pose bounds.",
            std::move(metrics));
    }

    marrow::renderer::PreparedScene setup_scene;
    marrow::renderer::RenderCommandList setup_command_list;
    marrow::renderer::PreparedSceneBatchSummary setup_batch_summary;
    if (!prepare_renderer_scene(
            "setup",
            setup_skeleton,
            *atlas_result.atlas_data,
            &setup_scene,
            &setup_command_list,
            &setup_batch_summary,
            &report)) {
        return finish(report);
    }
    report.summary.setup_attachment_count =
        setup_scene.draw_commands.size() + setup_scene.clip_attachments.size();
    report.summary.draw_command_count = setup_command_list.commands.size();
    report.summary.clip_count = setup_scene.clip_attachments.size();
    {
        CheckMetrics metrics;
        metrics.values.emplace(
            "prepared_draw_commands",
            json_number(static_cast<double>(setup_scene.draw_commands.size())));
        metrics.values.emplace(
            "render_commands",
            json_number(static_cast<double>(setup_command_list.commands.size())));
        metrics.values.emplace(
            "draw_calls",
            json_number(static_cast<double>(setup_batch_summary.draw_call_count)));
        metrics.values.emplace(
            "clip_commands",
            json_number(static_cast<double>(setup_command_list.clip_commands.size())));
        report.add_passed(
            "renderer_preparation",
            "renderer_preparation_error",
            "Prepared setup pose and generated a render command list.",
            std::move(metrics));
    }

    std::optional<marrow::renderer::PreparedScene> selected_animation_scene;
    if (!sample_animations(
            options,
            skeleton_result.skeleton_data,
            *atlas_result.atlas_data,
            &report,
            &selected_animation_scene)) {
        return finish(report);
    }

    const marrow::renderer::PreparedScene& preview_scene =
        selected_animation_scene.has_value() ? *selected_animation_scene : setup_scene;
    if (options.skip_render) {
        report.add_passed(
            "preview",
            "renderer_preparation_error",
            "Preview window skipped by --skip-render.");
        return finish(report);
    }

    marrow::renderer::SampleAppWindow window;
    window.title = "Marrow Validator";
    window.width = 1280;
    window.height = 720;

    const marrow::renderer::DemoShell shell(
        window,
        preview_scene,
        atlas_image_path,
        options.hud_overlay);
    if (const std::optional<std::string> render_error = shell.run(options.auto_close_frames)) {
        report.add_failed(
            "preview",
            "renderer_preparation_error",
            *render_error);
        return finish(report);
    }
    report.add_passed(
        "preview",
        "renderer_preparation_error",
        "Preview window completed without renderer errors.");
    return finish(report);
}
