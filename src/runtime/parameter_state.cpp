#include "marrow/runtime/parameter_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace marrow::runtime {
namespace {

double filter_alpha(double delta_seconds, double time_constant) {
    if (time_constant == 0.0) {
        return 1.0;
    }
    return 1.0 - std::exp(-delta_seconds / time_constant);
}

double phoneme_input(
    const LipSyncMappingDefinition& mapping,
    std::string_view phoneme) {
    const auto found = std::find_if(
        mapping.phoneme_map.begin(),
        mapping.phoneme_map.end(),
        [&](const PhonemeValueDefinition& entry) {
            return entry.phoneme == phoneme;
        });
    return found == mapping.phoneme_map.end() ? 0.0 : found->value;
}

} // namespace

ParameterState::ParameterState(std::shared_ptr<const SkeletonData> data)
    : data_(std::move(data)) {
    if (data_ == nullptr) {
        throw std::invalid_argument("ParameterState requires SkeletonData");
    }
    lip_filter_states_.resize(data_->lip_sync().mappings.size());
}

const std::shared_ptr<const SkeletonData>& ParameterState::data() const {
    return data_;
}

bool ParameterState::activate_expression(std::string_view id) {
    const std::optional<std::size_t> definition_index = data_->find_expression_index(id);
    if (!definition_index.has_value()) {
        return false;
    }

    const auto existing = std::find_if(
        active_expressions_.begin(),
        active_expressions_.end(),
        [&](const ActiveExpression& expression) {
            return expression.definition_index == *definition_index;
        });
    if (existing != active_expressions_.end()) {
        if (existing->phase == ExpressionPhase::Active ||
            existing->phase == ExpressionPhase::FadingIn) {
            return true;
        }
        existing->activation_order = next_activation_order_++;
        if (data_->expressions()[*definition_index].duration == 0.0) {
            existing->weight = 1.0;
            existing->phase = ExpressionPhase::Active;
        } else {
            existing->phase = ExpressionPhase::FadingIn;
        }
        return true;
    }

    ActiveExpression expression;
    expression.definition_index = *definition_index;
    expression.activation_order = next_activation_order_++;
    if (data_->expressions()[*definition_index].duration == 0.0) {
        expression.weight = 1.0;
        expression.phase = ExpressionPhase::Active;
    }
    active_expressions_.push_back(expression);
    return true;
}

bool ParameterState::deactivate_expression(std::string_view id) {
    const std::optional<std::size_t> definition_index = data_->find_expression_index(id);
    if (!definition_index.has_value()) {
        return false;
    }
    const auto existing = std::find_if(
        active_expressions_.begin(),
        active_expressions_.end(),
        [&](const ActiveExpression& expression) {
            return expression.definition_index == *definition_index;
        });
    if (existing == active_expressions_.end()) {
        return false;
    }

    const ExpressionDefinition& definition = data_->expressions()[*definition_index];
    if (definition.reset_policy == ExpressionResetPolicy::Hold) {
        existing->phase = ExpressionPhase::Held;
        return true;
    }
    if (definition.duration == 0.0) {
        active_expressions_.erase(existing);
        return true;
    }
    existing->phase = ExpressionPhase::FadingOut;
    return true;
}

bool ParameterState::clear_expression(std::string_view id) {
    const std::optional<std::size_t> definition_index = data_->find_expression_index(id);
    if (!definition_index.has_value()) {
        return false;
    }
    const auto before = active_expressions_.size();
    active_expressions_.erase(
        std::remove_if(
            active_expressions_.begin(),
            active_expressions_.end(),
            [&](const ActiveExpression& expression) {
                return expression.definition_index == *definition_index;
            }),
        active_expressions_.end());
    return active_expressions_.size() != before;
}

void ParameterState::clear_expressions() {
    active_expressions_.clear();
}

std::vector<std::string> ParameterState::active_expression_ids() const {
    std::vector<const ActiveExpression*> ordered;
    ordered.reserve(active_expressions_.size());
    for (const ActiveExpression& expression : active_expressions_) {
        ordered.push_back(&expression);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const ActiveExpression* lhs, const ActiveExpression* rhs) {
            return lhs->activation_order < rhs->activation_order;
        });

    std::vector<std::string> ids;
    ids.reserve(ordered.size());
    for (const ActiveExpression* expression : ordered) {
        ids.push_back(data_->expressions()[expression->definition_index].id);
    }
    return ids;
}

