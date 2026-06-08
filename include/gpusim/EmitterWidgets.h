#pragma once
//
//  gpusim/EmitterWidgets.h — Dear ImGui editors for the "custom" EmitterDesc
//  fields that a plain reflection/field loop can't render: the colour-over-life
//  gradient and the size-over-life curve, each with per-segment ease modes.
//
//  These edit the desc's stop/key arrays in place and return true when anything
//  changed, so the host can re-Configure() the emitter (the world then re-bakes
//  the lifetime LUT and the change shows next frame).
//
//  OPTIONAL header: it depends on Dear ImGui in addition to the core library.
//  The GpuSim library itself never includes it — only a host that already has
//  ImGui does (the lab here, or an engine inspector). This is the single source
//  for these widgets; DawnEngine's inspector adopts it in place of its inline
//  copies (Editor.cpp) when it migrates to consume the library.
//
#include <gpusim/EmitterDesc.h>   // GradientStop, CurveKey, EaseMode, EvalGradient/Curve, MAX_*
#include <imgui.h>
#include <cstdint>

namespace gpusim {

inline constexpr const char* kEaseNames[] = { "Linear", "Smooth", "EaseIn", "EaseOut", "Hold" };

// Colour-over-life gradient editor. Returns true if any value changed.
inline bool DrawColourGradientEditor(const char* label, GradientStop* stops, uint32_t& count)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);

    // gradient preview bar
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float  w  = ImGui::GetContentRegionAvail().x;
    const float  h  = 24.0f;
    const ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    const int SEG = 64;
    for (int i = 0; i < SEG; ++i) {
        float t0 = float(i) / SEG, t1 = float(i + 1) / SEG;
        glm::vec4 a = EvalGradient(stops, count, t0);
        glm::vec4 b = EvalGradient(stops, count, t1);
        ImU32 ca = ImGui::ColorConvertFloat4ToU32(ImVec4(a.r, a.g, a.b, a.a));
        ImU32 cb = ImGui::ColorConvertFloat4ToU32(ImVec4(b.r, b.g, b.b, b.a));
        dl->AddRectFilledMultiColor(ImVec2(p0.x + t0 * w, p0.y), ImVec2(p0.x + t1 * w, p1.y),
                                    ca, cb, cb, ca);
    }
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255));
    for (uint32_t i = 0; i < count; ++i) {
        float x = p0.x + stops[i].t * w;
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 220), 1.5f);
    }
    ImGui::Dummy(ImVec2(w, h));

    // per-stop rows (t clamped to neighbours so the array stays sorted)
    int removeIdx = -1;
    for (uint32_t i = 0; i < count; ++i) {
        ImGui::PushID((int)i);
        float lo = (i == 0) ? 0.f : stops[i - 1].t;
        float hi = (i + 1 >= count) ? 1.f : stops[i + 1].t;
        ImGui::SetNextItemWidth(80.f);
        changed |= ImGui::DragFloat("t", &stops[i].t, 0.005f, lo, hi);
        ImGui::SameLine();
        changed |= ImGui::ColorEdit4("##c", &stops[i].colour.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        int easeIdx = (int)stops[i].ease;
        if (ImGui::Combo("##ease", &easeIdx, kEaseNames, 5)) { stops[i].ease = (EaseMode)easeIdx; changed = true; }
        if (count > 1) { ImGui::SameLine(); if (ImGui::SmallButton("x")) removeIdx = (int)i; }
        ImGui::PopID();
    }
    if (removeIdx >= 0) { for (uint32_t j = removeIdx; j + 1 < count; ++j) stops[j] = stops[j + 1]; --count; changed = true; }

    if (count < (uint32_t)MAX_GRADIENT_STOPS && ImGui::SmallButton("+ Add Stop")) {
        uint32_t pos = count; float bestGap = -1.f, newT = 1.f;            // insert in widest gap
        for (uint32_t i = 0; i + 1 < count; ++i) {
            float g = stops[i + 1].t - stops[i].t;
            if (g > bestGap) { bestGap = g; pos = i + 1; newT = 0.5f * (stops[i].t + stops[i + 1].t); }
        }
        GradientStop ns{ newT, EvalGradient(stops, count, newT) };
        for (uint32_t j = count; j > pos; --j) stops[j] = stops[j - 1];
        stops[pos] = ns; ++count; changed = true;
    }
    ImGui::PopID();
    return changed;
}

// Size-over-life curve editor. Returns true if any value changed.
inline bool DrawSizeCurveEditor(const char* label, CurveKey* keys, uint32_t& count)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);

    float samples[64];
    float vmax = 0.001f;
    for (int i = 0; i < 64; ++i) { samples[i] = EvalCurve(keys, count, float(i) / 63.f); if (samples[i] > vmax) vmax = samples[i]; }
    ImGui::PlotLines("##curve", samples, 64, 0, nullptr, 0.f, vmax,
                     ImVec2(ImGui::GetContentRegionAvail().x, 60.f));

    int removeIdx = -1;
    for (uint32_t i = 0; i < count; ++i) {
        ImGui::PushID((int)i);
        float lo = (i == 0) ? 0.f : keys[i - 1].t;
        float hi = (i + 1 >= count) ? 1.f : keys[i + 1].t;
        ImGui::SetNextItemWidth(80.f);
        changed |= ImGui::DragFloat("t", &keys[i].t, 0.005f, lo, hi);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        changed |= ImGui::DragFloat("size", &keys[i].value, 0.01f, 0.f, 100.f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        int easeIdx = (int)keys[i].ease;
        if (ImGui::Combo("##ease", &easeIdx, kEaseNames, 5)) { keys[i].ease = (EaseMode)easeIdx; changed = true; }
        if (count > 1) { ImGui::SameLine(); if (ImGui::SmallButton("x")) removeIdx = (int)i; }
        ImGui::PopID();
    }
    if (removeIdx >= 0) { for (uint32_t j = removeIdx; j + 1 < count; ++j) keys[j] = keys[j + 1]; --count; changed = true; }

    if (count < (uint32_t)MAX_CURVE_KEYS && ImGui::SmallButton("+ Add Key")) {
        uint32_t pos = count; float bestGap = -1.f, newT = 1.f;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            float g = keys[i + 1].t - keys[i].t;
            if (g > bestGap) { bestGap = g; pos = i + 1; newT = 0.5f * (keys[i].t + keys[i + 1].t); }
        }
        CurveKey nk{ newT, EvalCurve(keys, count, newT) };
        for (uint32_t j = count; j > pos; --j) keys[j] = keys[j - 1];
        keys[pos] = nk; ++count; changed = true;
    }
    ImGui::PopID();
    return changed;
}

} // namespace gpusim
