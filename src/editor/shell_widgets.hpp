// Charcoal Studio v2 — reusable ImGui widget atoms.
//
// These map the design bundle's CSS atoms (.panel-head, .section-head, .chip,
// .seg-toggle, .slider, .data-field, .ghost-input, .empty-hero) onto immediate
// mode so every panel composes the same vocabulary. Colors come from
// shell_theme.hpp; nothing here hard-codes ImVec4 literals.
#ifndef MARROW_EDITOR_SHELL_WIDGETS_HPP
#define MARROW_EDITOR_SHELL_WIDGETS_HPP

#include "imgui.h"

#include "icon_registry.hpp"

namespace marrow::editor::shell::widgets {

// ── Ghost input (transparent frame + 2px underline that lights on focus) ────
// Call push() immediately before the input widget, pop() immediately after.
// pop() draws the underline from the just-submitted item's rect, so it must be
// the next call after the widget. Replaces the duplicated inline pattern.
void ghost_input_push();
void ghost_input_pop();

// ── Panel header strip (icon + title + optional right-aligned meta) ─────────
// Drawn as a full-width tonal band (surface_default) inside the panel body.
void panel_head(
    const IconRegistry& icons,
    Icon icon,
    const char* title,
    const char* meta = nullptr);

// ── Collapsible section header (uppercase title + right count) ──────────────
// Returns true when expanded. Mirrors .section-head.
bool section_header(const char* label, const char* count = nullptr,
                    bool default_open = true);

// ── Chip / badge pill ──────────────────────────────────────────────────────
enum class ChipTone { Neutral, Prim, Warn, Err };
void chip(const char* text, ChipTone tone = ChipTone::Neutral,
          bool leading_dot = false);

// ── Segmented toggle (one active of N) ─────────────────────────────────────
// Returns true when selection changed; *current updated in place.
bool seg_toggle(const char* id, const char* const* options, int count,
                int* current);

// ── Architectural slider (track + fill + 2px primary bar + right label) ────
// Returns true while being edited. Float value in [vmin, vmax].
bool bar_slider(const char* id, float* value, float vmin, float vmax,
                const char* fmt = "%.2f", float height = 22.0f);

// ── Data field (label + value, underline encodes editability) ──────────────
// Display unit for dense numeric readouts: editable => faint underline,
// readonly => none, focus => primary underline. Pure display (no editing).
void data_field(const char* label, const char* value, bool editable,
                bool focus = false);

// ── Persistent context line (icon + primary text + dim trailing detail) ────
void context_line(
    const IconRegistry& icons,
    Icon icon,
    const ImVec4& icon_color,
    const char* primary,
    const char* detail = nullptr);

// ── Editorial empty state (eyebrow + large headline + body) ────────────────
void empty_hero(const char* eyebrow, const char* headline, const char* body);

}  // namespace marrow::editor::shell::widgets

#endif  // MARROW_EDITOR_SHELL_WIDGETS_HPP
