// Windows-only material editor. No renderer runtime dependencies.
#include "glad.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "common/json_incl.hpp"
#include "preview.hpp"
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Document = json::json;
namespace {
std::string toUtf8(const wchar_t *source) {
    if(!source || !*source)return {};
    int count=WideCharToMultiByte(CP_UTF8,0,source,-1,nullptr,0,nullptr,nullptr);
    if(count<=0)return {};
    std::string out(size_t(count),'\0');
    WideCharToMultiByte(CP_UTF8,0,source,-1,out.data(),count,nullptr,nullptr);
    out.pop_back();
    return out;
}
std::string chooseFile(GLFWwindow *window,bool image) {
    wchar_t path[32768] = {};
    const wchar_t *filter=image
        ? L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr\0All files\0*.*\0\0"
        : L"Material metadata\0*.json\0All files\0*.*\0\0";
    OPENFILENAMEW dlg{};
    dlg.lStructSize=sizeof(dlg);
    dlg.hwndOwner=glfwGetWin32Window(window);
    dlg.lpstrFilter=filter;
    dlg.lpstrFile=path;
    dlg.nMaxFile=DWORD(sizeof(path)/sizeof(path[0]));
    dlg.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dlg)?toUtf8(path):std::string{};
}
float number(const Document &obj,const char *key,float fallback) {
    auto it=obj.find(key);
    if(it==obj.end()||!it->is_number())return fallback;
    try {float f=it->get<float>();return std::isfinite(f)?f:fallback;}
    catch(...) {return fallback;}
}
std::array<float,3> rgb(const Document &obj,const char *key,
                        std::array<float,3> fallback) {
    auto it=obj.find(key);
    if(it==obj.end()||!it->is_array()||it->size()!=3)return fallback;
    for(size_t i=0;i<3;++i)if((*it)[i].is_number())fallback[i]=(*it)[i].get<float>();
    return fallback;
}
std::array<float,4> rgba(const Document &obj,const char *key,
                         std::array<float,4> fallback) {
    auto it=obj.find(key);
    if(it==obj.end()||!it->is_array()||it->size()!=4)return fallback;
    for(size_t i=0;i<4;++i)if((*it)[i].is_number())fallback[i]=(*it)[i].get<float>();
    return fallback;
}
std::string stringValue(const Document &obj,const char *key) {
    auto it=obj.find(key);
    return it!=obj.end()&&it->is_string()?it->get<std::string>():std::string{};
}
std::string resolvedFile(const fs::path &directory,const std::string &name) {
    if(name.empty())return {};
    fs::path p=fs::u8path(name);
    if(p.is_relative())p=directory/p;
    return p.u8string();
}
PreviewMaterial fromMetadata(const Document &entry,const Document &root,
                             const fs::path &directory) {
    PreviewMaterial p;
    p.albedo=rgba(entry,"albedo_factor",p.albedo);
    p.emissive=rgb(entry,"emissive_factor",p.emissive);
    p.matcapFactor=rgb(entry,"matcap_factor",p.matcapFactor);
    const char *names[]={"albedo_map","surface_map",nullptr,"normal_map",
        "normal_map2","emissive_map","matcap_map","matcap_surface_map",
        "clearcoat_surface_map","clearcoat_normal_map"};
    for(size_t i=0;i<TextureCount;++i)
        if(names[i])p.files[i]=resolvedFile(directory,stringValue(entry,names[i]));
    std::string env=stringValue(entry,"environment_map");
    if(env.empty())env=stringValue(root,"environment_map");
    p.files[EnvironmentMap]=resolvedFile(directory,env);
    const bool surf=entry.find("surface_map")!=entry.end();
    p.reflection=number(entry,"reflection_strength",surf?1.0f:0.0f);
    p.roughness=number(entry,"roughness",surf?1.0f:0.0f);
    p.metalness=number(entry,"metalness",surf?1.0f:0.0f);
    p.normalStrength=number(entry,"normal_strength",1.0f);
    p.normalStrength2=number(entry,"normal_strength2",1.0f);
    p.matcapRoughness=number(entry,"matcap_roughness",0.0f);
    const bool coatSurface=entry.find("clearcoat_surface_map")!=entry.end();
    p.coat=number(entry,"clearcoat_strength",coatSurface?1.0f:0.0f);
    p.coatRoughness=number(entry,"clearcoat_roughness",.1f);
    p.coatTransmission=number(entry,"clearcoat_transmission",1.0f);
    return p;
}
void setArray(Document &obj,const char *key,const float *values,int size) {
    Document a=Document::array();
    for(int i=0;i<size;++i)a.push_back(values[i]);
    obj[key]=std::move(a);
}
}

