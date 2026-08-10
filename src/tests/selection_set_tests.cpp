#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/selection.hpp"

namespace {

using marrow::editor::AttachmentSelection;
using marrow::editor::BoneSelection;
using marrow::editor::ConstraintKind;
using marrow::editor::ConstraintSelection;
using marrow::editor::SelectionItem;
using marrow::editor::SelectionSet;
using marrow::editor::SlotSelection;
using marrow::editor::reconcile_selection_to_runtime;
using marrow::editor::selection_item_exists;

class TestSuite {
public:
    template <typename Function>
    void run(std::string name, Function&& function) {
        current_case_ = std::move(name);
        const int failures_before = failures_;
        std::forward<Function>(function)();
        if (failures_ == failures_before) {
            std::cout << "PASS: " << current_case_ << '\n';
        } else {
            std::cout << "FAIL: " << current_case_ << '\n';
        }
        ++case_count_;
    }

    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << current_case_ << ": " << message << '\n';
        }
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "SelectionSet: " << case_count_ << " cases passed\n";
            return 0;
        }
        std::cerr << "SelectionSet: " << failures_ << " failure(s) across "
                  << case_count_ << " cases\n";
        return 1;
    }

private:
    std::string current_case_;
    int failures_{0};
    int case_count_{0};
};

SelectionItem bone(std::string name) {
    return BoneSelection{std::move(name)};
}

SelectionItem slot(std::string name) {
    return SlotSelection{std::move(name)};
}

SelectionItem attachment(
    std::string slot_name,
    std::string skin_name,
    std::string attachment_name) {
    return AttachmentSelection{
        std::move(slot_name),
        std::move(skin_name),
        std::move(attachment_name)};
}

SelectionItem constraint(ConstraintKind kind, std::string name) {
    return ConstraintSelection{kind, std::move(name)};
}

void expect_items(
    TestSuite& suite,
    const SelectionSet& selections,
    const std::vector<SelectionItem>& expected,
    std::string_view context) {
    suite.expect(selections.items() == expected, context);
    suite.expect(
        selections.items().empty() == (selections.active() == nullptr),
        "empty and active invariants must agree");
}

void test_scoped_typed_identity(TestSuite& suite) {
    const SelectionItem shared_bone = bone("shared");
    const SelectionItem case_bone = bone("Shared");
    const SelectionItem shared_slot = slot("shared");
    const SelectionItem body_default = attachment("body", "default", "image");
    const SelectionItem face_default = attachment("face", "default", "image");
    const SelectionItem body_mage = attachment("body", "mage", "image");
    const SelectionItem ik = constraint(ConstraintKind::Ik, "follow");
    const SelectionItem path = constraint(ConstraintKind::Path, "follow");
    const SelectionItem transform = constraint(ConstraintKind::Transform, "follow");
    const SelectionItem physics = constraint(ConstraintKind::Physics, "follow");
    const std::vector<SelectionItem> all{
        shared_bone,
        case_bone,
        shared_slot,
        body_default,
        face_default,
        body_mage,
        ik,
        path,
        transform,
        physics,
    };

    SelectionSet selections;
    suite.expect(selections.add_range(all, physics), "distinct typed identities should add");
    expect_items(suite, selections, all, "typed and scoped identities must not collide");
    for (const SelectionItem& item : all) {
        suite.expect(selections.contains(item), "every exact identity should be present");
    }
    suite.expect(
        selections.active_constraint() != nullptr &&
            selections.active_constraint()->kind == ConstraintKind::Physics,
        "constraint active accessor should preserve kind");
    suite.expect(selections.active_bone() == nullptr, "mixed active type should be exact");
}

