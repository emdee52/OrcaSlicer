// [ORCAPORT:SU-5]_START — shared look for the Neotko curve gizmos (s248).
//
// Born inside GLGizmoHeightAdaptiveEffects and pulled out here the moment a SECOND gizmo wanted
// it, which is the only honest reason to share anything. Project owner's framing: "Orca's style is
// Orca's, but these gizmos are ours" — so this is the one place that decides what ours looks like.
//
// It was born for the fork's two curve editors (GLGizmoHeightAdaptiveEffects and
// GLGizmoPrecisionALH), which the VL-* features would bring in and which are NOT ported here. Its
// users in this tree are the fork's own gizmos: GLGizmoSupportZones (which still carries private
// copies of the widget layer below - consolidating those is a deliberate follow-up) and
// GLGizmoExtrude, the reference implementation of the panel style.
//
// ⚠️ This header is PURELY cosmetic. Nothing here may ever gate behaviour, and no colour may
// acquire a meaning that is not already carried by the code that draws it.
#ifndef slic3r_GizmoStyle_hpp_
#define slic3r_GizmoStyle_hpp_

#include <algorithm>

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

// ─────────────────────────────────────────────────────────────────────────────
// [ORCAPORT:HF-3] The panel widget layer: the shapes every custom gizmo panel uses.
//
// Same reason this header exists at all - it is pulled out when a SECOND gizmo wants it. Extrude is
// the second; Support Zones still carries private copies of the same widgets in its own anonymous
// namespace, and consolidating those is a deliberate follow-up so a user-verified gizmo is not
// refactored inside a UI task. New gizmos should use THESE.
//
// Shape rules, all of which come from the fork's panels:
//   - a fixed-width label column, so a stack of rows reads as a table and not as sentences;
//   - a slider with a typed field beside it and NO +/- step buttons (they cost a third of the width);
//   - a switch for a yes/no, not a checkbox, when the row has a label on the left;
//   - segmented buttons for a small exclusive choice, never a dropdown;
//   - hints WRAP inside a fixed panel width instead of widening the window.
// ─────────────────────────────────────────────────────────────────────────────

// Width of the label column. Derived from the widest caption the caller uses, so it is exact rather
// than a magic number: pass the longest label in the panel.
inline float gizmo_label_w(const char *widest_caption)
{
    return ImGui::CalcTextSize(widest_caption).x + ImGui::GetStyle().ItemSpacing.x * 0.5f;
}

// Slider + typed field on one row, with the caption in the label column. Returns true when either
// one changed the value. The field is clamped to the same range as the slider, so typing can never
// push the value outside it.
inline bool gizmo_row_slider(const char *id, const char *caption, float *v, float mn, float mx,
                             const char *fmt, float label_w, const char *tip)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    // 🔑 The label column is measured from the ROW's start, not from the window's left edge.
    // ImGui::SameLine() takes a window-relative offset while the caption starts inside the window
    // padding, so passing label_w straight through put the widget WindowPadding.x too far left and
    // the widget's own background covered the caption's tail - which is how EVERY panel's widest
    // caption ended up clipped ("Hole width" -> "Hole wid", "Boundary" -> "Boundar"), the widest one
    // being exactly the one that loses the race.
    const float col = ImGui::GetCursorPosX() + label_w;
    ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(GizmoCol::TextDim));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (tip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(col);
    // The slider takes whatever is left after the field, and prints NO value of its own: the typed
    // field beside it already shows the number, and an empty format is how ImGui is told to draw
    // none. Printing it twice (the slider's inline text and the field) was the reference's own
    // redundancy, not a decision - the standard says "a slider with a typed field beside it".
    const float field_w = ImGui::CalcTextSize("0000.00").x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - field_w - ImGui::GetStyle().ItemSpacing.x, 1.f));
    if (ImGui::SliderFloat("##v", v, mn, mx, ""))
        changed = true;
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(field_w);
    float typed = *v;
    if (ImGui::InputFloat("##t", &typed, 0.f, 0.f, fmt, ImGuiInputTextFlags_EnterReturnsTrue)) {
        *v      = std::clamp(typed, mn, mx);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// Label on the left, toggle switch on the right. The switch is drawn, not an ImGui::Checkbox, so it
// reads as a state rather than as an action. Returns true when it was flipped.
inline bool gizmo_row_switch(const char *id, const char *caption, bool *v, const char *tip = nullptr)
{
    ImGui::PushID(id);
    const float  h = ImGui::GetFrameHeight() * 0.72f;
    const float  w = h * 1.9f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(caption);
    if (tip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    // Right-align: place the invisible button so its right edge sits on the content's right edge.
    ImGui::SameLine(std::max(ImGui::GetWindowContentRegionMax().x - w, ImGui::GetCursorPosX()));
    const ImVec2 p   = ImGui::GetCursorScreenPos();
    const bool   hit = ImGui::InvisibleButton("##sw", ImVec2(w, h));
    if (hit)
        *v = !*v;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                      *v ? gizmo_col_u32(GizmoCol::Accent) : gizmo_col_u32(GizmoCol::SurfaceHi), h * 0.5f);
    const float kr = h * 0.5f - 2.f;
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - kr - 2.f : p.x + kr + 2.f, p.y + h * 0.5f), kr,
                        IM_COL32(255, 255, 255, 240), 20);
    ImGui::PopID();
    return hit;
}

