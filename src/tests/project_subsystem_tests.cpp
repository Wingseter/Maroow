#include "marrow/editor/project.hpp"
#include "project_internal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace fs = std::filesystem;
namespace json = marrow::runtime::json;
using namespace marrow::editor;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Scratch {
    fs::path path;
    Scratch() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            path = fs::temp_directory_path() /
                ("marrow-project-subsystem-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("could not reserve scratch directory");
    }
    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

json::Document document(std::string_view text, const fs::path& path = {}) {
    auto parsed = json::parse_document(text, path);
    require(bool(parsed), parsed.error ? parsed.error->format() : "JSON parse failed");
    return std::move(*parsed.document);
}

json::Value& member(json::Value& value, const char* key) {
    auto* result = json::find_member(value, key);
    require(result != nullptr, std::string("missing JSON member: ") + key);
    return *result;
}

std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

json::Document offline_document(const fs::path& path) {
    return document(R"({
      "marrow":"1.0",
      "runtime":{"skeleton":"missing.mskl","atlases":["missing.matl"]},
      "editor":{"name":"offline"}
    })", path);
}

ProjectData parse_only(const json::Document& source) {
    ProjectData project;
    auto error = project_detail::parse_project_document(source, &project);
    require(!error, error ? error->format() : "project parse failed");
    return project;
}

void parse_without_assets() {
    Scratch scratch;
    auto source = offline_document(scratch.path / "offline.marrow");
    auto project = parse_only(source);
    require(project.source_path == source.source_path, "parse lost source identity");
    require(project.runtime_assets.skeleton_path == "missing.mskl", "parse rebased paths");
    require(fs::is_empty(scratch.path), "parse performed filesystem writes");
    auto loaded = load_project(source);
    require(!loaded && loaded.error.has_value() && !loaded.project,
            "public load must still load assets, not return a parse-only success");
    require(loaded.error->source_path == project.resolved_skeleton_path(),
            "public load did not report the missing skeleton");
}

void parse_failure_is_atomic_and_diagnostic() {
    Scratch scratch;
    auto source = offline_document(scratch.path / "diagnostic.marrow");
    auto original = parse_only(source);
    const auto before = serialize_project(original);
    auto invalid = source;
    member(member(invalid.root, "editor"), "name") = json::Value("", {});
    auto error = project_detail::parse_project_document(invalid, &original);
    require(error.has_value() && error->message.find("$.editor.name") != std::string::npos,
            "parse lost the precise editor-name diagnostic");
    require(error->source_path == source.source_path && error->location.line > 1,
            "parse lost source path/location");
    require(serialize_project(original) == before && original.source_path == source.source_path,
            "failed parse partially overwrote its output");
    const auto public_error = load_project(invalid);
    require(!public_error && public_error.error && public_error.error->format() == error->format(),
            "public and parse-only validation diagnostics diverged");

    // This fails at the final cross-reference check, after all field parsers ran.
    invalid = source;
    invalid.root.as_object()["atlas_packs"] = document(R"([
      {"atlas":"unlisted.matl","sprites":[{"name":"part","image":"part.png"}]}
    ])").root;
    error = project_detail::parse_project_document(invalid, &original);
    require(error && error->message.find("$.atlas_packs[0].atlas") != std::string::npos,
            "late parse validation was skipped");
    require(serialize_project(original) == before, "late failure partially committed parsed state");
}