void test_replace_toggle_and_clear(TestSuite& suite) {
    const SelectionItem root = bone("root");
    const SelectionItem body = slot("body");
    const SelectionItem image = attachment("body", "default", "body_image");

    SelectionSet selections;
    suite.expect(selections.replace(root), "replace should initialize the set");
    suite.expect(!selections.replace(root), "identical replace should be a no-op");
    suite.expect(
        selections.active_bone() != nullptr &&
            selections.active_bone()->bone_name == "root",
        "replace should activate its item");

    suite.expect(selections.toggle(body), "toggle should append a missing item");
    expect_items(suite, selections, {root, body}, "toggle should preserve insertion order");
    suite.expect(
        selections.active_slot() != nullptr && selections.active_slot()->slot_name == "body",
        "newly toggled item should become active");

    suite.expect(selections.toggle(root), "toggle should remove an existing non-active item");
    expect_items(suite, selections, {body}, "toggle removal should compact order");
    suite.expect(selections.active_slot() != nullptr, "non-active removal should preserve active");

    suite.expect(selections.toggle(image), "toggle should append another type");
    suite.expect(selections.toggle(image), "toggle should remove the active item");
    suite.expect(
        selections.active_slot() != nullptr,
        "active removal should fall back to the last remaining item");
    suite.expect(selections.clear(), "clear should report a change");
    expect_items(suite, selections, {}, "clear should remove every item");
    suite.expect(!selections.clear(), "clearing an empty set should be a no-op");
}

void test_range_add_order_and_active(TestSuite& suite) {
    const SelectionItem root = bone("root");
    const SelectionItem body = slot("body");
    const SelectionItem image = attachment("body", "default", "body_image");

    SelectionSet selections;
    selections.replace(root);
    suite.expect(
        selections.add_range({body, root, image, body}, image),
        "range add should accept an active member from the ordered input");
    expect_items(
        suite,
        selections,
        {root, body, image},
        "range add should neither move existing items nor add duplicates");
    suite.expect(selections.active_attachment() != nullptr, "range active should be applied");
    suite.expect(
        !selections.add_range({image, body}, image),
        "an unchanged range and active should be a no-op");

    suite.expect(
        selections.add_range({root}, root),
        "range add may activate an existing item without moving it");
    expect_items(
        suite,
        selections,
        {root, body, image},
        "active-only range changes must preserve order");
    suite.expect(selections.active_bone() != nullptr, "existing range member should activate");
}

void test_active_removal_fallback(TestSuite& suite) {
    const SelectionItem root = bone("root");
    const SelectionItem body = slot("body");
    const SelectionItem image = attachment("body", "default", "body_image");
    SelectionSet selections;
    selections.add_range({root, body, image}, body);

    selections.toggle(body);
    expect_items(
        suite,
        selections,
        {root, image},
        "removing a middle active item should preserve survivor order");
    suite.expect(
        selections.active_attachment() != nullptr,
        "active fallback should use the final insertion-order survivor");
}

void test_prune(TestSuite& suite) {
    const SelectionItem root = bone("root");
    const SelectionItem body = slot("body");
    const SelectionItem image = attachment("body", "default", "body_image");
    const SelectionItem follow = constraint(ConstraintKind::Ik, "follow");
    SelectionSet selections;
    selections.add_range({root, body, image, follow}, body);

    suite.expect(
        selections.prune([&](const SelectionItem& item) { return item != image; }),
        "prune should remove rejected members");
    expect_items(
        suite,
        selections,
        {root, body, follow},
        "prune should preserve survivor order");
    suite.expect(selections.active_slot() != nullptr, "prune should preserve a surviving active");

    suite.expect(
        selections.prune([&](const SelectionItem& item) { return item != body; }),
        "prune should remove the active member");
    suite.expect(
        selections.active_constraint() != nullptr,
        "pruned active should fall back to the final survivor");
    suite.expect(
        !selections.prune([](const SelectionItem&) { return true; }),
        "all-retained prune should be a no-op");
}

