// [ORCAPORT:SU-5]_START — shared look for the Neotko curve gizmos (s248).
//
// Born inside GLGizmoHeightAdaptiveEffects and pulled out here the moment a SECOND gizmo wanted
// it, which is the only honest reason to share anything. Project owner's framing: "Orca's style is
// Orca's, but these gizmos are ours" — so this is the one place that decides what ours looks like.
//
// Users today: GLGizmoHeightAdaptiveEffects, GLGizmoPrecisionALH. They are near-twins by
// construction (the HAE editor was cloned from the ALH one: same per-object session, same
// click-to-add / drag / right-click-to-delete, same curve drawn over a Z axis), so them looking
// like two unrelated tools was an accident of history, not a decision.
//
// ⚠️ This header is PURELY cosmetic. Nothing here may ever gate behaviour, and no colour may
// acquire a meaning that is not already carried by the code that draws it.
#ifndef slic3r_GizmoStyle_hpp_
#define slic3r_GizmoStyle_hpp_

#include <imgui/imgui.h>

#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r { namespace GUI {

// One ramp, not eleven loose IM_COL32s. Before this the two editors had five different greys
// between them and no two matched. Rules of the palette:
//   - Teal is the accent and means "this is your curve / your selection".
//   - Amber (Warn) is reserved EXCLUSIVELY for "something is off". Never decorative.
//   - Every grey is a step on the same ramp, so a panel reads as one surface.
//   - The semantic colours at the end exist because ALH's editor genuinely encodes meaning in
//     colour (forbidden band, optimal line, locked point) and those meanings predate this file.
enum class GizmoCol {
    PanelBg,       // el fondo de la VENTANA del gizmo (ver gizmo_push_window_style)
    Canvas,        // bottom of the graph gradient
    CanvasTop,     // top of it
    Surface,       // widget backgrounds, inactive borders
    SurfaceHi,
    Grid,          // minor gridlines
    GridMajor,     // labelled ticks, first-layer marker
    Accent,
    AccentBright,
    AccentDim,
    AccentGhost,   // area fill under a curve
    Warn,          // ⚠️ "something is off" ONLY
    TextDim,
    Ink,           // brightest text
    // Semantic, ALH's editor:
    Forbid,        // band the envelope rules out
    Optimal,       // the suggested-height line
    Slope,         // informational slope-exposure shading
    Locked,        // a point the user cannot move
    Endpoint,      // the top endpoint, height-movable only
};

// s290 — LA RAMPA TIENE DOS CARAS, Y LA VENTANA TAMBIEN.
//
// Primer intento fallido, que conviene tener escrito: solo con dar colores claros a los widgets no
// basta. El fondo de un panel de gizmo lo fija ImGuiWrapper::init_style() (ImGuiWrapper.cpp:2929)
// con COL_WINDOW_BACKGROUND = {0.1, 0.1, 0.1, 0.8}, una vez y SIN rama de tema, y el texto por
// defecto se queda blanco. Pintar widgets claros ahi dentro daba "30 deg" en blanco sobre un
// deslizador claro y botones ilegibles.
//
// 🔑 La ventana se arregla empujando ImGuiCol_WindowBg ANTES del Begin, que es lo que hace
// gizmo_push_window_style(). Con eso la ventana es nuestra en los dos temas y los widgets encajan.
//
// Los papeles NO cambian de una cara a otra: Canvas sigue siendo el lienzo, Ink el texto mas
// legible, el ambar sigue significando "algo va mal". Solo se invierte la luminancia, y quien
// dibuja no se entera de nada.
//
// 🚨 El tema se lee de wxGetApp().dark_mode(), el MISMO sitio del que come el resto de Orca. No
// inventes aqui una segunda fuente de verdad.
inline bool gizmo_is_dark() { return Slic3r::GUI::wxGetApp().dark_mode(); }

