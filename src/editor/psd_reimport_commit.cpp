#include "marrow/editor/psd_reimport_commit.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>

#include "atomic_file_write.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "psd_reimport_commit_internal.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace marrow::editor {

const std::array<PsdCommitStep, 15> kAllCommitSteps{{
    PsdCommitStep::ValidateRequest,
    PsdCommitStep::PruneUnpreserved,
    PsdCommitStep::ValidateStagedBundle,
    PsdCommitStep::OpenJournal,
    PsdCommitStep::BackupLayers,
    PsdCommitStep::BackupTexture,
    PsdCommitStep::BackupAtlas,
    PsdCommitStep::BackupSkeleton,
    PsdCommitStep::PlaceLayers,
    PsdCommitStep::PlaceTexture,
    PsdCommitStep::PlaceAtlas,
    PsdCommitStep::PlaceSkeleton,
    PsdCommitStep::AdoptRuntimeSources,
    PsdCommitStep::UpdateProvenance,
    PsdCommitStep::CleanJournal,
}};

// No `default:`. Clang's `-Wswitch` is on without any flag in this tree, so a new
// enumerator is named here at every build. `AGENTS.md` is explicit that it WARNS
// and does not fail, and that GCC is silent without `-Wall`, so the warning is a
// convenience and R0's ordered ledger identity is the actual detector.
const char* psd_commit_step_name(PsdCommitStep step) {
    switch (step) {
        case PsdCommitStep::ValidateRequest:
            return "ValidateRequest";
        case PsdCommitStep::PruneUnpreserved:
            return "PruneUnpreserved";
        case PsdCommitStep::ValidateStagedBundle:
            return "ValidateStagedBundle";
        case PsdCommitStep::OpenJournal:
            return "OpenJournal";
        case PsdCommitStep::BackupLayers:
            return "BackupLayers";
        case PsdCommitStep::BackupTexture:
            return "BackupTexture";
        case PsdCommitStep::BackupAtlas:
            return "BackupAtlas";
        case PsdCommitStep::BackupSkeleton:
            return "BackupSkeleton";
        case PsdCommitStep::PlaceLayers:
            return "PlaceLayers";
        case PsdCommitStep::PlaceTexture:
            return "PlaceTexture";
        case PsdCommitStep::PlaceAtlas:
            return "PlaceAtlas";
        case PsdCommitStep::PlaceSkeleton:
            return "PlaceSkeleton";
        case PsdCommitStep::AdoptRuntimeSources:
            return "AdoptRuntimeSources";
        case PsdCommitStep::UpdateProvenance:
            return "UpdateProvenance";
        case PsdCommitStep::CleanJournal:
            return "CleanJournal";
    }
    return "<unknown>";
}

namespace detail {
namespace {

std::mutex& failpoint_mutex() {
    static std::mutex mutex;
    return mutex;
}

CommitFailpoint& commit_failpoint() {
    static CommitFailpoint callback;
    return callback;
}

CommitRollbackFailpoint& rollback_failpoint() {
    static CommitRollbackFailpoint callback;
    return callback;
}

} // namespace

void set_psd_commit_failpoint_for_testing(CommitFailpoint callback) {
    const std::lock_guard<std::mutex> lock(failpoint_mutex());
    commit_failpoint() = std::move(callback);
}

void set_psd_commit_rollback_failpoint_for_testing(CommitRollbackFailpoint callback) {
    const std::lock_guard<std::mutex> lock(failpoint_mutex());
    rollback_failpoint() = std::move(callback);
}

namespace {

std::string consult_commit_failpoint(PsdCommitStep step) {
    CommitFailpoint callback;
    {
        const std::lock_guard<std::mutex> lock(failpoint_mutex());
        callback = commit_failpoint();
    }
    return callback ? callback(step) : std::string();
}

std::string consult_rollback_failpoint(PsdCommitStep step) {
    CommitRollbackFailpoint callback;
    {
        const std::lock_guard<std::mutex> lock(failpoint_mutex());
        callback = rollback_failpoint();
    }
    return callback ? callback(step) : std::string();
}

} // namespace
} // namespace detail