void unknown_fields_and_determinism() {
    auto source = offline_document("offline.marrow");
    const auto opaque = document(R"({"nested":[1,true,null,"future"]})").root;
    source.root.as_object()["future_root"] = opaque;
    member(source.root, "runtime").as_object()["future_runtime"] = opaque;
    member(source.root, "editor").as_object()["future_editor"] = opaque;
    source.root.as_object()["snap"] = document(R"({"world_grid_step":10,"future_snap":{"nested":[1,true,null,"future"]}})").root;
    source.root.as_object()["animation_edits"] = document(R"([
      {"op":"future_operation","payload":{"nested":[1,true,null,"future"]}}
    ])").root;
    auto project = parse_only(source);
    const std::string first = serialize_project(project);
    auto emitted = document(first, source.source_path);
    const auto expected = json::serialize_pretty(opaque);
    require(json::serialize_pretty(member(emitted.root, "future_root")) == expected,
            "unknown root field was lost");
    require(json::serialize_pretty(member(member(emitted.root, "runtime"), "future_runtime")) == expected,
            "unknown runtime reference field was lost");
    require(json::serialize_pretty(member(member(emitted.root, "editor"), "future_editor")) == expected,
            "unknown editor field was lost");
    require(json::serialize_pretty(member(member(emitted.root, "snap"), "future_snap")) == expected,
            "unknown snap field was lost");
    require(json::serialize_pretty(member(emitted.root, "animation_edits")) ==
            json::serialize_pretty(member(source.root, "animation_edits")),
            "unknown animation operation was not preserved verbatim as JSON");
    require(serialize_project(parse_only(emitted)) == first,
            "parse/serialize did not reach a deterministic representation");
}

void overlay_order_and_input_immutability() {
    auto source = offline_document("overlay.marrow");
    source.root.as_object()["animation_edits"] = document(R"([
      {"op":"rename","from":"idle","to":"renamed"}
    ])").root;
    source.root.as_object()["timeline_edits"] = document(R"({"animations":{"renamed":{
      "bones":{"root":{"rotate":[{"time":0,"angle":42}]}}
    }}})").root;
    source.root.as_object()["constraint_edits"] = document(R"({
      "operations":[{"op":"rename","family":"ik","from":"old","to":"new"}],
      "ik":[{"name":"new","bones":["root"],"target":"target","mix":0.75}]
    })").root;
    auto project = parse_only(source);
    auto base = document(R"({"version":1,"animations":{"idle":{"bones":{"root":{
      "rotate":[{"time":0,"angle":1}]
    }}}},"ik":[{"name":"old","mix":0.1}],"skins":{"default":{"ik":["old"]}},
    "future_base":{"keep":true}})");
    const auto before_base = json::serialize_pretty(base.root);
    const auto before_project = serialize_project(project);
    auto runtime = build_project_runtime_document(project, base);
    auto& animations = member(runtime.root, "animations");
    require(json::find_member(animations, "idle") == nullptr, "rename retained the old animation");
    auto& keys = member(member(member(member(animations, "renamed"), "bones"), "root"), "rotate");
    require(member(keys.as_array().front(), "angle").as_number() == 42,
            "timeline overrides must run after animation rename");
    auto& ik = member(runtime.root, "ik").as_array();
    require(ik.size() == 1 && member(ik.front(), "name").as_string() == "new" &&
            member(ik.front(), "mix").as_number() == 0.75,
            "constraint upsert must run after lifecycle rename");
    require(member(member(member(runtime.root, "skins"), "default"), "ik").as_array().front().as_string() == "new",
            "constraint rename did not update skin references");
    require(json::serialize_pretty(base.root) == before_base && serialize_project(project) == before_project,
            "materialization mutated one of its inputs");
    require(json::serialize_pretty(member(runtime.root, "future_base")) ==
            json::serialize_pretty(member(base.root, "future_base")), "base additive field was lost");
}

void runtime_excludes_authoring_intent() {
    auto source = json::load_document(fs::absolute("assets/fixtures/player_idle.marrow"));
    require(bool(source), "missing player fixture");
    auto& key = member(member(member(member(member(member(source.document->root,
        "timeline_edits"), "animations"), "idle"), "bones"), "spine"), "rotate").as_array().front();
    key.as_object()["curve_mode"] = json::Value("auto", {});
    key.as_object()["curve_driver"] = json::Value("angle", {});
    auto loaded = load_project(*source.document);
    require(bool(loaded), loaded.error ? loaded.error->format() : "could not load curve fixture");
    auto saved = document(serialize_project(*loaded.project));
    auto& authored = member(member(member(member(member(member(saved.root,
        "timeline_edits"), "animations"), "idle"), "bones"), "spine"), "rotate").as_array().front();
    require(member(authored, "curve_mode").as_string() == "auto", "project writer lost authoring intent");
    auto runtime = build_project_runtime_document(*loaded.project, *loaded.base_skeleton_document);
    auto& exported = member(member(member(member(member(runtime.root,
        "animations"), "idle"), "bones"), "spine"), "rotate").as_array().front();
    require(json::find_member(exported, "curve_mode") == nullptr &&
            json::find_member(exported, "curve_driver") == nullptr, "runtime writer leaked authoring intent");
}