inline ImU32 gizmo_col_u32(GizmoCol c)
{
    if (! gizmo_is_dark()) {
        switch (c) {
        case GizmoCol::PanelBg:      return IM_COL32(246, 247, 249, 235);
        case GizmoCol::Canvas:       return IM_COL32(231, 235, 240, 255);
        case GizmoCol::CanvasTop:    return IM_COL32(243, 246, 249, 255);
        case GizmoCol::Surface:      return IM_COL32(222, 227, 233, 255);
        case GizmoCol::SurfaceHi:    return IM_COL32(205, 212, 220, 255);
        case GizmoCol::Grid:         return IM_COL32(158, 167, 178, 110);
        case GizmoCol::GridMajor:    return IM_COL32(118, 128, 140, 190);
        case GizmoCol::Accent:       return IM_COL32(  0, 150, 138, 255);
        case GizmoCol::AccentBright: return IM_COL32(  0, 118, 108, 255); // en claro "brillante" = mas contraste, no mas luz
        case GizmoCol::AccentDim:    return IM_COL32(176, 222, 216, 255);
        case GizmoCol::AccentGhost:  return IM_COL32(  0, 150, 138,  42);
        case GizmoCol::Warn:         return IM_COL32(188,  98,   0, 255);
        case GizmoCol::TextDim:      return IM_COL32( 98, 108, 120, 255);
        case GizmoCol::Ink:          return IM_COL32( 24,  28,  34, 255);
        case GizmoCol::Forbid:       return IM_COL32(190,  44,  44, 255);
        case GizmoCol::Optimal:      return IM_COL32( 24, 142,  66, 255);
        case GizmoCol::Slope:        return IM_COL32(118,  64, 194, 255);
        case GizmoCol::Locked:       return IM_COL32(134, 142, 152, 255);
        case GizmoCol::Endpoint:     return IM_COL32(174, 126,  12, 255);
        }
        return IM_COL32(255, 0, 255, 255); // loud on purpose: an unhandled enum should be seen
    }

    switch (c) {
    case GizmoCol::PanelBg:      return IM_COL32( 26,  26,  26, 204); // el de Orca, {0.1,0.1,0.1,0.8}
    case GizmoCol::Canvas:       return IM_COL32( 18,  21,  26, 255);
    case GizmoCol::CanvasTop:    return IM_COL32( 28,  33,  40, 255);
    case GizmoCol::Surface:      return IM_COL32( 44,  50,  58, 255);
    case GizmoCol::SurfaceHi:    return IM_COL32( 58,  66,  76, 255);
    case GizmoCol::Grid:         return IM_COL32( 52,  60,  69, 110);
    case GizmoCol::GridMajor:    return IM_COL32( 88, 100, 112, 190);
    case GizmoCol::Accent:       return IM_COL32(  0, 170, 155, 255);
    case GizmoCol::AccentBright: return IM_COL32( 46, 214, 196, 255);
    case GizmoCol::AccentDim:    return IM_COL32(  0, 120, 110, 255);
    case GizmoCol::AccentGhost:  return IM_COL32( 46, 214, 196,  30);
    case GizmoCol::Warn:         return IM_COL32(255, 150,  50, 255);
    case GizmoCol::TextDim:      return IM_COL32(150, 160, 170, 255);
    case GizmoCol::Ink:          return IM_COL32(226, 232, 238, 255);
    case GizmoCol::Forbid:       return IM_COL32(214,  69,  69, 255);
    case GizmoCol::Optimal:      return IM_COL32( 74, 222, 128, 255);
    case GizmoCol::Slope:        return IM_COL32(167, 110, 232, 255);
    case GizmoCol::Locked:       return IM_COL32(126, 136, 146, 255);
    case GizmoCol::Endpoint:     return IM_COL32(240, 190,  60, 255);
    }
    return IM_COL32(255, 0, 255, 255); // loud on purpose: an unhandled enum should be seen
}

inline ImVec4 gizmo_col(GizmoCol c) { return ImGui::ColorConvertU32ToFloat4(gizmo_col_u32(c)); }

// The same colour at a different opacity, without spelling out the RGB again.
inline ImU32 gizmo_fade(GizmoCol c, float alpha)
{
    ImVec4 v = gizmo_col(c);
    v.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// 🔑 EL ESTILO DE LA VENTANA. Va ANTES de GizmoImguiBegin(), y su pop DESPUES de GizmoImguiEnd().
//
// Tiene que ser antes del Begin porque ImGui resuelve WindowBg y el color del titulo al abrir la
// ventana: empujarlo despues (donde vive gizmo_push_panel_style) no pinta nada. ImGuiCol_Text va
// aqui, y no en el estilo de widgets, por lo mismo — asi cubre el titulo ademas del cuerpo, y de
// paso todo texto que no pida un color explicito deja de ser blanco fijo.
//
// 🚨 ImGuiCol_ChildBg se queda FUERA a proposito: ALH y HAE abren BeginChild, y un fondo opaco
// ahi se suma al de la ventana y ennegrece esas zonas. Los hijos heredan, que es lo correcto.
//
// 🚨 Si te saltas el pop, el fondo claro se cuela en TODOS los demas paneles de gizmo de Orca.
inline void gizmo_push_window_style()
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      gizmo_col(GizmoCol::PanelBg));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       gizmo_col(GizmoCol::PanelBg));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,       gizmo_col(GizmoCol::PanelBg));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, gizmo_col(GizmoCol::PanelBg));
    ImGui::PushStyleColor(ImGuiCol_Border,        gizmo_col(GizmoCol::Surface));
    ImGui::PushStyleColor(ImGuiCol_Separator,     gizmo_col(GizmoCol::Surface));
    ImGui::PushStyleColor(ImGuiCol_Text,          gizmo_col(GizmoCol::Ink));
}

