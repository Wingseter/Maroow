// Charcoal Studio v2 — single source of truth for editor design tokens.
//
// Every color the editor paints should come from here (not inline ImVec4
// literals) so the visual language stays consistent across all panels. Values
// mirror docs/design tokens.css; RGBA(0-255) shown in comments, stored as the
// ImGui 0..1 float form. C++17 inline variables keep this header-only.
#ifndef MARROW_EDITOR_SHELL_THEME_HPP
#define MARROW_EDITOR_SHELL_THEME_HPP

#include "imgui.h"

namespace marrow::editor::shell::theme {

// Construct from 0-255 ints so the table reads like the design spec.
inline ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        a);
}

inline ImVec4 with_alpha(const ImVec4& c, float a) {
    return ImVec4(c.x, c.y, c.z, a);
}

inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImU32 u32(const ImVec4& c, float a) {
    return ImGui::ColorConvertFloat4ToU32(with_alpha(c, a));
}

// ── Tone tiers (canvas → panel → widget → active) ───────────────────────────
inline const ImVec4 kSurfaceLowest  = rgb(11, 14, 20);   // #0b0e14 docking empty, canvas floor
inline const ImVec4 kSurface        = rgb(16, 19, 25);   // #101319 main window bg
inline const ImVec4 kSurfaceLow     = rgb(25, 28, 34);   // #191c22 menubar, panel bg
inline const ImVec4 kSurfaceDefault = rgb(29, 32, 38);   // #1d2026 header bg
inline const ImVec4 kSurfaceHigh    = rgb(39, 42, 48);   // #272a30 button/input default
inline const ImVec4 kSurfaceHighest = rgb(50, 53, 59);   // #32353b scrollbar grab, glass base
inline const ImVec4 kSurfaceBright  = rgb(54, 57, 64);   // #363940 button hover (glow)
inline const ImVec4 kSurfaceCard    = rgb(34, 37, 43);   // #22252b read-only cards
inline const ImVec4 kSurfaceRail    = rgb(20, 23, 29);   // #14171d vertical rails

// ── Foreground text ─────────────────────────────────────────────────────────
inline const ImVec4 kOnSurface = rgb(225, 226, 234);     // #e1e2ea body
inline const ImVec4 kSecondary = rgb(190, 199, 220);     // #bec7dc labels
inline const ImVec4 kInactive  = rgb(141, 145, 153);     // #8d9199 disabled, captions
inline const ImVec4 kFaint     = rgb(100, 104, 113);     // #646871 meta, helper

// ── Point lights (blue) ─────────────────────────────────────────────────────
inline const ImVec4 kPrimary          = rgb(179, 197, 255); // #b3c5ff slider/icon/keyframe
inline const ImVec4 kPrimaryContainer = rgb(96, 139, 255);  // #608bff active, focus, CTA
inline const ImVec4 kPrimaryDim       = rgb(82, 105, 168);  // #5269a8 dim primary, ghost

// ── Point lights (red — moments only) ───────────────────────────────────────
inline const ImVec4 kTertiary    = rgb(255, 84, 80);     // #ff5450 playhead, dirty, destructive
inline const ImVec4 kTertiaryDim = rgb(168, 70, 67);     // #a84643 dim red, past states

// ── Borders (ghost only, never 100% opaque) ─────────────────────────────────
inline const ImVec4 kOutlineVariant = rgb(67, 70, 84, 0.70f); // #434654 @ 70%
inline const ImVec4 kOutlineFaint   = rgb(67, 70, 84, 0.35f); // #434654 @ 35%

// ── Semantic state ──────────────────────────────────────────────────────────
inline const ImVec4 kStateOk    = kPrimary;                 // ok = quiet primary, not green
inline const ImVec4 kStateOkBg  = rgb(96, 139, 255, 0.08f);
inline const ImVec4 kStateWarn  = rgb(212, 168, 110);       // #d4a86e amber, low chroma
inline const ImVec4 kStateWarnBg= rgb(212, 168, 110, 0.10f);
inline const ImVec4 kStateErr   = kTertiary;
inline const ImVec4 kStateErrBg = rgb(255, 84, 80, 0.10f);

// ── Agent attribution (quiet monitoring; primary at low alpha, no new hue) ──
inline const ImVec4 kAttrAgentGlow = rgb(96, 139, 255, 0.18f);
inline const ImVec4 kAttrAgentLine = rgb(179, 197, 255, 0.55f);
inline const ImVec4 kAttrAgentBg   = rgb(96, 139, 255, 0.05f);

// ── Mode environment washes (full-viewport tint at shell level) ─────────────
inline const ImVec4 kModeSetup     = ImVec4(0, 0, 0, 0);    // transparent
inline const ImVec4 kModeAnimation = rgb(96, 139, 255, 0.025f);
inline const ImVec4 kModePaint     = rgb(96, 139, 255, 0.05f);
inline const ImVec4 kModeAgent     = rgb(96, 139, 255, 0.04f);

// ── Radius / metrics (spec: sm 2px default, md 4px cards) ───────────────────
inline constexpr float kRadiusSm = 2.0f;
inline constexpr float kRadiusMd = 4.0f;

}  // namespace marrow::editor::shell::theme

namespace marrow::editor::shell {

// Type scale handles. g_font_regular stays the ImGui default (added first).
// The rest are optional accents; helpers fall back to regular when null.
extern ImFont* g_font_regular;   // body 15
extern ImFont* g_font_semibold;  // titles 15 semibold
extern ImFont* g_font_small;     // captions / labels ~12
extern ImFont* g_font_display;   // empty-state hero ~22 semibold
extern ImFont* g_font_mono;      // data scale (coords/time) ~14, mono if bundled

}  // namespace marrow::editor::shell

#endif  // MARROW_EDITOR_SHELL_THEME_HPP
