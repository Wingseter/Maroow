// Headless characterization test for the AI agent dispatch pipeline.
//
// Every registered operation is exercised through the stable C ABI. The test
// intentionally treats the JSON response as a protocol contract: common
// metadata, registry metadata, monotonic IDs, dry-run immutability, history
// grouping, review-only file safety, and JSON/binary export equivalence are
// checked without reaching into dispatcher implementation state.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/marrow_c.h"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/runtime/json.hpp"

namespace {

namespace json = marrow::runtime::json;

struct OperationExpectation {
    std::string_view name;
    std::string_view category;
    bool mutating;
    bool requires_review;
    bool dry_run_supported;
};

constexpr std::array<OperationExpectation, 64> kExpectedOperations{{
    {"operations.list", "inspection", false, false, false},
    {"scene.describe", "inspection", false, false, false},
    {"bones.list", "inspection", false, false, false},
    {"animation.list", "inspection", false, false, false},
    {"slots.list", "inspection", false, false, false},
    {"skins.list", "inspection", false, false, false},
    {"attachments.list", "inspection", false, false, false},
    {"constraints.list", "inspection", false, false, false},
    {"parameters.list", "inspection", false, false, false},
    {"timeline.describe", "inspection", false, false, false},
    {"mesh.describe", "inspection", false, false, false},
    {"project.diagnostics", "inspection", false, false, false},
    {"export.preview", "validation", false, false, false},
    {"runtime.validate", "validation", false, false, false},
    {"compare_runtime_export", "validation", false, false, false},
    {"agent.permissions.describe", "management", false, false, false},
    {"agent.pause", "management", false, false, false},
    {"agent.resume", "management", false, false, false},
    {"agent.terminate", "management", false, false, false},
    {"undo", "edit", true, false, false},
    {"redo", "edit", true, false, false},
    {"parameter.set", "edit", true, false, true},
    {"deformer.create", "edit", true, false, true},
    {"keyform.capture", "edit", true, false, true},
    {"expression.create", "edit", true, false, true},
    {"lip_sync.map", "edit", true, false, true},
    {"animation.create", "edit", true, false, true},
    {"animation.duplicate", "edit", true, false, true},
    {"animation.rename", "edit", true, false, true},
    {"animation.delete", "edit", true, false, true},
    {"animation.set_duration", "edit", true, false, true},
    {"timeline.retime_keyframes", "edit", true, false, true},
    {"timeline.set_interpolation", "edit", true, false, true},
    {"timeline.set_curve_mode", "edit", true, false, true},
    {"timeline.set_loop_sync", "edit", true, false, true},
    {"timeline.scale_key_times", "edit", true, false, true},
    {"set_transform", "edit", true, false, true},
    {"remove_transform_keyframe", "edit", true, false, false},
    {"set_event_keyframe", "edit", true, false, true},
    {"remove_event_keyframe", "edit", true, false, false},
    {"set_deform_keyframe", "edit", true, false, true},
    {"remove_deform_keyframe", "edit", true, false, false},
    {"set_vertex_weights", "edit", true, false, true},
    {"normalize_weights", "edit", true, false, true},
    {"mesh.rebind_weights", "edit", true, false, true},
    {"mesh.generate_weights", "edit", true, false, true},
    {"edit_ik_constraint", "edit", true, false, true},
    {"edit_path_constraint", "edit", true, false, true},
    {"edit_transform_constraint", "edit", true, false, true},
    {"edit_physics_constraint", "edit", true, false, true},
    {"constraint.rename", "edit", true, false, true},
    {"constraint.delete", "edit", true, false, true},
    {"set_slot_color_keyframe", "edit", true, false, true},
    {"remove_slot_color_keyframe", "edit", true, false, false},
    {"set_attachment_keyframe", "edit", true, false, true},
    {"remove_attachment_keyframe", "edit", true, false, false},
    {"set_draw_order_keyframe", "edit", true, false, true},
    {"remove_draw_order_keyframe", "edit", true, false, false},
    {"save", "management", true, true, false},
    {"export_runtime", "management", true, true, false},
    {"import.spine_json", "management", true, true, true},
    {"import.spine_atlas", "management", true, true, true},
    {"import.psd_layers", "management", true, true, true},
    {"atlas.pack", "management", true, true, true},
}};

const OperationExpectation* find_expected_operation(std::string_view name) {
    for (const OperationExpectation& operation : kExpectedOperations) {
        if (operation.name == name) {
            return &operation;
        }
    }
    return nullptr;
}

const json::Value* member(const json::Value* object, std::string_view name) {
    return object != nullptr && object->is_object()
        ? json::find_member(*object, name)
        : nullptr;
}

std::optional<bool> bool_member(const json::Value* object, std::string_view name) {
    const json::Value* value = member(object, name);
    return value != nullptr && value->is_boolean()
        ? std::optional<bool>(value->as_boolean())
        : std::nullopt;
}

std::optional<double> number_member(const json::Value* object, std::string_view name) {
    const json::Value* value = member(object, name);
    return value != nullptr && value->is_number()
        ? std::optional<double>(value->as_number())
        : std::nullopt;
}

std::optional<std::string_view> string_member(
    const json::Value* object,
    std::string_view name) {
    const json::Value* value = member(object, name);
    return value != nullptr && value->is_string()
        ? std::optional<std::string_view>(value->as_string())
        : std::nullopt;
}

struct DispatchObservation {
    bool parsed{false};
    bool ok{false};
    std::string payload;
    json::Value root;

    const json::Value* scene_delta() const {
        return parsed ? member(&root, "scene_delta") : nullptr;
    }
};

class Harness {
public:
    explicit Harness(MarrowProject* project)
        : project_(project) {}

    void set_project(MarrowProject* project) {
        project_ = project;
        last_activity_id_ = 0U;
    }

    /// Dispatches a command WITHOUT the harness's own JSON pre-parse, so a
    /// payload the parser itself refuses can still be asserted on.
    bool dispatch_rejects(std::string_view command) {
        MarrowStringView result{};
        const std::string command_copy(command);
        const MarrowStatusCode status =
            marrow_editor_agent_dispatch(project_, command_copy.c_str(), &result);
        if (status != MARROW_STATUS_OK || result.data == nullptr) {
            return true;
        }
        const json::LoadResult parsed =
            json::parse_document(std::string_view(result.data, result.size));
        if (!parsed || !parsed.document->root.is_object()) {
            return true;
        }
        const json::Value* ok = json::find_member(parsed.document->root, "ok");
        return ok == nullptr || !ok->is_boolean() || !ok->as_boolean();
    }

    void expect(bool condition, std::string_view label, std::string_view detail) {
        if (condition) {
            return;
        }
        ++failures_;
        std::cerr << "[FAIL] " << label << ": " << detail << '\n';
    }

    DispatchObservation invoke(
        std::string_view label,
        std::string_view command,
        bool expect_ok = true,
        std::string_view expected_error_code = {},
        bool registered_operation = true) {
        const std::size_t failures_before = failures_;
        DispatchObservation observation;

        const json::LoadResult parsed_command = json::parse_document(command);
        if (!parsed_command || !parsed_command.document->root.is_object()) {
            expect(false, label, "test command is not valid JSON");
            return observation;
        }
        const auto requested_op = string_member(&parsed_command.document->root, "op");
        if (!requested_op.has_value()) {
            expect(false, label, "test command does not contain a string op");
            return observation;
        }

        const OperationExpectation* expected = find_expected_operation(*requested_op);
        if (registered_operation) {
            expect(expected != nullptr, label, "operation is missing from the expected registry");
            invoked_operations_.insert(std::string(*requested_op));
        }

        MarrowStringView result{};
        const std::string command_copy(command);
        const MarrowStatusCode status =
            marrow_editor_agent_dispatch(project_, command_copy.c_str(), &result);
        if (status != MARROW_STATUS_OK) {
            MarrowStringView error{};
            marrow_get_last_error_message(&error);
            expect(
                false,
                label,
                "C dispatch status " + std::to_string(static_cast<int>(status)) +
                    ": " + std::string(error.data ? error.data : "", error.size));
            return observation;
        }

        observation.payload.assign(result.data ? result.data : "", result.size);
        json::LoadResult parsed_result = json::parse_document(observation.payload);
        if (!parsed_result || !parsed_result.document->root.is_object()) {
            expect(false, label, "dispatcher result is not a JSON object: " + observation.payload);
            return observation;
        }
        observation.parsed = true;
        observation.root = std::move(parsed_result.document->root);

        const auto ok = bool_member(&observation.root, "ok");
        observation.ok = ok.value_or(false);
        expect(ok.has_value(), label, "response is missing boolean ok metadata");
        expect(observation.ok == expect_ok, label, expect_ok ? "expected ok=true" : "expected ok=false");

        const auto response_op = string_member(&observation.root, "op");
        expect(
            response_op.has_value() && *response_op == *requested_op,
            label,
            "response op metadata does not match the request");
        const auto message = string_member(&observation.root, "message");
        expect(
            message.has_value() && !message->empty(),
            label,
            "response is missing non-empty message metadata");
        expect(
            json::find_member(observation.root, "scene_delta") != nullptr,
            label,
            "response is missing scene_delta metadata");
        expect(
            bool_member(&observation.root, "mutating").has_value(),
            label,
            "response is missing mutating metadata");

        if (registered_operation && expected != nullptr) {
            const auto category = string_member(&observation.root, "category");
            expect(
                category.has_value() && *category == expected->category,
                label,
                "response category metadata changed");
            expect(
                bool_member(&observation.root, "mutating") ==
                    std::optional<bool>(expected->mutating),
                label,
                "response mutating metadata changed");
        }

        const auto activity = number_member(&observation.root, "activity_id");
        const bool valid_activity = activity.has_value() && *activity > 0.0 &&
            std::floor(*activity) == *activity;
        expect(valid_activity, label, "response is missing an integer activity_id");
        if (valid_activity) {
            const auto activity_id = static_cast<std::uint64_t>(*activity);
            expect(
                activity_id > last_activity_id_,
                label,
                "activity_id is not monotonically increasing");
            last_activity_id_ = activity_id;
        }

        if (!expect_ok) {
            const json::Value* error = member(&observation.root, "error");
            const auto code = string_member(error, "code");
            expect(code.has_value(), label, "rejected response is missing error.code");
            if (!expected_error_code.empty()) {
                expect(
                    code.has_value() && *code == expected_error_code,
                    label,
                    "rejected response error.code changed");
            }
        }

        if (failures_ == failures_before) {
            std::cout << "[ OK ] " << label << '\n';
        } else if (!observation.payload.empty()) {
            std::cerr << "       payload: " << observation.payload << '\n';
        }
        return observation;
    }

    void expect_complete_coverage() {
        std::set<std::string> expected;
        for (const OperationExpectation& operation : kExpectedOperations) {
            expected.insert(std::string(operation.name));
        }
        expect(
            invoked_operations_ == expected,
            "operation coverage",
            "not every registered operation was invoked");
    }

    bool passed() const {
        return failures_ == 0U;
    }

private:
    MarrowProject* project_{nullptr};
    std::size_t failures_{0};
    std::uint64_t last_activity_id_{0};
    std::set<std::string> invoked_operations_;
};

void expect_registry_contract(Harness& harness, const DispatchObservation& response) {
    const json::Value* operations = response.scene_delta();
    harness.expect(
        operations != nullptr && operations->is_array(),
        "operations.list registry",
        "scene_delta must be an operation array");
    if (operations == nullptr || !operations->is_array()) {
        return;
    }
    harness.expect(
        operations->as_array().size() == kExpectedOperations.size(),
        "operations.list registry",
        "registry operation count changed");

    std::set<std::string> unique_names;
    const std::size_t count = std::min(
        operations->as_array().size(),
        kExpectedOperations.size());
    for (std::size_t index = 0; index < count; ++index) {
        const json::Value& actual = operations->as_array()[index];
        const OperationExpectation& expected = kExpectedOperations[index];
        const auto name = string_member(&actual, "name");
        const auto category = string_member(&actual, "category");
        harness.expect(
            name.has_value() && *name == expected.name,
            "operations.list registry",
            "operation name or ordering changed at index " + std::to_string(index));
        harness.expect(
            category.has_value() && *category == expected.category,
            "operations.list registry",
            "operation category changed for " + std::string(expected.name));
        harness.expect(
            bool_member(&actual, "mutating") == std::optional<bool>(expected.mutating),
            "operations.list registry",
            "mutating metadata changed for " + std::string(expected.name));
        harness.expect(
            bool_member(&actual, "requires_review") ==
                std::optional<bool>(expected.requires_review),
            "operations.list registry",
            "review metadata changed for " + std::string(expected.name));
        harness.expect(
            bool_member(&actual, "dry_run_supported") ==
                std::optional<bool>(expected.dry_run_supported),
            "operations.list registry",
            "dry-run metadata changed for " + std::string(expected.name));
        if (name.has_value()) {
            unique_names.insert(std::string(*name));
        }
    }
    harness.expect(
        unique_names.size() == kExpectedOperations.size(),
        "operations.list registry",
        "operation names must be unique");
}

void expect_scene_contains(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& response,
    std::string_view needle) {
    const json::Value* delta = response.scene_delta();
    harness.expect(
        delta != nullptr && json::serialize_compact(*delta).find(needle) != std::string::npos,
        label,
        "scene_delta does not contain " + std::string(needle));
}

std::string compact_scene_delta(const DispatchObservation& response) {
    const json::Value* delta = response.scene_delta();
    return delta != nullptr ? json::serialize_compact(*delta) : std::string();
}

void expect_exact_scene_delta(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& response,
    std::string_view expected_json) {
    const json::LoadResult expected = json::parse_document(expected_json);
    harness.expect(
        expected && expected.document->root.is_object(),
        label,
        "characterization expectation is not a JSON object");
    if (!expected || !expected.document->root.is_object()) {
        return;
    }
    harness.expect(
        compact_scene_delta(response) ==
            json::serialize_compact(expected.document->root),
        label,
        "scene_delta wire shape changed: " + compact_scene_delta(response));
}

void expect_no_change(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& response) {
    harness.expect(
        string_member(&response.root, "message") ==
                std::optional<std::string_view>("No changes made.") &&
            response.scene_delta() != nullptr && response.scene_delta()->is_null(),
        label,
        "no-op message or null scene_delta changed");
}

std::optional<std::uint64_t> expect_review(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& response,
    std::string_view expected_op,
    std::string_view expected_kind) {
    const json::Value* review = member(&response.root, "review");
    harness.expect(
        review != nullptr && review->is_object(),
        label,
        "response must contain a review object");
    if (review == nullptr || !review->is_object()) {
        return std::nullopt;
    }
    harness.expect(
        bool_member(review, "required") == std::optional<bool>(true),
        label,
        "review.required must be true");
    harness.expect(
        bool_member(review, "allowed") == std::optional<bool>(true),
        label,
        "review target must pass the path whitelist");
    harness.expect(
        string_member(review, "op") == std::optional<std::string_view>(expected_op),
        label,
        "review op changed");
    harness.expect(
        string_member(review, "kind") == std::optional<std::string_view>(expected_kind),
        label,
        "review kind changed");
    const auto target = string_member(review, "target_path");
    harness.expect(
        target.has_value() && !target->empty(),
        label,
        "review target_path must be non-empty");
    const json::Value* targets = member(review, "targets");
    harness.expect(
        targets != nullptr && targets->is_array() && !targets->as_array().empty(),
        label,
        "review targets must be a non-empty array");

    const auto id = number_member(review, "id");
    const bool valid_id = id.has_value() && *id > 0.0 && std::floor(*id) == *id;
    harness.expect(valid_id, label, "review id must be a positive integer");
    return valid_id
        ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(*id))
        : std::nullopt;
}

struct FileSnapshot {
    std::filesystem::path path;
    bool exists{false};
    std::optional<std::string> bytes;
    std::optional<std::filesystem::file_time_type> write_time;
};

FileSnapshot snapshot_file(const std::filesystem::path& path) {
    FileSnapshot snapshot;
    snapshot.path = path;
    std::error_code error;
    snapshot.exists = std::filesystem::exists(path, error);
    if (error || !snapshot.exists) {
        return snapshot;
    }

    std::ifstream input(path, std::ios::binary);
    if (input) {
        snapshot.bytes = std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
    error.clear();
    const auto write_time = std::filesystem::last_write_time(path, error);
    if (!error) {
        snapshot.write_time = write_time;
    }
    return snapshot;
}

void expect_file_unchanged(Harness& harness, const FileSnapshot& before) {
    const FileSnapshot after = snapshot_file(before.path);
    harness.expect(
        after.exists == before.exists,
        "review-only file safety",
        before.path.string() + " existence changed before approval");
    harness.expect(
        after.bytes == before.bytes,
        "review-only file safety",
        before.path.string() + " contents changed before approval");
    harness.expect(
        after.write_time == before.write_time,
        "review-only file safety",
        before.path.string() + " write time changed before approval");
}

std::vector<std::filesystem::path> string_array_paths(
    const json::Value* object,
    std::string_view member_name) {
    std::vector<std::filesystem::path> paths;
    const json::Value* values = member(object, member_name);
    if (values == nullptr || !values->is_array()) {
        return paths;
    }
    for (const json::Value& value : values->as_array()) {
        if (value.is_string()) {
            paths.emplace_back(value.as_string());
        }
    }
    return paths;
}

std::optional<std::uint64_t> expect_export_equivalence(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& response,
    bool enforce_error_budget = true) {
    const json::Value* delta = response.scene_delta();
    harness.expect(
        bool_member(delta, "binary") == std::optional<bool>(true),
        label,
        "comparison must include binary export");
    const auto rotation_error = number_member(delta, "rotation_error_degrees");
    const auto position_error = number_member(delta, "position_error_pixels");
    harness.expect(
        rotation_error.has_value() && position_error.has_value(),
        label,
        "comparison must report rotation and position error metrics");
    if (enforce_error_budget) {
        harness.expect(
            rotation_error.has_value() && *rotation_error <= 0.1,
            label,
            "JSON/binary rotation error exceeds the established 0.1 degree budget (actual=" +
                (rotation_error.has_value() ? std::to_string(*rotation_error) : std::string("missing")) +
                ")");
        harness.expect(
            position_error.has_value() && *position_error <= 0.5,
            label,
            "JSON/binary position error exceeds the established 0.5 pixel budget (actual=" +
                (position_error.has_value() ? std::to_string(*position_error) : std::string("missing")) +
                ")");
    }
    harness.expect(
        number_member(delta, "json_bytes").value_or(0.0) > 0.0 &&
            number_member(delta, "binary_bytes").value_or(0.0) > 0.0,
        label,
        "comparison exports must be non-empty");
    const auto keyframes = number_member(delta, "rotate_keyframes");
    const bool valid_keyframes = keyframes.has_value() && *keyframes >= 0.0 &&
        std::floor(*keyframes) == *keyframes;
    harness.expect(valid_keyframes, label, "comparison must report rotate_keyframes");
    return valid_keyframes
        ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(*keyframes))
        : std::nullopt;
}

