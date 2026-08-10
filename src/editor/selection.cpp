#include "marrow/editor/selection.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {
namespace {

std::optional<std::size_t> find_item_index(
    const std::vector<SelectionItem>& items,
    const SelectionItem& item) {
    const auto iterator = std::find(items.begin(), items.end(), item);
    if (iterator == items.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(items.begin(), iterator));
}

template <typename Constraint>
bool contains_named_constraint(
    const std::vector<Constraint>& constraints,
    const std::string& name) {
    return std::any_of(
        constraints.begin(),
        constraints.end(),
        [&](const Constraint& constraint) { return constraint.name == name; });
}

} // namespace

bool SelectionSet::replace(SelectionItem item) {
    if (items_.size() == 1U && active_index_ == 0U && items_.front() == item) {
        return false;
    }

    std::vector<SelectionItem> replacement;
    replacement.push_back(std::move(item));
    items_.swap(replacement);
    active_index_ = 0U;
    return true;
}

bool SelectionSet::toggle(SelectionItem item) {
    std::vector<SelectionItem> replacement = items_;
    std::optional<std::size_t> replacement_active = active_index_;
    const auto existing_index = find_item_index(replacement, item);
    if (!existing_index.has_value()) {
        replacement.push_back(std::move(item));
        replacement_active = replacement.size() - 1U;
    } else {
        replacement.erase(replacement.begin() + static_cast<std::ptrdiff_t>(*existing_index));
        if (replacement.empty()) {
            replacement_active.reset();
        } else if (*replacement_active == *existing_index) {
            replacement_active = replacement.size() - 1U;
        } else if (*replacement_active > *existing_index) {
            --(*replacement_active);
        }
    }

    items_.swap(replacement);
    active_index_ = replacement_active;
    return true;
}

bool SelectionSet::add_range(
    const std::vector<SelectionItem>& ordered_items,
    const SelectionItem& active_item) {
    if (!find_item_index(ordered_items, active_item).has_value()) {
        return false;
    }

    std::vector<SelectionItem> replacement = items_;
    for (const SelectionItem& item : ordered_items) {
        if (!find_item_index(replacement, item).has_value()) {
            replacement.push_back(item);
        }
    }

    const std::size_t replacement_active = *find_item_index(replacement, active_item);
    if (replacement == items_ && active_index_ == replacement_active) {
        return false;
    }

    items_.swap(replacement);
    active_index_ = replacement_active;
    return true;
}

bool SelectionSet::clear() noexcept {
    if (items_.empty()) {
        return false;
    }
    items_.clear();
    active_index_.reset();
    return true;
}

bool SelectionSet::contains(const SelectionItem& item) const {
    return find_item_index(items_, item).has_value();
}

const std::vector<SelectionItem>& SelectionSet::items() const noexcept {
    return items_;
}

const SelectionItem* SelectionSet::active() const noexcept {
    if (!active_index_.has_value() || *active_index_ >= items_.size()) {
        return nullptr;
    }
    return &items_[*active_index_];
}

const BoneSelection* SelectionSet::active_bone() const noexcept {
    const SelectionItem* item = active();
    return item == nullptr ? nullptr : std::get_if<BoneSelection>(item);
}

const SlotSelection* SelectionSet::active_slot() const noexcept {
    const SelectionItem* item = active();
    return item == nullptr ? nullptr : std::get_if<SlotSelection>(item);
}

const AttachmentSelection* SelectionSet::active_attachment() const noexcept {
    const SelectionItem* item = active();
    return item == nullptr ? nullptr : std::get_if<AttachmentSelection>(item);
}

const ConstraintSelection* SelectionSet::active_constraint() const noexcept {
    const SelectionItem* item = active();
    return item == nullptr ? nullptr : std::get_if<ConstraintSelection>(item);
}

bool SelectionSet::prune(const Predicate& predicate) {
    std::vector<SelectionItem> replacement;
    replacement.reserve(items_.size());
    std::optional<std::size_t> replacement_active;

    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!predicate(items_[index])) {
            continue;
        }
        if (active_index_ == index) {
            replacement_active = replacement.size();
        }
        replacement.push_back(items_[index]);
    }

    if (replacement.size() == items_.size()) {
        return false;
    }
    if (replacement.empty()) {
        replacement_active.reset();
    } else if (!replacement_active.has_value()) {
        replacement_active = replacement.size() - 1U;
    }

    items_.swap(replacement);
    active_index_ = replacement_active;
    return true;
}

bool SelectionSet::remap(
    const SelectionItem& from,
    std::optional<SelectionItem> to) {
    if (!contains(from)) {
        return false;
    }

    std::vector<SelectionItem> replacement;
    replacement.reserve(items_.size());
    std::optional<std::size_t> replacement_active;

    for (std::size_t index = 0; index < items_.size(); ++index) {
        std::optional<SelectionItem> mapped_item = items_[index] == from
            ? to
            : std::optional<SelectionItem>(items_[index]);
        if (!mapped_item.has_value()) {
            continue;
        }

        auto mapped_index = find_item_index(replacement, *mapped_item);
        if (!mapped_index.has_value()) {
            replacement.push_back(std::move(*mapped_item));
            mapped_index = replacement.size() - 1U;
        }
        if (active_index_ == index) {
            replacement_active = *mapped_index;
        }
    }

    if (replacement.empty()) {
        replacement_active.reset();
    } else if (!replacement_active.has_value()) {
        replacement_active = replacement.size() - 1U;
    }

    if (replacement == items_ && replacement_active == active_index_) {
        return false;
    }
    items_.swap(replacement);
    active_index_ = replacement_active;
    return true;
}

bool selection_item_exists(
    const SelectionItem& item,
    const runtime::SkeletonData& skeleton) {
    return std::visit(
        [&](const auto& selection) {
            using Selection = std::decay_t<decltype(selection)>;
            if constexpr (std::is_same_v<Selection, BoneSelection>) {
                return skeleton.find_bone_index(selection.bone_name).has_value();
            } else if constexpr (std::is_same_v<Selection, SlotSelection>) {
                return skeleton.find_slot_index(selection.slot_name).has_value();
            } else if constexpr (std::is_same_v<Selection, AttachmentSelection>) {
                const auto slot_index = skeleton.find_slot_index(selection.slot_name);
                const auto skin_index = skeleton.find_skin_index(selection.skin_name);
                return slot_index.has_value() && skin_index.has_value() &&
                    skeleton.find_attachment(
                        *skin_index,
                        *slot_index,
                        selection.attachment_name) != nullptr;
            } else {
                switch (selection.kind) {
                case ConstraintKind::Ik:
                    return contains_named_constraint(
                        skeleton.ik_constraints(), selection.constraint_name);
                case ConstraintKind::Path:
                    return contains_named_constraint(
                        skeleton.path_constraints(), selection.constraint_name);
                case ConstraintKind::Transform:
                    return contains_named_constraint(
                        skeleton.transform_constraints(), selection.constraint_name);
                case ConstraintKind::Physics:
                    return contains_named_constraint(
                        skeleton.physics_constraints(), selection.constraint_name);
                }
            }
            return false;
        },
        item);
}

bool reconcile_selection_to_runtime(
    SelectionSet& selection,
    const runtime::SkeletonData& skeleton) {
    return selection.prune(
        [&](const SelectionItem& item) {
            return selection_item_exists(item, skeleton);
        });
}

} // namespace marrow::editor