namespace {

using runtime::json::Value;

std::string read_all_bytes(const std::filesystem::path& path, bool* ok_out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        *ok_out = false;
        return {};
    }
    std::string bytes(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    *ok_out = !stream.bad();
    return bytes;
}

Value make_string(std::string text) {
    return Value(std::move(text), {});
}

/** @brief One replaced artefact and the backup that holds its original bytes. */
struct JournalEntry {
    PsdCommitStep backup_step{PsdCommitStep::BackupLayers};
    PsdCommitStep place_step{PsdCommitStep::PlaceLayers};
    std::filesystem::path target;
    std::filesystem::path backup;
    /// @brief The staged artefact whose bytes replace `target`.
    std::filesystem::path staged;
    bool directory{false};
    /// @brief The target did not exist. Restoring it means deleting what replaced it.
    bool absent{false};
    bool backed_up{false};
    bool placed{false};
};

/**
 * @brief Everything the commit has done to the filesystem so far.
 *
 * The rollback reads only this. It never re-derives a path from the plan or the
 * project, because a rollback that re-derives is a rollback that can compute a
 * different answer than the step it is undoing did.
 */
struct Journal {
    std::filesystem::path manifest_path;
    bool manifest_written{false};
    std::vector<JournalEntry> entries;
};

std::string journal_identifier() {
    static std::uint64_t sequence = 0U;
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(::getpid());
#endif
    return std::to_string(pid) + "-" + std::to_string(++sequence);
}

/** @brief Reads one string member of a `.matl`'s `atlas` object. */
std::string atlas_image_member(const std::filesystem::path& atlas_path, std::string* error_out) {
    const runtime::json::LoadResult loaded = runtime::json::load_document(atlas_path);
    if (!loaded) {
        *error_out = "atlas '" + atlas_path.generic_string() + "' did not parse: " +
            loaded.error->message;
        return {};
    }
    const Value* atlas = runtime::json::find_member(loaded.document->root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        *error_out = "atlas '" + atlas_path.generic_string() + "' has no 'atlas' object";
        return {};
    }
    const Value* image = runtime::json::find_member(*atlas, "image");
    if (image == nullptr || !image->is_string() || image->as_string().empty()) {
        *error_out =
            "atlas '" + atlas_path.generic_string() + "' declares no 'image' member";
        return {};
    }
    return image->as_string();
}

/** @brief Every regular file under @p root, as paths relative to it, sorted. */
std::vector<std::filesystem::path> relative_files(
    const std::filesystem::path& root,
    std::string* error_out) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            *error_out = "could not read '" + root.generic_string() + "': " + error.message();
            return {};
        }
        std::error_code entry_error;
        if (iterator->is_regular_file(entry_error)) {
            files.push_back(
                std::filesystem::relative(iterator->path(), root, entry_error));
            if (entry_error) {
                *error_out = "could not relativize '" + iterator->path().generic_string() +
                    "': " + entry_error.message();
                return {};
            }
        }
    }
    if (error) {
        *error_out = "could not read '" + root.generic_string() + "': " + error.message();
        return {};
    }
    std::sort(files.begin(), files.end());
    return files;
}

/** @brief Builds the provenance a successful commit would store, from the PLAN. */
PsdImportProvenance provenance_from_plan(
    const PsdReimportPlan& plan,
    const std::filesystem::path& project_path,
    const std::filesystem::path& layers_directory) {
    PsdImportProvenance provenance;
    provenance.source_path = project_relative_path(project_path, plan.source_path);
    provenance.layers_directory = project_relative_path(project_path, layers_directory);
    provenance.layers.reserve(plan.layers.size());
    // The PLAN's order, which is ascending identity -- not the candidate record
    // order, which is what a user reorders in Photoshop without meaning anything
    // by it.
    for (const PsdPlannedLayer& layer : plan.layers) {
        if (layer.change == PsdLayerChangeKind::Missing) {
            if (!layer.preserve) {
                continue;
            }
            PsdLayerProvenance kept;
            kept.group_path = layer.group_path;
            kept.layer_name = layer.layer_name;
            kept.slot_name = layer.current_slot_name;
            kept.attachment_name = layer.current_attachment_name;
            kept.bone_name = layer.current_bone_name;
            kept.image_file = layer.current_image_file;
            provenance.layers.push_back(std::move(kept));
            continue;
        }
        PsdLayerProvenance stored;
        stored.group_path = layer.group_path;
        stored.layer_name = layer.layer_name;
        stored.slot_name = layer.proposed_slot_name;
        stored.attachment_name = layer.proposed_attachment_name;
        stored.bone_name = layer.proposed_bone_name;
        stored.image_file = layer.proposed_image_file;
        provenance.layers.push_back(std::move(stored));
    }
    return provenance;
}