// Hamilton-product quaternions: left-multiplying applies each drag in
// fixed camera/screen axes, rather than around the material's rotated axes.
using Orientation = std::array<float,4>; // x, y, z, w
static Orientation multiplyOrientation(const Orientation &a,const Orientation &b) {
    return {
        a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]
    };
}
static Orientation screenDrag(float dx,float dy) {
    float halfYaw=dx*.0045f,halfPitch=dy*.0045f;
    Orientation yaw={0,std::sin(halfYaw),0,std::cos(halfYaw)};
    Orientation pitch={std::sin(halfPitch),0,0,std::cos(halfPitch)};
    return multiplyOrientation(yaw,pitch);
}

class Editor {
public:
    explicit Editor(GLFWwindow *window) : window_(window) {
        // Retain the prototype's initial slightly angled view.
        orientation_=multiplyOrientation(screenDrag(-.35f/.009f,0),
                                         screenDrag(0,.15f/.009f));
        preview_=std::make_unique<MaterialPreview>();
        status_="Open a compiled metadata.json to begin.";
    }
    void frame() {
        // Let ImGui's own editable text boxes handle Ctrl+Z while typing.
        if(!ImGui::GetIO().WantTextInput) {
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_O))open();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_S))save();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Z))undo();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_Z) ||
               ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Y))redo();
        }

        ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport(),
                                    ImGuiDockNodeFlags_PassthruCentralNode);
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                if(ImGui::MenuItem("Open metadata...","Ctrl+O"))open();
                if(ImGui::MenuItem("Save metadata","Ctrl+S",false,loaded_))save();
                if(ImGui::MenuItem("Exit"))glfwSetWindowShouldClose(window_,GLFW_TRUE);
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit")) {
                const std::string undoLabel=undo_.empty()?"Undo":
                    "Undo "+undo_.back().key;
                const std::string redoLabel=redo_.empty()?"Redo":
                    "Redo "+redo_.back().key;
                if(ImGui::MenuItem(undoLabel.c_str(),"Ctrl+Z",false,!undo_.empty()))
                    undo();
                if(ImGui::MenuItem(redoLabel.c_str(),"Ctrl+Y / Ctrl+Shift+Z",
                                   false,!redo_.empty()))
                    redo();
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("JkGfxMod Material Editor  |  Win32 + Dear ImGui + OpenGL 3.3");
            ImGui::EndMainMenuBar();
        }
        renderList();
        renderViewport();
        renderProperties();
        // Once the slider/drag is released, the next press is a separate
        // undo action even if it changes the exact same material field.
        if(!ImGui::IsAnyItemActive())groupActive_=false;
    }
    bool confirmClose() {
        if(!dirty_)return true;
        int r=MessageBoxW(glfwGetWin32Window(window_),
            L"Discard unsaved changes to metadata.json?",L"Unsaved changes",
            MB_ICONWARNING|MB_YESNO);
        return r==IDYES;
    }

