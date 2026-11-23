#include "editor_slider.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
struct FrameResult {
    float value=0.0f;
    bool tempInput=false;
    bool modified=false;
    ImVec2 sliderPos{};
};

FrameResult drawFrame(float &value,ImGuiSliderFlags flags) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f,0.0f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(430.0f,150.0f),ImGuiCond_Always);
    ImGui::Begin("slider test",nullptr,
                 ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetNextItemWidth(300.0f);
    const ImVec2 sliderPos=ImGui::GetCursorScreenPos();
    const bool modified=editor_slider::sliderFloat(
        "##test",&value,0.0f,1.0f,"Strength: %.3f",flags);
    const bool tempInput=ImGui::TempInputIsActive(ImGui::GetID("##test"));
    ImGui::End();
    ImGui::Render();
    return {value,tempInput,modified,sliderPos};
}

void testDoubleClick(ImGuiSliderFlags flags) {
    ImGui::CreateContext();
    ImGuiIO &io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(600.0f,400.0f);
    io.DeltaTime=1.0f/60.0f;
    io.Fonts->AddFontDefault();
    unsigned char *pixels=nullptr;
    int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    require(pixels&&width>0&&height>0,"Font atlas must initialize");

    float value=0.50f;
    const auto setup=drawFrame(value,flags);
    const ImVec2 pos(setup.sliderPos.x+50.0f,setup.sliderPos.y+10.0f);
    io.AddMousePosEvent(pos.x,pos.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
    const auto click1=drawFrame(value,flags);
    require(!click1.tempInput,
            "First click should retain normal slider dragging");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);
    drawFrame(value,flags);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
    const auto click2=drawFrame(value,flags);
    require(click2.tempInput,
            "Second click must enter ImGui's native numeric input mode");

    // Direct numeric input must remain active on subsequent frames and
    // preserve the ordinary ctrl-click validation policy for each slider.
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);
    const auto next=drawFrame(value,flags);
    require(next.tempInput,
            "Numeric editor must retain focus after double-click");

    io.AddInputCharactersUTF8("1.75");
    FrameResult typed;
    // The ImGui input-event queue can defer text for a frame when
    // mouse-up and text events are received close together.
    for(int frame=0;frame<8;++frame) {
        typed=drawFrame(value,flags);
        if(typed.modified)break;
    }
    if(!typed.modified) {
        const ImGuiContext &ctx=*GImGui;
        std::cerr<<"Slider diagnostic: value="<<value
                 <<" typed-temp="<<typed.tempInput
                 <<" active-id="<<ctx.ActiveId
                 <<" temp-id="<<ctx.TempInputId
                 <<" text-id="<<ctx.InputTextState.ID
                 <<" deactivated-id="<<ctx.InputTextDeactivatedState.ID
                 <<" nav-activate="<<ctx.NavActivateId
                 <<" want-text="<<ctx.IO.WantTextInput
                 <<" want-keyboard="<<ctx.IO.WantCaptureKeyboard
                 <<" queue-length="<<ctx.IO.InputQueueCharacters.Size
                 <<" mouse-down="<<ctx.IO.MouseDown[0]<<"\\n";
    }
    require(typed.modified,
            "Typed numeric value must be accepted by the slider");
    const float expected=flags&ImGuiSliderFlags_AlwaysClamp?1.0f:1.75f;
    require(std::fabs(value-expected)<0.0001f,
            "Typed value must follow the slider's clamping policy");

    ImGui::DestroyContext();
}
int main() {
    try {
        testDoubleClick(ImGuiSliderFlags_AlwaysClamp);
        testDoubleClick(ImGuiSliderFlags_None);
        std::cout<<"Slider double-click text-entry tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Slider interaction test failed: "<<e.what()<<"\n";
        return 1;
    }
}