void expect_parameter_snapshot_unchanged(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& before,
    const DispatchObservation& after) {
    harness.expect(
        compact_scene_delta(before) == compact_scene_delta(after),
        label,
        "dry-run changed parameter values, dirty state, history, or a revision");
}

void expect_revision_advanced(
    Harness& harness,
    std::string_view label,
    const DispatchObservation& before,
    const DispatchObservation& after,
    std::string_view revision_name) {
    const auto before_revision = number_member(before.scene_delta(), revision_name);
    const auto after_revision = number_member(after.scene_delta(), revision_name);
    harness.expect(
        before_revision.has_value() && after_revision.has_value() &&
            *after_revision > *before_revision,
        label,
        std::string(revision_name) + " did not advance");
}

bool exercise_parameter_operations(Harness& harness) {
    constexpr const char* kParameterProjectPath =
        "assets/fixtures/parameter_face_basic.marrow";
    MarrowProject* parameter_project = nullptr;
    const MarrowStatusCode load_status =
        marrow_editor_project_load(kParameterProjectPath, &parameter_project);
    if (load_status != MARROW_STATUS_OK || parameter_project == nullptr) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        harness.expect(
            false,
            "parameter project load",
            std::string(error.data ? error.data : "", error.size));
        return false;
    }

    harness.set_project(parameter_project);
    const DispatchObservation initial = harness.invoke(
        "parameters.list initial",
        R"json({"op":"parameters.list"})json");
    harness.expect(
        number_member(initial.scene_delta(), "count") == std::optional<double>(2.0) &&
            number_member(initial.scene_delta(), "group_count") ==
                std::optional<double>(1.0),
        "parameters.list initial",
        "fixture definitions or groups were not reported");
    harness.expect(
        number_member(member(initial.scene_delta(), "direct_values"), "mouth.open") ==
                std::optional<double>(0.0) &&
            number_member(member(initial.scene_delta(), "final_values"), "mouth.open") ==
                std::optional<double>(0.0),
        "parameters.list initial",
        "direct/final fixture defaults were not reported");
    harness.expect(
        bool_member(initial.scene_delta(), "project_dirty") ==
                std::optional<bool>(false) &&
            number_member(initial.scene_delta(), "undo_count") ==
                std::optional<double>(0.0),
        "parameters.list initial",
        "fresh parameter fixture should be clean with empty history");

    const DispatchObservation parameter_dry_run = harness.invoke(
        "parameter.set dry-run",
        R"json({"op":"parameter.set","args":{"id":"mouth.open","value":2,"dry_run":true}})json");
    harness.expect(
        number_member(parameter_dry_run.scene_delta(), "requested") ==
                std::optional<double>(2.0) &&
            number_member(parameter_dry_run.scene_delta(), "applied") ==
                std::optional<double>(1.0) &&
            bool_member(parameter_dry_run.scene_delta(), "clamped") ==
                std::optional<bool>(true),
        "parameter.set dry-run",
        "requested/applied/clamped response changed");
    const DispatchObservation after_parameter_dry = harness.invoke(
        "parameters.list after parameter.set dry-run",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness, "parameter.set dry-run immutability", initial, after_parameter_dry);

    const DispatchObservation before_parameter_live = after_parameter_dry;
    const DispatchObservation parameter_live = harness.invoke(
        "parameter.set live",
        R"json({"op":"parameter.set","args":{"id":"mouth.open","value":0.5}})json");
    harness.expect(
        number_member(parameter_live.scene_delta(), "applied") ==
                std::optional<double>(0.5) &&
            bool_member(parameter_live.scene_delta(), "clamped") ==
                std::optional<bool>(false),
        "parameter.set live",
        "live preview value response changed");
    const DispatchObservation after_parameter_live = harness.invoke(
        "parameters.list after parameter.set",
        R"json({"op":"parameters.list"})json");
    harness.expect(
        number_member(member(after_parameter_live.scene_delta(), "direct_values"), "mouth.open") ==
                std::optional<double>(0.5) &&
            bool_member(after_parameter_live.scene_delta(), "project_dirty") ==
                std::optional<bool>(false),
        "parameter.set preview impact",
        "parameter.set did not update only transient direct preview state");
    harness.expect(
        number_member(before_parameter_live.scene_delta(), "project_revision") ==
                number_member(after_parameter_live.scene_delta(), "project_revision") &&
            number_member(before_parameter_live.scene_delta(), "runtime_revision") ==
                number_member(after_parameter_live.scene_delta(), "runtime_revision"),
        "parameter.set preview impact",
        "parameter.set changed project or runtime revision");
    expect_revision_advanced(
        harness,
        "parameter.set preview impact",
        before_parameter_live,
        after_parameter_live,
        "preview_revision");
    harness.expect(
        number_member(after_parameter_live.scene_delta(), "undo_count") ==
            std::optional<double>(1.0),
        "parameter.set preview impact",
        "parameter.set did not create one undo entry");

    const DispatchObservation parameter_undo = harness.invoke(
        "parameter.set undo",
        R"json({"op":"undo"})json");
    harness.expect(
        string_member(&parameter_undo.root, "message").value_or(std::string_view{}).find(
            "via Agent") != std::string_view::npos,
        "parameter.set undo",
        "Agent history label was not preserved");
    const DispatchObservation after_parameter_undo = harness.invoke(
        "parameters.list after parameter undo",
        R"json({"op":"parameters.list"})json");
    harness.expect(
        number_member(member(after_parameter_undo.scene_delta(), "direct_values"), "mouth.open") ==
            std::optional<double>(0.0),
        "parameter.set undo",
        "undo did not restore the direct preview value");
    harness.invoke("parameter.set redo", R"json({"op":"redo"})json");
    const DispatchObservation after_parameter_redo = harness.invoke(
        "parameters.list after parameter redo",
        R"json({"op":"parameters.list"})json");
    harness.expect(
        number_member(member(after_parameter_redo.scene_delta(), "direct_values"), "mouth.open") ==
            std::optional<double>(0.5),
        "parameter.set redo",
        "redo did not restore the direct preview value");

    constexpr std::string_view kDeformerDryRun = R"json(
        {"op":"deformer.create","args":{"dry_run":true,"deformer":{
          "id":"agent.face.roll","name":"Agent Face Roll","kind":"rotation",
          "target_slots":["face"],
          "parameter_bindings":[{"parameter":"mouth.open","axis":"angle"}],
          "pivot":[0,0],"influence":0.75,
          "keyforms":[{"value":0,"angle":0},{"value":1,"angle":20}]
        }}}
    )json";
    constexpr std::string_view kDeformerLive = R"json(
        {"op":"deformer.create","args":{"deformer":{
          "id":"agent.face.roll","name":"Agent Face Roll","kind":"rotation",
          "target_slots":["face"],
          "parameter_bindings":[{"parameter":"mouth.open","axis":"angle"}],
          "pivot":[0,0],"influence":0.75,
          "keyforms":[{"value":0,"angle":0},{"value":1,"angle":20}]
        }}}
    )json";
    const DispatchObservation before_deformer_dry = after_parameter_redo;
    harness.invoke("deformer.create dry-run", kDeformerDryRun);
    const DispatchObservation after_deformer_dry = harness.invoke(
        "parameters.list after deformer.create dry-run",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness,
        "deformer.create dry-run immutability",
        before_deformer_dry,
        after_deformer_dry);
    harness.invoke("deformer.create live", kDeformerLive);
    const DispatchObservation after_deformer_live = harness.invoke(
        "parameters.list after deformer.create",
        R"json({"op":"parameters.list"})json");
    harness.expect(
        bool_member(after_deformer_live.scene_delta(), "project_dirty") ==
            std::optional<bool>(true),
        "deformer.create live",
        "persistent mutation did not dirty the project");
    expect_revision_advanced(
        harness,
        "deformer.create live",
        after_deformer_dry,
        after_deformer_live,
        "project_revision");
    expect_revision_advanced(
        harness,
        "deformer.create live",
        after_deformer_dry,
        after_deformer_live,
        "runtime_revision");
    expect_revision_advanced(
        harness,
        "deformer.create live",
        after_deformer_dry,
        after_deformer_live,
        "preview_revision");
    harness.invoke(
        "runtime.validate after deformer.create",
        R"json({"op":"runtime.validate"})json");

    const DispatchObservation before_capture_dry = after_deformer_live;
    harness.invoke(
        "keyform.capture dry-run",
        R"json({"op":"keyform.capture","args":{"deformer":"agent.face.roll","dry_run":true}})json");
    const DispatchObservation after_capture_dry = harness.invoke(
        "parameters.list after keyform.capture dry-run",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness,
        "keyform.capture dry-run immutability",
        before_capture_dry,
        after_capture_dry);
    harness.invoke(
        "keyform.capture live",
        R"json({"op":"keyform.capture","args":{"deformer":"agent.face.roll"}})json");
    const DispatchObservation after_capture_live = harness.invoke(
        "parameters.list after keyform.capture",
        R"json({"op":"parameters.list"})json");
    expect_revision_advanced(
        harness,
        "keyform.capture live",
        after_capture_dry,
        after_capture_live,
        "runtime_revision");
    harness.invoke(
        "keyform.capture collision rejected",
        R"json({"op":"keyform.capture","args":{"deformer":"agent.face.roll","dry_run":true}})json",
        false,
        "invalid_request");
    harness.invoke(
        "keyform.capture replacement dry-run",
        R"json({"op":"keyform.capture","args":{"deformer":"agent.face.roll","replace":true,"dry_run":true}})json");
    const DispatchObservation after_capture_collision_checks = harness.invoke(
        "parameters.list after keyform collision checks",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness,
        "keyform.capture collision dry-run immutability",
        after_capture_live,
        after_capture_collision_checks);
    const DispatchObservation capture_undo = harness.invoke(
        "keyform.capture undo",
        R"json({"op":"undo"})json");
    harness.expect(
        string_member(&capture_undo.root, "message").value_or(std::string_view{}).find(
            "via Agent") != std::string_view::npos,
        "keyform.capture undo",
        "Agent capture history label was not preserved");
    const DispatchObservation after_capture_undo = harness.invoke(
        "parameters.list after keyform capture undo",
        R"json({"op":"parameters.list"})json");
    expect_revision_advanced(
        harness,
        "keyform.capture undo runtime rebuild",
        after_capture_live,
        after_capture_undo,
        "runtime_revision");
    harness.invoke("keyform.capture redo", R"json({"op":"redo"})json");
    harness.invoke(
        "runtime.validate after keyform.capture redo",
        R"json({"op":"runtime.validate"})json");

    constexpr std::string_view kExpressionDryRun = R"json(
        {"op":"expression.create","args":{"dry_run":true,"expression":{
          "id":"agent.smile","name":"Agent Smile",
          "targets":[{"parameter":"mouth.open","value":0.25}],
          "duration":0.1,"blend":"additive","priority":5,"reset_policy":"restore"
        }}}
    )json";
    constexpr std::string_view kExpressionLive = R"json(
        {"op":"expression.create","args":{"expression":{
          "id":"agent.smile","name":"Agent Smile",
          "targets":[{"parameter":"mouth.open","value":0.25}],
          "duration":0.1,"blend":"additive","priority":5,"reset_policy":"restore"
        }}}
    )json";
    const DispatchObservation before_expression_dry = harness.invoke(
        "parameters.list before expression.create dry-run",
        R"json({"op":"parameters.list"})json");
    harness.invoke("expression.create dry-run", kExpressionDryRun);
    const DispatchObservation after_expression_dry = harness.invoke(
        "parameters.list after expression.create dry-run",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness,
        "expression.create dry-run immutability",
        before_expression_dry,
        after_expression_dry);
    harness.invoke("expression.create live", kExpressionLive);
    const DispatchObservation after_expression_live = harness.invoke(
        "parameters.list after expression.create",
        R"json({"op":"parameters.list"})json");
    expect_revision_advanced(
        harness,
        "expression.create live",
        after_expression_dry,
        after_expression_live,
        "runtime_revision");
    harness.invoke(
        "runtime.validate after expression.create",
        R"json({"op":"runtime.validate"})json");

    constexpr std::string_view kLipDryRun = R"json(
        {"op":"lip_sync.map","args":{"dry_run":true,"mapping":{
          "source":"amplitude","parameter":"mouth.open","scale":1,"bias":0,
          "attack":0.02,"release":0.08,"smoothing":0.04
        }}}
    )json";
    constexpr std::string_view kLipLive = R"json(
        {"op":"lip_sync.map","args":{"mapping":{
          "source":"amplitude","parameter":"mouth.open","scale":1,"bias":0,
          "attack":0.02,"release":0.08,"smoothing":0.04
        }}}
    )json";
    const DispatchObservation before_lip_dry = after_expression_live;
    harness.invoke("lip_sync.map dry-run", kLipDryRun);
    const DispatchObservation after_lip_dry = harness.invoke(
        "parameters.list after lip_sync.map dry-run",
        R"json({"op":"parameters.list"})json");
    expect_parameter_snapshot_unchanged(
        harness,
        "lip_sync.map dry-run immutability",
        before_lip_dry,
        after_lip_dry);
    harness.invoke("lip_sync.map live", kLipLive);
    const DispatchObservation after_lip_live = harness.invoke(
        "parameters.list after lip_sync.map",
        R"json({"op":"parameters.list"})json");
    expect_revision_advanced(
        harness,
        "lip_sync.map live",
        after_lip_dry,
        after_lip_live,
        "runtime_revision");
    harness.invoke(
        "runtime.validate after lip_sync.map",
        R"json({"op":"runtime.validate"})json");
    harness.invoke("lip_sync.map undo", R"json({"op":"undo"})json");
    const DispatchObservation after_lip_undo = harness.invoke(
        "parameters.list after lip_sync.map undo",
        R"json({"op":"parameters.list"})json");
    expect_revision_advanced(
        harness,
        "lip_sync.map undo runtime rebuild",
        after_lip_live,
        after_lip_undo,
        "runtime_revision");
    harness.invoke("lip_sync.map redo", R"json({"op":"redo"})json");
    harness.invoke(
        "runtime.validate after lip_sync.map redo",
        R"json({"op":"runtime.validate"})json");

    marrow_editor_project_destroy(parameter_project);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* project_path = "assets/fixtures/player_idle.marrow";
    if (argc > 1) {
        project_path = argv[1];
    }

    MarrowProject* project = nullptr;
    const MarrowStatusCode load_status = marrow_editor_project_load(project_path, &project);
    if (load_status != MARROW_STATUS_OK) {
        MarrowStringView error{};
        marrow_get_last_error_message(&error);
        std::cerr << "Failed to load project: "
                  << std::string(error.data ? error.data : "", error.size)
                  << '\n';
        return 1;
    }
    std::cout << "Loaded project: " << project_path << '\n';

    Harness harness(project);
    std::string registry_error;
    harness.expect(
        marrow::editor::validate_agent_operation_registry(&registry_error),
        "operation registry integrity",
        registry_error.empty() ? "registry validation failed" : registry_error);
    harness.expect(
        marrow::editor::agent_operation_descriptor_count() == kExpectedOperations.size(),
        "operation registry integrity",
        "descriptor count must match the operation protocol contract");
    const marrow::editor::AgentOperationDescriptor* registered_descriptors =
        marrow::editor::agent_operation_descriptors();
    for (std::size_t index = 0;
         index < marrow::editor::agent_operation_descriptor_count();
         ++index) {
        harness.expect(
            registered_descriptors[index].has_handler,
            "operation registry integrity",
            "registered operation is missing its handler");
    }

    const std::array<std::filesystem::path, 5> reviewed_temp_targets{{
        "/tmp/agent_spine_import_sample.mskl",
        "/tmp/agent_spine_import_sample.matl",
        "/tmp/agent_psd_import_sample.mskl",
        "/tmp/agent_psd_import_sample.matl",
        "/tmp/agent_atlas_pack_sample.matl",
    }};
    for (const auto& target : reviewed_temp_targets) {
        std::error_code error;
        std::filesystem::remove(target, error);
        harness.expect(
            !error,
            "review target setup",
            "could not clear deterministic target " + target.string());
    }

    const FileSnapshot project_file_before = snapshot_file(project_path);
    harness.expect(
        project_file_before.exists && project_file_before.bytes.has_value(),
        "project snapshot",
        "could not snapshot the project file");

    // Registry and inspection/validation protocol baseline.
    const DispatchObservation operations = harness.invoke(
        "operations.list",
        "{\"op\":\"operations.list\"}");
    expect_registry_contract(harness, operations);

    const DispatchObservation scene = harness.invoke(
        "scene.describe",
        "{\"op\":\"scene.describe\"}");
    expect_scene_contains(harness, "scene.describe", scene, "slot_count");
    harness.invoke("bones.list", "{\"op\":\"bones.list\"}");
    harness.invoke("animation.list", "{\"op\":\"animation.list\"}");
    expect_scene_contains(
        harness,
        "slots.list",
        harness.invoke("slots.list", "{\"op\":\"slots.list\"}"),
        "spark_fx");
    expect_scene_contains(
        harness,
        "skins.list",
        harness.invoke("skins.list", "{\"op\":\"skins.list\"}"),
        "mesh_base");
    expect_scene_contains(
        harness,
        "attachments.list",
        harness.invoke("attachments.list", "{\"op\":\"attachments.list\"}"),
        "body_mesh");
    expect_scene_contains(
        harness,
        "constraints.list",
        harness.invoke("constraints.list", "{\"op\":\"constraints.list\"}"),
        "editor_arm_reach");
    (void)exercise_parameter_operations(harness);
    harness.set_project(project);
    const DispatchObservation initial_timeline = harness.invoke(
        "timeline.describe initial",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"idle\"}}");
    expect_scene_contains(harness, "timeline.describe initial", initial_timeline, "draw_order_keyframes");
    const DispatchObservation initial_aim_timeline = harness.invoke(
        "timeline.describe aim initial",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    harness.expect(
        number_member(initial_aim_timeline.scene_delta(), "duration") ==
                std::optional<double>(0.5) &&
            number_member(initial_aim_timeline.scene_delta(), "inferred_duration") ==
                std::optional<double>(0.5) &&
            bool_member(initial_aim_timeline.scene_delta(), "has_explicit_duration") ==
                std::optional<bool>(true) &&
            number_member(initial_aim_timeline.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.5),
        "timeline.describe aim initial",
        "explicit and inferred duration metadata changed");
    expect_scene_contains(
        harness,
        "mesh.describe",
        harness.invoke(
            "mesh.describe",
            "{\"op\":\"mesh.describe\",\"args\":{\"skin\":\"mesh_base\","
            "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}"),
        "vertex_count");
    const DispatchObservation initial_diagnostics = harness.invoke(
        "project.diagnostics initial",
        "{\"op\":\"project.diagnostics\"}");
    expect_scene_contains(harness, "project.diagnostics initial", initial_diagnostics, "error_count");

    const DispatchObservation export_preview = harness.invoke(
        "export.preview",
        "{\"op\":\"export.preview\",\"args\":{\"binary\":true}}");
    harness.expect(
        bool_member(export_preview.scene_delta(), "binary") == std::optional<bool>(true),
        "export.preview",
        "binary preview metadata changed");
    std::vector<FileSnapshot> export_files_before;
    for (const auto& path : string_array_paths(export_preview.scene_delta(), "targets")) {
        export_files_before.push_back(snapshot_file(path));
    }
    harness.expect(
        !export_files_before.empty(),
        "export.preview",
        "preview must report resolved export targets");

    expect_scene_contains(
        harness,
        "runtime.validate",
        harness.invoke("runtime.validate", "{\"op\":\"runtime.validate\"}"),
        "diagnostics");
    const DispatchObservation baseline_comparison = harness.invoke(
        "compare_runtime_export baseline",
        "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}");
    const auto baseline_rotate_keyframes =
        expect_export_equivalence(harness, "compare_runtime_export baseline", baseline_comparison);

    const DispatchObservation initial_permissions = harness.invoke(
        "agent.permissions.describe initial",
        "{\"op\":\"agent.permissions.describe\"}");
    harness.expect(
        bool_member(initial_permissions.scene_delta(), "paused") == std::optional<bool>(false) &&
            bool_member(initial_permissions.scene_delta(), "terminated") == std::optional<bool>(false) &&
            number_member(initial_permissions.scene_delta(), "pending_reviews") ==
                std::optional<double>(0.0),
        "agent.permissions.describe initial",
        "initial agent state changed");

    // Every dry-run-capable edit/import is validated, then diagnostics and the
    // timeline are compared byte-for-byte to prove no authoring mutation.
    const std::string timeline_before_dry_runs = compact_scene_delta(initial_timeline);
    const std::string aim_timeline_before_dry_runs =
        compact_scene_delta(initial_aim_timeline);
    const std::string diagnostics_before_dry_runs = compact_scene_delta(initial_diagnostics);
    harness.invoke(
        "animation.create dry-run",
        "{\"op\":\"animation.create\",\"args\":{\"name\":\"agent_empty\","
        "\"dry_run\":true}}");
    harness.invoke(
        "animation.duplicate dry-run",
        "{\"op\":\"animation.duplicate\",\"args\":{\"source\":\"idle\","
        "\"name\":\"agent_idle_copy\",\"dry_run\":true}}");
    harness.invoke(
        "animation.rename dry-run",
        "{\"op\":\"animation.rename\",\"args\":{\"from\":\"attack\","
        "\"to\":\"agent_attack\",\"dry_run\":true}}");
    harness.invoke(
        "animation.delete dry-run",
        "{\"op\":\"animation.delete\",\"args\":{\"name\":\"attack\","
        "\"dry_run\":true}}");
    const DispatchObservation duration_dry_run = harness.invoke(
        "animation.set_duration dry-run",
        "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"aim\","
        "\"duration\":0.75,\"dry_run\":true}}");
    harness.expect(
        string_member(duration_dry_run.scene_delta(), "animation") ==
                std::optional<std::string_view>("aim") &&
            bool_member(duration_dry_run.scene_delta(), "dry_run") ==
                std::optional<bool>(true) &&
            number_member(duration_dry_run.scene_delta(), "duration") ==
                std::optional<double>(0.75) &&
            number_member(duration_dry_run.scene_delta(), "inferred_duration") ==
                std::optional<double>(0.5) &&
            number_member(duration_dry_run.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.75),
        "animation.set_duration dry-run",
        "duration dry-run metadata changed");
    harness.invoke(
        "timeline.retime_keyframes dry-run",
        "{\"op\":\"timeline.retime_keyframes\",\"args\":{\"delta\":0.05,"
        "\"snap\":false,\"keys\":[{\"kind\":\"transform\","
        "\"animation\":\"idle\",\"bone\":\"spine\","
        "\"channel\":\"translate\",\"time\":0.5},{\"kind\":\"slot_color\","
        "\"animation\":\"idle\",\"slot\":\"body\",\"time\":0.5}],"
        "\"dry_run\":true}}");
    harness.invoke(
        "set_transform dry-run",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.625,"
        "\"angle\":12,\"dry_run\":true}}");
    harness.invoke(
        "set_event_keyframe dry-run",
        "{\"op\":\"set_event_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.42,\"event\":\"footstep\",\"int\":7,\"dry_run\":true}}");
    harness.invoke(
        "set_deform_keyframe dry-run",
        "{\"op\":\"set_deform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":0.625,"
        "\"offsets\":[0,0,1,0,0,1,0,0],\"dry_run\":true}}");
    harness.invoke(
        "set_vertex_weights dry-run",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":1,\"influences\":[{\"bone\":\"spine\",\"x\":60,\"y\":0,"
        "\"weight\":0.5},{\"bone\":\"arm_l\",\"x\":20,\"y\":0,\"weight\":0.5}]}],"
        "\"dry_run\":true}}");
    harness.invoke(
        "normalize_weights dry-run",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"dry_run\":true}}");
    harness.invoke(
        "edit_ik_constraint dry-run",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"mix\":0.5,\"dry_run\":true}}");
    const DispatchObservation path_constraint_dry_run = harness.invoke(
        "edit_path_constraint dry-run",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.2,\"dry_run\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_path_constraint dry-run",
        path_constraint_dry_run,
        R"json({
          "dry_run":true,"name":"editor_guide_follow","slot":"guide",
          "bones":["path_a","path_b","path_c"],"position":0.2,
          "spacing":0.3,"spacing_mode":"percent","rotate_mix":1,
          "translate_mix":1
        })json");
    const DispatchObservation transform_constraint_dry_run = harness.invoke(
        "edit_transform_constraint dry-run",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"translate_mix\":0.5,"
        "\"offset\":{\"x\":-8},\"dry_run\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_transform_constraint dry-run",
        transform_constraint_dry_run,
        R"json({
          "dry_run":true,"name":"editor_transform_follow",
          "source":"transform_source","bones":["transform_target"],
          "rotate_mix":0.5,"translate_mix":0.5,"scale_mix":1,
          "shear_mix":0.75,"offset":{"rotation":15,"x":-8,"y":20,
          "scale_x":0.2,"scale_y":-0.1,"shear_x":5,"shear_y":-2}
        })json");
    const DispatchObservation physics_constraint_dry_run = harness.invoke(
        "edit_physics_constraint dry-run",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"mix\":0.8,"
        "\"wind\":{\"x\":10},\"dry_run\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_physics_constraint dry-run",
        physics_constraint_dry_run,
        R"json({
          "dry_run":true,"name":"editor_ribbon_secondary",
          "bones":["ribbon_01","ribbon_02"],"step":0.0166666667,
          "x":1,"y":1,"rotate":1,"scale_x":0.35,"shear_x":0,
          "limit":30,"inertia":0.85,"damping":4,"strength":18,
          "mass_inverse":1,"gravity":{"x":0,"y":-24},
          "wind":{"x":10,"y":0},"mix":0.8
        })json");
    harness.invoke(
        "set_slot_color_keyframe dry-run",
        "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"color\":{\"r\":0.5,"
        "\"g\":0.75,\"b\":1,\"a\":0.8},\"dry_run\":true}}");
    harness.invoke(
        "set_attachment_keyframe dry-run",
        "{\"op\":\"set_attachment_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"attachment\":\"body\","
        "\"dry_run\":true}}");
    harness.invoke(
        "set_draw_order_keyframe dry-run",
        "{\"op\":\"set_draw_order_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.75,\"slots\":[\"arm_l\",\"body\",\"fx_mask\","
        "\"spark_fx\",\"spawn_anchor\",\"hurtbox\",\"guide\"],\"dry_run\":true}}");

    harness.invoke(
        "import.spine_json dry-run",
        "{\"op\":\"import.spine_json\",\"args\":{"
        "\"input\":\"assets/fixtures/spine_import_sample.json\","
        "\"output\":\"/tmp/agent_spine_import_sample.mskl\",\"dry_run\":true}}");
    harness.invoke(
        "import.spine_atlas dry-run",
        "{\"op\":\"import.spine_atlas\",\"args\":{"
        "\"input\":\"assets/fixtures/spine_import_sample.atlas\","
        "\"output\":\"/tmp/agent_spine_import_sample.matl\",\"dry_run\":true}}");
    harness.invoke(
        "import.psd_layers dry-run",
        "{\"op\":\"import.psd_layers\",\"args\":{"
        "\"input\":\"assets/fixtures/psd_import_sample.psd\","
        "\"output\":\"/tmp/agent_psd_import_sample.mskl\","
        "\"atlas_output\":\"/tmp/agent_psd_import_sample.matl\",\"dry_run\":true}}");
    harness.invoke(
        "atlas.pack dry-run",
        "{\"op\":\"atlas.pack\",\"args\":{"
        "\"output\":\"/tmp/agent_atlas_pack_sample.matl\",\"dry_run\":true}}");
    for (const auto& target : reviewed_temp_targets) {
        harness.expect(
            !std::filesystem::exists(target),
            "import/pack dry-run immutability",
            target.string() + " was written by a dry-run");
    }

    const DispatchObservation timeline_after_dry_runs = harness.invoke(
        "timeline.describe after dry-runs",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"idle\"}}");
    const DispatchObservation aim_timeline_after_dry_runs = harness.invoke(
        "timeline.describe aim after dry-runs",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    const DispatchObservation diagnostics_after_dry_runs = harness.invoke(
        "project.diagnostics after dry-runs",
        "{\"op\":\"project.diagnostics\"}");
    harness.expect(
        compact_scene_delta(timeline_after_dry_runs) == timeline_before_dry_runs,
        "dry-run timeline immutability",
        "timeline.describe changed after dry-runs");
    harness.expect(
        compact_scene_delta(aim_timeline_after_dry_runs) ==
            aim_timeline_before_dry_runs,
        "duration dry-run immutability",
        "aim duration changed after a dry-run");
    harness.expect(
        compact_scene_delta(diagnostics_after_dry_runs) == diagnostics_before_dry_runs,
        "dry-run project immutability",
        "project.diagnostics changed after dry-runs");

    harness.invoke(
        "animation.set_duration rejected shrink",
        "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"aim\","
        "\"duration\":0.25}}",
        false,
        "validation_failed");
    const DispatchObservation aim_after_rejected_shrink = harness.invoke(
        "timeline.describe aim after rejected shrink",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    harness.expect(
        compact_scene_delta(aim_after_rejected_shrink) ==
            aim_timeline_before_dry_runs,
        "animation.set_duration rejected shrink",
        "rejected duration shrink changed the runtime");
    harness.invoke(
        "undo after rejected duration shrink",
        "{\"op\":\"undo\"}",
        false,
        "nothing_to_undo");

    const DispatchObservation duration_set = harness.invoke(
        "animation.set_duration",
        "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"aim\","
        "\"duration\":0.75}}");
    harness.expect(
        string_member(duration_set.scene_delta(), "animation") ==
                std::optional<std::string_view>("aim") &&
            bool_member(duration_set.scene_delta(), "dry_run") ==
                std::optional<bool>(false) &&
            number_member(duration_set.scene_delta(), "duration") ==
                std::optional<double>(0.75) &&
            number_member(duration_set.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.75),
        "animation.set_duration",
        "live duration mutation metadata changed");
    const DispatchObservation aim_after_duration_set = harness.invoke(
        "timeline.describe aim after duration set",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    harness.expect(
        number_member(aim_after_duration_set.scene_delta(), "duration") ==
                std::optional<double>(0.75) &&
            number_member(aim_after_duration_set.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.75),
        "animation.set_duration",
        "live duration did not reach the runtime");
    harness.invoke("undo animation duration", "{\"op\":\"undo\"}");
    const DispatchObservation aim_after_duration_undo = harness.invoke(
        "timeline.describe aim after duration undo",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    harness.expect(
        number_member(aim_after_duration_undo.scene_delta(), "duration") ==
                std::optional<double>(0.5) &&
            number_member(aim_after_duration_undo.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.5),
        "undo animation duration",
        "undo did not restore the authored duration");
    harness.invoke("redo animation duration", "{\"op\":\"redo\"}");
    const DispatchObservation aim_after_duration_redo = harness.invoke(
        "timeline.describe aim after duration redo",
        "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
    harness.expect(
        number_member(aim_after_duration_redo.scene_delta(), "duration") ==
                std::optional<double>(0.75) &&
            number_member(aim_after_duration_redo.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.75),
        "redo animation duration",
        "redo did not restore the authored duration edit");

    // Pause, terminate, and resume must gate mutations while retaining protocol
    // metadata and monotonic activity IDs.
    harness.invoke("agent.pause", "{\"op\":\"agent.pause\"}");
    harness.invoke(
        "paused mutation blocked",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.8,\"angle\":5}}",
        false,
        "blocked");
    harness.invoke("agent.resume after pause", "{\"op\":\"agent.resume\"}");
    const DispatchObservation resumed_permissions = harness.invoke(
        "agent.permissions.describe resumed",
        "{\"op\":\"agent.permissions.describe\"}");
    harness.expect(
        bool_member(resumed_permissions.scene_delta(), "paused") ==
                std::optional<bool>(false) &&
            bool_member(resumed_permissions.scene_delta(), "terminated") ==
                std::optional<bool>(false),
        "agent.resume",
        "resume must clear paused");

    const DispatchObservation retimed = harness.invoke(
        "timeline.retime_keyframes",
        "{\"op\":\"timeline.retime_keyframes\",\"args\":{\"delta\":0.05,"
        "\"snap\":false,\"keys\":[{\"kind\":\"transform\","
        "\"animation\":\"idle\",\"bone\":\"spine\","
        "\"channel\":\"translate\",\"time\":0.5},{\"kind\":\"slot_color\","
        "\"animation\":\"idle\",\"slot\":\"body\",\"time\":0.5}]}}");
    harness.expect(
        number_member(retimed.scene_delta(), "key_count") == std::optional<double>(2.0) &&
            number_member(retimed.scene_delta(), "applied_delta") ==
                std::optional<double>(0.05),
        "timeline.retime_keyframes",
        "atomic retime metadata changed");
    harness.invoke("undo timeline retime", "{\"op\":\"undo\"}");

    // --- MAR-169: timeline.set_interpolation. The dry run doubles as the
    // read-back channel, reporting each selected key's current curve without
    // mutating anything. ---
    {
        const char* kTranslateKey =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"translate\",\"time\":0.0}";
        const auto previous_curve = [&](const DispatchObservation& observation)
            -> const json::Value* {
            const json::Value* keys = member(observation.scene_delta(), "keys");
            if (keys == nullptr || !keys->is_array() || keys->as_array().empty()) {
                return nullptr;
            }
            return member(&keys->as_array()[0], "previous_interpolation");
        };
        const auto curve_is_string = [&](const json::Value* curve,
                                         std::string_view expected) {
            return curve != nullptr && curve->is_string() &&
                curve->as_string() == expected;
        };
        const auto curve_matches = [&](const json::Value* curve,
                                       const std::array<double, 4>& expected) {
            if (curve == nullptr || !curve->is_array() ||
                curve->as_array().size() != 4U) {
                return false;
            }
            for (std::size_t index = 0U; index < 4U; ++index) {
                const json::Value& value = curve->as_array()[index];
                if (!value.is_number() ||
                    std::abs(value.as_number() - expected[index]) > 1e-5) {
                    return false;
                }
            }
            return true;
        };

        const DispatchObservation before_revision = harness.invoke(
            "scene.describe before interpolation dry run", "{\"op\":\"scene.describe\"}");
        (void)before_revision;
        const DispatchObservation interpolation_dry_run = harness.invoke(
            "timeline.set_interpolation dry run",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey +
                "],\"interpolation\":[0.2,-0.4,0.8,1.6],\"dry_run\":true}}");
        harness.expect(
            bool_member(interpolation_dry_run.scene_delta(), "dry_run") ==
                    std::optional<bool>(true) &&
                number_member(interpolation_dry_run.scene_delta(), "key_count") ==
                    std::optional<double>(1.0) &&
                number_member(
                    interpolation_dry_run.scene_delta(), "changed_key_count") ==
                    std::optional<double>(1.0) &&
                bool_member(interpolation_dry_run.scene_delta(), "keys_truncated") ==
                    std::optional<bool>(false) &&
                curve_is_string(previous_curve(interpolation_dry_run), "linear"),
            "timeline.set_interpolation dry run",
            "dry run did not report the current curve of every selected key");

        const DispatchObservation interpolation_live = harness.invoke(
            "timeline.set_interpolation",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":[0.2,-0.4,0.8,1.6]}}");
        harness.expect(
            number_member(interpolation_live.scene_delta(), "changed_key_count") ==
                    std::optional<double>(1.0) &&
                bool_member(interpolation_live.scene_delta(), "dry_run") ==
                    std::optional<bool>(false),
            "timeline.set_interpolation live",
            "a live easing write did not report its changed key count");

        const DispatchObservation read_back = harness.invoke(
            "timeline.set_interpolation read-back",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.expect(
            curve_matches(previous_curve(read_back), {0.2, -0.4, 0.8, 1.6}),
            "timeline.set_interpolation read-back",
            "the stored overshoot curve did not survive the live write");

        // A second identical live call changes nothing.
        harness.invoke(
            "timeline.set_interpolation no_change",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":[0.2,-0.4,0.8,1.6]}}",
            false,
            "no_change");

        // A slot-colour key and a deform key are both supported families.
        harness.invoke(
            "timeline.set_interpolation slot_color",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"slot_color\",\"animation\":\"idle\",\"slot\":\"body\","
            "\"time\":0.0}],\"interpolation\":[0.1,0.9,0.4,0.2]}}");
        harness.invoke(
            "timeline.set_interpolation deform",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"deform\",\"animation\":\"idle\",\"slot\":\"body\","
            "\"attachment\":\"body_mesh\",\"time\":0.0}],"
            "\"interpolation\":[0.15,0.85,0.45,0.25]}}");
        harness.invoke("undo interpolation deform", "{\"op\":\"undo\"}");
        harness.invoke("undo interpolation slot_color", "{\"op\":\"undo\"}");

        // Undo restores the previous curve, verified through a follow-up dry run.
        harness.invoke("undo timeline interpolation", "{\"op\":\"undo\"}");
        const DispatchObservation after_undo = harness.invoke(
            "timeline.set_interpolation after undo",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(after_undo), "linear"),
            "undo timeline interpolation",
            "undo did not restore the previous curve");

        // Rejections. Each leaves the project untouched, which the follow-up
        // read-back proves.
        harness.invoke(
            "timeline.set_interpolation requires interpolation",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "]}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects out-of-range x",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":[1.5,0.0,0.5,1.0]}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects a short array",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":[0.0,0.0,0.5]}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects an unknown kind string",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"quadratic\"}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects draw_order keys",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"draw_order\",\"animation\":\"idle\",\"time\":0.0}],"
            "\"interpolation\":\"linear\"}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects slot_attachment keys",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"slot_attachment\",\"animation\":\"idle\",\"slot\":\"body\","
            "\"time\":0.0}],\"interpolation\":\"linear\"}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects an empty key list",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[],"
            "\"interpolation\":\"linear\"}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects duplicate keys",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "," + kTranslateKey +
                "],\"interpolation\":[0.3,0.3,0.6,0.6]}}",
            false,
            "invalid_request");
        harness.invoke(
            "timeline.set_interpolation rejects an unresolvable key",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"translate\",\"time\":99.0}],"
            "\"interpolation\":[0.3,0.3,0.6,0.6]}}",
            false,
            "not_found");
        const DispatchObservation after_rejections = harness.invoke(
            "timeline.set_interpolation unchanged after rejections",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(after_rejections), "linear"),
            "timeline.set_interpolation rejection atomicity",
            "a rejected easing request mutated the project");
    }


    // --- MAR-170: the four new preset tokens on the same operation. The
    // registry stays at 57; only this one argument's vocabulary grew. ---
    {
        const char* kTranslateKey =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"translate\",\"time\":0.0}";
        const char* kSecondKey =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"translate\",\"time\":0.5}";
        const auto previous_curve = [&](const DispatchObservation& observation)
            -> const json::Value* {
            const json::Value* keys = member(observation.scene_delta(), "keys");
            if (keys == nullptr || !keys->is_array() || keys->as_array().empty()) {
                return nullptr;
            }
            return member(&keys->as_array()[0], "previous_interpolation");
        };
        const auto curve_is_string = [&](const json::Value* curve,
                                         std::string_view expected) {
            return curve != nullptr && curve->is_string() &&
                curve->as_string() == expected;
        };
        const auto curve_matches = [&](const json::Value* curve,
                                       const std::array<double, 4>& expected) {
            if (curve == nullptr || !curve->is_array() ||
                curve->as_array().size() != 4U) {
                return false;
            }
            for (std::size_t index = 0U; index < 4U; ++index) {
                const json::Value& value = curve->as_array()[index];
                if (!value.is_number() ||
                    std::abs(value.as_number() - expected[index]) > 1e-5) {
                    return false;
                }
            }
            return true;
        };
        // Spelled out literally: a test that reads kCurvePresets proves nothing.
        const std::array<std::pair<const char*, std::array<double, 4>>, 4> kTokens{{
            {"ease", {0.25, 0.1, 0.25, 1.0}},
            {"ease_in", {0.42, 0.0, 1.0, 1.0}},
            {"ease_out", {0.0, 0.0, 0.58, 1.0}},
            {"ease_in_out", {0.42, 0.0, 0.58, 1.0}},
        }};

        for (const auto& [token, expected] : kTokens) {
            harness.invoke(
                std::string("timeline.set_interpolation ") + token,
                std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                    kTranslateKey + "],\"interpolation\":\"" + token + "\"}}");
            const DispatchObservation read_back = harness.invoke(
                std::string("timeline.set_interpolation ") + token + " read-back",
                std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                    kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
            harness.expect(
                curve_matches(previous_curve(read_back), expected),
                std::string("timeline.set_interpolation ") + token,
                std::string("the ") + token +
                    " preset token did not store its fixed control points");
            harness.invoke(
                std::string("undo ") + token, "{\"op\":\"undo\"}");
        }

        // "linear" and "stepped" behave exactly as before.
        harness.invoke(
            "timeline.set_interpolation stepped preset token",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"stepped\"}}");
        const DispatchObservation stepped_read_back = harness.invoke(
            "timeline.set_interpolation stepped read-back",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(stepped_read_back), "stepped"),
            "timeline.set_interpolation stepped preset token",
            "the stepped token no longer stores a stepped curve");
        harness.invoke("undo stepped preset token", "{\"op\":\"undo\"}");

        // Hyphenated and camelCase spellings stay rejected: one spelling per
        // concept, and the rejection must leave the project untouched.
        for (const char* rejected : {"ease-in", "easeIn", "bounce"}) {
            harness.invoke(
                std::string("timeline.set_interpolation rejects ") + rejected,
                std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                    kTranslateKey + "],\"interpolation\":\"" + rejected + "\"}}",
                false,
                "invalid_request");
        }
        const DispatchObservation after_token_rejections = harness.invoke(
            "timeline.set_interpolation unchanged after token rejections",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(after_token_rejections), "linear"),
            "timeline.set_interpolation token rejection atomicity",
            "a rejected preset token mutated the project");

        // One multi-key preset call is one history entry, and undo restores
        // every key.
        harness.invoke(
            "timeline.set_interpolation multi-key preset",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "," + kSecondKey +
                "],\"interpolation\":\"ease_in_out\"}}");
        harness.invoke("undo multi-key preset", "{\"op\":\"undo\"}");
        const DispatchObservation after_multi_undo = harness.invoke(
            "timeline.set_interpolation multi-key undo read-back",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kTranslateKey + "],\"interpolation\":\"ease\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(after_multi_undo), "linear"),
            "timeline.set_interpolation multi-key preset undo",
            "one undo did not restore every key a multi-key preset wrote");
        const DispatchObservation second_after_multi_undo = harness.invoke(
            "timeline.set_interpolation multi-key undo second key",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kSecondKey + "],\"interpolation\":\"ease\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(second_after_multi_undo), "stepped"),
            "timeline.set_interpolation multi-key preset undo",
            "one undo did not restore the second key a multi-key preset wrote");

        // set_transform still creates a Linear key: a headless agent's output
        // must not depend on the invoking human's preference file.
        harness.invoke(
            "set_transform without interpolation",
            "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
            "\"bone\":\"spine\",\"channel\":\"translate\",\"time\":0.875,"
            "\"x\":3,\"y\":4}}");
        const DispatchObservation seeded = harness.invoke(
            "set_transform default easing read-back",
            "{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"translate\",\"time\":0.875}],"
            "\"interpolation\":\"ease\",\"dry_run\":true}}");
        harness.expect(
            curve_is_string(previous_curve(seeded), "linear"),
            "set_transform default easing",
            "an agent-created key no longer defaults to Linear");
        harness.invoke("undo agent-created key", "{\"op\":\"undo\"}");
    }

    // --- MAR-171: timeline.set_curve_mode, the 58th operation. ---
    {
        const char* kSpineRotate =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.0}";
        const char* kSpineRotateSecond =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.5}";
        const auto first_key_member = [&](const DispatchObservation& observation,
                                          std::string_view name)
            -> const json::Value* {
            const json::Value* keys = member(observation.scene_delta(), "keys");
            if (keys == nullptr || !keys->is_array() || keys->as_array().empty()) {
                return nullptr;
            }
            return member(&keys->as_array()[0], name);
        };
        const auto string_is = [](const json::Value* value, std::string_view expected) {
            return value != nullptr && value->is_string() &&
                value->as_string() == expected;
        };
        const auto curve_matches = [&](const json::Value* curve,
                                       const std::array<double, 4>& expected) {
            if (curve == nullptr || !curve->is_array() ||
                curve->as_array().size() != 4U) {
                return false;
            }
            for (std::size_t index = 0U; index < 4U; ++index) {
                const json::Value& value = curve->as_array()[index];
                if (!value.is_number() ||
                    std::abs(value.as_number() - expected[index]) > 1e-5) {
                    return false;
                }
            }
            return true;
        };
        // The design's §6.6 worked example, spelled out rather than recomputed.
        const std::array<double, 4> kSegment0{
            1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};

        // A dry run reports the current mode, driver, and curve of every
        // selected key without touching the session.
        const DispatchObservation dry_run = harness.invoke(
            "timeline.set_curve_mode dry run",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"mode\":\"auto\",\"driver\":\"angle\","
                "\"dry_run\":true}}");
        harness.expect(
            bool_member(dry_run.scene_delta(), "dry_run") ==
                    std::optional<bool>(true) &&
                number_member(dry_run.scene_delta(), "key_count") ==
                    std::optional<double>(1.0) &&
                string_is(member(dry_run.scene_delta(), "mode"), "auto") &&
                string_is(member(dry_run.scene_delta(), "driver"), "angle") &&
                string_is(first_key_member(dry_run, "previous_mode"), "manual") &&
                first_key_member(dry_run, "previous_driver") != nullptr &&
                first_key_member(dry_run, "previous_driver")->is_null(),
            "timeline.set_curve_mode dry run",
            "the dry run did not report the current mode and driver of each key");

        const DispatchObservation live = harness.invoke(
            "timeline.set_curve_mode live",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "," + kSpineRotateSecond +
                "],\"mode\":\"auto\",\"driver\":\"angle\"}}");
        harness.expect(
            number_member(live.scene_delta(), "changed_key_count") ==
                    std::optional<double>(2.0) &&
                number_member(live.scene_delta(), "resolved_key_count") ==
                    std::optional<double>(2.0) &&
                bool_member(live.scene_delta(), "dry_run") ==
                    std::optional<bool>(false),
            "timeline.set_curve_mode live",
            "a live curve-mode write did not report its changed and resolved counts");

        const DispatchObservation read_back = harness.invoke(
            "timeline.set_curve_mode read-back",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"mode\":\"auto\",\"driver\":\"angle\","
                "\"dry_run\":true}}");
        harness.expect(
            string_is(first_key_member(read_back, "previous_mode"), "auto") &&
                string_is(first_key_member(read_back, "previous_driver"), "angle") &&
                curve_matches(
                    first_key_member(read_back, "previous_interpolation"), kSegment0),
            "timeline.set_curve_mode read-back",
            "the resolved curve and recorded intent did not survive the live write");

        // A second identical live call changes nothing at all.
        harness.invoke(
            "timeline.set_curve_mode no_change",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "," + kSpineRotateSecond +
                "],\"mode\":\"auto\",\"driver\":\"angle\"}}",
            false,
            "no_change");

        // timeline.set_interpolation on an auto key demotes it, proven through
        // the Agent surface by the next dry run's previous_mode.
        harness.invoke(
            "timeline.set_interpolation demotes an auto key",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"interpolation\":\"ease\"}}");
        const DispatchObservation demoted = harness.invoke(
            "timeline.set_curve_mode after demotion",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"mode\":\"auto\",\"driver\":\"angle\","
                "\"dry_run\":true}}");
        harness.expect(
            string_is(first_key_member(demoted, "previous_mode"), "manual"),
            "timeline.set_interpolation demotion",
            "writing an absolute easing did not demote the key to manual");
        harness.invoke("undo the demotion", "{\"op\":\"undo\"}");

        // A neighbour retime changes an auto key's curve in one history entry
        // that one undo fully reverses.
        const DispatchObservation before_retime = harness.invoke(
            "timeline.set_curve_mode before retime",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"mode\":\"auto\",\"dry_run\":true}}");
        harness.expect(
            curve_matches(
                first_key_member(before_retime, "previous_interpolation"), kSegment0),
            "timeline.set_curve_mode undo",
            "one undo did not restore the resolved curve");

        // Explicit reconciliation: re-applying `auto` to already-auto keys
        // succeeds and reports resolver work with no intent change.
        harness.invoke(
            "timeline.retime_keyframes moves an auto neighbour",
            std::string("{\"op\":\"timeline.retime_keyframes\",\"args\":{\"keys\":[") +
                kSpineRotateSecond + "],\"delta\":0.25}}");
        harness.invoke("undo the neighbour retime", "{\"op\":\"undo\"}");

        // Rejections, each leaving the project untouched.
        struct CurveModeRejection {
            const char* label;
            std::string request;
        };
        const std::vector<CurveModeRejection> rejections{
            {"missing mode",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "]}}"},
            {"unknown mode",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "],\"mode\":\"automatic\"}}"},
            {"unknown driver",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "],\"mode\":\"auto\",\"driver\":\"z\"}}"},
            {"driver with manual",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "],\"mode\":\"manual\",\"driver\":\"angle\"}}"},
            {"driver the family does not own",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "],\"mode\":\"auto\",\"driver\":\"x\"}}"},
            {"a deform key",
             "{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":["
             "{\"kind\":\"deform\",\"animation\":\"idle\",\"slot\":\"body\","
             "\"attachment\":\"body_mesh\",\"time\":0.0}],\"mode\":\"auto\"}}"},
            {"a draw_order key",
             "{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":["
             "{\"kind\":\"draw_order\",\"animation\":\"idle\",\"time\":0.0}],"
             "\"mode\":\"auto\"}}"},
            {"a duplicate selector",
             std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                 kSpineRotate + "," + kSpineRotate + "],\"mode\":\"auto\"}}"},
            {"an empty keys array",
             "{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[],"
             "\"mode\":\"auto\"}}"},
        };
        for (const CurveModeRejection& rejection : rejections) {
            harness.invoke(
                std::string("timeline.set_curve_mode rejects ") + rejection.label,
                rejection.request,
                false,
                "invalid_request");
        }
        harness.invoke(
            "timeline.set_curve_mode rejects an unresolvable selector",
            "{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":9.75}],\"mode\":\"auto\"}}",
            false,
            "not_found");

        const DispatchObservation after_rejections = harness.invoke(
            "timeline.set_curve_mode unchanged after rejections",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "],\"mode\":\"auto\",\"dry_run\":true}}");
        harness.expect(
            string_is(first_key_member(after_rejections, "previous_mode"), "auto") &&
                curve_matches(
                    first_key_member(after_rejections, "previous_interpolation"),
                    kSegment0),
            "timeline.set_curve_mode rejection atomicity",
            "a rejected curve-mode request mutated the project");

        // Back to manual so the rest of the smoke sees the fixture's curves.
        harness.invoke(
            "timeline.set_curve_mode back to manual",
            std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                kSpineRotate + "," + kSpineRotateSecond + "],\"mode\":\"manual\"}}");

        // An explicitly supplied easing on an AUTOMATIC slot-colour key must
        // not be silently discarded by the resolver that runs after it.
        // `set_slot_color_keyframe` writes `interpolation` directly rather than
        // through `set_keyframe_interpolation()`, so it carries the demotion
        // itself, exactly as the numeric inspector does.
        {
            const char* kBodyColor =
                "{\"kind\":\"slot_color\",\"animation\":\"idle\",\"slot\":\"body\","
                "\"time\":0.0}";
            harness.invoke(
                "timeline.set_curve_mode auto on a slot colour key",
                std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                    kBodyColor + "],\"mode\":\"auto\",\"driver\":\"r\"}}");

            // Colour only, no easing argument: the key stays automatic and its
            // curve is re-resolved against the new driver value.
            harness.invoke(
                "set_slot_color_keyframe without an easing keeps auto",
                "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"slot\":\"body\",\"time\":0.0,\"color\":{\"r\":0.5,\"g\":0.5,\"b\":0.5,\"a\":1.0}}}");
            const DispatchObservation still_auto = harness.invoke(
                "slot colour key still auto after a colour-only write",
                std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                    kBodyColor + "],\"mode\":\"auto\",\"dry_run\":true}}");
            harness.expect(
                string_is(first_key_member(still_auto, "previous_mode"), "auto"),
                "set_slot_color_keyframe colour-only",
                "a colour-only write must leave an automatic key automatic");

            // An explicit easing is an absolute authored curve: it demotes the
            // key and survives the resolver that follows it.
            harness.invoke(
                "set_slot_color_keyframe with an explicit easing demotes",
                "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"slot\":\"body\",\"time\":0.0,"
                "\"color\":{\"r\":0.5,\"g\":0.5,\"b\":0.5,\"a\":1.0},"
                "\"interpolation\":[0.2,0.3,0.7,0.8]}}");
            const DispatchObservation demoted_color = harness.invoke(
                "slot colour easing survived the resolver",
                std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                    kBodyColor + "],\"mode\":\"auto\",\"dry_run\":true}}");
            harness.expect(
                string_is(first_key_member(demoted_color, "previous_mode"), "manual") &&
                    curve_matches(
                        first_key_member(demoted_color, "previous_interpolation"),
                        {0.2, 0.3, 0.7, 0.8}),
                "set_slot_color_keyframe explicit easing",
                "an explicitly supplied easing was silently discarded by the resolver");

            harness.invoke("undo the explicit slot colour easing", "{\"op\":\"undo\"}");
            harness.invoke("undo the colour-only slot write", "{\"op\":\"undo\"}");
            harness.invoke("undo the slot colour auto mode", "{\"op\":\"undo\"}");
        }

        harness.invoke("undo back to manual", "{\"op\":\"undo\"}");
        harness.invoke("undo the automatic application", "{\"op\":\"undo\"}");
    }

    // --- MAR-172: timeline.set_loop_sync, the 59th operation. ---
    {
        const char* kSpineLane =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\"}";
        const char* kAimLane =
            "{\"kind\":\"transform\",\"animation\":\"aim\",\"bone\":\"arm_l\","
            "\"channel\":\"rotate\"}";
        const auto first_lane_member = [&](const DispatchObservation& observation,
                                           std::string_view name)
            -> const json::Value* {
            const json::Value* entries = member(observation.scene_delta(), "lanes");
            if (entries == nullptr || !entries->is_array() ||
                entries->as_array().empty()) {
                return nullptr;
            }
            return member(&entries->as_array()[0], name);
        };
        const auto lane_string_is = [](const json::Value* value,
                                       std::string_view expected) {
            return value != nullptr && value->is_string() &&
                value->as_string() == expected;
        };
        const auto lane_number_is = [](const json::Value* value, double expected) {
            return value != nullptr && value->is_number() &&
                std::abs(value->as_number() - expected) <= 1e-6;
        };

        // Enabling before an explicit duration exists is rejected, naming the
        // remedy, and leaves the project untouched.
        harness.invoke(
            "timeline.set_loop_sync rejects a clip with no explicit duration",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true}}",
            false,
            "invalid_request");

        harness.invoke(
            "animation.set_duration for the loop boundary",
            "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"idle\","
            "\"duration\":1.5}}");

        // A dry run reports the resulting boundary key without touching the
        // session, and reports no boundary key existed before.
        const DispatchObservation loop_dry_run = harness.invoke(
            "timeline.set_loop_sync dry run",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            bool_member(loop_dry_run.scene_delta(), "dry_run") ==
                    std::optional<bool>(true) &&
                number_member(loop_dry_run.scene_delta(), "lane_count") ==
                    std::optional<double>(1.0) &&
                number_member(loop_dry_run.scene_delta(), "created_key_count") ==
                    std::optional<double>(1.0) &&
                bool_member(loop_dry_run.scene_delta(), "lanes_truncated") ==
                    std::optional<bool>(false) &&
                first_lane_member(loop_dry_run, "previous_enabled") != nullptr &&
                first_lane_member(loop_dry_run, "previous_enabled")->is_boolean() &&
                !first_lane_member(loop_dry_run, "previous_enabled")->as_boolean() &&
                lane_string_is(
                    first_lane_member(loop_dry_run, "boundary_action"), "created") &&
                lane_number_is(first_lane_member(loop_dry_run, "boundary_time"), 1.5),
            "timeline.set_loop_sync dry run",
            "the dry run did not report the resulting boundary key of each lane");

        const DispatchObservation loop_live = harness.invoke(
            "timeline.set_loop_sync live",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true}}");
        harness.expect(
            number_member(loop_live.scene_delta(), "changed_lane_count") ==
                    std::optional<double>(1.0) &&
                number_member(loop_live.scene_delta(), "created_key_count") ==
                    std::optional<double>(1.0) &&
                bool_member(loop_live.scene_delta(), "dry_run") ==
                    std::optional<bool>(false),
            "timeline.set_loop_sync live",
            "a live loop-sync write did not report its changed and created counts");

        // A second identical live call changes nothing at all.
        harness.invoke(
            "timeline.set_loop_sync no_change",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true}}",
            false,
            "no_change");

        // A managed boundary key's value and easing are derived from the key at
        // time zero, so an Agent write there would be reverted by the sync in
        // the same transaction and a removal would strand the lane's contract.
        // The GUI skips such a key and reports it; the Agent rejects it
        // atomically, naming the remedy. This block proves both halves of the
        // rejection: the operation fails AND the lane's authored keys survive.
        {
            const char* kBoundaryKey =
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":1.5}";
            const char* kMiddleKey =
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":1.0}";
            const auto still_there = [&](const char* label, const char* key) {
                return harness.invoke(
                    label,
                    std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                        key + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
            };
            still_there("the spine key at 1.0 exists before the guards", kMiddleKey);

            harness.invoke(
                "timeline.set_interpolation rejects a managed loop boundary",
                std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                    kBoundaryKey + "],\"interpolation\":\"ease\"}}",
                false,
                "invalid_request");
            harness.invoke(
                "timeline.set_curve_mode rejects a managed loop boundary",
                std::string("{\"op\":\"timeline.set_curve_mode\",\"args\":{\"keys\":[") +
                    kBoundaryKey + "],\"mode\":\"auto\"}}",
                false,
                "invalid_request");
            harness.invoke(
                "set_transform rejects a managed loop boundary",
                "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
                "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":1.5,\"angle\":45}}",
                false,
                "invalid_request");
            harness.invoke(
                "remove_transform_keyframe rejects a managed loop boundary",
                "{\"op\":\"remove_transform_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":1.5}}",
                false,
                "invalid_request");
            // The data-loss assertion. Before the guard existed the removal
            // returned `ok` and the authored key at 1.0 was promoted to the
            // boundary -- moved to 1.5 and overwritten from key 0 -- so a test
            // that only checked the return code would have missed it.
            still_there("the spine key at 1.0 survived the rejected removal", kMiddleKey);
            still_there(
                "the spine key at 0.5 survived the rejected removal",
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":0.5}");

            // The same guard on the other two families, each opted in here and
            // undone at the end of the block.
            harness.invoke(
                "timeline.set_loop_sync enables the colour and deform lanes",
                "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
                "{\"kind\":\"slot_color\",\"animation\":\"idle\",\"slot\":\"body\"},"
                "{\"kind\":\"deform\",\"animation\":\"idle\",\"slot\":\"body\","
                "\"attachment\":\"body_mesh\"}],\"enabled\":true}}");

            harness.invoke(
                "set_slot_color_keyframe rejects a managed loop boundary",
                "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"slot\":\"body\",\"time\":1.5,"
                "\"color\":{\"r\":0.1,\"g\":0.2,\"b\":0.3,\"a\":0.4}}}",
                false,
                "invalid_request");
            harness.invoke(
                "remove_slot_color_keyframe rejects a managed loop boundary",
                "{\"op\":\"remove_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"slot\":\"body\",\"time\":1.5}}",
                false,
                "invalid_request");
            still_there(
                "the body colour key at 1.0 survived the rejected removal",
                "{\"kind\":\"slot_color\",\"animation\":\"idle\",\"slot\":\"body\","
                "\"time\":1.0}");

            harness.invoke(
                "remove_deform_keyframe rejects a managed loop boundary",
                "{\"op\":\"remove_deform_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":1.5}}",
                false,
                "invalid_request");
            still_there(
                "the body deform key at 1.0 survived the rejected removal",
                "{\"kind\":\"deform\",\"animation\":\"idle\",\"slot\":\"body\","
                "\"attachment\":\"body_mesh\",\"time\":1.0}");

            // Every guarded operation still works on a key that is NOT the
            // managed boundary, so the guard is boundary-specific rather than a
            // blanket lock on an opted-in lane.
            harness.invoke(
                "timeline.set_interpolation still writes a non-boundary key",
                std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                    kMiddleKey + "],\"interpolation\":\"ease\"}}");
            harness.invoke("undo the non-boundary easing", "{\"op\":\"undo\"}");
            harness.invoke(
                "remove_transform_keyframe still removes a non-boundary key",
                "{\"op\":\"remove_transform_keyframe\",\"args\":{\"animation\":\"idle\","
                "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.5}}");
            harness.invoke("undo the non-boundary removal", "{\"op\":\"undo\"}");

            harness.invoke("undo the colour and deform enable", "{\"op\":\"undo\"}");
        }

        // set_transform on the time-zero key updates the boundary key in the
        // SAME history entry, proven by the following dry run's read-back.
        harness.invoke(
            "set_transform on the time-zero key of an opted-in lane",
            "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
            "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.0,\"angle\":21}}");
        const DispatchObservation followed = harness.invoke(
            "timeline.set_loop_sync read-back after set_transform",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            first_lane_member(followed, "previous_boundary") != nullptr &&
                first_lane_member(followed, "previous_boundary")->is_object() &&
                lane_number_is(
                    member(first_lane_member(followed, "previous_boundary"), "angle"),
                    21.0),
            "timeline.set_loop_sync follows the first key",
            "the boundary key did not follow the time-zero key in the same entry");
        harness.invoke("undo the time-zero transform", "{\"op\":\"undo\"}");

        // animation.set_duration moves the boundary key in one reversible entry.
        harness.invoke(
            "animation.set_duration moves the boundary",
            "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"idle\","
            "\"duration\":2.0}}");
        const DispatchObservation moved = harness.invoke(
            "timeline.set_loop_sync read-back after a duration move",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            lane_number_is(first_lane_member(moved, "boundary_time"), 2.0),
            "timeline.set_loop_sync duration move",
            "a duration change did not move the managed boundary key");
        harness.invoke("undo the duration move", "{\"op\":\"undo\"}");
        const DispatchObservation restored = harness.invoke(
            "timeline.set_loop_sync read-back after undo",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            lane_number_is(first_lane_member(restored, "boundary_time"), 1.5),
            "timeline.set_loop_sync undo",
            "one undo did not restore the boundary key's time");

        // A shrink below the one-millisecond spacing floor is rejected with the
        // project unchanged: the sync cancels the enclosing transaction.
        // The duration primitive accepts 1.0005 -- its boundary-excluding floor
        // is 1.0 -- and the sync's one-millisecond spacing check then rejects,
        // cancelling the enclosing transaction. The session surfaces that as a
        // commit-time validation failure.
        harness.invoke(
            "animation.set_duration rejected onto the spacing floor",
            "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"idle\","
            "\"duration\":1.0005}}",
            false,
            "validation_failed");

        // The runtime-only aim lane adopts its existing key at 0.5. The smoke's
        // earlier duration cases leave `aim` at 0.75, so the precondition is
        // asserted rather than assumed and then restored to the fixture's own
        // boundary, which is exactly where the adoptable key sits.
        const DispatchObservation aim_before_adoption = harness.invoke(
            "timeline.describe aim before adoption",
            "{\"op\":\"timeline.describe\",\"args\":{\"animation\":\"aim\"}}");
        harness.expect(
            number_member(aim_before_adoption.scene_delta(), "explicit_duration") ==
                std::optional<double>(0.75),
            "timeline.set_loop_sync adoption precondition",
            "the adoption case expects aim at the smoke's 0.75 duration");
        harness.invoke(
            "animation.set_duration aim back onto its last key",
            "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"aim\","
            "\"duration\":0.5}}");
        const DispatchObservation adopted = harness.invoke(
            "timeline.set_loop_sync adopts a runtime-only lane",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kAimLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            lane_string_is(
                first_lane_member(adopted, "boundary_action"), "adopted"),
            "timeline.set_loop_sync adoption action",
            "an existing key at the boundary must be adopted, not created");
        harness.expect(
            number_member(adopted.scene_delta(), "created_key_count") ==
                std::optional<double>(0.0),
            "timeline.set_loop_sync adoption creates nothing",
            "adoption must create no key");
        harness.invoke(
            "timeline.set_loop_sync materializes the aim lane",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kAimLane + "],\"enabled\":true}}");
        harness.invoke("undo the aim materialization", "{\"op\":\"undo\"}");
        harness.invoke("undo the aim duration", "{\"op\":\"undo\"}");

        // Rejections, each leaving the project untouched.
        struct LoopSyncRejection {
            const char* label;
            std::string request;
        };
        const std::vector<LoopSyncRejection> loop_rejections{
            {"missing enabled",
             std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                 kSpineLane + "]}}"},
            {"a non-boolean enabled",
             std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                 kSpineLane + "],\"enabled\":\"yes\"}}"},
            {"a deform lane with no attachment",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"deform\",\"animation\":\"idle\",\"slot\":\"body\"}],"
             "\"enabled\":true}}"},
            {"a draw_order lane",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"draw_order\",\"animation\":\"idle\"}],\"enabled\":true}}"},
            {"an event lane",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"event\",\"animation\":\"idle\"}],\"enabled\":true}}"},
            {"a slot_attachment lane",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"slot_attachment\",\"animation\":\"idle\","
             "\"slot\":\"body\"}],\"enabled\":true}}"},
            {"an unknown kind",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"physics\",\"animation\":\"idle\"}],\"enabled\":true}}"},
            {"an unknown transform channel",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
             "\"channel\":\"spinx\"}],\"enabled\":true}}"},
            {"a duplicate lane",
             std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                 kSpineLane + "," + kSpineLane + "],\"enabled\":true}}"},
            {"an empty lanes array",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[],"
             "\"enabled\":true}}"},
            {"a lane with no key at time zero",
             "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
             "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"arm_l\","
             "\"channel\":\"rotate\"}],\"enabled\":true}}"},
        };
        for (const LoopSyncRejection& rejection : loop_rejections) {
            harness.invoke(
                std::string("timeline.set_loop_sync rejects ") + rejection.label,
                rejection.request,
                false,
                "invalid_request");
        }
        harness.invoke(
            "timeline.set_loop_sync rejects an unresolvable lane",
            "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"no_such_bone\","
            "\"channel\":\"rotate\"}],\"enabled\":true}}",
            false,
            "not_found");

        const DispatchObservation after_loop_rejections = harness.invoke(
            "timeline.set_loop_sync unchanged after rejections",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            lane_number_is(
                first_lane_member(after_loop_rejections, "boundary_time"), 1.5) &&
                first_lane_member(after_loop_rejections, "previous_enabled") != nullptr &&
                first_lane_member(after_loop_rejections, "previous_enabled")
                    ->as_boolean(),
            "timeline.set_loop_sync rejection atomicity",
            "a rejected loop-sync request mutated the project");

        // Disabling succeeds even from a state the enable path would reject,
        // which is what makes the atomic rejection humane.
        harness.invoke(
            "timeline.set_loop_sync disables a lane in a rejected state",
            "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"arm_l\","
            "\"channel\":\"rotate\"}],\"enabled\":false}}",
            false,
            "no_change");
        harness.invoke(
            "timeline.set_loop_sync disable",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":false}}");
        const DispatchObservation released = harness.invoke(
            "timeline.set_loop_sync read-back after disable",
            std::string("{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":[") +
                kSpineLane + "],\"enabled\":true,\"dry_run\":true}}");
        harness.expect(
            first_lane_member(released, "previous_enabled") != nullptr &&
                !first_lane_member(released, "previous_enabled")->as_boolean() &&
                lane_number_is(first_lane_member(released, "boundary_time"), 1.5),
            "timeline.set_loop_sync disable",
            "disabling must clear the flag and leave the boundary key in place");

        harness.invoke("undo the loop-sync disable", "{\"op\":\"undo\"}");
        harness.invoke("undo the loop-sync enable", "{\"op\":\"undo\"}");
        harness.invoke("undo the loop-boundary duration", "{\"op\":\"undo\"}");
    }

    // --- MAR-173: timeline.scale_key_times, the 60th operation. ---
    {
        const char* kSpine0 =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.0}";
        const char* kSpineHalf =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.5}";
        const char* kSpineOne =
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":1.0}";
        const auto whole_lane = [&](std::string tail) {
            return std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                kSpine0 + "," + kSpineHalf + "," + kSpineOne + "]," + tail + "}}";
        };
        const auto key_entry = [&](const DispatchObservation& observation,
                                   std::size_t index) -> const json::Value* {
            const json::Value* entries = member(observation.scene_delta(), "keys");
            if (entries == nullptr || !entries->is_array() ||
                entries->as_array().size() <= index) {
                return nullptr;
            }
            return &entries->as_array()[index];
        };
        const auto key_number_is = [](const json::Value* value, double expected) {
            return value != nullptr && value->is_number() &&
                std::abs(value->as_number() - expected) <= 1e-6;
        };
        const auto key_bool_is = [](const json::Value* value, bool expected) {
            return value != nullptr && value->is_boolean() &&
                value->as_boolean() == expected;
        };

        // The dry run doubles as the precondition assertion: it reports each
        // key's `previous_time`, so the lane's shape is checked, not assumed.
        const DispatchObservation scale_dry_run = harness.invoke(
            "timeline.scale_key_times dry run",
            whole_lane("\"scale\":1.25,\"pivot\":\"start\",\"dry_run\":true"));
        harness.expect(
            bool_member(scale_dry_run.scene_delta(), "dry_run") ==
                    std::optional<bool>(true) &&
                number_member(scale_dry_run.scene_delta(), "requested_scale") ==
                    std::optional<double>(1.25) &&
                number_member(scale_dry_run.scene_delta(), "applied_scale") ==
                    std::optional<double>(1.25) &&
                number_member(scale_dry_run.scene_delta(), "pivot_time") ==
                    std::optional<double>(0.0) &&
                number_member(scale_dry_run.scene_delta(), "original_span") ==
                    std::optional<double>(1.0) &&
                number_member(scale_dry_run.scene_delta(), "scaled_span") ==
                    std::optional<double>(1.25) &&
                number_member(scale_dry_run.scene_delta(), "key_count") ==
                    std::optional<double>(3.0) &&
                number_member(scale_dry_run.scene_delta(), "moved_key_count") ==
                    std::optional<double>(2.0) &&
                bool_member(scale_dry_run.scene_delta(), "keys_truncated") ==
                    std::optional<bool>(false) &&
                bool_member(scale_dry_run.scene_delta(), "snap") ==
                    std::optional<bool>(false),
            "timeline.scale_key_times dry run",
            "the dry run did not report the ratio, the pivot, and both spans");
        harness.expect(
            key_number_is(member(key_entry(scale_dry_run, 0U), "previous_time"), 0.0) &&
                key_number_is(member(key_entry(scale_dry_run, 0U), "time"), 0.0) &&
                key_bool_is(member(key_entry(scale_dry_run, 0U), "moved"), false) &&
                key_number_is(
                    member(key_entry(scale_dry_run, 1U), "previous_time"), 0.5) &&
                key_number_is(member(key_entry(scale_dry_run, 1U), "time"), 0.625) &&
                key_bool_is(member(key_entry(scale_dry_run, 1U), "moved"), true) &&
                key_number_is(
                    member(key_entry(scale_dry_run, 2U), "previous_time"), 1.0) &&
                key_number_is(member(key_entry(scale_dry_run, 2U), "time"), 1.25),
            "timeline.scale_key_times dry-run key report",
            "the per-key report did not carry previous_time, time, and moved");

        // The same selection with the other pivot is a different edit.
        const DispatchObservation end_pivot = harness.invoke(
            "timeline.scale_key_times dry run with the end pivot",
            whole_lane("\"scale\":0.5,\"pivot\":\"end\",\"dry_run\":true"));
        harness.expect(
            number_member(end_pivot.scene_delta(), "pivot_time") ==
                    std::optional<double>(1.0) &&
                key_number_is(member(key_entry(end_pivot, 0U), "time"), 0.5) &&
                key_number_is(member(key_entry(end_pivot, 2U), "time"), 1.0),
            "timeline.scale_key_times both pivots",
            "the two pivots must produce different results for the same selection");

        // `export.preview` reports the resolved export TARGET PATHS, not the
        // exported content, so a time-only edit correctly leaves its payload
        // identical. The design's §12.6 expected the payload to differ; that is
        // wrong about this operation, and asserting the difference would have
        // passed only on the response envelope's revision metadata. The
        // content-level proof lives in marrow_project_smoke's MAR-173 export
        // block, which exports the mutated project and asserts the loaded key
        // time. What is asserted here is what this operation actually promises:
        // a scaled project stays exportable to the same targets.
        const auto preview_targets = [&](const DispatchObservation& observation) {
            std::string joined;
            const json::Value* targets = member(observation.scene_delta(), "targets");
            if (targets == nullptr || !targets->is_array()) return joined;
            for (const json::Value& entry : targets->as_array()) {
                if (entry.is_string()) joined += entry.as_string() + "|";
            }
            return joined;
        };
        const DispatchObservation preview_before = harness.invoke(
            "export.preview before the scale", "{\"op\":\"export.preview\"}");

        const DispatchObservation scale_live = harness.invoke(
            "timeline.scale_key_times live",
            whole_lane("\"scale\":1.25,\"pivot\":\"start\""));
        harness.expect(
            bool_member(scale_live.scene_delta(), "dry_run") ==
                    std::optional<bool>(false) &&
                number_member(scale_live.scene_delta(), "moved_key_count") ==
                    std::optional<double>(2.0) &&
                number_member(scale_live.scene_delta(), "applied_scale") ==
                    std::optional<double>(1.25),
            "timeline.scale_key_times live",
            "a live scale did not report its applied ratio and moved count");

        const DispatchObservation preview_after = harness.invoke(
            "export.preview after the scale", "{\"op\":\"export.preview\"}");
        harness.expect(
            preview_before.parsed && preview_after.parsed &&
                !preview_targets(preview_before).empty() &&
                preview_targets(preview_before) == preview_targets(preview_after),
            "timeline.scale_key_times export preview",
            "a scaled project must still preview the same export targets");

        // A second identical live call moves nothing.
        harness.invoke(
            "timeline.scale_key_times no_change",
            std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":0.0},"
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":0.625},"
                "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                "\"channel\":\"rotate\",\"time\":1.25}],\"scale\":1.0,"
                "\"pivot\":\"start\"}}",
            false,
            "no_change");

        harness.invoke("undo the live scale", "{\"op\":\"undo\"}");
        const DispatchObservation preview_undone = harness.invoke(
            "export.preview after the undo", "{\"op\":\"export.preview\"}");
        harness.expect(
            preview_targets(preview_undone) == preview_targets(preview_before),
            "timeline.scale_key_times export preview undo",
            "undoing a scale must leave the export targets where they were");
        const DispatchObservation restored = harness.invoke(
            "timeline.scale_key_times read-back after undo",
            whole_lane("\"scale\":1.25,\"pivot\":\"start\",\"dry_run\":true"));
        harness.expect(
            key_number_is(member(key_entry(restored, 1U), "previous_time"), 0.5) &&
                key_number_is(member(key_entry(restored, 2U), "previous_time"), 1.0),
            "timeline.scale_key_times undo",
            "one undo did not restore every key time");

        // `previous_time` must be a PROJECT read, not an echo of the request.
        // A selector's `time` only has to identify a key within the resolver's
        // one-microsecond window, so this asks for 0.5000009 and 0.9999993 and
        // requires the report to come back as the stored 0.5 and 1.0. An echo
        // would return the offsets verbatim and pass every equality above,
        // which is what made the read-back assertion tautological before.
        const DispatchObservation offset_selectors = harness.invoke(
            "timeline.scale_key_times reports resolved times, not the request",
            "{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.0},"
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.5000009},"
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\",\"time\":0.9999993}],\"scale\":1.25,"
            "\"pivot\":\"start\",\"dry_run\":true}}");
        const json::Value* offset_previous =
            member(key_entry(offset_selectors, 1U), "previous_time");
        const json::Value* offset_last =
            member(key_entry(offset_selectors, 2U), "previous_time");
        harness.expect(
            offset_previous != nullptr && offset_previous->is_number() &&
                offset_previous->as_number() == 0.5 && offset_last != nullptr &&
                offset_last->is_number() && offset_last->as_number() == 1.0 &&
                key_number_is(member(key_entry(offset_selectors, 1U), "time"), 0.625) &&
                number_member(offset_selectors.scene_delta(), "original_span") ==
                    std::optional<double>(1.0),
            "timeline.scale_key_times resolved-time reporting",
            "previous_time echoed the requested time instead of reading the project");

        // Snapping reshapes the ratio, and only on request.
        const DispatchObservation snapped = harness.invoke(
            "timeline.scale_key_times snapped dry run",
            whole_lane("\"scale\":1.234,\"pivot\":\"start\",\"snap\":true,"
                       "\"frames_per_second\":60,\"dry_run\":true"));
        const std::optional<double> applied =
            number_member(snapped.scene_delta(), "applied_scale");
        harness.expect(
            applied.has_value() && std::abs(*applied - 1.234) > 1e-9 &&
                std::abs(*applied * 60.0 - std::round(*applied * 60.0)) < 1e-6 &&
                bool_member(snapped.scene_delta(), "snap") == std::optional<bool>(true),
            "timeline.scale_key_times snapping",
            "snap:true must reshape the ratio so the moved edge lands on a frame");

        // A loop-pinned key rejects, naming the remedy.
        harness.invoke(
            "animation.set_duration for the scale pin case",
            "{\"op\":\"animation.set_duration\",\"args\":{\"animation\":\"idle\","
            "\"duration\":1.5}}");
        harness.invoke(
            "timeline.set_loop_sync for the scale pin case",
            "{\"op\":\"timeline.set_loop_sync\",\"args\":{\"lanes\":["
            "{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
            "\"channel\":\"rotate\"}],\"enabled\":true}}");
        harness.invoke(
            "timeline.scale_key_times rejects a loop-pinned key",
            whole_lane("\"scale\":1.2,\"pivot\":\"end\""),
            false,
            "invalid_request");
        // Both halves of the rejection: the call failed AND the authored key
        // beside the pinned one survived.
        harness.invoke(
            "the spine key at 0.5 survived the rejected scale",
            std::string("{\"op\":\"timeline.set_interpolation\",\"args\":{\"keys\":[") +
                kSpineHalf + "],\"interpolation\":\"linear\",\"dry_run\":true}}");
        harness.invoke("undo the scale pin enable", "{\"op\":\"undo\"}");
        harness.invoke("undo the scale pin duration", "{\"op\":\"undo\"}");

        // Rejections, each leaving the project untouched.
        struct ScaleRejection {
            const char* label;
            std::string request;
            const char* code;
        };
        const std::vector<ScaleRejection> scale_rejections{
            {"a missing scale", whole_lane("\"pivot\":\"start\""), "invalid_request"},
            {"a non-numeric scale",
             whole_lane("\"scale\":\"1.5\",\"pivot\":\"start\""), "invalid_request"},
            {"a zero scale", whole_lane("\"scale\":0,\"pivot\":\"start\""),
             "invalid_request"},
            {"a negative scale", whole_lane("\"scale\":-1,\"pivot\":\"start\""),
             "invalid_request"},
            {"a missing pivot", whole_lane("\"scale\":1.25"), "invalid_request"},
            {"an unknown pivot", whole_lane("\"scale\":1.25,\"pivot\":\"middle\""),
             "invalid_request"},
            {"an empty keys array",
             "{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[],"
             "\"scale\":1.25,\"pivot\":\"start\"}}",
             "invalid_request"},
            {"a duplicate key",
             std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                 kSpine0 + "," + kSpineHalf + "," + kSpineHalf +
                 "],\"scale\":1.25,\"pivot\":\"start\"}}",
             "invalid_request"},
            {"a single-key selection",
             std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                 kSpineHalf + "],\"scale\":1.25,\"pivot\":\"start\"}}",
             "invalid_request"},
            {"keys from two animations",
             std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                 kSpine0 +
                 ",{\"kind\":\"transform\",\"animation\":\"aim\",\"bone\":\"arm_l\","
                 "\"channel\":\"rotate\",\"time\":0.0}],\"scale\":1.25,"
                 "\"pivot\":\"start\"}}",
             "invalid_request"},
            {"a non-event collision",
             whole_lane("\"scale\":0.001,\"pivot\":\"start\""), "invalid_request"},
            {"an intrusion into an unselected neighbour",
             std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                 kSpine0 + "," + kSpineHalf +
                 "],\"scale\":1.999,\"pivot\":\"start\"}}",
             "invalid_request"},
            {"a partial event tie",
             "{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":["
             "{\"kind\":\"event\",\"animation\":\"idle\",\"time\":0.25,\"ordinal\":0},"
             "{\"kind\":\"event\",\"animation\":\"idle\",\"time\":0.8}],"
             "\"scale\":1.5,\"pivot\":\"start\"}}",
             "invalid_request"},
            {"snap with a non-positive frames_per_second",
             whole_lane("\"scale\":1.25,\"pivot\":\"start\",\"snap\":true,"
                        "\"frames_per_second\":0"),
             "invalid_request"},
            {"an unresolvable key",
             std::string("{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":[") +
                 kSpine0 +
                 ",{\"kind\":\"transform\",\"animation\":\"idle\",\"bone\":\"spine\","
                 "\"channel\":\"rotate\",\"time\":0.42}],\"scale\":1.25,"
                 "\"pivot\":\"start\"}}",
             "not_found"},
        };
        for (const ScaleRejection& rejection : scale_rejections) {
            harness.invoke(
                std::string("timeline.scale_key_times rejects ") + rejection.label,
                rejection.request,
                false,
                rejection.code);
        }
        const DispatchObservation after_scale_rejections = harness.invoke(
            "timeline.scale_key_times unchanged after rejections",
            whole_lane("\"scale\":1.25,\"pivot\":\"start\",\"dry_run\":true"));
        harness.expect(
            key_number_is(
                member(key_entry(after_scale_rejections, 1U), "previous_time"), 0.5) &&
                key_number_is(
                    member(key_entry(after_scale_rejections, 2U), "previous_time"), 1.0),
            "timeline.scale_key_times rejection atomicity",
            "a rejected scale request mutated the project");

        // Event ties move together, and the whole tie is a legal selection.
        harness.invoke(
            "timeline.scale_key_times carries a complete event tie",
            "{\"op\":\"timeline.scale_key_times\",\"args\":{\"keys\":["
            "{\"kind\":\"event\",\"animation\":\"idle\",\"time\":0.25,\"ordinal\":0},"
            "{\"kind\":\"event\",\"animation\":\"idle\",\"time\":0.25,\"ordinal\":1},"
            "{\"kind\":\"event\",\"animation\":\"idle\",\"time\":0.8}],"
            "\"scale\":1.5,\"pivot\":\"start\",\"dry_run\":true}}");
    }


    // Two merge-enabled transform edits must form one undo group. Temporary
    // JSON/binary comparison gives an implementation-independent key count.
    harness.invoke(
        "set_transform merged first",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.625,"
        "\"angle\":12,\"merge\":true}}");
    harness.invoke(
        "set_transform merged second",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.75,"
        "\"angle\":30,\"merge\":true}}");
    const auto merged_rotate_keyframes = expect_export_equivalence(
        harness,
        "compare_runtime_export after merged edits",
        harness.invoke(
            "compare_runtime_export after merged edits",
            "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}"),
        false);
    if (baseline_rotate_keyframes.has_value() && merged_rotate_keyframes.has_value()) {
        harness.expect(
            *merged_rotate_keyframes == *baseline_rotate_keyframes + 2U,
            "merged edit key count",
            "two transform keys were not added");
    }
    harness.invoke("undo merged transform edits", "{\"op\":\"undo\"}");
    const auto undone_rotate_keyframes = expect_export_equivalence(
        harness,
        "compare_runtime_export after undo",
        harness.invoke(
            "compare_runtime_export after undo",
            "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}"));
    if (baseline_rotate_keyframes.has_value() && undone_rotate_keyframes.has_value()) {
        harness.expect(
            *undone_rotate_keyframes == *baseline_rotate_keyframes,
            "undo grouping",
            "one undo did not revert both merged transform edits");
    }
    harness.invoke("redo merged transform edits", "{\"op\":\"redo\"}");
    const auto redone_rotate_keyframes = expect_export_equivalence(
        harness,
        "compare_runtime_export after redo",
        harness.invoke(
            "compare_runtime_export after redo",
            "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}"),
        false);
    if (merged_rotate_keyframes.has_value() && redone_rotate_keyframes.has_value()) {
        harness.expect(
            *redone_rotate_keyframes == *merged_rotate_keyframes,
            "redo grouping",
            "redo did not restore both merged transform edits");
    }
    // Operations registered with dry_run_supported=false must reject an
    // explicit dry_run request instead of silently executing the real
    // mutation. The successful removals right below double as proof that the
    // rejected attempts deleted nothing.
    harness.invoke(
        "remove_transform_keyframe rejects dry_run",
        "{\"op\":\"remove_transform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.625,\"dry_run\":true}}",
        false,
        "dry_run_unsupported");
    harness.invoke(
        "undo rejects dry_run",
        "{\"op\":\"undo\",\"args\":{\"dry_run\":true}}",
        false,
        "dry_run_unsupported");
    harness.invoke(
        "remove_transform_keyframe first",
        "{\"op\":\"remove_transform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.625}}");
    harness.invoke(
        "remove_transform_keyframe second",
        "{\"op\":\"remove_transform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.75}}");

    // Remaining edit operations use deterministic fixture targets. Set/remove
    // pairs leave their timeline slice clean while constraints and mesh weights
    // prove persistent session mutations still export equivalently.
    const DispatchObservation invalid_ik_target = harness.invoke(
        "edit_ik_constraint invalid target rollback",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"target\":\"missing_agent_target\"}}",
        false,
        "invalid_request");
    harness.expect(
        string_member(&invalid_ik_target.root, "message")
                .value_or(std::string_view{})
                .find("Failed to apply IK constraint edit: ") == 0U,
        "edit_ik_constraint invalid target rollback",
        "commit-time failure prefix changed");
    const DispatchObservation ik_after_failed_commit = harness.invoke(
        "edit_ik_constraint after failed commit",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"dry_run\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_ik_constraint failed commit rollback",
        ik_after_failed_commit,
        R"json({"dry_run":true,"name":"editor_arm_reach","mix":0.75})json");

    harness.invoke(
        "edit_ik_constraint",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"mix\":0.5}}");

    const DispatchObservation path_constraint_live = harness.invoke(
        "edit_path_constraint",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.25,\"rotate_mix\":0.75,\"merge\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_path_constraint live delta",
        path_constraint_live,
        R"json({
          "dry_run":false,"name":"editor_guide_follow","slot":"guide",
          "bones":["path_a","path_b","path_c"],"position":0.25,
          "spacing":0.3,"spacing_mode":"percent","rotate_mix":0.75,
          "translate_mix":1
        })json");
    const DispatchObservation path_constraint_no_change = harness.invoke(
        "edit_path_constraint no change",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.25,\"rotate_mix\":0.75,\"merge\":true}}",
        false,
        "no_change");
    expect_no_change(
        harness, "edit_path_constraint no change", path_constraint_no_change);

    const DispatchObservation transform_constraint_live = harness.invoke(
        "edit_transform_constraint",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"translate_mix\":0.5,"
        "\"offset\":{\"x\":-8},\"merge\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_transform_constraint live delta",
        transform_constraint_live,
        R"json({
          "dry_run":false,"name":"editor_transform_follow",
          "source":"transform_source","bones":["transform_target"],
          "rotate_mix":0.5,"translate_mix":0.5,"scale_mix":1,
          "shear_mix":0.75,"offset":{"rotation":15,"x":-8,"y":20,
          "scale_x":0.2,"scale_y":-0.1,"shear_x":5,"shear_y":-2}
        })json");
    const DispatchObservation transform_constraint_no_change = harness.invoke(
        "edit_transform_constraint no change",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"translate_mix\":0.5,"
        "\"offset\":{\"x\":-8},\"merge\":true}}",
        false,
        "no_change");
    expect_no_change(
        harness,
        "edit_transform_constraint no change",
        transform_constraint_no_change);

    const DispatchObservation physics_constraint_live = harness.invoke(
        "edit_physics_constraint",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"mix\":0.8,"
        "\"wind\":{\"x\":10},\"merge\":true}}");
    expect_exact_scene_delta(
        harness,
        "edit_physics_constraint live delta",
        physics_constraint_live,
        R"json({
          "dry_run":false,"name":"editor_ribbon_secondary",
          "bones":["ribbon_01","ribbon_02"],"step":0.0166666667,
          "x":1,"y":1,"rotate":1,"scale_x":0.35,"shear_x":0,
          "limit":30,"inertia":0.85,"damping":4,"strength":18,
          "mass_inverse":1,"gravity":{"x":0,"y":-24},
          "wind":{"x":10,"y":0},"mix":0.8
        })json");
    const DispatchObservation physics_constraint_no_change = harness.invoke(
        "edit_physics_constraint no change",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"mix\":0.8,"
        "\"wind\":{\"x\":10},\"merge\":true}}",
        false,
        "no_change");
    expect_no_change(
        harness, "edit_physics_constraint no change", physics_constraint_no_change);

    harness.invoke("undo merged constraint edits", "{\"op\":\"undo\"}");
    const DispatchObservation path_after_constraint_undo = harness.invoke(
        "path constraint after merged undo",
        "{\"op\":\"edit_path_constraint\",\"args\":{"
        "\"name\":\"editor_guide_follow\",\"dry_run\":true}}");
    const DispatchObservation transform_after_constraint_undo = harness.invoke(
        "transform constraint after merged undo",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"dry_run\":true}}");
    const DispatchObservation physics_after_constraint_undo = harness.invoke(
        "physics constraint after merged undo",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"dry_run\":true}}");
    expect_exact_scene_delta(
        harness,
        "path constraint merged undo",
        path_after_constraint_undo,
        R"json({
          "dry_run":true,"name":"editor_guide_follow","slot":"guide",
          "bones":["path_a","path_b","path_c"],"position":0.1,
          "spacing":0.3,"spacing_mode":"percent","rotate_mix":1,
          "translate_mix":1
        })json");
    expect_exact_scene_delta(
        harness,
        "transform constraint merged undo",
        transform_after_constraint_undo,
        R"json({
          "dry_run":true,"name":"editor_transform_follow",
          "source":"transform_source","bones":["transform_target"],
          "rotate_mix":0.5,"translate_mix":0.25,"scale_mix":1,
          "shear_mix":0.75,"offset":{"rotation":15,"x":-10,"y":20,
          "scale_x":0.2,"scale_y":-0.1,"shear_x":5,"shear_y":-2}
        })json");
    expect_exact_scene_delta(
        harness,
        "physics constraint merged undo",
        physics_after_constraint_undo,
        R"json({
          "dry_run":true,"name":"editor_ribbon_secondary",
          "bones":["ribbon_01","ribbon_02"],"step":0.0166666667,
          "x":1,"y":1,"rotate":1,"scale_x":0.35,"shear_x":0,
          "limit":30,"inertia":0.85,"damping":4,"strength":18,
          "mass_inverse":1,"gravity":{"x":0,"y":-24},
          "wind":{"x":12,"y":0},"mix":1
        })json");
    harness.invoke("redo merged constraint edits", "{\"op\":\"redo\"}");
    const DispatchObservation path_after_constraint_redo = harness.invoke(
        "path constraint after merged redo",
        "{\"op\":\"edit_path_constraint\",\"args\":{"
        "\"name\":\"editor_guide_follow\",\"dry_run\":true}}");
    harness.expect(
        number_member(path_after_constraint_redo.scene_delta(), "position") ==
                std::optional<double>(0.25) &&
            number_member(path_after_constraint_redo.scene_delta(), "rotate_mix") ==
                std::optional<double>(0.75),
        "redo merged constraint edits",
        "one redo did not restore the shared Agent constraint group");
    harness.invoke(
        "set_event_keyframe",
        "{\"op\":\"set_event_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.42,\"event\":\"footstep\",\"int\":7,\"float\":0.5,"
        "\"string\":\"agent\",\"audio_path\":\"sfx/agent.wav\",\"volume\":0.6,"
        "\"balance\":-0.1}}");
    harness.invoke(
        "remove_event_keyframe",
        "{\"op\":\"remove_event_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.42,\"event\":\"footstep\"}}");
    harness.invoke(
        "set_deform_keyframe",
        "{\"op\":\"set_deform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":0.625,"
        "\"offsets\":[0,0,1,0,0,1,0,0]}}");
    harness.invoke(
        "remove_deform_keyframe",
        "{\"op\":\"remove_deform_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"time\":0.625}}");
    harness.invoke(
        "set_vertex_weights",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":1,\"influences\":[{\"bone\":\"spine\",\"x\":60,\"y\":0,"
        "\"weight\":0.5},{\"bone\":\"arm_l\",\"x\":20,\"y\":0,\"weight\":0.5}]}]}}");
    harness.invoke(
        "normalize_weights",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
    const DispatchObservation normalize_weights_idempotent = harness.invoke(
        "normalize_weights idempotent",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
    harness.expect(
        string_member(&normalize_weights_idempotent.root, "message") ==
                std::optional<std::string_view>("Mesh weights already normalized.") &&
            normalize_weights_idempotent.scene_delta() != nullptr &&
            normalize_weights_idempotent.scene_delta()->is_null(),
        "normalize_weights idempotent",
        "idempotent normalize success contract changed");

    // ── MAR-175: the canonical rules, asserted on the AGENT surface ────────
    //
    // Every rule below is also covered by the shell smoke and the unit tests.
    // It is asserted here too because a rule enforced only in the GUI is a rule
    // a script can walk straight past.
    const auto weight_rows = [&](std::string_view label) -> const json::Value* {
        static DispatchObservation described;
        described = harness.invoke(
            label,
            "{\"op\":\"mesh.describe\",\"args\":{\"skin\":\"mesh_base\","
            "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
        return member(described.scene_delta(), "weights");
    };
    const auto influence_count =
        [&](const json::Value* rows, std::size_t vertex) -> std::optional<std::size_t> {
        if (rows == nullptr || !rows->is_array() || vertex >= rows->as_array().size() ||
            !rows->as_array()[vertex].is_array()) {
            return std::nullopt;
        }
        return rows->as_array()[vertex].as_array().size();
    };
    const auto influence_of =
        [&](const json::Value* rows, std::size_t vertex, std::size_t slot) -> const json::Value* {
        if (rows == nullptr || !rows->is_array() || vertex >= rows->as_array().size() ||
            !rows->as_array()[vertex].is_array() ||
            slot >= rows->as_array()[vertex].as_array().size()) {
            return nullptr;
        }
        return &rows->as_array()[vertex].as_array()[slot];
    };
    const auto serialize_rows = [&](const json::Value* rows, std::size_t vertex) -> std::string {
        if (rows == nullptr || !rows->is_array() || vertex >= rows->as_array().size()) {
            return "<missing>";
        }
        return json::serialize_pretty_round_trip(rows->as_array()[vertex]);
    };

    // A duplicate bone used to commit `ok: true` and then make the project
    // unsavable -- validate_project_for_save refuses a repeated bone. It now
    // merges, and the merged weight is the sum.
    harness.invoke(
        "set_vertex_weights duplicate bone merges",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":2,\"influences\":[{\"bone\":\"spine\",\"x\":10,\"y\":0,"
        "\"weight\":0.5},{\"bone\":\"spine\",\"x\":30,\"y\":0,\"weight\":0.5}]}]}}");
    {
        const json::Value* rows = weight_rows("mesh.describe after duplicate merge");
        harness.expect(
            influence_count(rows, 2U) == std::optional<std::size_t>(1U),
            "set_vertex_weights duplicate bone merges",
            "a repeated bone must merge into one influence");
        harness.expect(
            number_member(influence_of(rows, 2U, 0U), "weight") == std::optional<double>(1.0) &&
                number_member(influence_of(rows, 2U, 0U), "x") == std::optional<double>(20.0),
            "set_vertex_weights duplicate bone merges",
            "the merged influence must carry the summed weight and the weight-weighted mean bind");
    }

    // A zero weight used to survive to save and be refused there.
    const std::string vertex3_before_zero_drop =
        serialize_rows(weight_rows("mesh.describe before zero drop"), 3U);
    harness.invoke(
        "set_vertex_weights drops a zero influence",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":1,\"influences\":[{\"bone\":\"spine\",\"x\":60,\"y\":0,"
        "\"weight\":0.8},{\"bone\":\"arm_l\",\"x\":20,\"y\":0,\"weight\":0}]}]}}");
    {
        const json::Value* rows = weight_rows("mesh.describe after zero drop");
        harness.expect(
            influence_count(rows, 1U) == std::optional<std::size_t>(1U) &&
                number_member(influence_of(rows, 1U, 0U), "weight") ==
                    std::optional<double>(1.0),
            "set_vertex_weights drops a zero influence",
            "a zero-weight influence must be dropped and the survivor normalized");
        // Survival: the vertex this call did not name is byte-identical.
        harness.expect(
            serialize_rows(rows, 3U) == vertex3_before_zero_drop,
            "set_vertex_weights drops a zero influence",
            "an unnamed vertex must be byte-identical after a scoped weight write");
    }

    // D6 -- non-finite weights. `NaN <= 1e-6` is false, so a NaN survived both
    // guards of the two normalizers this story deletes and then poisoned every
    // influence on the vertex. On the AGENT surface that defect was never
    // actually reachable: JSON has no NaN or Infinity literal, and the parser
    // refuses a numeric literal that overflows to infinity ("invalid numeric
    // value"), so the payload dies before the operation runs. Asserted here so
    // nobody later "fixes" the guard away on the grounds that no agent test
    // exercises it; the reachable paths -- the brush and any in-process caller
    // of the primitive -- are covered by marrow_mesh_weight_model_tests and
    // marrow_project_smoke.
    harness.expect(
        harness.dispatch_rejects(
            "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
            "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
            "{\"index\":1,\"influences\":[{\"bone\":\"spine\",\"x\":60,\"y\":0,"
            "\"weight\":1e400}]}]}}"),
        "set_vertex_weights rejects a non-finite weight",
        "an overflowing weight literal must never reach the project");
    {
        const json::Value* rows = weight_rows("mesh.describe after non-finite rejection");
        harness.expect(
            influence_count(rows, 1U) == std::optional<std::size_t>(1U) &&
                number_member(influence_of(rows, 1U, 0U), "weight") ==
                    std::optional<double>(1.0),
            "set_vertex_weights rejects a non-finite weight",
            "a rejected weight write must leave the vertex untouched");
    }

    // MAR-175 C1: canonicalization is unconditional, so an explicit
    // "normalize": false has no implementable meaning and rejects loudly.
    harness.invoke(
        "set_vertex_weights rejects normalize:false",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"normalize\":false,"
        "\"vertices\":[{\"index\":1,\"influences\":[{\"bone\":\"spine\","
        "\"x\":60,\"y\":0,\"weight\":0.5}]}]}}",
        false,
        "invalid_request");
    harness.invoke(
        "set_vertex_weights still accepts normalize:true",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"normalize\":true,"
        "\"vertices\":[{\"index\":1,\"influences\":[{\"bone\":\"spine\","
        "\"x\":60,\"y\":0,\"weight\":0.4},{\"bone\":\"arm_l\",\"x\":20,"
        "\"y\":0,\"weight\":0.4}]}]}}");
    harness.invoke(
        "set_vertex_weights rejects five influences",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":1,\"influences\":[{\"bone\":\"root\",\"x\":0,\"y\":0,\"weight\":0.2},"
        "{\"bone\":\"spine\",\"x\":0,\"y\":0,\"weight\":0.2},"
        "{\"bone\":\"arm_l\",\"x\":0,\"y\":0,\"weight\":0.2},"
        "{\"bone\":\"ik_upper\",\"x\":0,\"y\":0,\"weight\":0.2},"
        "{\"bone\":\"ik_lower\",\"x\":0,\"y\":0,\"weight\":0.2}]}]}}",
        false);

    // MAR-175: normalize_weights gains an optional vertex scope. Absent means
    // every vertex, which is the shipped behaviour.
    const std::string vertex0_before_scope =
        serialize_rows(weight_rows("mesh.describe before scoped normalize"), 0U);
    const std::string vertex3_before_scope =
        serialize_rows(weight_rows("mesh.describe before scoped normalize 3"), 3U);
    const DispatchObservation scoped_dry_run = harness.invoke(
        "normalize_weights scoped dry-run",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[1],"
        "\"dry_run\":true}}");
    harness.expect(
        number_member(scoped_dry_run.scene_delta(), "vertex_count") ==
                std::optional<double>(4.0) &&
            number_member(scoped_dry_run.scene_delta(), "scoped_vertex_count") ==
                std::optional<double>(1.0),
        "normalize_weights scoped dry-run",
        "a scoped normalize must report the attachment size and the vertices it addressed");
    const DispatchObservation scoped_normalize = harness.invoke(
        "normalize_weights scoped to one vertex",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[1]}}");
    harness.expect(
        string_member(&scoped_normalize.root, "message") ==
            std::optional<std::string_view>("Mesh weights already normalized."),
        "normalize_weights scoped to one vertex",
        "normalizing an already-canonical vertex must keep the shipped no_change message");
    {
        const json::Value* rows = weight_rows("mesh.describe after scoped normalize");
        harness.expect(
            serialize_rows(rows, 0U) == vertex0_before_scope &&
                serialize_rows(rows, 3U) == vertex3_before_scope,
            "normalize_weights scoped to one vertex",
            "vertices outside the scope must be byte-identical");
    }
    harness.invoke(
        "normalize_weights rejects an empty scope",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[]}}",
        false);
    harness.invoke(
        "normalize_weights rejects an out-of-range vertex",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[99]}}",
        false,
        "invalid_request");
    harness.invoke(
        "normalize_weights rejects a repeated vertex",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[1,1]}}",
        false,
        "invalid_request");
    harness.invoke(
        "normalize_weights rejects a fractional vertex index",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[1.5]}}",
        false);

    // ── MAR-175: mesh.rebind_weights ──────────────────────────────────────
    const DispatchObservation rebind_dry_run = harness.invoke(
        "mesh.rebind_weights dry-run",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"dry_run\":true}}");
    harness.expect(
        bool_member(rebind_dry_run.scene_delta(), "dry_run") == std::optional<bool>(true) &&
            number_member(rebind_dry_run.scene_delta(), "vertex_count") ==
                std::optional<double>(4.0) &&
            number_member(rebind_dry_run.scene_delta(), "scoped_vertex_count") ==
                std::optional<double>(4.0),
        "mesh.rebind_weights dry-run",
        "the dry run must report the attachment and scope sizes");
    const std::string vertex0_before_rebind =
        serialize_rows(weight_rows("mesh.describe before rebind"), 0U);
    harness.invoke(
        "mesh.rebind_weights live",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
    {
        const json::Value* rows = weight_rows("mesh.describe after rebind");
        // Rebind changes bind offsets only: every weight must be untouched.
        bool weights_held = true;
        for (std::size_t vertex = 0; vertex < 4U; ++vertex) {
            const auto count = influence_count(rows, vertex);
            if (!count.has_value()) {
                weights_held = false;
                break;
            }
            for (std::size_t slot = 0; slot < *count; ++slot) {
                if (!number_member(influence_of(rows, vertex, slot), "weight").has_value()) {
                    weights_held = false;
                }
            }
        }
        harness.expect(
            weights_held, "mesh.rebind_weights live", "rebind must keep every influence readable");
    }
    const DispatchObservation rebind_again = harness.invoke(
        "mesh.rebind_weights idempotent",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}");
    harness.expect(
        string_member(&rebind_again.root, "message") ==
            std::optional<std::string_view>("Mesh weights already bound to the setup pose."),
        "mesh.rebind_weights idempotent",
        "a rebind that moves nothing must report no_change with its documented message");
    harness.invoke("undo mesh.rebind_weights", "{\"op\":\"undo\"}");
    harness.expect(
        serialize_rows(weight_rows("mesh.describe after rebind undo"), 0U) ==
            vertex0_before_rebind,
        "undo mesh.rebind_weights",
        "undo must restore the pre-rebind bind offsets bit-exactly");
    harness.invoke(
        "mesh.rebind_weights rejects a missing attachment",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"no_such_mesh\"}}",
        false,
        "not_found");
    harness.invoke(
        "mesh.rebind_weights requires a target",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\"}}",
        false);
    harness.invoke(
        "mesh.rebind_weights rejects an out-of-range vertex",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[99]}}",
        false,
        "invalid_request");
    harness.invoke(
        "mesh.rebind_weights rejects an empty scope",
        "{\"op\":\"mesh.rebind_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":[]}}",
        false);

    // ── MAR-176: mesh.generate_weights ────────────────────────────────────
    //
    // Earlier cases in this smoke have rewritten these vertices, so restore the
    // fixture's authored influences first. The exact 0.5/0.5 tie below is a
    // property of WHERE the vertex sits -- past spine's segment end and behind
    // arm_l's segment start, so both clamp to spine's world origin -- and
    // asserting it against whatever the previous case happened to leave behind
    // would be asserting nothing.
    harness.invoke(
        "restore fixture weights before generate",
        "{\"op\":\"set_vertex_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"vertices\":["
        "{\"index\":0,\"influences\":[{\"bone\":\"spine\",\"x\":-64,\"y\":-80,\"weight\":1}]},"
        "{\"index\":2,\"influences\":[{\"bone\":\"spine\",\"x\":64,\"y\":80,\"weight\":0.2},"
        "{\"bone\":\"arm_l\",\"x\":94,\"y\":70,\"weight\":0.6}]}]}}");
    const std::string vertex0_before_generate =
        serialize_rows(weight_rows("mesh.describe before generate"), 0U);
    const std::string vertex1_before_generate =
        serialize_rows(weight_rows("mesh.describe before generate"), 1U);
    const std::string vertex2_before_generate =
        serialize_rows(weight_rows("mesh.describe before generate"), 2U);
    const std::string vertex3_before_generate =
        serialize_rows(weight_rows("mesh.describe before generate"), 3U);
    const DispatchObservation generate_dry_run = harness.invoke(
        "mesh.generate_weights dry-run",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\",\"arm_l\"],"
        "\"dry_run\":true}}");
    harness.expect(
        string_member(&generate_dry_run.root, "message") ==
            std::optional<std::string_view>("Mesh weight generation validated."),
        "mesh.generate_weights dry-run",
        "a dry run must report the documented validation message");
    harness.expect(
        bool_member(generate_dry_run.scene_delta(), "dry_run") == std::optional<bool>(true) &&
            number_member(generate_dry_run.scene_delta(), "vertex_count") ==
                std::optional<double>(4.0) &&
            number_member(generate_dry_run.scene_delta(), "scoped_vertex_count") ==
                std::optional<double>(4.0) &&
            number_member(generate_dry_run.scene_delta(), "candidate_bone_count") ==
                std::optional<double>(2.0),
        "mesh.generate_weights dry-run",
        "the dry run must report the attachment size, the scope size, and the candidate count");
    // A dry run must leave the project exactly where it was.
    harness.expect(
        serialize_rows(weight_rows("mesh.describe after generate dry run"), 2U) ==
            vertex2_before_generate,
        "mesh.generate_weights dry-run",
        "a dry run must not touch the project");
    const DispatchObservation generate_dry_run_again = harness.invoke(
        "mesh.generate_weights dry-run repeated",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\",\"arm_l\"],"
        "\"dry_run\":true}}");
    harness.expect(
        json::serialize_pretty_round_trip(*generate_dry_run.scene_delta()) ==
            json::serialize_pretty_round_trip(*generate_dry_run_again.scene_delta()),
        "mesh.generate_weights dry-run repeated",
        "two dry runs must report an identical payload, affected_vertices included");

    // Scoped: vertices [0] only. Vertex 0 carries one influence in the fixture
    // and must gain arm_l; the other three must be byte-identical afterwards.
    harness.invoke(
        "mesh.generate_weights scoped to vertex 0",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\",\"arm_l\"],"
        "\"vertices\":[0]}}");
    {
        const json::Value* rows = weight_rows("mesh.describe after scoped generate");
        harness.expect(
            influence_count(rows, 0U) == std::optional<std::size_t>(2U),
            "mesh.generate_weights scoped to vertex 0",
            "vertex 0 must gain a second influence");
        harness.expect(
            string_member(influence_of(rows, 0U, 0U), "bone") ==
                    std::optional<std::string_view>("spine") &&
                string_member(influence_of(rows, 0U, 1U), "bone") ==
                    std::optional<std::string_view>("arm_l"),
            "mesh.generate_weights scoped to vertex 0",
            "vertex 0 must hold spine then arm_l in canonical order");
        harness.expect(
            serialize_rows(rows, 1U) == vertex1_before_generate &&
                serialize_rows(rows, 2U) == vertex2_before_generate &&
                serialize_rows(rows, 3U) == vertex3_before_generate,
            "mesh.generate_weights scoped to vertex 0",
            "vertices outside the scope must be byte-identical");
    }
    harness.invoke("undo scoped mesh.generate_weights", "{\"op\":\"undo\"}");
    harness.expect(
        serialize_rows(weight_rows("mesh.describe after scoped generate undo"), 0U) ==
            vertex0_before_generate,
        "undo scoped mesh.generate_weights",
        "undo must restore the pre-generate influences bit-exactly");

    // Unscoped: every vertex. Vertex 2's two candidates are exactly equidistant
    // -- both clamp to spine's world origin -- so the weights are exactly one
    // half each and the tie breaks on ascending skeleton index.
    harness.invoke(
        "mesh.generate_weights live",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\",\"arm_l\"]}}");
    {
        const json::Value* rows = weight_rows("mesh.describe after generate");
        harness.expect(
            number_member(influence_of(rows, 2U, 0U), "weight") ==
                    std::optional<double>(0.5) &&
                number_member(influence_of(rows, 2U, 1U), "weight") ==
                    std::optional<double>(0.5),
            "mesh.generate_weights live",
            "vertex 2's equidistant candidates must weigh exactly 0.5 each");
        harness.expect(
            string_member(influence_of(rows, 2U, 0U), "bone") ==
                    std::optional<std::string_view>("spine") &&
                string_member(influence_of(rows, 2U, 1U), "bone") ==
                    std::optional<std::string_view>("arm_l"),
            "mesh.generate_weights live",
            "an exact weight tie must order spine before arm_l on skeleton index");
    }
    const DispatchObservation generate_again = harness.invoke(
        "mesh.generate_weights repeated",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\",\"arm_l\"]}}");
    // Deliberately NOT asserted as `no_change`: generation is deterministic but
    // not bit-exactly idempotent, because BoneWorldTransform is float32 while
    // bind offsets are double. A repeat may legitimately report a change of a
    // few ULPs. What is asserted is that it succeeds and keeps the exact tie.
    harness.expect(
        bool_member(&generate_again.root, "ok") == std::optional<bool>(true),
        "mesh.generate_weights repeated",
        "a repeated generate must succeed whether or not it reports a change");
    harness.expect(
        number_member(influence_of(weight_rows("mesh.describe after repeat"), 2U, 0U),
                      "weight") == std::optional<double>(0.5),
        "mesh.generate_weights repeated",
        "the exact tie must survive a second generate");
    // How many history entries the repeat created is exactly the open question
    // of section 7.6: a repeated generate is deterministic but not bit-exactly
    // idempotent, so it may legitimately commit a few-ULP change or may report
    // no_change. Drive the undo count off what it actually reported rather than
    // assuming either answer -- assuming one is how a test starts passing for
    // the wrong reason.
    const bool repeat_committed =
        bool_member(generate_again.scene_delta(), "changed") == std::optional<bool>(true);
    std::cout << "  MAR-176 note: a repeated mesh.generate_weights reported changed="
              << (repeat_committed ? "true" : "false")
              << " -- generation is deterministic but not bit-exactly idempotent, so either "
                 "answer is correct and neither is asserted.\n";
    if (repeat_committed) {
        harness.invoke("undo mesh.generate_weights repeat", "{\"op\":\"undo\"}");
    }
    harness.invoke("undo mesh.generate_weights", "{\"op\":\"undo\"}");
    harness.expect(
        serialize_rows(weight_rows("mesh.describe after generate undo"), 2U) ==
            vertex2_before_generate,
        "undo mesh.generate_weights",
        "undo must restore the pre-generate influences bit-exactly");

    // AC1 on the wire: `bones` is required and is never expanded.
    harness.invoke(
        "mesh.generate_weights requires bones",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\"}}",
        false);
    harness.invoke(
        "mesh.generate_weights rejects an empty bones array",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[]}}",
        false);
    harness.invoke(
        "mesh.generate_weights rejects an unknown bone",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"nope\"]}}",
        false,
        "not_found");
    harness.invoke(
        "mesh.generate_weights rejects a repeated bone",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\","
        "\"bones\":[\"spine\",\"spine\"]}}",
        false);
    harness.invoke(
        "mesh.generate_weights rejects a non-string bone entry",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[7]}}",
        false);
    harness.invoke(
        "mesh.generate_weights rejects an empty bone name",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"\"]}}",
        false);
    harness.invoke(
        "mesh.generate_weights rejects a missing attachment",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"no_such_mesh\","
        "\"bones\":[\"spine\"]}}",
        false,
        "not_found");
    harness.invoke(
        "mesh.generate_weights rejects an out-of-range vertex",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\"],"
        "\"vertices\":[99]}}",
        false,
        "invalid_request");
    harness.invoke(
        "mesh.generate_weights rejects an empty scope",
        "{\"op\":\"mesh.generate_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"bones\":[\"spine\"],"
        "\"vertices\":[]}}",
        false);
    harness.expect(
        serialize_rows(weight_rows("mesh.describe after generate rejections"), 2U) ==
            vertex2_before_generate,
        "mesh.generate_weights rejections",
        "no rejected generate may change the project");

    // ── MAR-178: constraint.rename / constraint.delete ─────────────────────
    //
    // Read back the SURVIVORS by name from `constraints.list` rather than the
    // return code: the worst lifecycle failure lands on load, not on save.
    const auto constraint_names = [&](std::string_view label) {
        const DispatchObservation listed =
            harness.invoke(label, "{\"op\":\"constraints.list\"}");
        return compact_scene_delta(listed);
    };

    const std::string constraints_before_dry_run =
        constraint_names("constraints.list before constraint dry run");

    const DispatchObservation rename_dry_run = harness.invoke(
        "constraint.rename dry-run",
        "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"transform\","
        "\"from\":\"editor_transform_follow\",\"to\":\"transform_follow_v2\","
        "\"dry_run\":true}}");
    harness.expect(
        bool_member(rename_dry_run.scene_delta(), "dry_run") ==
                std::optional<bool>(true) &&
            string_member(rename_dry_run.scene_delta(), "ownership") ==
                std::optional<std::string_view>("project") &&
            number_member(rename_dry_run.scene_delta(), "skin_reference_count") ==
                std::optional<double>(0.0),
        "constraint.rename dry-run",
        "the dry run must report ownership and the pre-mutation skin summary");
    harness.expect(
        constraint_names("constraints.list after constraint dry run") ==
            constraints_before_dry_run,
        "constraint.rename dry-run",
        "a dry run must leave constraints.list byte-identical");

    const DispatchObservation rename_live = harness.invoke(
        "constraint.rename live",
        "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"transform\","
        "\"from\":\"editor_transform_follow\",\"to\":\"transform_follow_v2\"}}");
    {
        // The dry-run payload must equal the live payload apart from `dry_run`,
        // which is the cheapest proof that both ran the same primitive.
        std::string dry = compact_scene_delta(rename_dry_run);
        const std::string marker = "\"dry_run\":true,";
        const auto position = dry.find(marker);
        if (position != std::string::npos) {
            dry.erase(position, marker.size());
        }
        harness.expect(
            dry == compact_scene_delta(rename_live),
            "constraint.rename live",
            "the dry-run and live scene_delta must differ only by \"dry_run\": " +
                compact_scene_delta(rename_live));
    }
    const std::string constraints_after_rename =
        constraint_names("constraints.list after rename");
    harness.expect(
        constraints_after_rename.find("transform_follow_v2") != std::string::npos &&
            constraints_after_rename.find("editor_transform_follow") ==
                std::string::npos,
        "constraint.rename live",
        "the renamed constraint must read back under its new name only");

    harness.invoke("undo constraint.rename", "{\"op\":\"undo\"}");
    harness.expect(
        constraint_names("constraints.list after rename undo") ==
            constraints_before_dry_run,
        "undo constraint.rename",
        "undo must restore the exact pre-rename constraint list");

    // --- Delete, asserted on the survivors. --------------------------------
    harness.invoke(
        "constraint.delete live",
        "{\"op\":\"constraint.delete\",\"args\":{\"family\":\"physics\","
        "\"name\":\"editor_ribbon_secondary\"}}");
    {
        const std::string survivors =
            constraint_names("constraints.list after delete");
        harness.expect(
            survivors.find("editor_ribbon_secondary") == std::string::npos &&
                survivors.find("editor_arm_reach") != std::string::npos &&
                survivors.find("editor_guide_follow") != std::string::npos &&
                survivors.find("editor_transform_follow") != std::string::npos,
            "constraint.delete live",
            "the delete must remove exactly its own target and leave the other "
            "three families' constraints readable");
    }
    harness.invoke("undo constraint.delete", "{\"op\":\"undo\"}");
    harness.expect(
        constraint_names("constraints.list after delete undo") ==
            constraints_before_dry_run,
        "undo constraint.delete",
        "undo must restore the deleted constraint");

    // --- Dry-run and live must reject identically, message for message. -----
    {
        const DispatchObservation rejected_dry = harness.invoke(
            "constraint.rename dry-run rejects an unchanged target",
            "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"ik\","
            "\"from\":\"editor_arm_reach\",\"to\":\"editor_arm_reach\","
            "\"dry_run\":true}}",
            false);
        const DispatchObservation rejected_live = harness.invoke(
            "constraint.rename live rejects an unchanged target",
            "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"ik\","
            "\"from\":\"editor_arm_reach\",\"to\":\"editor_arm_reach\"}}",
            false);
        const auto dry_message = string_member(&rejected_dry.root, "message");
        const auto live_message = string_member(&rejected_live.root, "message");
        harness.expect(
            dry_message.has_value() && live_message.has_value() &&
                *dry_message == *live_message,
            "constraint.rename dry-run/live message parity",
            "a hand-written dry-run check drifts from the primitive's message; "
            "measured dry='" +
                std::string(dry_message.value_or("")) + "' live='" +
                std::string(live_message.value_or("")) + "'");
    }

    // --- Family validation. -------------------------------------------------
    harness.invoke(
        "constraint.rename rejects an unknown family",
        "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"bone\","
        "\"from\":\"editor_arm_reach\",\"to\":\"arm_v2\"}}",
        false);
    harness.invoke(
        "constraint.rename requires a family",
        "{\"op\":\"constraint.rename\",\"args\":{"
        "\"from\":\"editor_arm_reach\",\"to\":\"arm_v2\"}}",
        false);
    harness.invoke(
        "constraint.rename rejects a right name in the wrong family",
        "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"ik\","
        "\"from\":\"editor_ribbon_secondary\",\"to\":\"arm_v2\"}}",
        false,
        "not_found");
    harness.invoke(
        "constraint.delete requires a name",
        "{\"op\":\"constraint.delete\",\"args\":{\"family\":\"physics\"}}",
        false);
    harness.invoke(
        "constraint.delete rejects a missing constraint",
        "{\"op\":\"constraint.delete\",\"args\":{\"family\":\"physics\","
        "\"name\":\"never_existed\"}}",
        false,
        "not_found");

    // --- A rejection leaves the history clean: the next undo must reverse the
    //     PREVIOUS edit, not the rejected one.
    {
        harness.invoke(
            "constraint.rename seeds a history entry",
            "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"path\","
            "\"from\":\"editor_guide_follow\",\"to\":\"guide_follow_v2\"}}");
        harness.invoke(
            "constraint.rename rejected after a real edit",
            "{\"op\":\"constraint.rename\",\"args\":{\"family\":\"path\","
            "\"from\":\"guide_follow_v2\",\"to\":\"guide_follow_v2\"}}",
            false);
        harness.invoke("undo after a rejected constraint.rename", "{\"op\":\"undo\"}");
        harness.expect(
            constraint_names("constraints.list after the rejected rename undo") ==
                constraints_before_dry_run,
            "undo after a rejected constraint.rename",
            "a rejected edit must leave no history entry, so undo reverses the "
            "previous one");
    }

    harness.invoke(
        "set_slot_color_keyframe",
        "{\"op\":\"set_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"color\":{\"r\":0.5,\"g\":0.75,"
        "\"b\":1,\"a\":0.8}}}");
    harness.invoke(
        "remove_slot_color_keyframe",
        "{\"op\":\"remove_slot_color_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625}}");
    harness.invoke(
        "set_attachment_keyframe",
        "{\"op\":\"set_attachment_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625,\"attachment\":\"body\"}}");
    harness.invoke(
        "remove_attachment_keyframe",
        "{\"op\":\"remove_attachment_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"slot\":\"body\",\"time\":0.625}}");
    harness.invoke(
        "set_draw_order_keyframe",
        "{\"op\":\"set_draw_order_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.75,\"slots\":[\"arm_l\",\"body\",\"fx_mask\",\"spark_fx\","
        "\"spawn_anchor\",\"hurtbox\",\"guide\"]}}");
    harness.invoke(
        "remove_draw_order_keyframe",
        "{\"op\":\"remove_draw_order_keyframe\",\"args\":{\"animation\":\"idle\","
        "\"time\":0.75}}");

    const DispatchObservation created_animation = harness.invoke(
        "animation.create",
        "{\"op\":\"animation.create\",\"args\":{\"name\":\"agent_empty\"}}");
    harness.expect(
        string_member(created_animation.scene_delta(), "selected_animation") ==
            std::optional<std::string_view>("agent_empty"),
        "animation.create",
        "created animation was not selected inside its transaction");
    const DispatchObservation duplicated_animation = harness.invoke(
        "animation.duplicate",
        "{\"op\":\"animation.duplicate\",\"args\":{\"source\":\"idle\","
        "\"name\":\"agent_idle_copy\"}}");
    harness.expect(
        string_member(duplicated_animation.scene_delta(), "selected_animation") ==
            std::optional<std::string_view>("agent_idle_copy"),
        "animation.duplicate",
        "duplicated animation was not selected inside its transaction");
    const DispatchObservation renamed_animation = harness.invoke(
        "animation.rename",
        "{\"op\":\"animation.rename\",\"args\":{\"from\":\"agent_empty\","
        "\"to\":\"agent_empty_renamed\"}}");
    harness.expect(
        string_member(renamed_animation.scene_delta(), "selected_animation") ==
            std::optional<std::string_view>("agent_idle_copy"),
        "animation.rename",
        "renaming an unselected animation changed the current preview");
    const DispatchObservation deleted_animation = harness.invoke(
        "animation.delete",
        "{\"op\":\"animation.delete\",\"args\":{\"name\":\"agent_idle_copy\"}}");
    const auto selected_after_delete =
        string_member(deleted_animation.scene_delta(), "selected_animation");
    harness.expect(
        selected_after_delete.has_value() && !selected_after_delete->empty() &&
            *selected_after_delete != "agent_idle_copy" &&
            bool_member(deleted_animation.scene_delta(), "queue_enabled") ==
                std::optional<bool>(false),
        "animation.delete",
        "deleting the selected animation did not atomically remap its preview");

    // Review-only commands must enqueue deterministic, whitelisted targets,
    // allocate monotonic IDs, and never touch files before user approval.
    const DispatchObservation diagnostics_before_reviews = harness.invoke(
        "project.diagnostics before reviews",
        "{\"op\":\"project.diagnostics\"}");
    const auto dirty_before_reviews =
        bool_member(diagnostics_before_reviews.scene_delta(), "project_dirty");
    std::uint64_t last_review_id = 0U;
    std::size_t review_count = 0U;
    const auto record_review = [&](
                                   std::string_view label,
                                   const DispatchObservation& response,
                                   std::string_view op,
                                   std::string_view kind) {
        const auto id = expect_review(harness, label, response, op, kind);
        if (id.has_value()) {
            harness.expect(
                *id > last_review_id,
                label,
                "review IDs are not monotonically increasing");
            last_review_id = *id;
        }
        ++review_count;
    };

    record_review(
        "save review",
        harness.invoke("save review", "{\"op\":\"save\"}"),
        "save",
        "save");
    record_review(
        "export_runtime review",
        harness.invoke(
            "export_runtime review",
            "{\"op\":\"export_runtime\",\"args\":{\"binary\":true}}"),
        "export_runtime",
        "export_runtime");
    record_review(
        "import.spine_json review",
        harness.invoke(
            "import.spine_json review",
            "{\"op\":\"import.spine_json\",\"args\":{"
            "\"input\":\"assets/fixtures/spine_import_sample.json\","
            "\"output\":\"/tmp/agent_spine_import_sample.mskl\",\"dry_run\":false}}"),
        "import.spine_json",
        "import_or_pack");
    record_review(
        "import.spine_atlas review",
        harness.invoke(
            "import.spine_atlas review",
            "{\"op\":\"import.spine_atlas\",\"args\":{"
            "\"input\":\"assets/fixtures/spine_import_sample.atlas\","
            "\"output\":\"/tmp/agent_spine_import_sample.matl\",\"dry_run\":false}}"),
        "import.spine_atlas",
        "import_or_pack");
    record_review(
        "import.psd_layers review",
        harness.invoke(
            "import.psd_layers review",
            "{\"op\":\"import.psd_layers\",\"args\":{"
            "\"input\":\"assets/fixtures/psd_import_sample.psd\","
            "\"output\":\"/tmp/agent_psd_import_sample.mskl\","
            "\"atlas_output\":\"/tmp/agent_psd_import_sample.matl\",\"dry_run\":false}}"),
        "import.psd_layers",
        "import_or_pack");
    record_review(
        "atlas.pack review",
        harness.invoke(
            "atlas.pack review",
            "{\"op\":\"atlas.pack\",\"args\":{"
            "\"output\":\"/tmp/agent_atlas_pack_sample.matl\",\"dry_run\":false}}"),
        "atlas.pack",
        "import_or_pack");

    const DispatchObservation permissions_after_reviews = harness.invoke(
        "agent.permissions.describe after reviews",
        "{\"op\":\"agent.permissions.describe\"}");
    harness.expect(
        number_member(permissions_after_reviews.scene_delta(), "pending_reviews") ==
            std::optional<double>(static_cast<double>(review_count)),
        "review queue",
        "permissions did not report every queued review");
    const DispatchObservation diagnostics_after_reviews = harness.invoke(
        "project.diagnostics after reviews",
        "{\"op\":\"project.diagnostics\"}");
    harness.expect(
        number_member(diagnostics_after_reviews.scene_delta(), "review_queue_count") ==
            std::optional<double>(static_cast<double>(review_count)),
        "review queue",
        "diagnostics did not report every queued review");
    harness.expect(
        bool_member(diagnostics_after_reviews.scene_delta(), "project_dirty") ==
            dirty_before_reviews,
        "review queue",
        "queueing reviews changed project dirty state");

    for (const auto& target : reviewed_temp_targets) {
        harness.expect(
            !std::filesystem::exists(target),
            "review-only file safety",
            target.string() + " was written before approval");
    }
    expect_file_unchanged(harness, project_file_before);
    for (const FileSnapshot& snapshot : export_files_before) {
        expect_file_unchanged(harness, snapshot);
    }

    expect_export_equivalence(
        harness,
        "compare_runtime_export final",
        harness.invoke(
            "compare_runtime_export final",
            "{\"op\":\"compare_runtime_export\",\"args\":{\"binary\":true}}"));

    harness.invoke(
        "unknown operation rejected",
        "{\"op\":\"definitely_not_a_real_op\"}",
        false,
        "unknown_operation",
        false);

    // Terminate is the user's kill switch: the terminated agent must not be
    // able to un-terminate itself through agent.resume. Only editor-side user
    // action restores access, so this block runs last - nothing after it may
    // require mutating dispatch.
    harness.invoke("agent.terminate", "{\"op\":\"agent.terminate\"}");
    const DispatchObservation terminated_permissions = harness.invoke(
        "agent.permissions.describe terminated",
        "{\"op\":\"agent.permissions.describe\"}");
    harness.expect(
        bool_member(terminated_permissions.scene_delta(), "paused") ==
                std::optional<bool>(true) &&
            bool_member(terminated_permissions.scene_delta(), "terminated") ==
                std::optional<bool>(true),
        "agent.terminate",
        "terminate must set paused and terminated");
    harness.invoke(
        "terminated mutation blocked",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.8,\"angle\":5}}",
        false,
        "blocked");
    harness.invoke(
        "agent.resume rejected after terminate",
        "{\"op\":\"agent.resume\"}",
        false,
        "terminated");
    const DispatchObservation still_terminated_permissions = harness.invoke(
        "agent.permissions.describe still terminated",
        "{\"op\":\"agent.permissions.describe\"}");
    harness.expect(
        bool_member(still_terminated_permissions.scene_delta(), "paused") ==
                std::optional<bool>(true) &&
            bool_member(still_terminated_permissions.scene_delta(), "terminated") ==
                std::optional<bool>(true),
        "agent.resume rejected after terminate",
        "a terminated session must stay terminated after agent.resume");
    harness.invoke(
        "terminated mutation still blocked",
        "{\"op\":\"set_transform\",\"args\":{\"animation\":\"idle\","
        "\"bone\":\"spine\",\"channel\":\"rotate\",\"time\":0.8,\"angle\":5}}",
        false,
        "blocked");
    harness.expect_complete_coverage();

    marrow_editor_project_destroy(project);

    if (!harness.passed()) {
        std::cerr << "agent_dispatch_smoke: FAILED\n";
        return 1;
    }
    std::cout << "agent_dispatch_smoke: PASSED\n";
    return 0;
}