inline void gizmo_pop_window_style() { ImGui::PopStyleColor(7); }

// The widget styling both panels push. Call between GizmoImguiBegin() and the panel body, and pair
// it with gizmo_pop_panel_style() before GizmoImguiEnd().
//
// 🚨 A PushStyleColor whose Pop is skipped by an early `return` LEAKS INTO EVERY OTHER IMGUI
// WINDOW. If the panel body has early exits, put the body in its own function and keep the
// push/pop in the caller — that is exactly why GLGizmoHeightAdaptiveEffects has
// render_panel_body().
inline void gizmo_push_panel_style()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,    4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(7.f, 4.f));
    ImGui::PushStyleColor(ImGuiCol_Button,           gizmo_col(GizmoCol::Surface));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,    gizmo_col(GizmoCol::AccentDim));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,     gizmo_col(GizmoCol::Accent));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          gizmo_col(GizmoCol::Surface));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   gizmo_col(GizmoCol::SurfaceHi));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    gizmo_col(GizmoCol::SurfaceHi));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       gizmo_col(GizmoCol::Accent));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, gizmo_col(GizmoCol::AccentBright));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,        gizmo_col(GizmoCol::AccentBright));
    ImGui::PushStyleColor(ImGuiCol_Header,           gizmo_col(GizmoCol::AccentDim));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,    gizmo_col(GizmoCol::SurfaceHi));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,     gizmo_col(GizmoCol::Accent));
}

inline void gizmo_pop_panel_style()
{
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(6);
}

// The graph background both editors sit on: rounded base, then the vertical gradient inset by a
// pixel. AddRectFilledMultiColor has no rounding parameter, which is why it takes two calls.
inline void gizmo_draw_canvas(ImDrawList* dl, const ImVec2& p0, float width, float height)
{
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), gizmo_col_u32(GizmoCol::Canvas), 6.0f);
    dl->AddRectFilledMultiColor(ImVec2(p0.x + 1.f, p0.y + 1.f),
                                ImVec2(p0.x + width - 1.f, p0.y + height - 1.f),
                                gizmo_col_u32(GizmoCol::CanvasTop), gizmo_col_u32(GizmoCol::CanvasTop),
                                gizmo_col_u32(GizmoCol::Canvas),    gizmo_col_u32(GizmoCol::Canvas));
}

// A floating readout: pill, not a box. Draws at `pos` and sizes itself to `text`.
inline void gizmo_draw_pill(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 border)
{
    const ImVec2 tsz = ImGui::CalcTextSize(text);
    const float  r   = (tsz.y + 4.f) * 0.5f;
    const ImVec2 a(pos.x - 7.f, pos.y - 2.f), b(pos.x + tsz.x + 7.f, pos.y + tsz.y + 2.f);
    dl->AddRectFilled(a, b, gizmo_fade(GizmoCol::Canvas, 0.95f), r);
    dl->AddRect(a, b, border, r, 0, 1.2f);
    dl->AddText(pos, gizmo_col_u32(GizmoCol::Ink), text);
}

// A curve point: optional halo when live, filled disc, and a ring in the CANVAS colour rather than
// black — a black outline on a dark gradient reads as a hole punched in the graph, this reads as a
// bead sitting on the curve.
inline void gizmo_draw_node(ImDrawList* dl, const ImVec2& c, float radius, ImU32 fill, bool lit)
{
    if (lit) {
        ImVec4 halo = ImGui::ColorConvertU32ToFloat4(fill);
        halo.w = 0.22f;
        dl->AddCircleFilled(c, radius + 5.f, ImGui::ColorConvertFloat4ToU32(halo), 20);
    }
    dl->AddCircleFilled(c, radius, fill, 20);
    dl->AddCircle(c, radius, gizmo_col_u32(GizmoCol::Canvas), 20, 1.6f);
}

}} // namespace Slic3r::GUI

#endif
// [ORCAPORT:SU-5]_END
