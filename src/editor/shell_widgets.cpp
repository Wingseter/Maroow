#include "shell_widgets.hpp"

#include <cstdio>
#include <string>

#include "imgui_internal.h"

#include "shell_theme.hpp"

namespace marrow::editor::shell::widgets {

namespace t = marrow::editor::shell::theme;

namespace {

void draw_icon(const IconRegistry& icons, Icon icon, float size,
               const ImVec4& tint) {
    const ImTextureID tex = icons.get(icon);
    if (tex != 0) {
        ImGui::ImageWithBg(
            tex, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1),
            ImVec4(0, 0, 0, 0), tint);
    } else {
        ImGui::Dummy(ImVec2(size, size));
    }
}

}  // namespace

// ── Ghost input ─────────────────────────────────────────────────────────────
void ghost_input_push() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          t::with_alpha(t::kSurfaceBright, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          t::with_alpha(t::kPrimaryContainer, 0.15f));
}

void ghost_input_pop() {
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive() || ImGui::IsItemFocused();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(rmin.x, rmax.y - 1.0f),
        ImVec2(rmax.x, rmax.y - 1.0f),
        active ? t::u32(t::kPrimaryContainer) : t::u32(t::kSurfaceHigh),
        2.0f);
    ImGui::PopStyleColor(3);
}

// ── Panel header strip ──────────────────────────────────────────────────────
void panel_head(const IconRegistry& icons, Icon icon, const char* title,
                const char* meta) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line = ImGui::GetTextLineHeight();
    const float pad_x = 12.0f;
    const float pad_y = 9.0f;
    const float h = line + pad_y * 2.0f;

    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), t::u32(t::kSurfaceDefault));

    ImGui::SetCursorScreenPos(ImVec2(p0.x + pad_x, p0.y + pad_y));
    ImGui::BeginGroup();
    draw_icon(icons, icon, line, t::kPrimary);
    ImGui::SameLine(0.0f, 8.0f);
    if (g_font_semibold) ImGui::PushFont(g_font_semibold);
    ImGui::TextColored(t::kSecondary, "%s", title);
    if (g_font_semibold) ImGui::PopFont();
    if (meta != nullptr && *meta != '\0') {
        const ImVec2 ts = ImGui::CalcTextSize(meta);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(
            ImVec2(p0.x + w - pad_x - ts.x, p0.y + pad_y));
        ImGui::TextColored(t::kFaint, "%s", meta);
    }
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h));
    ImGui::Dummy(ImVec2(w, 0.0f));
}

// ── Collapsible section header ──────────────────────────────────────────────
bool section_header(const char* label, const char* count, bool default_open) {
    ImGui::PushStyleColor(ImGuiCol_Header, t::kSurfaceDefault);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, t::kSurfaceHigh);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, t::kSurfaceHigh);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (default_open) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    std::string up(label);
    for (char& ch : up) ch = static_cast<char>(::toupper(ch));
    const bool open = ImGui::CollapsingHeader(up.c_str(), flags);
    ImGui::PopStyleColor(3);

    if (count != nullptr && *count != '\0') {
        const ImVec2 ts = ImGui::CalcTextSize(count);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x - ts.x - 4.0f);
        ImGui::TextColored(t::kFaint, "%s", count);
    }
    return open;
}

// ── Chip ────────────────────────────────────────────────────────────────────
void chip(const char* text, ChipTone tone, bool leading_dot) {
    ImVec4 bg, fg;
    switch (tone) {
        case ChipTone::Prim: bg = t::kAttrAgentBg;  fg = t::kPrimary;  break;
        case ChipTone::Warn: bg = t::kStateWarnBg;  fg = t::kStateWarn; break;
        case ChipTone::Err:  bg = t::kStateErrBg;   fg = t::kTertiary; break;
        default:             bg = t::kSurfaceHigh;  fg = t::kSecondary; break;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float pad_x = 8.0f, pad_y = 3.0f, dot_r = 3.0f;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float dot_w = leading_dot ? (dot_r * 2.0f + 6.0f) : 0.0f;
    const ImVec2 sz(ts.x + pad_x * 2.0f + dot_w, ts.y + pad_y * 2.0f);

    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), t::u32(bg),
                      t::kRadiusSm);
    float tx = p.x + pad_x;
    if (leading_dot) {
        dl->AddCircleFilled(
            ImVec2(p.x + pad_x + dot_r, p.y + sz.y * 0.5f), dot_r, t::u32(fg));
        tx += dot_r * 2.0f + 6.0f;
    }
    dl->AddText(ImVec2(tx, p.y + pad_y), t::u32(fg), text);
    ImGui::Dummy(sz);
}

