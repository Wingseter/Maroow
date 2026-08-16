#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace marrow::runtime {
class SkeletonData;
}

namespace marrow::editor {

/** @brief Imported-rig constraint family used as part of a selection identity. */
enum class ConstraintKind {
    Ik,
    Path,
    Transform,
    Physics,
};

/** @brief Case-sensitive imported bone identity. */
struct BoneSelection {
    std::string bone_name;

    friend bool operator==(const BoneSelection& left, const BoneSelection& right) {
        return left.bone_name == right.bone_name;
    }
    friend bool operator!=(const BoneSelection& left, const BoneSelection& right) {
        return !(left == right);
    }
};

/** @brief Case-sensitive imported slot identity. */
struct SlotSelection {
    std::string slot_name;

    friend bool operator==(const SlotSelection& left, const SlotSelection& right) {
        return left.slot_name == right.slot_name;
    }
    friend bool operator!=(const SlotSelection& left, const SlotSelection& right) {
        return !(left == right);
    }
};

/** @brief Fully scoped case-sensitive attachment identity. */
struct AttachmentSelection {
    std::string slot_name;
    std::string skin_name;
    std::string attachment_name;

    friend bool operator==(
        const AttachmentSelection& left,
        const AttachmentSelection& right) {
        return left.slot_name == right.slot_name &&
            left.skin_name == right.skin_name &&
            left.attachment_name == right.attachment_name;
    }
    friend bool operator!=(
        const AttachmentSelection& left,
        const AttachmentSelection& right) {
        return !(left == right);
    }
};

/** @brief Constraint identity scoped by both family and authored name. */
struct ConstraintSelection {
    ConstraintKind kind{ConstraintKind::Ik};
    std::string constraint_name;

    friend bool operator==(
        const ConstraintSelection& left,
        const ConstraintSelection& right) {
        return left.kind == right.kind &&
            left.constraint_name == right.constraint_name;
    }
    friend bool operator!=(
        const ConstraintSelection& left,
        const ConstraintSelection& right) {
        return !(left == right);
    }
};

using SelectionItem = std::variant<
    BoneSelection,
    SlotSelection,
    AttachmentSelection,
    ConstraintSelection>;

/**
 * @brief UI-independent transient selection with stable insertion order.
 *
 * Members are unique by exact typed identity. A non-empty set always has one
 * active member. The model has no project, preview, history, or persistence
 * ownership; callers decide how names resolve against a current runtime.
 */
class SelectionSet {
public:
    using Predicate = std::function<bool(const SelectionItem&)>;

    /** @brief Replaces all members with one active item. */
    bool replace(SelectionItem item);
    /** @brief Appends and activates a missing item, or removes an existing item. */
    bool toggle(SelectionItem item);
    /**
     * @brief Adds missing members in input order and activates an input member.
     * @return False without mutation when `active_item` is absent from the input,
     * or when both membership and active item were already unchanged.
     */
    bool add_range(
        const std::vector<SelectionItem>& ordered_items,
        const SelectionItem& active_item);
    bool clear() noexcept;

    bool contains(const SelectionItem& item) const;
    const std::vector<SelectionItem>& items() const noexcept;
    const SelectionItem* active() const noexcept;
    const BoneSelection* active_bone() const noexcept;
    const SlotSelection* active_slot() const noexcept;
    const AttachmentSelection* active_attachment() const noexcept;
    const ConstraintSelection* active_constraint() const noexcept;

    /** @brief Retains members accepted by `predicate` without reordering them. */
    bool prune(const Predicate& predicate);
    /**
     * @brief Renames one exact identity in place, or removes it for `nullopt`.
     *
     * Target collisions retain the member that appeared first in the original
     * order, and an active source or target follows that surviving member.
     */
    bool remap(
        const SelectionItem& from,
        std::optional<SelectionItem> to);

private:
    std::vector<SelectionItem> items_;
    std::optional<std::size_t> active_index_;
};

/** @brief Tests one exact typed identity against immutable runtime data. */
bool selection_item_exists(
    const SelectionItem& item,
    const runtime::SkeletonData& skeleton);

/** @brief Drops only identities absent from an adopted runtime source. */
bool reconcile_selection_to_runtime(
    SelectionSet& selection,
    const runtime::SkeletonData& skeleton);

} // namespace marrow::editor
