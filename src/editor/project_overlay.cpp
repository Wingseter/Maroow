#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include <algorithm>
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace {

void rename_animation_mixing_references(
    Value* root,
    std::string_view from,
    std::string_view to) {
    if (root == nullptr || !root->is_object()) {
        return;
    }
    Value* mixing = marrow::runtime::json::find_member(*root, "mixing");
    Value* entries = mixing != nullptr && mixing->is_object()
        ? marrow::runtime::json::find_member(*mixing, "entries")
        : nullptr;
    if (entries == nullptr || !entries->is_array()) {
        return;
    }
    for (Value& entry : entries->as_array()) {
        if (!entry.is_object()) {
            continue;
        }
        for (const char* key : {"from", "to"}) {
            Value* name = marrow::runtime::json::find_member(entry, key);
            if (name != nullptr && name->is_string() && name->as_string() == from) {
                name->as_string() = std::string(to);
            }
        }
    }
}

void remove_animation_mixing_references(Value* root, std::string_view animation_name) {
    if (root == nullptr || !root->is_object()) {
        return;
    }
    Value* mixing = marrow::runtime::json::find_member(*root, "mixing");
    Value* entries = mixing != nullptr && mixing->is_object()
        ? marrow::runtime::json::find_member(*mixing, "entries")
        : nullptr;
    if (entries == nullptr || !entries->is_array()) {
        return;
    }
    auto& values = entries->as_array();
    values.erase(
        std::remove_if(
            values.begin(),
            values.end(),
            [&](const Value& entry) {
                if (!entry.is_object()) {
                    return false;
                }
                const Value* from = find_optional_member(entry, "from");
                const Value* to = find_optional_member(entry, "to");
                return (from != nullptr && from->is_string() &&
                        from->as_string() == animation_name) ||
                    (to != nullptr && to->is_string() &&
                     to->as_string() == animation_name);
            }),
        values.end());
    if (values.empty()) {
        mixing->as_object().erase("entries");
    }
}

} // namespace