// ── Segmented toggle ────────────────────────────────────────────────────────
bool seg_toggle(const char* id, const char* const* options, int count,
                int* current) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, t::kRadiusSm);
    for (int i = 0; i < count; ++i) {
        const bool sel = (*current == i);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              sel ? t::kSurfaceBright : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t::kSurfaceHigh);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t::kSurfaceBright);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              sel ? t::kOnSurface : t::kInactive);
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(options[i]) && !sel) {
            *current = i;
            changed = true;
        }
        ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

// ── Architectural bar slider ────────────────────────────────────────────────
bool bar_slider(const char* id, float* value, float vmin, float vmax,
                const char* fmt, float height) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (w <= 1.0f) {
        ImGui::PopID();
        return false;
    }
    ImGui::InvisibleButton("##bar", ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    bool changed = false;
    if (held && vmax > vmin) {
        const float mx = ImGui::GetIO().MousePos.x;
        float frac = (mx - p.x) / w;
        frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
        const float nv = vmin + frac * (vmax - vmin);
        if (nv != *value) {
            *value = nv;
            changed = true;
        }
    }
    const float span = (vmax > vmin) ? (vmax - vmin) : 1.0f;
    float frac = (*value - vmin) / span;
    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + height);
    dl->AddRectFilled(p, q, t::u32(t::kSurfaceHigh), t::kRadiusSm);
    dl->AddRectFilled(p, ImVec2(p.x + frac * w, q.y),
                      t::u32(t::kPrimaryContainer, 0.25f), t::kRadiusSm);
    const float bx = p.x + frac * w;
    dl->AddRectFilled(ImVec2(bx - 1.0f, p.y - 2.0f),
                      ImVec2(bx + 1.0f, q.y + 2.0f),
                      t::u32(hovered || held ? t::kPrimary : t::kPrimary,
                             hovered || held ? 1.0f : 0.85f));
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *value);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(q.x - ts.x - 8.0f, p.y + (height - ts.y) * 0.5f),
                t::u32(t::kFaint), buf);
    ImGui::PopID();
    return changed;
}

// ── Data field ──────────────────────────────────────────────────────────────
void data_field(const char* label, const char* value, bool editable,
                bool focus) {
    const float h = ImGui::GetTextLineHeight() + 10.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 4.0f, p.y + 5.0f));
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImGui::TextColored(t::kFaint, "%s", label);
    if (g_font_small) ImGui::PopFont();

    ImFont* mono = g_font_mono ? g_font_mono : ImGui::GetFont();
    ImGui::PushFont(mono);
    const ImVec2 vs = ImGui::CalcTextSize(value);
    dl->AddText(ImVec2(p.x + w - vs.x - 6.0f, p.y + 5.0f),
                t::u32(editable ? t::kOnSurface : t::kSecondary), value);
    ImGui::PopFont();

    // underline: editable => faint, focus => primary, readonly => none
    if (editable || focus) {
        dl->AddLine(ImVec2(p.x, p.y + h - 1.0f),
                    ImVec2(p.x + w, p.y + h - 1.0f),
                    focus ? t::u32(t::kPrimary)
                          : t::u32(t::kOutlineFaint),
                    2.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, 0.0f));
    ImGui::EndGroup();
}

// ── Context line ────────────────────────────────────────────────────────────
void context_line(const IconRegistry& icons, Icon icon,
                   const ImVec4& icon_color, const char* primary,
                   const char* detail) {
    draw_icon(icons, icon, ImGui::GetTextLineHeight(), icon_color);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(t::kOnSurface, "%s", primary);
    if (detail != nullptr && *detail != '\0') {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(t::kFaint, "%s", detail);
    }
}

// ── Empty hero ──────────────────────────────────────────────────────────────
void empty_hero(const char* eyebrow, const char* headline, const char* body) {
    ImGui::Dummy(ImVec2(0.0f, 24.0f));
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImGui::TextColored(t::kFaint, "%s", eyebrow);
    if (g_font_small) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    if (g_font_display) ImGui::PushFont(g_font_display);
    ImGui::PushStyleColor(ImGuiCol_Text, t::kOnSurface);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.0f);
    ImGui::TextWrapped("%s", headline);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (g_font_display) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, t::kInactive);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.0f);
    ImGui::TextWrapped("%s", body);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}  // namespace marrow::editor::shell::widgets
