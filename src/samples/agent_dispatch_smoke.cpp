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

constexpr std::array<OperationExpectation, 56> kExpectedOperations{{
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
    {"set_transform", "edit", true, false, true},
    {"remove_transform_keyframe", "edit", true, false, false},
    {"set_event_keyframe", "edit", true, false, true},
    {"remove_event_keyframe", "edit", true, false, false},
    {"set_deform_keyframe", "edit", true, false, true},
    {"remove_deform_keyframe", "edit", true, false, false},
    {"set_vertex_weights", "edit", true, false, true},
    {"normalize_weights", "edit", true, false, true},
    {"edit_ik_constraint", "edit", true, false, true},
    {"edit_path_constraint", "edit", true, false, true},
    {"edit_transform_constraint", "edit", true, false, true},
    {"edit_physics_constraint", "edit", true, false, true},
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
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"dry_run\":true}}");
    harness.invoke(
        "normalize_weights dry-run",
        "{\"op\":\"normalize_weights\",\"args\":{\"skin\":\"mesh_base\","
        "\"slot\":\"body\",\"attachment\":\"body_mesh\",\"dry_run\":true}}");
    harness.invoke(
        "edit_ik_constraint dry-run",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"mix\":0.5,\"dry_run\":true}}");
    harness.invoke(
        "edit_path_constraint dry-run",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.2,\"dry_run\":true}}");
    harness.invoke(
        "edit_transform_constraint dry-run",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"translate_mix\":0.5,"
        "\"offset\":{\"x\":-8},\"dry_run\":true}}");
    harness.invoke(
        "edit_physics_constraint dry-run",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"mix\":0.8,"
        "\"wind\":{\"x\":10},\"dry_run\":true}}");
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
    harness.invoke(
        "edit_ik_constraint",
        "{\"op\":\"edit_ik_constraint\",\"args\":{\"name\":\"editor_arm_reach\","
        "\"mix\":0.5}}");
    harness.invoke(
        "edit_path_constraint",
        "{\"op\":\"edit_path_constraint\",\"args\":{\"name\":\"editor_guide_follow\","
        "\"position\":0.25,\"rotate_mix\":0.75}}");
    harness.invoke(
        "edit_transform_constraint",
        "{\"op\":\"edit_transform_constraint\",\"args\":{"
        "\"name\":\"editor_transform_follow\",\"translate_mix\":0.5,"
        "\"offset\":{\"x\":-8}}}");
    harness.invoke(
        "edit_physics_constraint",
        "{\"op\":\"edit_physics_constraint\",\"args\":{"
        "\"name\":\"editor_ribbon_secondary\",\"mix\":0.8,\"wind\":{\"x\":10}}}");
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