/** @brief The running commit's own mutable state, so `advance` has one home. */
class CommitRun {
public:
    CommitRun(EditorSession& session, const PsdReimportPlan& plan,
              const PsdReimportCommitOptions& options)
        : session_(session), plan_(plan), options_(options) {}

    PsdReimportCommitResult run();

private:
    /// @brief The ONLY place that appends to the ledger or consults the failpoint.
    bool advance(PsdCommitStep step) {
        result_.steps_executed.push_back(step);
        std::string injected = detail::consult_commit_failpoint(step);
        if (injected.empty()) {
            return true;
        }
        fail(step, "injected failure: " + injected);
        return false;
    }

    void fail(PsdCommitStep step, std::string cause) {
        result_.ok = false;
        result_.failed_step = step;
        result_.error = std::string(psd_commit_step_name(step)) + ": " + std::move(cause);
    }

    bool validate_request();
    bool prune_unpreserved();
    bool validate_staged_bundle();
    bool open_journal();
    bool backup(JournalEntry& entry);
    bool place(JournalEntry& entry);
    bool adopt_runtime_sources();
    bool update_provenance();
    void clean_journal();
    void rollback();
    /// @brief The ONLY place that appends to the rollback ledger or consults its seam.
    bool rollback_advance(PsdCommitStep step);

    EditorSession& session_;
    const PsdReimportPlan& plan_;
    const PsdReimportCommitOptions& options_;
    PsdReimportCommitResult result_;

    std::filesystem::path project_path_;
    std::filesystem::path project_directory_;
    std::filesystem::path target_skeleton_;
    std::filesystem::path target_atlas_;
    std::filesystem::path target_texture_;
    std::filesystem::path target_layers_;
    std::vector<std::filesystem::path> other_atlases_;
    PsdImportProvenance proposed_provenance_;
    Journal journal_;
    runtime::json::Document staged_document_;
    bool staged_document_loaded_{false};
};

