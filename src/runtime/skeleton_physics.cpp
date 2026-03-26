#include "skeleton_internal.hpp"

#include <algorithm>

namespace marrow::runtime {

void Skeleton::reset_physics_state() {
    pending_physics_delta_seconds_ = 0.0;
    physics_constraint_states_.clear();
    physics_constraint_states_.resize(data_->physics_constraints().size());
}

void Skeleton::reset_physics() {
    update_world_transforms(PhysicsMode::Reset);
}

void Skeleton::update_physics(double delta_seconds) {
    pending_physics_delta_seconds_ = std::max(0.0, delta_seconds);
    update_world_transforms(PhysicsMode::Update);
    pending_physics_delta_seconds_ = 0.0;
}

} // namespace marrow::runtime