namespace project_detail {

/**
 * @brief Applies the ordered lifecycle records to a runtime document (Phase A).
 *
 * Runs over the four root constraint arrays *and* over every
 * `skins[*].<family>` name array, because skins reference constraints by name
 * and `parse_skin_scope_members()` fails the whole load on an unresolvable one.
 * A rename that missed a skin, or a delete that left one behind, would produce
 * a project that still saves and can never be opened again.
 *
 * `build_project_runtime_document()` is public and can be called without the
 * validator, so this is defensive: an unresolvable operation is a no-op and the
 * walk continues, exactly as the mesh-weight overlay skips a missing
 * attachment. Each operation either applies completely or not at all;
 * rejection is `validate_constraint_lifecycle_operations()`'s job, and both
 * real callers run it.
 */
void apply_constraint_lifecycle_operations(
    Value* root,
    const std::vector<ConstraintLifecycleOperation>& operations) {
    if (root == nullptr || !root->is_object() || operations.empty()) {
        return;
    }

    const auto is_named = [](const Value& element, const std::string& name) {
        const Value* member = find_optional_member(element, "name");
        return member != nullptr && member->is_string() && member->as_string() == name;
    };

    for (const ConstraintLifecycleOperation& operation : operations) {
        const std::string key(constraint_family_json_key(operation.family));
        Value* family_value = marrow::runtime::json::find_member(*root, key);
        if (family_value == nullptr || !family_value->is_array()) {
            continue;
        }
        Value::Array& elements = family_value->as_array();
        const auto source = std::find_if(
            elements.begin(), elements.end(), [&](const Value& element) {
                return is_named(element, operation.name);
            });
        if (source == elements.end()) {
            continue;
        }
        if (operation.kind == ConstraintLifecycleKind::Rename) {
            const bool target_taken = std::any_of(
                elements.begin(), elements.end(), [&](const Value& element) {
                    return &element != &*source && is_named(element, operation.new_name);
                });
            if (target_taken) {
                continue;
            }
            source->as_object()["name"] = make_string_value(operation.new_name);
        } else {
            elements.erase(source);
        }

        // Referrer 1: `skins[*].{ik,path,transform,physics}` are arrays of
        // constraint *names*. The runtime treats these six keys as skin scopes
        // rather than slots, so an array under the family key is a reference
        // list and never an attachment map.
        Value* skins = marrow::runtime::json::find_member(*root, "skins");
        if (skins != nullptr && skins->is_object()) {
            for (auto& skin_entry : skins->as_object()) {
                Value& skin_value = skin_entry.second;
                if (!skin_value.is_object()) {
                    continue;
                }
                Value* references = marrow::runtime::json::find_member(skin_value, key);
                if (references == nullptr || !references->is_array()) {
                    continue;
                }
                Value::Array& names = references->as_array();
                if (operation.kind == ConstraintLifecycleKind::Rename) {
                    for (Value& name_value : names) {
                        if (name_value.is_string() &&
                            name_value.as_string() == operation.name) {
                            name_value = make_string_value(operation.new_name);
                        }
                    }
                    continue;
                }
                names.erase(
                    std::remove_if(
                        names.begin(),
                        names.end(),
                        [&](const Value& name_value) {
                            return name_value.is_string() &&
                                name_value.as_string() == operation.name;
                        }),
                    names.end());
                if (names.empty()) {
                    skin_value.as_object().erase(key);
                }
            }
        }

        // A family array that is present but empty is a hard parse failure
        // ("<family> constraints must not be empty when provided"), so the last
        // delete in a family erases the key rather than leaving `[]`.
        if (operation.kind == ConstraintLifecycleKind::Delete && elements.empty()) {
            root->as_object().erase(key);
        }
    }
}

void apply_animation_edits(Value* root, const std::vector<AnimationEdit>& edits) {
    if (root == nullptr || !root->is_object() || edits.empty()) {
        return;
    }
    Value* animations = marrow::runtime::json::find_member(*root, "animations");
    if (animations == nullptr) {
        root->as_object().emplace("animations", make_object_value());
        animations = marrow::runtime::json::find_member(*root, "animations");
    }
    if (animations == nullptr) {
        return;
    }
    if (!animations->is_object()) {
        *animations = make_object_value();
    }

    for (const AnimationEdit& edit : edits) {
        switch (edit.kind) {
        case AnimationEditKind::Create:
            animations->as_object()[edit.name] = edit.animation.is_object()
                ? edit.animation
                : make_object_value();
            break;
        case AnimationEditKind::Rename: {
            auto source = animations->as_object().find(edit.name);
            if (source == animations->as_object().end()) {
                break;
            }
            Value animation_value = std::move(source->second);
            animations->as_object().erase(source);
            animations->as_object()[edit.new_name] = std::move(animation_value);
            rename_animation_mixing_references(root, edit.name, edit.new_name);
            break;
        }
        case AnimationEditKind::Delete:
            animations->as_object().erase(edit.name);
            remove_animation_mixing_references(root, edit.name);
            break;
        case AnimationEditKind::SetDuration: {
            Value* animation = marrow::runtime::json::find_member(*animations, edit.name);
            if (animation != nullptr && animation->is_object()) {
                animation->as_object()["duration"] = make_number_value(edit.duration);
            }
            break;
        }
        case AnimationEditKind::Unknown:
            break;
        }
    }
}

} // namespace project_detail

std::vector<std::string> authored_animation_names(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document) {
    // Reproduce exactly the state `build_runtime_document` is in after
    // `apply_animation_edits` and BEFORE the overlay merge loops call
    // `ensure_object_member(animations, edit.animation_name)`. Doing this by
    // calling the shared fold, rather than re-deriving it, is deliberate: the
    // fold is in this file's anonymous namespace and a second implementation
    // would drift the first time an `AnimationEditKind` is added, with no
    // compiler diagnostic to catch it.
    //
    // The whole root is copied, not just `animations`, because a `Rename` also
    // rewrites `mixing` references.
    Value root = base_skeleton_document.root;
    apply_animation_edits(&root, project.animation_edits);

    std::vector<std::string> names;
    const Value* animations = marrow::runtime::json::find_member(root, "animations");
    if (animations == nullptr || !animations->is_object()) {
        return names;
    }
    // `Value::Object` is a `std::map<std::string, Value, std::less<>>`, so this
    // walk is already in sorted key order; the sort below makes that a promise
    // of this function rather than a property of the container it happens to use.
    const auto& members = animations->as_object();
    names.reserve(members.size());
    for (const auto& entry : members) {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace marrow::editor