bool CommitRun::validate_request() {
    if (!plan_) {
        fail(PsdCommitStep::ValidateRequest,
             "the plan carries an error (" + plan_.error->message + ")");
        return false;
    }
    if (!session_.has_project() || session_.project() == nullptr) {
        fail(PsdCommitStep::ValidateRequest, "no editor project is open");
        return false;
    }
    const ProjectData& project = *session_.project();

    const std::vector<std::pair<const char*, const std::filesystem::path*>> staged = {
        {"staged_skeleton_path", &plan_.staged_skeleton_path},
        {"staged_atlas_path", &plan_.staged_atlas_path},
        {"staged_texture_path", &plan_.staged_texture_path},
        {"staged_layers_directory", &plan_.staged_layers_directory},
    };
    for (const auto& entry : staged) {
        if (entry.second->empty()) {
            fail(PsdCommitStep::ValidateRequest,
                 std::string("the plan's ") + entry.first + " is empty");
            return false;
        }
        std::error_code exists_error;
        if (!std::filesystem::exists(*entry.second, exists_error)) {
            fail(PsdCommitStep::ValidateRequest,
                 std::string("the plan's ") + entry.first + " ('" +
                     entry.second->generic_string() + "') no longer exists");
            return false;
        }
    }

    // AC6's "missing preservation/deletion inputs". A reimport replaces a layer
    // directory the project must already own, and the `preserve` decisions are
    // keyed on provenance identities. A project with no PSD provenance has
    // neither, and committing into it would guess at both.
    if (!project.editor_metadata.import_sources.has_value() ||
        !project.editor_metadata.import_sources->psd.has_value()) {
        fail(PsdCommitStep::ValidateRequest,
             "the project carries no PSD provenance, so there is no layer directory "
             "to replace and no stored identity to preserve against");
        return false;
    }
    const PsdImportProvenance& stored = *project.editor_metadata.import_sources->psd;
    if (stored.layers_directory.empty()) {
        fail(PsdCommitStep::ValidateRequest,
             "the project's PSD provenance names no layers directory");
        return false;
    }

    project_path_ =
        options_.project_path.empty() ? project.source_path : options_.project_path;
    if (project_path_.empty()) {
        fail(PsdCommitStep::ValidateRequest, "the project has no file path");
        return false;
    }
    project_directory_ = project_path_.parent_path();
    target_skeleton_ = project.resolved_skeleton_path();
    const std::vector<std::filesystem::path> atlases = project.resolved_atlas_paths();
    if (atlases.empty()) {
        fail(PsdCommitStep::ValidateRequest, "the project references no atlas");
        return false;
    }
    target_atlas_ = atlases.front();
    other_atlases_.assign(atlases.begin() + 1, atlases.end());

    // The texture is NOT derived from the target atlas's file name. It is the
    // `image` member of the atlas document as it stands BEFORE the commit -- the
    // same resolution `atlas.cpp` performs, and the same one the runtime will
    // perform afterwards. Deriving it from the atlas's stem would be a second
    // rule, and on this repo's own fixture the two disagree: `player_idle.matl`
    // declares `"image": "player_fixture.png"`.
    std::string image_error;
    const std::string image = atlas_image_member(target_atlas_, &image_error);
    if (image.empty()) {
        fail(PsdCommitStep::ValidateRequest, image_error);
        return false;
    }
    if (std::filesystem::path(image).filename() != std::filesystem::path(image)) {
        fail(PsdCommitStep::ValidateRequest,
             "the project atlas's 'image' member ('" + image + "') is not a bare file name");
        return false;
    }
    target_texture_ = target_atlas_.parent_path() / image;
    target_layers_ = project.resolve_path(stored.layers_directory);

    // The staged atlas must already say what the target atlas says, because
    // placement is a byte copy and `image` is resolved against the file's own
    // directory. This is the one clause that cannot be written against the
    // committed bundle: after the commit, `image` names a file the commit itself
    // placed, and every spelling of "that file is there" is then self-satisfying.
    const std::string staged_image = atlas_image_member(plan_.staged_atlas_path, &image_error);
    if (staged_image.empty()) {
        fail(PsdCommitStep::ValidateRequest, image_error);
        return false;
    }
    if (staged_image != image) {
        fail(PsdCommitStep::ValidateRequest,
             "the staged atlas references image '" + staged_image + "' but the project atlas '" +
                 target_atlas_.filename().generic_string() + "' references '" + image +
                 "'; committing it would name a texture that is not beside it");
        return false;
    }
    if (plan_.staged_texture_path.filename() != std::filesystem::path(image)) {
        fail(PsdCommitStep::ValidateRequest,
             "the staged texture is named '" +
                 plan_.staged_texture_path.filename().generic_string() +
                 "' but the atlas references '" + image + "'");
        return false;
    }

    proposed_provenance_ = provenance_from_plan(plan_, project_path_, target_layers_);
    return advance(PsdCommitStep::ValidateRequest);
}