void save_reopen_export(const char* fixture) {
    Scratch scratch;
    auto loaded = load_project(fs::absolute(fixture));
    require(bool(loaded), loaded.error ? loaded.error->format() : "fixture load failed");
    const auto original = serialize_project(*loaded.project);
    const auto path = scratch.path / "save-as" / "project.marrow";
    const auto saved = save_project(*loaded.project, path);
    require(bool(saved), saved.error ? saved.error->format() : "save failed");
    require(saved.project->resolved_skeleton_path() == loaded.project->resolved_skeleton_path() &&
            saved.project->resolved_atlas_paths() == loaded.project->resolved_atlas_paths(),
            "Save As changed runtime asset identity");
    require(serialize_project(*loaded.project) == original, "save mutated the source project");
    auto reopened = load_project(path);
    require(bool(reopened), reopened.error ? reopened.error->format() : "reopen failed");
    require(serialize_project(*saved.project) == serialize_project(*reopened.project),
            "save/reopen changed project content");
    const auto good_bytes = bytes(path);
    auto invalid = *reopened.project;
    invalid.editor_metadata.name.clear();
    const auto refused = save_project(invalid, path);
    require(!refused && refused.error && bytes(path) == good_bytes,
            "invalid save damaged the existing destination");

    ProjectExportOptions options;
    options.skeleton_output_path = scratch.path / "export" / "runtime.mskl";
    options.binary_output_path = fs::path{};
    const auto exported = export_runtime_assets(*reopened.project, *reopened.base_skeleton_document, options);
    require(bool(exported), exported.error ? exported.error->format() : "export failed");
    require(exported.binary_path && exported.binary_path->extension() == ".mbin", "default binary path changed");
    const auto text_document = marrow::runtime::load_skeleton_document(exported.path);
    const auto binary_document = marrow::runtime::load_skeleton_document(*exported.binary_path);
    require(bool(text_document) && bool(binary_document), "exported runtime documents are not reloadable");
    const auto materialized = build_project_runtime_document(*reopened.project, *reopened.base_skeleton_document);
    require(json::serialize_pretty(text_document.document->root) == json::serialize_pretty(materialized.root),
            "JSON export diverged from in-memory materialization");
    require(bool(marrow::runtime::load_skeleton_data(*binary_document.document)), "binary export runtime validation failed");
    require(bytes(path) == good_bytes, "export modified the saved project");
}
} // namespace

int main() {
    unsigned passed = 0;
    const auto run = [&](const char* name, auto test) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
            return true;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return false;
        }
    };
    bool ok = true;
    ok = run("parse_without_assets", parse_without_assets) && ok;
    ok = run("parse_failure_is_atomic_and_diagnostic", parse_failure_is_atomic_and_diagnostic) && ok;
    ok = run("unknown_fields_and_determinism", unknown_fields_and_determinism) && ok;
    ok = run("overlay_order_and_input_immutability", overlay_order_and_input_immutability) && ok;
    ok = run("runtime_excludes_authoring_intent", runtime_excludes_authoring_intent) && ok;
    ok = run("player_save_reopen_export", [] { save_reopen_export("assets/fixtures/player_idle.marrow"); }) && ok;
    ok = run("parameter_save_reopen_export", [] { save_reopen_export("assets/fixtures/parameter_face_basic.marrow"); }) && ok;
    std::cout << passed << "/7 project subsystem cases passed\n";
    return ok ? 0 : 1;
}