private:
    struct Edit {
        int materialIndex=-1;
        std::string key;
        Document before, after;
    };
    static constexpr size_t maxHistory=128;
    void undo() {
        if(undo_.empty()||!loaded_)return;
        Edit step=std::move(undo_.back());
        undo_.pop_back();
        root_["materials"][size_t(step.materialIndex)]=step.before;
        observedRoot_["materials"][size_t(step.materialIndex)]=step.before;
        selection_=step.materialIndex;
        redo_.push_back(std::move(step));
        groupActive_=false;
        updateDirty();
        syncPreview();
        status_="Undid "+redo_.back().key;
    }
    void redo() {
        if(redo_.empty()||!loaded_)return;
        Edit step=std::move(redo_.back());
        redo_.pop_back();
        root_["materials"][size_t(step.materialIndex)]=step.after;
        observedRoot_["materials"][size_t(step.materialIndex)]=step.after;
        selection_=step.materialIndex;
        undo_.push_back(std::move(step));
        groupActive_=false;
        updateDirty();
        syncPreview();
        status_="Redid "+undo_.back().key;
    }
    void updateDirty() {dirty_=root_!=savedRoot_;}
    Document &selected() {
        return root_["materials"][size_t(selection_)];
    }
    const Document &selected() const {
        return root_["materials"][size_t(selection_)];
    }
    bool validSelection() const {
        return loaded_&&selection_>=0&&
               size_t(selection_)<root_["materials"].size();
    }
    void changed(const char *key="Material property",bool continuous=false) {
        if(!validSelection())return;
        Document &entry=selected();
        Document &previous=observedRoot_["materials"][size_t(selection_)];
        if(previous==entry)return;
        bool merge=continuous&&groupActive_&&!undo_.empty()&&
            undo_.back().materialIndex==selection_&&undo_.back().key==key;
        if(merge)
            undo_.back().after=entry;
        else {
            if(undo_.size()>=maxHistory)undo_.erase(undo_.begin());
            undo_.push_back({selection_,key,previous,entry});
        }
        previous=entry;
        redo_.clear();
        groupActive_=continuous;
        updateDirty();
        syncPreview();
        status_="Unsaved material changes (Ctrl+S)";
    }
    void syncPreview() {
        if(validSelection())preview_->setMaterial(
            fromMetadata(selected(),root_,fs::u8path(file_).parent_path()));
        else preview_->setMaterial(PreviewMaterial{});
    }
    void open() {
        if(dirty_&&!confirmClose())return;
        const std::string path=chooseFile(window_,false);
        if(path.empty())return;
        try {
            std::ifstream stream(fs::u8path(path),std::ios::binary);
            if(!stream)throw std::runtime_error("Unable to open file.");
            Document doc;stream>>doc;
            if(!doc.is_object()||doc.find("materials")==doc.end()||
               !doc["materials"].is_array())
                throw std::runtime_error("Not a compiled material pack (missing materials array).");
            root_=std::move(doc);
            observedRoot_=root_; savedRoot_=root_;
            undo_.clear(); redo_.clear(); groupActive_=false;
            file_=path;loaded_=true;dirty_=false;
            selection_=root_["materials"].empty()?-1:0;
            syncPreview();
            status_="Loaded "+std::to_string(root_["materials"].size())+" materials.";
        } catch(const std::exception &e) {
            status_=std::string("Open failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),"Material editor",MB_ICONERROR);
        }
    }
    void save() {
        if(!loaded_)return;
        try {
            fs::path destination=fs::u8path(file_);
            fs::path temp=destination;
            temp+=L".tmp";
            {
                std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
                if(!stream)throw std::runtime_error("Cannot write temporary file.");
                stream<<root_.dump(4)<<"\n";
                stream.flush();
                if(!stream)throw std::runtime_error("Failed to write JSON.");
            }
            if(!MoveFileExW(temp.c_str(),destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace metadata.json. Close other programs using it.");
            savedRoot_=root_;
            updateDirty();
            status_="Saved metadata.json. No MAT signatures were regenerated.";
        } catch(const std::exception &e) {
            status_=std::string("Save failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),"Save failed",MB_ICONERROR);
        }
    }
    void renderList() {
        ImGui::SetNextWindowPos(ImVec2(10,35),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(255,690),ImGuiCond_FirstUseEver);
        ImGui::Begin("Materials");
        if(ImGui::Button("Open pack..."))open();
        ImGui::SameLine();
        ImGui::BeginDisabled(!loaded_);
        if(ImGui::Button("Save"))save();
        ImGui::EndDisabled();
        if(ImGui::Button("Undo") && !undo_.empty())undo();
        ImGui::SameLine();
        if(ImGui::Button("Redo") && !redo_.empty())redo();
        if(loaded_) {
            ImGui::TextWrapped("%s",fs::u8path(file_).filename().u8string().c_str());
            ImGui::Separator();
            filter_.Draw("Search");
            if(ImGui::BeginChild("MaterialList",ImVec2(0,-105),true)) {
                for(size_t i=0;i<root_["materials"].size();++i) {
                    const Document &item=root_["materials"][i];
                    std::string name=stringValue(item,"name");
                    if(name.empty())name="(unnamed)";
                    if(!filter_.PassFilter(name.c_str()))continue;
                    ImGui::PushID(int(i));
                    if(ImGui::Selectable(name.c_str(),selection_==int(i))) {
                        selection_=int(i);syncPreview();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextWrapped("Open an existing compiled material pack. The preview updates immediately when you change a property.");
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s",status_.c_str());
        if(dirty_)ImGui::TextColored(ImVec4(1,.76f,.33f,1),"Unsaved changes");
        ImGui::End();
    }
    void renderViewport() {
        ImGui::SetNextWindowPos(ImVec2(275,35),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(660,690),ImGuiCond_FirstUseEver);
        ImGui::Begin("Live Material Preview");
        if(ImGui::RadioButton("Sphere",!plane_))plane_=false;
        ImGui::SameLine();
        if(ImGui::RadioButton("Plane",plane_))plane_=true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        const char *options[]={"Studio","Outdoor","Night"};
        if(ImGui::Combo("Fallback environment",&environment_,options,3))
            preview_->setPreset(environment_);
        if(ImGui::Button("Override envmap...")) {
            std::string path=chooseFile(window_,true);
            if(!path.empty()) {
                preview_->setEnvironmentOverride(path);override_=path;
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Use material / pack envmap")) {
            override_.clear();preview_->setEnvironmentOverride({});
        }
        const char *views[]={"Lit material (renderer-style)","EnvMap reflection only",
                             "Raw EnvMap (diagnostic)"};
        ImGui::SetNextItemWidth(230);
        ImGui::Combo("Preview shading",&inspectionMode_,views,3);
        ImGui::Checkbox("Apply Fresnel to EnvMap reflections",&envFresnel_);
        ImGui::TextDisabled(envFresnel_ ?
            "Renderer-style Fresnel enabled (preview only)." :
            "Fresnel disabled: show the full EnvMap reflection intensity (preview only).");
        ImGui::TextWrapped("Source: %s",preview_->environmentDescription().c_str());
        if(!preview_->hasLoadedEnvironmentFile())
            ImGui::TextDisabled("A built-in environment preset is being used.");
        if(validSelection() && number(selected(),"reflection_strength",
             selected().contains("surface_map")?1.0f:0.0f)<=0.0f)
            ImGui::TextColored(ImVec4(1.0f,.7f,.35f,1.0f),
               "Reflection strength is 0: EnvMap reflections are disabled.");
        ImGui::TextDisabled("All game reflection modes use EnvMap sampling in this preview.");
        ImVec2 area=ImGui::GetContentRegionAvail();
        area.x=std::max(area.x,64.0f);
        area.y=std::max(area.y-35.0f,64.0f);
        if(preview_->valid()) {
            preview_->draw(int(area.x),int(area.y),orientation_,plane_,
                           inspectionMode_,envFresnel_);
            // Image() is a passive item: dragging it moves the ImGui window.
            // An invisible button claims the mouse press for viewport orbiting,
            // while the image is drawn over that item's exact screen rectangle.
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##material_preview_orbit", area,
                                   ImGuiButtonFlags_MouseButtonLeft);
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef((ImTextureID)preview_->outputTexture()),
                canvasPos, ImVec2(canvasPos.x + area.x, canvasPos.y + area.y),
                ImVec2(0,1), ImVec2(1,0));
            if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta=ImGui::GetIO().MouseDelta;
                // Compose around the screen's vertical and horizontal axes.
                // Normalize to avoid accumulated floating-point drift.
                orientation_=multiplyOrientation(screenDrag(delta.x,delta.y),
                                                 orientation_);
                float length=std::sqrt(
                    orientation_[0]*orientation_[0]+orientation_[1]*orientation_[1]+
                    orientation_[2]*orientation_[2]+orientation_[3]*orientation_[3]);
                if(length>0.0f)
                    for(float &component:orientation_)component/=length;
            }
        } else {
            ImGui::TextWrapped("GLSL preview failed: %s",preview_->error().c_str());
        }
        ImGui::TextDisabled("Drag to rotate. Studio lighting differs from in-game lighting.");
        ImGui::End();
    }
    bool floatField(const char *label,const char *key,float fallback,
                    float low,float high,bool allowAboveSlider=false) {
        Document &obj=selected();
        float v=number(obj,key,fallback);
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        const float available=ImGui::GetContentRegionAvail().x;
        const bool hasOverride=obj.find(key)!=obj.end();
        const float sliderWidth=std::max(90.0f,available-120.0f);
        ImGui::SetNextItemWidth(sliderWidth);
        float position=std::clamp(v,low,high);
        bool modified=false;
        if(ImGui::SliderFloat("##slider",&position,low,high,"%.3f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            obj[key]=position;changed(key,ImGui::IsItemActive());modified=true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(73.0f);
        float typed=v;
        if(ImGui::InputFloat("##exact_value",&typed,0,0,"%.3f")) {
            if(std::isfinite(typed)) {
                typed=allowAboveSlider?std::max(low,typed):std::clamp(typed,low,high);
                obj[key]=typed;changed(key,ImGui::IsItemActive());modified=true;
            }
        }
        ImGui::SameLine();
        if(hasOverride&&ImGui::SmallButton("x")) {
            obj.erase(key);changed(key);modified=true;
        } else if(!hasOverride) {
            ImGui::TextDisabled("  ");
        }
        if(allowAboveSlider && v>high)
            ImGui::SetItemTooltip("Manual value exceeds the 0-1 slider: this is supported by the renderer.");
        ImGui::PopID();
        return modified;
    }
    void colorField(const char *label,const char *key,float *values,int count,
                    bool hdr=false) {
        Document &obj=selected();
        bool changedValue=(count==4)
            ?ImGui::ColorEdit4(label,values,hdr?ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float:0)
            :ImGui::ColorEdit3(label,values,hdr?ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float:0);
        if(changedValue){
            setArray(obj,key,values,count);
            changed(key,ImGui::IsItemActive());
        }
        ImGui::SameLine();
        if(obj.find(key)!=obj.end()) {
            ImGui::PushID(key);
            if(ImGui::SmallButton("x")){obj.erase(key);changed(key);}
            ImGui::PopID();
        }
    }
    void imageInfo(const char *label,const char *key) {
        std::string value=stringValue(selected(),key);
        if(value.empty())return;
        ImGui::TextWrapped("%s: %s",label,value.c_str());
        fs::path path=fs::u8path(file_).parent_path()/fs::u8path(value);
        std::error_code ec;
        if(!fs::exists(path,ec)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,.5f,.35f,1),"[missing]");
        }
    }
    void renderProperties() {
        ImGui::SetNextWindowPos(ImVec2(950,35),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(370,690),ImGuiCond_FirstUseEver);
        ImGui::Begin("Material Properties");
        if(!validSelection()) {
            ImGui::TextWrapped("Select a material to edit its properties and see the result in the OpenGL preview.");
            ImGui::End();return;
        }
        const std::string name=stringValue(selected(),"name");
        ImGui::TextWrapped("Material: %s",name.c_str());
        ImGui::TextDisabled("Changes update the preview immediately; save metadata separately.");
        if(ImGui::CollapsingHeader("Albedo",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto v=rgba(selected(),"albedo_factor",{1,1,1,1});
            colorField("Tint / alpha","albedo_factor",v.data(),4);
            imageInfo("Albedo texture","albedo_map");
            ImGui::TextDisabled("Existing compiled PNG textures are read-only.");
        }
        if(ImGui::CollapsingHeader("Reflections",ImGuiTreeNodeFlags_DefaultOpen)) {
            bool hasSurface=selected().find("surface_map")!=selected().end();
            floatField("Strength","reflection_strength",hasSurface?1.0f:0.0f,0,1,true);
            if(number(selected(),"reflection_strength",hasSurface?1.0f:0.0f)>1.0f)
                ImGui::TextDisabled("Above 1: explicitly overridden in the numeric field.");
            floatField("Roughness","roughness",hasSurface?1.0f:0.0f,0,1);
            floatField("Metalness","metalness",hasSurface?1.0f:0.0f,0,1);
            static const char *modes[]={"envmap","screen","fixed","planar","hemi","cube","shared"};
            static const char *modeLabels[]={"EnvMap","Screen","Fixed","Planar",
                                            "Hemisphere","Cubemap","Shared"};
            comboField("Game reflection mode","reflection_mode",
                       modes,modeLabels,7);
            ImGui::TextDisabled("Preview uses EnvMap for all game reflection modes.");
            imageInfo("Surface (R/G/B)","surface_map");
        }
        if(ImGui::CollapsingHeader("Normal maps",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("Normal strength","normal_strength",1,0,4);
            floatField("Layer 2 strength","normal_strength2",1,0,4);
            imageInfo("Normal 1","normal_map");imageInfo("Normal 2","normal_map2");
        }
        if(ImGui::CollapsingHeader("Clearcoat",ImGuiTreeNodeFlags_DefaultOpen)) {
            bool map=selected().find("clearcoat_surface_map")!=selected().end();
            floatField("Coat strength","clearcoat_strength",map?1.0f:0.0f,0,1);
            floatField("Coat roughness","clearcoat_roughness",.1f,0,1);
            floatField("Coat transmission","clearcoat_transmission",1,0,1);
            imageInfo("Coat RGB","clearcoat_surface_map");
            imageInfo("Coat normal","clearcoat_normal_map");
        }
        if(ImGui::CollapsingHeader("MatCap",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("MatCap roughness","matcap_roughness",0,0,1);
            auto v=rgb(selected(),"matcap_factor",{1,1,1});
            if(ImGui::DragFloat3("MatCap RGB factor",v.data(),.02f,0,4,"%.2f")) {
                setArray(selected(),"matcap_factor",v.data(),3);
                changed("matcap_factor",ImGui::IsItemActive());
            }
            imageInfo("MatCap","matcap_map");
            imageInfo("MatCap mask","matcap_surface_map");
        }
        if(ImGui::CollapsingHeader("Emissive",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto v=rgb(selected(),"emissive_factor",{0,0,0});
            if(ImGui::DragFloat3("Emissive RGB",v.data(),.05f,0,32,"%.2f")) {
                setArray(selected(),"emissive_factor",v.data(),3);
                changed("emissive_factor",ImGui::IsItemActive());
            }
            imageInfo("Emissive map","emissive_map");
        }
        if(ImGui::CollapsingHeader("Other settings")) {
            static const char *alphas[]={"blend","mask","dithered"};
            static const char *alphaLabels[]={"Blend (smooth alpha)","Mask (alpha cutoff)",
                                              "Dithered transparency"};
            static const char *blends[]={"normal","additive","multiply","fluid"};
            static const char *blendLabels[]={"Normal","Additive","Multiply","Fluid"};
            comboField("Alpha mode","alpha_mode",alphas,alphaLabels,3);
            comboField("Blend mode","blend_mode",blends,blendLabels,4);
            if(stringValue(selected(),"alpha_mode")=="mask")
                floatField("Alpha cutoff","alpha_cutoff",.5f,0,1);
            else if(selected().contains("alpha_cutoff")) {
                ImGui::TextColored(ImVec4(1,.7f,.35f,1),
                    "Alpha cutoff is set but applies only to Mask mode.");
                if(ImGui::SmallButton("Remove unused alpha cutoff")) {
                    selected().erase("alpha_cutoff");changed("alpha_cutoff");
                }
            }
            floatField("Depth bias","depth_bias",0,-16,16);
        }
        if(ImGui::CollapsingHeader("Compiled textures")) {
            imageInfo("Environment","environment_map");
            imageInfo("Albedo","albedo_map");
            imageInfo("Surface","surface_map");
            imageInfo("Normal","normal_map");
            imageInfo("Normal 2","normal_map2");
            imageInfo("Emissive","emissive_map");
            imageInfo("Displacement","displacement_map");
            imageInfo("MatCap","matcap_map");
            imageInfo("MatCap mask","matcap_surface_map");
            imageInfo("Coat surface","clearcoat_surface_map");
            imageInfo("Coat normal","clearcoat_normal_map");
            ImGui::Separator();
            ImGui::TextWrapped("MAT replacements/signatures are preserved exactly when saving. Texture importing and incremental recompilation are not included in this milestone.");
        }
        ImGui::End();
    }
    void comboField(const char *label,const char *key,
                    const char *const*values,const char *const*labels,int count) {
        Document &obj=selected();
        std::string value=stringValue(obj,key);
        int current=0;
        for(int i=0;i<count;++i)if(value==values[i])current=i;
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        std::string display=labels[current];
        if(value.empty())display+=" (renderer default)";
        if(ImGui::BeginCombo("##choice",display.c_str())) {
            if(ImGui::Selectable("Use renderer default",value.empty())) {
                obj.erase(key);changed(key);
            }
            ImGui::Separator();
            for(int i=0;i<count;++i) {
                if(ImGui::Selectable(labels[i],!value.empty()&&current==i)) {
                    obj[key]=values[i];changed(key);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    GLFWwindow *window_;
    std::unique_ptr<MaterialPreview> preview_;
    Document root_=Document::object();
    Document savedRoot_=Document::object(), observedRoot_=Document::object();
    std::vector<Edit> undo_,redo_;
    std::string file_,status_,override_;
    bool loaded_=false,dirty_=false,plane_=false,groupActive_=false;
    int selection_=-1,environment_=0,inspectionMode_=0;
    bool envFresnel_=true;
    Orientation orientation_{0,0,0,1};
    ImGuiTextFilter filter_;
};

int runEditor() {
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4);
    GLFWwindow *window=glfwCreateWindow(1350,800,"JkGfxMod Material Editor",nullptr,nullptr);
    if(!window){glfwTerminate();return 2;}
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        MessageBoxW(glfwGetWin32Window(window),L"OpenGL 3.3 loader failed.",
                    L"Material editor",MB_ICONERROR);
        glfwDestroyWindow(window);glfwTerminate();return 3;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    {
        Editor editor(window);
        bool confirmed=false;
        while(true) {
            glfwPollEvents();
            if(glfwWindowShouldClose(window)) {
                if(!confirmed&&!editor.confirmClose())glfwSetWindowShouldClose(window,GLFW_FALSE);
                else break;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            editor.frame();
            ImGui::Render();
            int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glViewport(0,0,w,h);
            glDisable(GL_DEPTH_TEST);
            glClearColor(.10f,.11f,.13f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) { return runEditor(); }