bool CommitRun::prune_unpreserved() {
    runtime::json::LoadResult loaded = runtime::json::load_document(plan_.staged_skeleton_path);
    if (!loaded) {
        fail(PsdCommitStep::PruneUnpreserved,
             "the staged skeleton did not parse: " + loaded.error->message);
        return false;
    }
    staged_document_ = std::move(*loaded.document);
    staged_document_loaded_ = true;

    Value* slots = runtime::json::find_member(staged_document_.root, "slots");
    if (slots != nullptr && !slots->is_array()) {
        fail(PsdCommitStep::PruneUnpreserved,
             "the staged skeleton's 'slots' member is not an array");
        return false;
    }

    std::vector<std::string> dropped_slots;
    for (const PsdPlannedLayer& layer : plan_.layers) {
        if (layer.change != PsdLayerChangeKind::Missing || layer.preserve) {
            continue;
        }
        // An ABSENT slot is not an error, and this is the one place in the design
        // that measurement moved. `build_skeleton_document` assigns
        // `(*root)["slots"]` from the candidate and erases `skins` outright
        // (`psd_import.cpp:1038-1041`), so a `Missing` layer's slot is already gone
        // from every staged document this importer produces -- with `preserve` true
        // or false. Erroring on "the slot is not there" would turn every
        // `preserve == false` decision into a hard failure against the only
        // importer that exists.
        //
        // The consequence, stated rather than papered over: `preserve` governs the
        // stored provenance IDENTITY, not the art. Keeping a dropped layer's slot
        // in the committed skeleton would need the importer to merge slots instead
        // of replacing them, and MAR-189 does not change the importer.
        if (slots == nullptr) {
            continue;
        }
        Value::Array& array = slots->as_array();
        const auto match =
            std::find_if(array.begin(), array.end(), [&layer](const Value& slot) {
                const Value* name = runtime::json::find_member(slot, "name");
                return name != nullptr && name->is_string() &&
                    name->as_string() == layer.current_slot_name;
            });
        if (match == array.end()) {
            continue;
        }
        array.erase(match);
        dropped_slots.push_back(layer.current_slot_name);

        // The importer erases `skins` wholesale (`psd_import.cpp:1041`), so a
        // staged document normally has none. Handled anyway: this must not depend
        // on a property of a producer it does not own.
        Value* skins = runtime::json::find_member(staged_document_.root, "skins");
        if (skins != nullptr && skins->is_object()) {
            for (auto& skin : skins->as_object()) {
                if (!skin.second.is_object()) {
                    continue;
                }
                skin.second.as_object().erase(layer.current_slot_name);
            }
        }
    }

    if (!dropped_slots.empty()) {
        const std::string text = runtime::json::serialize_pretty(staged_document_.root);
        const std::string write_error = detail::write_file_atomically(
            plan_.staged_skeleton_path, text, "staged skeleton");
        if (!write_error.empty()) {
            fail(PsdCommitStep::PruneUnpreserved, write_error);
            return false;
        }
    }
    return advance(PsdCommitStep::PruneUnpreserved);
}

bool CommitRun::validate_staged_bundle() {
    // The staged document is re-read from disk rather than reused from the prune
    // step, so what is validated is what will be placed.
    const runtime::json::LoadResult document =
        runtime::load_skeleton_document(plan_.staged_skeleton_path);
    if (!document) {
        fail(PsdCommitStep::ValidateStagedBundle,
             "the staged skeleton did not load: " + document.error->format());
        return false;
    }
    const runtime::AtlasDataResult staged_atlas =
        runtime::AtlasLoader::load(plan_.staged_atlas_path);
    if (!staged_atlas) {
        fail(PsdCommitStep::ValidateStagedBundle,
             "the staged atlas did not load: " + staged_atlas.error->format());
        return false;
    }
    for (const std::filesystem::path& atlas_path : other_atlases_) {
        const runtime::AtlasDataResult other = runtime::AtlasLoader::load(atlas_path);
        if (!other) {
            fail(PsdCommitStep::ValidateStagedBundle,
                 "a project atlas did not load: " + other.error->format());
            return false;
        }
    }

    ProjectData candidate = *session_.project();
    ProjectImportSources sources = candidate.editor_metadata.import_sources.value_or(
        ProjectImportSources{});
    sources.psd = proposed_provenance_;
    candidate.editor_metadata.import_sources = std::move(sources);

    if (session_.runtime_data() != nullptr) {
        const AuthoringResult extension =
            auto_extend_explicit_animation_durations(&candidate, *session_.runtime_data());
        if (!extension) {
            fail(PsdCommitStep::ValidateStagedBundle,
                 "duration auto-extension refused the staged bundle: " + extension.error);
            return false;
        }
    }

    const ProjectRuntimeResult runtime_result =
        build_project_runtime(candidate, *document.document);
    if (!runtime_result) {
        fail(PsdCommitStep::ValidateStagedBundle,
             "the staged bundle does not build with the project's overlays: " +
                 runtime_result.error->message);
        return false;
    }
    return advance(PsdCommitStep::ValidateStagedBundle);
}

