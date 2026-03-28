#include "marrow/runtime/profiler.hpp"

#include <sstream>

namespace marrow::runtime {
namespace {

std::uint64_t seconds_to_microseconds(double seconds) {
    if (seconds <= 0.0) {
        return 0;
    }

    return static_cast<std::uint64_t>(seconds * 1.0e6);
}

std::string format_microseconds(std::uint64_t microseconds) {
    std::ostringstream stream;
    stream << microseconds << "US";
    return stream.str();
}

} // namespace

ProfilerCapture::ProfilerCapture(bool enabled)
    : enabled_(enabled) {}

bool ProfilerCapture::enabled() const {
    return enabled_;
}

void ProfilerCapture::begin_frame() {
    if (!enabled_) {
        return;
    }

    frame_ = {};
    frame_started_ = true;
    frame_start_ = Clock::now();
}

void ProfilerCapture::end_frame() {
    if (!enabled_ || !frame_started_) {
        return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - frame_start_).count();
    frame_.total_us = static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed);
    frame_started_ = false;
}

void ProfilerCapture::add_phase_microseconds(
    ProfilerPhase phase,
    std::uint64_t microseconds) {
    if (!enabled_ || microseconds == 0U) {
        return;
    }

    switch (phase) {
    case ProfilerPhase::Animation:
        frame_.animation_us += microseconds;
        break;
    case ProfilerPhase::Transform:
        frame_.transform_us += microseconds;
        break;
    case ProfilerPhase::Constraints:
        frame_.constraint_us += microseconds;
        break;
    case ProfilerPhase::Skinning:
        frame_.skinning_us += microseconds;
        break;
    case ProfilerPhase::Render:
        frame_.render_us += microseconds;
        break;
    }
}

void ProfilerCapture::add_world_transform_timing(
    const WorldTransformTimingBreakdown& timing_breakdown) {
    if (!enabled_) {
        return;
    }

    frame_.transform_us += seconds_to_microseconds(timing_breakdown.transform_seconds);
    frame_.constraint_us += seconds_to_microseconds(timing_breakdown.constraint_seconds);
}

void ProfilerCapture::add_draw_stats(const ProfilerDrawStats& draw_stats) {
    if (!enabled_) {
        return;
    }

    frame_.skeleton_count += draw_stats.skeleton_count;
    frame_.draw_calls += draw_stats.draw_calls;
    frame_.vertices += draw_stats.vertices;
    frame_.batch_merges += draw_stats.batch_merges;
    frame_.batch_break_reasons.texture_changes += draw_stats.break_reasons.texture_changes;
    frame_.batch_break_reasons.blend_changes += draw_stats.break_reasons.blend_changes;
    frame_.batch_break_reasons.clip_changes += draw_stats.break_reasons.clip_changes;
    frame_.batch_break_reasons.shader_changes += draw_stats.break_reasons.shader_changes;
}

ProfilerFrame marrow_profiler_frame(const ProfilerCapture& capture) {
    ProfilerFrame frame = capture.frame_;
    if (frame.total_us == 0U) {
        frame.total_us =
            frame.animation_us +
            frame.transform_us +
            frame.constraint_us +
            frame.skinning_us +
            frame.render_us;
    }
    return frame;
}

std::vector<std::string> profiler_hud_lines(const ProfilerFrame& frame) {
    std::vector<std::string> lines;

    std::ostringstream counts_stream;
    counts_stream << "SKELS " << frame.skeleton_count
                  << "  DRAWS " << frame.draw_calls
                  << "  VERTS " << frame.vertices
                  << "  MERGES " << frame.batch_merges;
    lines.push_back(counts_stream.str());

    std::ostringstream phase_stream;
    phase_stream << "FRAME " << format_microseconds(frame.total_us)
                 << "  ANIM " << format_microseconds(frame.animation_us)
                 << "  XFORM " << format_microseconds(frame.transform_us)
                 << "  CONSTR " << format_microseconds(frame.constraint_us);
    lines.push_back(phase_stream.str());

    std::ostringstream render_stream;
    render_stream << "SKIN " << format_microseconds(frame.skinning_us)
                  << "  RENDER " << format_microseconds(frame.render_us)
                  << "  BREAKS T" << frame.batch_break_reasons.texture_changes
                  << " B" << frame.batch_break_reasons.blend_changes
                  << " C" << frame.batch_break_reasons.clip_changes;
    lines.push_back(render_stream.str());

    return lines;
}

} // namespace marrow::runtime
