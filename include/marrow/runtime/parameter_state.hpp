#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime {

/**
 * @brief Composes external lip input and expression presets over Skeleton direct parameters.
 *
 * ParameterState is mutable playback state and is intentionally separate from AnimationState.
 * It must be created from, and applied to, the same immutable SkeletonData as its Skeleton.
 */
class ParameterState {
public:
    explicit ParameterState(std::shared_ptr<const SkeletonData> data);

    /// @brief Returns the immutable definitions used by this state.
    const std::shared_ptr<const SkeletonData>& data() const;

    /// @brief Starts or reactivates an expression by stable id.
    bool activate_expression(std::string_view id);
    /// @brief Applies the expression's restore/hold deactivation policy.
    bool deactivate_expression(std::string_view id);
    /// @brief Removes one expression immediately, including held expressions.
    bool clear_expression(std::string_view id);
    /// @brief Removes every active or held expression immediately.
    void clear_expressions();
    /// @brief Returns active and held expression ids in activation order.
    std::vector<std::string> active_expression_ids() const;

    /// @brief Sets the external amplitude input. Non-finite values are rejected.
    bool set_amplitude(double amplitude);
    /// @brief Alias for set_amplitude used by editor/runtime integrations.
    bool set_lip_amplitude(double amplitude) {
        return set_amplitude(amplitude);
    }
    /// @brief Returns the current amplitude input.
    double amplitude() const;
    /// @brief Sets the current external phoneme token.
    void set_phoneme(std::string_view phoneme);
    /// @brief Alias for set_phoneme used by editor/runtime integrations.
    void set_lip_phoneme(std::string_view phoneme) {
        set_phoneme(phoneme);
    }
    /// @brief Returns the current external phoneme token.
    std::string_view phoneme() const;

    /**
     * @brief Advances expression fades and lip envelope/smoothing filters.
     * @param delta_seconds Non-negative finite time step.
     * @return False when the time step is invalid; otherwise true.
     */
    bool update(double delta_seconds);

    /**
     * @brief Composes this state over one matching Skeleton's direct values.
     * @return False when the Skeleton uses different SkeletonData; otherwise true.
     */
    bool apply(Skeleton& skeleton) const;

    /// @brief Clears expressions, inputs, and filter history.
    void reset();

private:
    enum class ExpressionPhase {
        FadingIn,
        Active,
        FadingOut,
        Held,
    };

    struct ActiveExpression {
        std::size_t definition_index{0};
        std::uint64_t activation_order{0};
        double weight{0.0};
        ExpressionPhase phase{ExpressionPhase::FadingIn};
    };

    struct LipFilterState {
        double envelope{0.0};
        double smoothed{0.0};
    };

    std::shared_ptr<const SkeletonData> data_;
    std::vector<ActiveExpression> active_expressions_;
    std::vector<LipFilterState> lip_filter_states_;
    std::uint64_t next_activation_order_{1};
    double amplitude_{0.0};
    std::string phoneme_;
};

} // namespace marrow::runtime
