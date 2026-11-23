#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace editor_slider {
// Dear ImGui 1.92.9b only supports Ctrl+click for direct text input on
// SliderFloat. DragFloat also supports double-click. Mirror DragFloat's
// double-click ownership check and ask SliderFloat to activate its *own*
// built-in TempInputScalar, preserving normal click/drag, wheel, keyboard,
// and right-click reset behavior. No extra InputFloat widget is created.
inline bool sliderFloat(const char *label,float *value,float min,float max,
                        const char *format,ImGuiSliderFlags flags) {
    ImGuiWindow *window=ImGui::GetCurrentWindow();
    if(window&&!window->SkipItems) {
        ImGuiContext &context=*GImGui;
        const ImGuiID id=window->GetID(label);
        const ImVec2 topLeft=window->DC.CursorPos;
        const ImVec2 bottomRight=topLeft+ImVec2(
            ImGui::CalcItemWidth(),ImGui::GetFrameHeight());
        if(context.IO.MouseClickedCount[ImGuiMouseButton_Left]==2&&
           ImGui::IsMouseHoveringRect(topLeft,bottomRight)&&
           ImGui::TestKeyOwner(ImGuiKey_MouseLeft,id)) {
            context.NavActivateId=id;
            context.NavActivateFlags=ImGuiActivateFlags_PreferInput;
        }
    }
    return ImGui::SliderFloat(label,value,min,max,format,flags);
}
} // namespace editor_slider