// Segmented buttons: a small exclusive choice. Returns the newly selected index, or -1 if nothing
// was clicked. `n` is the number of labels.
inline int gizmo_segment(const char *id, const char *const *labels, int n, int sel, const char *tip = nullptr)
{
    int clicked = -1;
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        if (i > 0)
            ImGui::SameLine(0.f, 2.f);
        const bool on = (i == sel);
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, gizmo_col(GizmoCol::Accent));
        const bool hit = ImGui::Button(labels[i]);
        if (on)
            ImGui::PopStyleColor();
        if (tip != nullptr && i == 0 && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        if (hit)
            clicked = i;
    }
    ImGui::PopID();
    return clicked;
}

// A typed field on a row, with the caption in the label column and NO step buttons. This is for a
// value that has no useful range - a free offset, a diameter, a depth - where a slider would only
// invent bounds the geometry does not have. For anything the code already clamps, use
// gizmo_row_slider instead, which shows the range.
inline bool gizmo_row_input(const char *id, const char *caption, float *v, const char *fmt,
                            float label_w, const char *tip = nullptr)
{
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    const float col = ImGui::GetCursorPosX() + label_w; // row-relative: see gizmo_row_slider
    ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(GizmoCol::TextDim));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (tip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(col);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000.00").x + ImGui::GetStyle().FramePadding.x * 2.f);
    const bool changed = ImGui::InputFloat("##v", v, 0.f, 0.f, fmt, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopID();
    return changed;
}

// The same row for a whole number (a loop count, a hole number). Enter-driven, same as the float one.
inline bool gizmo_row_input_int(const char *id, const char *caption, int *v, float label_w,
                                const char *tip = nullptr)
{
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    const float col = ImGui::GetCursorPosX() + label_w; // row-relative: see gizmo_row_slider
    ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(GizmoCol::TextDim));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (tip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(col);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x + ImGui::GetStyle().FramePadding.x * 2.f);
    const bool changed = ImGui::InputInt("##v", v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopID();
    return changed;
}

// The panel's primary action: right-aligned, in the accent colour, on a line of its OWN. That last
// part is the whole reason this exists - SameLine() attaches to the PREVIOUS line, so the HF-3
// panel's Extrude button was printed across the second line of its own wrapped hint ("...then set
// the [Extrude] the offset and press Extrude"), which is unreadable and hides the words.
//
// NewLine() only advances when the current line already holds something, so this is correct both
// after a wrapped hint (cursor already at a fresh line, nothing added) and after a SameLine'd row
// like gizmo_row_switch (cursor on that row, one line added).
inline bool gizmo_primary_button(const char *label)
{
    ImGui::NewLine();
    const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 3.f;
    ImGui::SetCursorPosX(std::max(ImGui::GetWindowContentRegionMax().x - bw, ImGui::GetCursorPosX()));
    ImGui::PushStyleColor(ImGuiCol_Button, gizmo_col(GizmoCol::Accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, gizmo_col(GizmoCol::AccentBright));
    const bool hit = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    return hit;
}

// The same label column, then a segmented choice on the row. A panel uses this when the choice has a
// caption and belongs in the table, instead of standing alone like a tool mode (gizmo_segment, which
// is what the Extrude panel's own mode row uses).
inline int gizmo_row_segment(const char *id, const char *caption, const char *const *labels, int n,
                             int sel, float label_w, const char *tip = nullptr)
{
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    const float col = ImGui::GetCursorPosX() + label_w; // row-relative: see gizmo_row_slider
    ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(GizmoCol::TextDim));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (tip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(col);
    const int clicked = gizmo_segment("##seg", labels, n, sel);
    ImGui::PopID();
    return clicked;
}

// ─────────────────────────────────────────────────────────────────────────────
// [ORCAPORT:PP-1] The icon button: the shape the Perforations pattern row introduced.
//
// Size and glyph fill are THE standard for this kind of button - a square icon toggle whose whole
// meaning is its drawing, with the name on hover. Support Zones draws its own copy today (its
// glyph_toggle) and moves onto this one when its widgets are consolidated, which is the point of
// keeping the two numbers here rather than at the call site.
//
// `size` is 28 px at the reference font, scaled the same way gizmo_panel_width() is, so a high-DPI
// screen gets a 28 px-proportioned button rather than a literal 28 that would look tiny.
// ─────────────────────────────────────────────────────────────────────────────
constexpr float kGizmoIconGlyph = 0.78f; // the drawing's share of the button; the chrome is a frame

inline float gizmo_icon_size()
{
    return 28.f * (ImGui::GetFontSize() / 13.5f);
}

// `draw` paints the glyph inside the box [p, p + s] in the colour it is handed, so every gizmo keeps
// its own iconography and this stays pure chrome. Returns true when it was clicked.
inline bool gizmo_icon_toggle(const char *id, bool active, const char *tip,
                              void (*draw)(ImDrawList *, const ImVec2 &, float, ImU32))
{
    const float size = gizmo_icon_size();
    const bool  hit  = ImGui::InvisibleButton(id, ImVec2(size, size));
    ImDrawList *dl   = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const bool   hv = ImGui::IsItemHovered();
    const float  r  = size * 0.15f;
    dl->AddRectFilled(a, b, active ? gizmo_fade(GizmoCol::Accent, hv ? 1.0f : 0.85f)
                                   : gizmo_col_u32(hv ? GizmoCol::SurfaceHi : GizmoCol::Surface), r);
    if (active)
        dl->AddRect(a, b, gizmo_col_u32(GizmoCol::AccentBright), r, 0, 1.2f);
    const float gs = size * kGizmoIconGlyph;
    draw(dl, ImVec2(a.x + (size - gs) * 0.5f, a.y + (size - gs) * 0.5f), gs,
         active ? gizmo_col_u32(GizmoCol::Ink) : gizmo_fade(GizmoCol::TextDim, hv ? 1.0f : 0.85f));
    if (hv && tip != nullptr)
        ImGui::SetTooltip("%s", tip);
    return hit;
}

// A hint at the bottom of a panel: dim, indented slightly so it reads as prose and not as a row, and
// WRAPPED to the panel width. This is the thing that keeps a panel narrow.
//
// The colour is a parameter because the hint is also how a panel says "something is off", and the
// push below used to override any colour pushed by the caller - which is exactly how HF-3's amber
// warning ended up dim. Pass GizmoCol::Warn for a warning; never anything but TextDim or Warn.
inline void gizmo_hint(const char *text, GizmoCol col = GizmoCol::TextDim)
{
    ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(col));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// The narrow panel width, in the same units the gizmo panels are laid out in. One place to change it.
inline float gizmo_panel_width()
{
    return 360.f * (ImGui::GetFontSize() / 13.5f);
}

}} // namespace Slic3r::GUI

#endif
// [ORCAPORT:SU-5]_END