void test_remap_and_collisions(TestSuite& suite) {
    const SelectionItem old_root = bone("old_root");
    const SelectionItem new_root = bone("new_root");
    const SelectionItem body = slot("body");
    SelectionSet selections;
    selections.add_range({old_root, body}, old_root);

    suite.expect(selections.remap(old_root, new_root), "remap should rename in place");
    expect_items(suite, selections, {new_root, body}, "remap should preserve the source position");
    suite.expect(
        selections.active_bone() != nullptr &&
            selections.active_bone()->bone_name == "new_root",
        "active should follow a renamed item");
    suite.expect(selections.remap(body, std::nullopt), "null remap should delete");
    expect_items(suite, selections, {new_root}, "delete remap should compact order");

    const SelectionItem middle = slot("middle");
    SelectionSet earlier_source;
    earlier_source.add_range({old_root, middle, new_root}, new_root);
    suite.expect(
        earlier_source.remap(old_root, new_root),
        "remap should collapse a collision with a later target");
    expect_items(
        suite,
        earlier_source,
        {new_root, middle},
        "the original-order first mapped target should survive");
    suite.expect(
        earlier_source.active_bone() != nullptr,
        "an active colliding target should follow its survivor");

    SelectionSet later_source;
    later_source.add_range({new_root, middle, old_root}, old_root);
    suite.expect(
        later_source.remap(old_root, new_root),
        "remap should collapse a collision with an earlier target");
    expect_items(
        suite,
        later_source,
        {new_root, middle},
        "an earlier original target should remain first");
    suite.expect(
        later_source.active_bone() != nullptr &&
            later_source.active_bone()->bone_name == "new_root",
        "an active source should follow the surviving target");

    later_source.add_range({new_root, middle}, middle);
    suite.expect(later_source.remap(middle, std::nullopt), "active delete remap should change");
    suite.expect(
        later_source.active_bone() != nullptr,
        "active delete remap should fall back to the final survivor");
}

void test_atomic_invalid_and_no_ops(TestSuite& suite) {
    const SelectionItem root = bone("root");
    const SelectionItem body = slot("body");
    const SelectionItem missing = bone("missing");
    SelectionSet selections;
    selections.add_range({root, body}, body);
    const std::vector<SelectionItem> before_items = selections.items();
    const SelectionItem before_active = *selections.active();

    suite.expect(
        !selections.add_range({root, missing}, body),
        "range active absent from the ordered input should be rejected");
    expect_items(
        suite,
        selections,
        before_items,
        "invalid range active should leave all items unchanged");
    suite.expect(
        selections.active() != nullptr && *selections.active() == before_active,
        "invalid range active should leave active unchanged");
    suite.expect(
        !selections.remap(missing, root),
        "remapping an absent source should be a no-op");
    suite.expect(
        !selections.remap(root, root),
        "remapping an item to itself should be a no-op");
    expect_items(suite, selections, before_items, "remap no-ops should preserve state");
}

void test_constraint_remap_delete_and_collision(TestSuite& suite) {
    const ConstraintSelection old_ik{ConstraintKind::Ik, "old_follow"};
    const ConstraintSelection new_ik{ConstraintKind::Ik, "new_follow"};
    const SelectionItem body = slot("body");

    SelectionSet selections;
    selections.add_range({old_ik, body}, old_ik);
    suite.expect(
        selections.remap_constraint(old_ik, new_ik),
        "constraint remap should rename the exact source in place");
    expect_items(
        suite,
        selections,
        {new_ik, body},
        "constraint remap should preserve unrelated types and order");
    suite.expect(
        selections.active_constraint() != nullptr &&
            *selections.active_constraint() == new_ik,
        "active constraint should follow its renamed identity");

    SelectionSet collision;
    collision.add_range({new_ik, body, old_ik}, old_ik);
    suite.expect(
        collision.remap_constraint(old_ik, new_ik),
        "constraint remap should collapse an exact target collision");
    expect_items(
        suite,
        collision,
        {new_ik, body},
        "the first original constraint identity should survive a collision");
    suite.expect(
        collision.active_constraint() != nullptr &&
            *collision.active_constraint() == new_ik,
        "an active colliding source should follow the survivor");
    suite.expect(
        collision.remap_constraint(new_ik, std::nullopt),
        "null constraint remap should delete only that constraint");
    expect_items(
        suite,
        collision,
        {body},
        "constraint deletion should preserve unrelated selections");
}