bool ParameterState::set_amplitude(double amplitude) {
    if (!std::isfinite(amplitude)) {
        return false;
    }
    amplitude_ = amplitude;
    return true;
}

double ParameterState::amplitude() const {
    return amplitude_;
}

void ParameterState::set_phoneme(std::string_view phoneme) {
    phoneme_.assign(phoneme.begin(), phoneme.end());
}

std::string_view ParameterState::phoneme() const {
    return phoneme_;
}

bool ParameterState::update(double delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) {
        return false;
    }

    for (ActiveExpression& active : active_expressions_) {
        const double duration = data_->expressions()[active.definition_index].duration;
        if (active.phase == ExpressionPhase::FadingIn) {
            if (duration == 0.0) {
                active.weight = 1.0;
            } else {
                active.weight = std::min(1.0, active.weight + delta_seconds / duration);
            }
            if (active.weight >= 1.0) {
                active.phase = ExpressionPhase::Active;
            }
        } else if (active.phase == ExpressionPhase::FadingOut) {
            if (duration == 0.0) {
                active.weight = 0.0;
            } else {
                active.weight = std::max(0.0, active.weight - delta_seconds / duration);
            }
        }
    }
    active_expressions_.erase(
        std::remove_if(
            active_expressions_.begin(),
            active_expressions_.end(),
            [](const ActiveExpression& expression) {
                return expression.phase == ExpressionPhase::FadingOut &&
                    expression.weight <= 0.0;
            }),
        active_expressions_.end());

    const std::vector<LipSyncMappingDefinition>& mappings = data_->lip_sync().mappings;
    for (std::size_t index = 0; index < mappings.size(); ++index) {
        const LipSyncMappingDefinition& mapping = mappings[index];
        LipFilterState& filter = lip_filter_states_[index];
        const double source = mapping.source == LipSyncSource::Amplitude
            ? amplitude_
            : phoneme_input(mapping, phoneme_);
        const double target = source * mapping.scale + mapping.bias;
        const double envelope_tau = target > filter.envelope ? mapping.attack : mapping.release;
        const double envelope_alpha = filter_alpha(delta_seconds, envelope_tau);
        filter.envelope += (target - filter.envelope) * envelope_alpha;
        const double smoothing_alpha = filter_alpha(delta_seconds, mapping.smoothing);
        filter.smoothed += (filter.envelope - filter.smoothed) * smoothing_alpha;
    }
    return true;
}

bool ParameterState::apply(Skeleton& skeleton) const {
    if (skeleton.data() != data_) {
        return false;
    }

    std::vector<double> composed = skeleton.direct_parameter_values();
    const std::vector<LipSyncMappingDefinition>& mappings = data_->lip_sync().mappings;
    for (std::size_t index = 0; index < mappings.size(); ++index) {
        if (mappings[index].parameter_index.has_value()) {
            composed[*mappings[index].parameter_index] = lip_filter_states_[index].smoothed;
        }
    }

    std::vector<const ActiveExpression*> ordered;
    ordered.reserve(active_expressions_.size());
    for (const ActiveExpression& expression : active_expressions_) {
        ordered.push_back(&expression);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [&](const ActiveExpression* lhs, const ActiveExpression* rhs) {
            const ExpressionDefinition& lhs_definition =
                data_->expressions()[lhs->definition_index];
            const ExpressionDefinition& rhs_definition =
                data_->expressions()[rhs->definition_index];
            if (lhs_definition.priority != rhs_definition.priority) {
                return lhs_definition.priority < rhs_definition.priority;
            }
            return lhs->activation_order < rhs->activation_order;
        });

    for (const ActiveExpression* active : ordered) {
        const ExpressionDefinition& definition =
            data_->expressions()[active->definition_index];
        for (const ExpressionTargetDefinition& target : definition.targets) {
            if (!target.parameter_index.has_value()) {
                continue;
            }
            double& value = composed[*target.parameter_index];
            if (definition.blend == ExpressionBlend::Additive) {
                value += target.value * active->weight;
            } else {
                value += (target.value - value) * active->weight;
            }
        }
    }
    return skeleton.apply_composed_parameter_values(composed);
}

void ParameterState::reset() {
    active_expressions_.clear();
    next_activation_order_ = 1U;
    amplitude_ = 0.0;
    phoneme_.clear();
    std::fill(lip_filter_states_.begin(), lip_filter_states_.end(), LipFilterState{});
}

} // namespace marrow::runtime