bool CommitRun::open_journal() {
    const std::string identifier = journal_identifier();
    journal_.manifest_path =
        project_directory_ / (".marrow-psd-journal-" + identifier + ".json");

    // The staged source is stored HERE, beside its target, rather than looked up
    // from the step at placement time. A step-to-path mapping written twice is a
    // mapping that can disagree with itself, and the disagreement would be a
    // byte-perfect file in the wrong place.
    const std::vector<JournalEntry> planned = {
        {PsdCommitStep::BackupLayers, PsdCommitStep::PlaceLayers, target_layers_, {},
         plan_.staged_layers_directory, true, false, false, false},
        {PsdCommitStep::BackupTexture, PsdCommitStep::PlaceTexture, target_texture_, {},
         plan_.staged_texture_path, false, false, false, false},
        {PsdCommitStep::BackupAtlas, PsdCommitStep::PlaceAtlas, target_atlas_, {},
         plan_.staged_atlas_path, false, false, false, false},
        {PsdCommitStep::BackupSkeleton, PsdCommitStep::PlaceSkeleton, target_skeleton_, {},
         plan_.staged_skeleton_path, false, false, false, false},
    };
    Value::Array targets;
    for (const JournalEntry& source : planned) {
        JournalEntry record = source;
        record.backup = record.target;
        record.backup += ".marrow-journal-" + identifier + ".bak";
        journal_.entries.push_back(record);

        Value::Object described;
        described.emplace("target", make_string(record.target.generic_string()));
        described.emplace("backup", make_string(record.backup.generic_string()));
        described.emplace("kind", make_string(record.directory ? "directory" : "file"));
        targets.push_back(Value(std::move(described), {}));
    }
    Value::Object root;
    root.emplace("journal", make_string(identifier));
    root.emplace("targets", Value(std::move(targets), {}));

    const std::string write_error = detail::write_file_atomically(
        journal_.manifest_path,
        runtime::json::serialize_pretty(Value(std::move(root), {})),
        "psd reimport journal");
    if (!write_error.empty()) {
        fail(PsdCommitStep::OpenJournal, write_error);
        return false;
    }
    journal_.manifest_written = true;
    return advance(PsdCommitStep::OpenJournal);
}

bool CommitRun::backup(JournalEntry& entry) {
    std::error_code error;
    if (!std::filesystem::exists(entry.target, error)) {
        // Recorded, not skipped. Restoring an absent target means DELETING what
        // replaced it, and a rollback cannot tell the two apart from the target's
        // own state at rollback time -- by then it exists either way.
        entry.absent = true;
        entry.backed_up = true;
        return advance(entry.backup_step);
    }
    std::filesystem::rename(entry.target, entry.backup, error);
    if (error) {
        fail(entry.backup_step,
             "could not move '" + entry.target.generic_string() + "' aside: " + error.message());
        return false;
    }
    entry.backed_up = true;
    return advance(entry.backup_step);
}