void test_constraint_only_prune(TestSuite& suite) {
    const SelectionItem root = bone("shared");
    const SelectionItem shared_slot = slot("shared");
    const ConstraintSelection ik{ConstraintKind::Ik, "shared"};
    const ConstraintSelection path{ConstraintKind::Path, "shared"};
    const ConstraintSelection physics{ConstraintKind::Physics, "keep"};

    SelectionSet selections;
    selections.add_range({root, ik, shared_slot, path, physics}, path);
    suite.expect(
        selections.prune_constraints([](const ConstraintSelection& selection) {
            return selection.kind != ConstraintKind::Path;
        }),
        "constraint-only prune should remove a rejected kind and name pair");
    expect_items(
        suite,
        selections,
        {root, ik, shared_slot, physics},
        "constraint-only prune should preserve other types and constraint kinds");
    suite.expect(
        selections.active_constraint() != nullptr &&
            *selections.active_constraint() == physics,
        "removing the active constraint should use the last survivor fallback");
    suite.expect(
        !selections.prune_constraints([](const ConstraintSelection&) { return true; }),
        "an all-retained constraint prune should be a no-op");
}

void test_runtime_exact_identity_reconciliation(TestSuite& suite) {
    const auto project = marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    suite.expect(
        static_cast<bool>(project) && project.skeleton_data != nullptr,
        "runtime reconciliation fixture should load");
    if (!project || project.skeleton_data == nullptr) {
        return;
    }
    const auto& skeleton = *project.skeleton_data;
    const SelectionItem arm = bone("arm_l");
    const SelectionItem wrong_case_arm = bone("Arm_l");
    const SelectionItem body = slot("body");
    const SelectionItem mage_arm = attachment("arm_l", "mage", "mage_arm_l");
    const SelectionItem wrong_skin_arm =
        attachment("arm_l", "warrior", "mage_arm_l");
    const SelectionItem ik = constraint(ConstraintKind::Ik, "editor_arm_reach");
    const SelectionItem same_name_path =
        constraint(ConstraintKind::Path, "editor_arm_reach");

    suite.expect(selection_item_exists(arm, skeleton), "exact bone name should resolve");
    suite.expect(!selection_item_exists(wrong_case_arm, skeleton), "bone case must be exact");
    suite.expect(selection_item_exists(body, skeleton), "exact slot name should resolve");
    suite.expect(
        selection_item_exists(mage_arm, skeleton),
        "fully scoped attachment should resolve");
    suite.expect(
        !selection_item_exists(wrong_skin_arm, skeleton),
        "another skin must not substitute for an attachment identity");
    suite.expect(selection_item_exists(ik, skeleton), "exact constraint kind and name should resolve");
    suite.expect(
        !selection_item_exists(same_name_path, skeleton),
        "another constraint kind must not substitute for the same name");

    SelectionSet selections;
    selections.add_range(
        {arm, wrong_case_arm, body, mage_arm, wrong_skin_arm, ik, same_name_path},
        same_name_path);
    suite.expect(
        reconcile_selection_to_runtime(selections, skeleton),
        "runtime reconciliation should prune missing exact identities");
    expect_items(
        suite,
        selections,
        {arm, body, mage_arm, ik},
        "runtime reconciliation should preserve survivor insertion order");
    suite.expect(
        selections.active_constraint() != nullptr &&
            selections.active_constraint()->kind == ConstraintKind::Ik,
        "a removed active identity should fall back to the last survivor");
    suite.expect(
        !reconcile_selection_to_runtime(selections, skeleton),
        "a reconciled selection should be stable on repeat");
}

} // namespace

int main() {
    TestSuite suite;
    suite.run("Scoped typed identity", [&] { test_scoped_typed_identity(suite); });
    suite.run("Replace toggle and clear", [&] { test_replace_toggle_and_clear(suite); });
    suite.run("Range add order and active", [&] { test_range_add_order_and_active(suite); });
    suite.run("Active removal fallback", [&] { test_active_removal_fallback(suite); });
    suite.run("Prune", [&] { test_prune(suite); });
    suite.run("Remap and collisions", [&] { test_remap_and_collisions(suite); });
    suite.run("Atomic invalid and no-ops", [&] { test_atomic_invalid_and_no_ops(suite); });
    suite.run("Constraint remap delete and collision", [&] {
        test_constraint_remap_delete_and_collision(suite);
    });
    suite.run("Constraint-only prune", [&] { test_constraint_only_prune(suite); });
    suite.run("Runtime exact identity reconciliation", [&] {
        test_runtime_exact_identity_reconciliation(suite);
    });
    return suite.finish();
}
