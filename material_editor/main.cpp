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

class Editor {
public:
    explicit Editor(GLFWwindow *window) : window_(window) {
        preview_=std::make_unique<MaterialPreview>();
        status_="Open a compiled metadata.json to begin.";
    }
    void frame() {
        if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_O))open();
        if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl|ImGuiKey_S))save();

        ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport(),
                                    ImGuiDockNodeFlags_PassthruCentralNode);
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                if(ImGui::MenuItem("Open metadata...","Ctrl+O"))open();
                if(ImGui::MenuItem("Save metadata","Ctrl+S",false,loaded_))save();
                if(ImGui::MenuItem("Exit"))glfwSetWindowShouldClose(window_,GLFW_TRUE);
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("JkGfxMod Material Editor  |  Win32 + Dear ImGui + OpenGL 3.3");
            ImGui::EndMainMenuBar();
        }
        renderList();
        renderViewport();
        renderProperties();
    }
    bool confirmClose() {
        if(!dirty_)return true;
        int r=MessageBoxW(glfwGetWin32Window(window_),
            L"Discard unsaved changes to metadata.json?",L"Unsaved changes",
            MB_ICONWARNING|MB_YESNO);
        return r==IDYES;
    }

private:
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
    void changed() {
        dirty_=true;
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
            dirty_=false;
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
        if(!override_.empty())ImGui::TextWrapped("Preview override: %s",override_.c_str());
        ImGui::TextDisabled("All reflection modes use this environment image in the editor.");
        ImVec2 area=ImGui::GetContentRegionAvail();
        area.x=std::max(area.x,64.0f);
        area.y=std::max(area.y-35.0f,64.0f);
        if(preview_->valid()) {
            preview_->draw(int(area.x),int(area.y),yaw_,pitch_,plane_);
            ImGui::Image((ImTextureID)(intptr_t)preview_->outputTexture(),
                         area,ImVec2(0,1),ImVec2(1,0));
            if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta=ImGui::GetIO().MouseDelta;
                yaw_+=delta.x*.009f;
                pitch_=std::clamp(pitch_+delta.y*.009f,-1.48f,1.48f);
            }
        } else {
            ImGui::TextWrapped("GLSL preview failed: %s",preview_->error().c_str());
        }
        ImGui::TextDisabled("Drag to rotate. Studio lighting differs from in-game lighting.");
        ImGui::End();
    }
    bool floatField(const char *label,const char *key,float fallback,
                    float low,float high) {
        Document &obj=selected();
        float v=number(obj,key,fallback);
        ImGui::PushID(key);
        bool modified=ImGui::SliderFloat(label,&v,low,high,"%.3f",
                                         high<=1.0f?ImGuiSliderFlags_AlwaysClamp:0);
        if(modified) {
            obj[key]=v;changed();
        }
        ImGui::SameLine();
        if(obj.find(key)!=obj.end()&&ImGui::SmallButton("x")) {
            obj.erase(key);changed();modified=true;
        }
        ImGui::PopID();
        return modified;
    }
    void colorField(const char *label,const char *key,float *values,int count,
                    bool hdr=false) {
        Document &obj=selected();
        bool changedValue=(count==4)
            ?ImGui::ColorEdit4(label,values,hdr?ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float:0)
            :ImGui::ColorEdit3(label,values,hdr?ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float:0);
        if(changedValue){setArray(obj,key,values,count);changed();}
        ImGui::SameLine();
        if(obj.find(key)!=obj.end()) {
            ImGui::PushID(key);
            if(ImGui::SmallButton("x")){obj.erase(key);changed();}
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
            floatField("Strength","reflection_strength",hasSurface?1.0f:0.0f,0,8);
            floatField("Roughness","roughness",hasSurface?1.0f:0.0f,0,1);
            floatField("Metalness","metalness",hasSurface?1.0f:0.0f,0,1);
            const char *modes[]={"envmap","screen","fixed","planar","hemi","cube","shared"};
            std::string mode=stringValue(selected(),"reflection_mode");
            int current=0;
            for(int i=0;i<7;++i)if(mode==modes[i])current=i;
            if(ImGui::Combo("Game reflection mode",&current,modes,7)) {
                selected()["reflection_mode"]=modes[current];changed();
            }
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
            ImGui::DragFloat3("MatCap RGB factor",v.data(),.02f,0,4,"%.2f");
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                setArray(selected(),"matcap_factor",v.data(),3);changed();
            }
            imageInfo("MatCap","matcap_map");
            imageInfo("MatCap mask","matcap_surface_map");
        }
        if(ImGui::CollapsingHeader("Emissive",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto v=rgb(selected(),"emissive_factor",{0,0,0});
            if(ImGui::DragFloat3("Emissive RGB",v.data(),.05f,0,32,"%.2f")) {
                setArray(selected(),"emissive_factor",v.data(),3);changed();
            }
            imageInfo("Emissive map","emissive_map");
        }
        if(ImGui::CollapsingHeader("Other settings")) {
            const char *alphas[]={"blend","mask","dithered"};
            const char *blends[]={"normal","additive","multiply","fluid"};
            comboField("Alpha mode","alpha_mode",alphas,3);
            comboField("Blend mode","blend_mode",blends,4);
            floatField("Alpha cutoff","alpha_cutoff",.5f,0,1);
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
    void comboField(const char *label,const char *key,const char *const*opts,int count) {
        std::string value=stringValue(selected(),key);
        int current=0;
        for(int i=0;i<count;++i)if(value==opts[i])current=i;
        if(ImGui::Combo(label,&current,opts,count)) {
            selected()[key]=opts[current];changed();
        }
    }

    GLFWwindow *window_;
    std::unique_ptr<MaterialPreview> preview_;
    Document root_=Document::object();
    std::string file_,status_,override_;
    bool loaded_=false,dirty_=false,plane_=false;
    int selection_=-1,environment_=0;
    float yaw_=-.35f,pitch_=.15f;
    ImGuiTextFilter filter_;
};

int main() {
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