bool CommitRun::place(JournalEntry& entry) {
    const std::filesystem::path& source = entry.staged;

    if (entry.directory) {
        std::string listing_error;
        const std::vector<std::filesystem::path> files =
            relative_files(source, &listing_error);
        if (!listing_error.empty()) {
            fail(entry.place_step, listing_error);
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(entry.target, error);
        if (error) {
            fail(entry.place_step,
                 "could not create '" + entry.target.generic_string() + "': " + error.message());
            return false;
        }
        entry.placed = true;
        for (const std::filesystem::path& relative : files) {
            bool read_ok = false;
            const std::string bytes = read_all_bytes(source / relative, &read_ok);
            if (!read_ok) {
                fail(entry.place_step,
                     "could not read staged layer '" + relative.generic_string() + "'");
                return false;
            }
            const std::filesystem::path destination = entry.target / relative;
            std::filesystem::create_directories(destination.parent_path(), error);
            const std::string write_error =
                detail::write_file_atomically(destination, bytes, "extracted layer");
            if (!write_error.empty()) {
                fail(entry.place_step, write_error);
                return false;
            }
        }
        return advance(entry.place_step);
    }

    bool read_ok = false;
    const std::string bytes = read_all_bytes(source, &read_ok);
    if (!read_ok) {
        fail(entry.place_step, "could not read '" + source.generic_string() + "'");
        return false;
    }
    // Through `write_file_atomically`, not `std::filesystem::copy`: the staging
    // root is caller-supplied and may be on another volume, and this creates its
    // temporary beside the DESTINATION so the rename is always local. It writes
    // by length with `fwrite`, so it is binary-safe for the PNG.
    const std::string write_error = detail::write_file_atomically(
        entry.target, bytes, entry.target.filename().generic_string());
    if (!write_error.empty()) {
        fail(entry.place_step, write_error);
        return false;
    }
    entry.placed = true;
    return advance(entry.place_step);
}

bool CommitRun::adopt_runtime_sources() {
    const SessionResult adopted = session_.adopt_runtime_sources();
    if (!adopted) {
        fail(PsdCommitStep::AdoptRuntimeSources, adopted.error->format());
        return false;
    }
    return advance(PsdCommitStep::AdoptRuntimeSources);
}

bool CommitRun::update_provenance() {
    if (!options_.update_provenance) {
        return advance(PsdCommitStep::UpdateProvenance);
    }
    EditDescriptor descriptor;
    descriptor.kind = EditKind::EditProperty;
    descriptor.label = "Update PSD import provenance";
    descriptor.impacts = EditImpact::Project;
    EditorSession::EditTransaction transaction = session_.begin_edit(descriptor);
    if (!transaction) {
        fail(PsdCommitStep::UpdateProvenance, transaction.error()->format());
        return false;
    }
    ProjectImportSources sources =
        transaction.project()->editor_metadata.import_sources.value_or(ProjectImportSources{});
    sources.psd = proposed_provenance_;
    transaction.project()->editor_metadata.import_sources = std::move(sources);
    const SessionResult committed = transaction.commit();
    if (!committed) {
        fail(PsdCommitStep::UpdateProvenance, committed.error->format());
        return false;
    }
    return advance(PsdCommitStep::UpdateProvenance);
}

void CommitRun::clean_journal() {
    for (const JournalEntry& entry : journal_.entries) {
        if (!entry.backed_up || entry.absent) {
            continue;
        }
        std::error_code error;
        if (entry.directory) {
            std::filesystem::remove_all(entry.backup, error);
        } else {
            std::filesystem::remove(entry.backup, error);
        }
        if (error) {
            result_.journal_residue.push_back(entry.backup);
        }
    }
    if (journal_.manifest_written) {
        std::error_code error;
        std::filesystem::remove(journal_.manifest_path, error);
        if (error) {
            result_.journal_residue.push_back(journal_.manifest_path);
        }
    }
}

bool CommitRun::rollback_advance(PsdCommitStep step) {
    result_.steps_rolled_back.push_back(step);
    const std::string injected = detail::consult_rollback_failpoint(step);
    if (injected.empty()) {
        return true;
    }
    if (result_.rollback_error.empty()) {
        result_.rollback_error = std::string("rollback of ") + psd_commit_step_name(step) +
            " failed: injected failure: " + injected;
    }
    return false;
}

void CommitRun::rollback() {
    // Reverse order, and driven entirely by what the journal RECORDS having been
    // done -- never by re-deriving a path from the plan. Removing what was placed
    // first, then renaming each original back, is what makes the restored bytes
    // the ORIGINAL bytes: the backup was never read, re-serialized or rewritten,
    // only moved.
    const auto executed = [this](PsdCommitStep step) {
        return std::find(
                   result_.steps_executed.begin(), result_.steps_executed.end(), step) !=
            result_.steps_executed.end();
    };
    const bool adoption_succeeded = executed(PsdCommitStep::AdoptRuntimeSources);

    // Reverse order starts HERE, with the last thing the commit did. A completed
    // `UpdateProvenance` is a committed edit in the session's own history, and the
    // files coming back does not take it out again -- the provenance would name
    // the candidate PSD while the bundle on disk is the original one.
    if (options_.update_provenance && executed(PsdCommitStep::UpdateProvenance)) {
        const SessionResult undone = session_.undo();
        if (!undone && result_.rollback_error.empty()) {
            result_.rollback_error = "the provenance edit could not be undone (" +
                undone.error->format() + "); reload the project";
        }
        if (!rollback_advance(PsdCommitStep::UpdateProvenance)) {
            return;
        }
    }

    for (auto entry = journal_.entries.rbegin(); entry != journal_.entries.rend(); ++entry) {
        if (!entry->placed) {
            continue;
        }
        std::error_code error;
        if (entry->directory) {
            std::filesystem::remove_all(entry->target, error);
        } else {
            std::filesystem::remove(entry->target, error);
        }
        if (error && result_.rollback_error.empty()) {
            result_.rollback_error = "could not remove the placed '" +
                entry->target.generic_string() + "': " + error.message();
        }
        if (!rollback_advance(entry->place_step)) {
            return;
        }
    }
    for (auto entry = journal_.entries.rbegin(); entry != journal_.entries.rend(); ++entry) {
        if (!entry->backed_up || entry->absent) {
            continue;
        }
        std::error_code error;
        std::filesystem::rename(entry->backup, entry->target, error);
        if (error && result_.rollback_error.empty()) {
            result_.rollback_error = "could not restore '" + entry->target.generic_string() +
                "' from its backup: " + error.message();
        }
        if (!rollback_advance(entry->backup_step)) {
            return;
        }
    }
    if (journal_.manifest_written) {
        std::error_code error;
        std::filesystem::remove(journal_.manifest_path, error);
        if (error && result_.rollback_error.empty()) {
            result_.rollback_error = "could not remove the journal manifest '" +
                journal_.manifest_path.generic_string() + "': " + error.message();
        }
        journal_.manifest_written = false;
        if (!rollback_advance(PsdCommitStep::OpenJournal)) {
            return;
        }
    }

    // Only after the files are back. The re-adopt reads exactly the bytes the
    // pre-commit session read, because they were restored by rename.
    //
    // `adopt_runtime_sources` refuses outright while an edit transaction is
    // active (`session.cpp:1901-1907`). The only step that opens one is
    // `UpdateProvenance`, and its transaction is destroyed before this runs --
    // `EditTransaction` is a local there and its failure path returns.
    if (adoption_succeeded) {
        const SessionResult readopted = session_.adopt_runtime_sources();
        if (!readopted && result_.rollback_error.empty()) {
            result_.rollback_error =
                "the files were restored but the session could not re-adopt them (" +
                readopted.error->format() + "); reload the project";
        }
        (void)rollback_advance(PsdCommitStep::AdoptRuntimeSources);
    }
}

PsdReimportCommitResult CommitRun::run() {
    const bool ok = validate_request() && prune_unpreserved() && validate_staged_bundle() &&
        open_journal() && backup(journal_.entries[0]) && backup(journal_.entries[1]) &&
        backup(journal_.entries[2]) && backup(journal_.entries[3]) &&
        place(journal_.entries[0]) && place(journal_.entries[1]) &&
        place(journal_.entries[2]) && place(journal_.entries[3]) &&
        adopt_runtime_sources() && update_provenance();
    if (!ok) {
        result_.rolled_back = true;
        rollback();
        return std::move(result_);
    }

    // Past the point of no return. `CleanJournal` removes the backups and the
    // manifest; a failure there leaves a correct, fully committed bundle plus
    // some residue, so it reports success with a populated residue list. A
    // uniform "any failure rolls back" would destroy a completed reimport here.
    clean_journal();
    result_.ok = true;
    // `advance`'s return is deliberately ignored HERE and only here. A failure
    // after the last step has nothing left to undo that would not itself destroy
    // a completed reimport, so the error is REPORTED (in `error`, with
    // `failed_step` set) and `ok` stays true. This branch is what inversion I12
    // mutates, and R3's CleanJournal row is the only arm of the sweep that
    // distinguishes it -- which is why the sweep is a table of per-step
    // expectations and not a uniform `!ok`.
    (void)advance(PsdCommitStep::CleanJournal);
    result_.ok = true;
    return std::move(result_);
}

} // namespace

PsdReimportCommitResult commit_psd_reimport(
    EditorSession& session,
    const PsdReimportPlan& plan,
    const PsdReimportCommitOptions& options) {
    CommitRun run(session, plan, options);
    return run.run();
}

} // namespace marrow::editor
