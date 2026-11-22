// Windows-only material editor. No renderer runtime dependencies.
#include "glad.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "native_mat.hpp"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "common/json_incl.hpp"
#include "preview.hpp"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
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
std::string chooseDirectory(GLFWwindow *window) {
    BROWSEINFOW dialog{};
    dialog.hwndOwner=glfwGetWin32Window(window);
    dialog.lpszTitle=L"Choose JK/MotS assets folder (loose MATs, CMPs, GOB/GOO archives)";
    dialog.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE result=SHBrowseForFolderW(&dialog);
    if(!result)return {};
    wchar_t path[MAX_PATH]={};
    const BOOL ok=SHGetPathFromIDListW(result,path);
    CoTaskMemFree(result);
    return ok?toUtf8(path):std::string{};
}
std::string choosePaletteFile(GLFWwindow *window) {
    wchar_t path[32768]={};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=glfwGetWin32Window(window);
    dialog.lpstrFilter=L"JK colormap (*.cmp)\0*.cmp\0All files\0*.*\0\0";
    dialog.lpstrFile=path;
    dialog.nMaxFile=DWORD(sizeof(path)/sizeof(path[0]));
    dialog.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dialog)?toUtf8(path):std::string{};
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
std::string chooseSaveMetadataFile(GLFWwindow *window,const std::string &current) {
    // Default to a sibling copy so Save As is safe without typing a name.
    wchar_t path[32768]={};
    const fs::path currentPath=fs::u8path(current);
    const std::wstring suggestion=
        (currentPath.parent_path()/
         fs::path(currentPath.stem().wstring()+L"-copy.json")).wstring();
    if(suggestion.size()+1<sizeof(path)/sizeof(path[0])) {
        std::copy(suggestion.begin(),suggestion.end(),path);
        path[suggestion.size()]=0;
    }
    OPENFILENAMEW dlg{};
    dlg.lStructSize=sizeof(dlg);
    dlg.hwndOwner=glfwGetWin32Window(window);
    dlg.lpstrFilter=L"JSON material metadata (*.json)\0*.json\0All files\0*.*\0\0";
    dlg.lpstrFile=path;
    dlg.nMaxFile=DWORD(sizeof(path)/sizeof(path[0]));
    dlg.lpstrDefExt=L"json";
    dlg.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&dlg)?toUtf8(path):std::string{};
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
        "clearcoat_surface_map","clearcoat_normal_map","displacement_map"};
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
    p.displacementFactor=number(entry,"displacement_factor",0.0f);
    p.matcapRoughness=number(entry,"matcap_roughness",0.0f);
    const bool coatSurface=entry.find("clearcoat_surface_map")!=entry.end();
    p.coat=number(entry,"clearcoat_strength",coatSurface?1.0f:0.0f);
    p.coatRoughness=number(entry,"clearcoat_roughness",.1f);
    p.coatTransmission=number(entry,"clearcoat_transmission",1.0f);
    std::string alpha=stringValue(entry,"alpha_mode");
    p.alphaMode=alpha=="mask"?1:(alpha=="dithered"?2:0);
    std::string blend=stringValue(entry,"blend_mode");
    p.blendMode=blend=="additive"?1:(blend=="multiply"?2:(blend=="fluid"?3:0));
    p.alphaCutoff=number(entry,"alpha_cutoff",0.5f);
    return p;
}
void setArray(Document &obj,const char *key,const float *values,int size) {
    Document a=Document::array();
    for(int i=0;i<size;++i)a.push_back(values[i]);
    obj[key]=std::move(a);
}
}

enum class PendingAction { None, Exit, Open };

// Turntable orbit keeps the scene fixed in world space.
// The camera moves on a sphere around the origin, world +Y stays up,
// and the view is free of roll. Pitch is clamped at the poles.
constexpr float kOrbitSensitivity=0.009f;
constexpr float kOrbitMaxPitch=1.55334306f; // 89 degrees

class Editor {
public:
    explicit Editor(GLFWwindow *window) : window_(window) {
        preview_=std::make_unique<MaterialPreview>();
        resetView();
        status_="Open a compiled metadata.json to begin.";
    }
    void frame() {
        // Let ImGui's own editable text boxes handle Ctrl+Z while typing.
        if(!ImGui::GetIO().WantTextInput) {
            if(ImGui::IsKeyPressed(ImGuiKey_Space,false) &&
               !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))
                resetView();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_O))open();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_S))saveAs();
            else if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_S))save();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Z))undo();
            if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_Z) ||
               ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Y))redo();
        }

        ImGuiID dock=ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport(),
                                    ImGuiDockNodeFlags_PassthruCentralNode);
        if(!layoutConfigured_) {
            layoutConfigured_=true;
            ImGuiDockNode *root=ImGui::DockBuilderGetNode(dock);
            // Create a proportional three-panel workspace only when there is
            // no previously saved dock layout. Resizing the host viewport
            // automatically resizes all three docked child panels.
            if(root && root->IsLeafNode() && root->Windows.empty()) {
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock,ImGui::GetMainViewport()->Size);
                ImGuiID left=0,rest=0,right=0,center=0;
                ImGui::DockBuilderSplitNode(dock,ImGuiDir_Left,0.20f,&left,&rest);
                ImGui::DockBuilderSplitNode(rest,ImGuiDir_Right,0.29f,&right,&center);
                ImGui::DockBuilderDockWindow("Materials",left);
                ImGui::DockBuilderDockWindow("Live Material Preview",center);
                ImGui::DockBuilderDockWindow("Material Properties",right);
                ImGui::DockBuilderFinish(dock);
            }
        }
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                if(ImGui::MenuItem("Open metadata...","Ctrl+O"))open();
                if(ImGui::MenuItem("Save metadata","Ctrl+S",false,loaded_))save();
                if(ImGui::MenuItem("Save As...","Ctrl+Shift+S",false,loaded_))
                    saveAs();
                if(ImGui::MenuItem("Exit"))requestExit();
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
            if(ImGui::BeginMenu("Tools")) {
                ImGui::MenuItem("Dear ImGui Demo",nullptr,&showDemo_);
                ImGui::MenuItem("Metrics / Debugger",nullptr,&showMetrics_);
                ImGui::MenuItem("Style Editor",nullptr,&showStyleEditor_);
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("JkGfxMod Material Editor  |  Win32 + Dear ImGui + OpenGL 3.3");
            ImGui::EndMainMenuBar();
        }
        renderList();
        renderViewport();
        renderProperties();
        renderUnsavedDialog();
        // These in-app tools show live widget/layout geometry, hover IDs,
        // docking internals and an editable palette for UI experimentation.
        if(showDemo_)ImGui::ShowDemoWindow(&showDemo_);
        if(showMetrics_)ImGui::ShowMetricsWindow(&showMetrics_);
        if(showStyleEditor_) {
            if(ImGui::Begin("Dear ImGui Style Editor",&showStyleEditor_))
                ImGui::ShowStyleEditor();
            ImGui::End();
        }
        // Once the slider/drag is released, the next press is a separate
        // undo action even if it changes the exact same material field.
        if(!ImGui::IsAnyItemActive())groupActive_=false;
    }
    bool exitApproved() const {return exitApproved_;}
    void requestExit() {
        if(exitApproved_)return;
        if(!dirty_) {
            exitApproved_=true;
            glfwSetWindowShouldClose(window_,GLFW_TRUE);
        } else {
            pendingAction_=PendingAction::Exit;
            openUnsavedDialog_=true;
        }
    }

private:
    void finishPendingAction(PendingAction action) {
        if(action==PendingAction::Exit) {
            exitApproved_=true;
            glfwSetWindowShouldClose(window_,GLFW_TRUE);
        } else if(action==PendingAction::Open) {
            openFile();
        }
    }
    void renderUnsavedDialog() {
        if(openUnsavedDialog_) {
            ImGui::OpenPopup("Unsaved changes##confirmation");
            openUnsavedDialog_=false;
        }
        if(!ImGui::BeginPopupModal("Unsaved changes##confirmation",nullptr,
                                  ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const bool exiting=pendingAction_==PendingAction::Exit;
        ImGui::TextUnformatted("There are unsaved pack or material changes.");
        ImGui::TextUnformatted(exiting
            ?"Save a copy, close without saving, or cancel?"
            :"Save a copy, open another pack without saving, or cancel?");
        ImGui::Separator();
        if(ImGui::Button("Save As...",ImVec2(124,0))) {
            // Cancelling the native Save dialog leaves this confirmation open.
            if(saveAs()) {
                const PendingAction action=pendingAction_;
                pendingAction_=PendingAction::None;
                ImGui::CloseCurrentPopup();
                finishPendingAction(action);
            }
        }
        ImGui::SameLine();
        if(ImGui::Button(exiting?"Close without saving":"Open without saving",
                         ImVec2(exiting?170.0f:170.0f,0))) {
            const PendingAction action=pendingAction_;
            pendingAction_=PendingAction::None;
            ImGui::CloseCurrentPopup();
            finishPendingAction(action);
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel",ImVec2(86,0))||
           ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingAction_=PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    void resetView() {
        // Each preview shape has a useful starting orientation.
        // Plane faces the viewer; cube reveals its front/right/top faces.
        orbitYaw_=shape_==PreviewShape::Plane?0.0f:
                  shape_==PreviewShape::Cube?0.55f:0.35f;
        orbitPitch_=shape_==PreviewShape::Plane?0.0f:
                    shape_==PreviewShape::Cube?0.38f:-0.15f;
        dragStartYaw_=orbitYaw_;
        dragStartPitch_=orbitPitch_;
        // The cube needs a little more breathing room than the sphere.
        // Raising the camera target shifts the cube lower in the frame.
        orbitDistance_=shape_==PreviewShape::Cube?4.4f:3.5f;
        orbitTarget_=shape_==PreviewShape::Cube
            ?std::array<float,3>{0.0f,0.18f,0.0f}
            :std::array<float,3>{0.0f,0.0f,0.0f};
        dragStartTarget_=orbitTarget_;
    }
    struct Edit {
        int materialIndex=-1; // -1: pack, otherwise material index
        std::string key;
        Document before, after;
    };
    static constexpr size_t maxHistory=128;
    static Document packSnapshot(const Document &doc) {
        Document state=Document::object();
        for(const char *key:{"name","description","comment","environment_map","images"}) {
            auto it=doc.find(key);
            if(it!=doc.end())state[key]=*it;
        }
        return state;
    }
    static void restorePackSnapshot(Document &doc,const Document &state) {
        // A pack undo must not replace unrelated material changes.
        for(const char *key:{"name","description","comment","environment_map","images"}) {
            auto it=state.find(key);
            if(it!=state.end())doc[key]=*it;
            else doc.erase(key);
        }
    }
    static void validatePackMetadata(const Document &doc) {
        for(const char *required:{"name","description"}) {
            auto it=doc.find(required);
            if(it==doc.end()||!it->is_string())
                throw std::runtime_error(std::string("Pack ")+required+
                                         " must be a string.");
            if(std::string(required)=="name"&&it->get<std::string>().empty())
                throw std::runtime_error("Pack name cannot be empty.");
        }
        for(const char *optional:{"comment","environment_map"}) {
            auto it=doc.find(optional);
            if(it!=doc.end()&&!it->is_string())
                throw std::runtime_error(std::string("Pack ")+optional+
                                         " must be a string.");
        }
        auto images=doc.find("images");
        if(images==doc.end())return;
        if(!images->is_array())
            throw std::runtime_error("Pack images must be an array.");
        std::set<int> ids;
        for(const auto &image:*images) {
            if(!image.is_object()||!image.contains("id")||
               !image["id"].is_number_integer()||
               !image.contains("file")||!image["file"].is_string())
                throw std::runtime_error(
                    "Each registered image must contain an integer id and a string file.");
            const int id=image["id"].get<int>();
            if(id<0||id>65535||!ids.insert(id).second)
                throw std::runtime_error("Pack image IDs must be unique and within 0..65535.");
            const std::string name=image["file"].get<std::string>();
            const fs::path path=fs::u8path(name);
            if(name.empty()||path.is_absolute()||path.has_parent_path()||
               path.filename().empty()||name=="."||name=="..")
                throw std::runtime_error(
                    "Pack image filenames must be nonempty and pack-local.");
        }
    }
    void undo() {
        if(undo_.empty()||!loaded_)return;
        Edit step=std::move(undo_.back());
        undo_.pop_back();
        if(step.materialIndex<0) {
            restorePackSnapshot(root_,step.before);
            restorePackSnapshot(observedRoot_,step.before);
        } else {
            root_["materials"][size_t(step.materialIndex)]=step.before;
            observedRoot_["materials"][size_t(step.materialIndex)]=step.before;
            selection_=step.materialIndex;
        }
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
        if(step.materialIndex<0) {
            restorePackSnapshot(root_,step.after);
            restorePackSnapshot(observedRoot_,step.after);
        } else {
            root_["materials"][size_t(step.materialIndex)]=step.after;
            observedRoot_["materials"][size_t(step.materialIndex)]=step.after;
            selection_=step.materialIndex;
        }
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
    void changedPack(const char *key,bool continuous=false) {
        if(!loaded_)return;
        const Document current=packSnapshot(root_);
        const Document previous=packSnapshot(observedRoot_);
        if(previous==current)return;
        const bool merge=continuous&&groupActive_&&!undo_.empty()&&
                         undo_.back().materialIndex==-1&&undo_.back().key==key;
        if(merge)undo_.back().after=current;
        else {
            if(undo_.size()>=maxHistory)undo_.erase(undo_.begin());
            undo_.push_back({-1,key,previous,current});
        }
        restorePackSnapshot(observedRoot_,current);
        redo_.clear();
        groupActive_=continuous;
        updateDirty();
        syncPreview();
        status_="Unsaved pack changes (Ctrl+S)";
    }
    void syncPreview() {
        if(!validSelection()) {preview_->setMaterial(PreviewMaterial{});return;}
        PreviewMaterial p=fromMetadata(selected(),root_,
                                     fs::u8path(file_).parent_path());
        const Document &entry=selected();
        bool keep=entry.value("keep_albedo",false)||
                  entry.value("use_original_albedo",false);
        if(keep && p.files[BaseMap].empty()) {
            std::string matName;
            int cel=0;
            auto it=entry.find("replaces");
            if(it!=entry.end() && it->is_array() && !it->empty()) {
                const Document &repl=(*it)[0];
                if(repl.is_object()) {
                    matName=stringValue(repl,"name");
                    cel=repl.value("cel",0);
                }
            }
            std::string key=nativeSource_.root()+"|"+nativeSource_.palette()+
                "|"+matName+"|"+std::to_string(cel);
            if(key!=nativeCacheKey_) {
                nativeCacheKey_=key;
                nativeLoaded_={};
                nativeStatus_.clear();
                if(!matName.empty())
                    nativeSource_.load(matName,cel,nativeLoaded_,nativeStatus_);
                else nativeStatus_="Original MAT replacement is not listed.";
            }
            if(nativeLoaded_.width>0) {
                p.nativeSourceKey=key;
                p.nativeWidth=nativeLoaded_.width;
                p.nativeHeight=nativeLoaded_.height;
                p.nativeRGBA=nativeLoaded_.rgba;
            }
        }
        preview_->setMaterial(p);
    }
    void open() {
        if(dirty_) {
            pendingAction_=PendingAction::Open;
            openUnsavedDialog_=true;
            return;
        }
        openFile();
    }
    void openFile() {
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
            nativeCacheKey_.clear();
            syncPreview();
            status_="Loaded "+std::to_string(root_["materials"].size())+" materials.";
        } catch(const std::exception &e) {
            status_=std::string("Open failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),"Material editor",MB_ICONERROR);
        }
    }
    void save() {writeMetadata(file_,false);}
    bool saveAs() {
        if(!loaded_)return false;
        const std::string destination=chooseSaveMetadataFile(window_,file_);
        return !destination.empty()&&writeMetadata(destination,true);
    }
    bool writeMetadata(const std::string &path,bool switchToCopy) {
        if(!loaded_||path.empty())return false;
        try {
            validatePackMetadata(root_);
            fs::path destination=fs::u8path(path);
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
            if(switchToCopy) {
                file_=path;
                // Texture paths are relative to metadata's location.
                nativeCacheKey_.clear();
                syncPreview();
            }
            savedRoot_=root_;
            updateDirty();
            status_=switchToCopy
                ?"Saved copy: "+fs::u8path(path).filename().u8string()
                :"Saved metadata.json. No MAT signatures were regenerated.";
            return true;
        } catch(const std::exception &e) {
            status_=std::string("Save failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),"Save failed",MB_ICONERROR);
            return false;
        }
    }
    void renderList() {
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
            if(ImGui::BeginTabBar("##MaterialsTabs")) {
                if(ImGui::BeginTabItem("Materials")) {
                    ImGui::TextWrapped("Pack: %s",stringValue(root_,"name").c_str());
                    filter_.Draw("Search");
                    if(ImGui::BeginChild("MaterialList",ImVec2(0,-88),true)) {
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
                    ImGui::EndTabItem();
                }
                // Pack metadata belongs with the pack and its materials,
                // not in the inspector for the currently selected material.
                if(ImGui::BeginTabItem("Pack Properties")) {
                    ImGui::BeginChild("PackPropertiesScroll",ImVec2(0,-88),false);
                    renderPackProperties();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        } else {
            ImGui::TextWrapped("Open an existing compiled material pack. The preview updates immediately when you change a property.");
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s",status_.c_str());
        if(dirty_)ImGui::TextColored(ImVec4(1,.76f,.33f,1),"Unsaved changes");
        ImGui::End();
    }
    void renderViewport() {
        ImGui::Begin("Live Material Preview");
        // Larger full-rectangle hit targets than RadioButton's tiny circle.
        // The selected shape stays visibly highlighted.
        const auto shapeButton=[this](const char *label,PreviewShape shape) {
            const bool selected=shape_==shape;
            if(selected)ImGui::PushStyleColor(
                ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const bool pressed=ImGui::Button(label,ImVec2(92,32));
            if(selected)ImGui::PopStyleColor();
            if(pressed && !selected) {
                shape_=shape;
                resetView();
            }
        };
        shapeButton("Sphere",PreviewShape::Sphere);
        ImGui::SameLine();
        shapeButton("Cube",PreviewShape::Cube);
        ImGui::SameLine();
        shapeButton("Plane",PreviewShape::Plane);
        if(ImGui::Button("Reset view (Space)"))resetView();
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
        ImGui::Checkbox("Fresnel",&envFresnel_);
        ImVec2 area=ImGui::GetContentRegionAvail();
        area.x=std::max(area.x,64.0f);
        area.y=std::max(area.y-35.0f,64.0f);
        if(preview_->valid()) {
            preview_->draw(int(area.x),int(area.y),orbitYaw_,orbitPitch_,
                           orbitDistance_,orbitTarget_,shape_,envFresnel_);
            // Image() is a passive item: dragging it moves the ImGui window.
            // An invisible button claims the mouse press for viewport orbiting,
            // while the image is drawn over that item's exact screen rectangle.
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##material_preview_orbit", area,
                ImGuiButtonFlags_MouseButtonLeft|
                ImGuiButtonFlags_MouseButtonMiddle);
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef((ImTextureID)preview_->outputTexture()),
                canvasPos, ImVec2(canvasPos.x + area.x, canvasPos.y + area.y),
                ImVec2(0,1), ImVec2(1,0));
            // Scroll only when the pointer is inside the rendered preview.
            // Multiplicative zoom feels uniform across different distances,
            // and bounds prevent clipping into the sphere or disappearing.
            if(ImGui::IsItemHovered()) {
                const float wheel=ImGui::GetIO().MouseWheel;
                if(wheel!=0.0f)
                    orbitDistance_=std::clamp(
                        orbitDistance_*std::exp(-0.15f*wheel),1.35f,16.0f);
            }
            // Left mouse orbits the camera; middle mouse pans its target
            // across the view plane. Anchoring either action at mouse-down
            // prevents frame-to-frame drift.
            if(ImGui::IsItemActivated()) {
                dragButton_=ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                    ?ImGuiMouseButton_Middle:ImGuiMouseButton_Left;
                dragStartYaw_=orbitYaw_;
                dragStartPitch_=orbitPitch_;
                if(dragButton_==ImGuiMouseButton_Middle) {
                    dragStartTarget_=orbitTarget_;
                    const float cy=std::cos(orbitYaw_),sy=std::sin(orbitYaw_);
                    const float cp=std::cos(orbitPitch_),sp=std::sin(orbitPitch_);
                    dragRight_={cy,0.0f,-sy};
                    dragUp_={-sy*sp,cp,-cy*sp};
                    // 2.9042 is the preview camera's vertical focal scale:
                    // use its actual viewport height for one-to-one drag.
                    dragPanUnitsPerPixel_=2.0f*orbitDistance_/
                        (2.9042f*std::max(area.y,1.0f));
                }
            }
            if(ImGui::IsItemActive()&&
               ImGui::IsMouseDragging(dragButton_,0.0f)) {
                const ImVec2 delta=ImGui::GetMouseDragDelta(dragButton_,0.0f);
                if(dragButton_==ImGuiMouseButton_Middle) {
                    for(size_t i=0;i<3;++i) {
                        orbitTarget_[i]=dragStartTarget_[i]+
                            (-delta.x*dragRight_[i]+delta.y*dragUp_[i])*
                                dragPanUnitsPerPixel_;
                    }
                } else {
                    orbitYaw_=dragStartYaw_-delta.x*kOrbitSensitivity;
                    orbitPitch_=std::clamp(
                        dragStartPitch_+delta.y*kOrbitSensitivity,
                        -kOrbitMaxPitch,kOrbitMaxPitch);
                }
            }
        } else {
            ImGui::TextWrapped("GLSL preview failed: %s",preview_->error().c_str());
        }
        ImGui::TextDisabled("LMB: rotate  |  MMB: move  |  Wheel: zoom  |  Space: reset.");
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
        ImGui::PushID(key);
        // Label above the color fields prevents the long inline label from
        // consuming the final column and squeezing subsequent file paths.
        ImGui::TextUnformatted(label);
        const bool hasOverride=obj.find(key)!=obj.end();
        const float width=ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(std::max(110.0f,width-(hasOverride?32.0f:0.0f)));
        const ImGuiColorEditFlags flags=ImGuiColorEditFlags_NoLabel|
            ImGuiColorEditFlags_Float|(hdr?ImGuiColorEditFlags_HDR:0);
        bool changedValue=count==4?
            ImGui::ColorEdit4("##color",values,flags):
            ImGui::ColorEdit3("##color",values,flags);
        if(changedValue){
            setArray(obj,key,values,count);
            changed(key,ImGui::IsItemActive());
        }
        if(hasOverride) {
            ImGui::SameLine();
            if(ImGui::SmallButton("x")){obj.erase(key);changed(key);}
        }
        // The next TextWrapped/imageInfo item naturally starts on a full line.
        ImGui::PopID();
    }
    void imageInfo(const char *label,const char *key) {
        std::string value=stringValue(selected(),key);
        if(value.empty())return;
        ImGui::TextWrapped("%s: %s",label,value.c_str());
        fs::path path=fs::u8path(file_).parent_path()/fs::u8path(value);
        std::error_code ec;
        if(!fs::exists(path,ec))
            ImGui::TextColored(ImVec4(1,.5f,.35f,1),"[missing texture]");

    }
    void packTextField(const char *label,const char *key,
                       bool multiline=false,bool optional=false) {
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        std::string value=stringValue(root_,key);
        bool edited=multiline
            ?ImGui::InputTextMultiline("##value",&value,ImVec2(-1,84),
                                       ImGuiInputTextFlags_WordWrap)
            :ImGui::InputText("##value",&value);
        if(edited) {
            if(optional&&value.empty())root_.erase(key);
            else root_[key]=value;
            changedPack(key,ImGui::IsItemActive());
        }
        ImGui::PopID();
    }
    void renderPackProperties() {
        ImGui::TextDisabled("Top-level compiled pack metadata");
        packTextField("Name","name");
        packTextField("Description","description",true);
        packTextField("Comment (optional)","comment",true,true);
        packTextField("Default environment map (optional)","environment_map",
                      false,true);
        const std::string env=stringValue(root_,"environment_map");
        if(!env.empty()) {
            std::error_code ec;
            if(!fs::exists(fs::u8path(file_).parent_path()/fs::u8path(env),ec))
                ImGui::TextColored(ImVec4(1,.7f,.3f,1),
                    "Environment image not found in the pack.");
        }
        ImGui::Separator();
        if(ImGui::CollapsingHeader("Registered images")) {
            ImGui::TextWrapped(
                "Pack-local COG images: edit metadata references only. Files are not copied.");
            auto it=root_.find("images");
            if(it!=root_.end()&&!it->is_array()) {
                ImGui::TextColored(ImVec4(1,.5f,.35f,1),
                    "Images is not an array. Repair metadata before editing.");
            } else {
                size_t count=it==root_.end()?0:it->size();
                ImGui::Text("%zu registered image%s",count,count==1?"":"s");
                for(size_t i=0;i<count;++i) {
                    ImGui::PushID(int(i));
                    Document &entry=root_["images"][i];
                    bool removed=false;
                    if(!entry.is_object()) {
                        ImGui::TextColored(ImVec4(1,.5f,.35f,1),
                            "Entry %zu is not an image object",i+1);
                        if(ImGui::SmallButton("Remove invalid entry")) {
                            root_["images"].erase(i);
                            if(root_["images"].empty())root_.erase("images");
                            changedPack("images");
                            removed=true;
                        }
                    } else {
                        int id=-1;
                        if(entry.contains("id")&&entry["id"].is_number_integer())
                            id=entry["id"].get<int>();
                        if(ImGui::InputInt("Image ID",&id)) {
                            entry["id"]=id;
                            changedPack("images",ImGui::IsItemActive());
                        }
                        std::string file=stringValue(entry,"file");
                        if(ImGui::InputText("File",&file)) {
                            entry["file"]=file;
                            changedPack("images",ImGui::IsItemActive());
                        }
                        if(ImGui::SmallButton("Remove image")) {
                            root_["images"].erase(i);
                            if(root_["images"].empty())root_.erase("images");
                            changedPack("images");
                            removed=true;
                        }
                        if(!file.empty()) {
                            std::error_code ec;
                            if(!fs::exists(fs::u8path(file_).parent_path()/
                                           fs::u8path(file),ec))
                                ImGui::TextDisabled("Referenced image is missing.");
                        }
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                    if(removed)break;
                }
                if(ImGui::Button("Add image")) {
                    std::set<int> used;
                    auto existing=root_.find("images");
                    if(existing!=root_.end())
                        for(const auto &image:*existing)
                            if(image.is_object()&&image.contains("id")&&
                               image["id"].is_number_integer())
                                used.insert(image["id"].get<int>());
                    int id=0;
                    while(id<=65535&&used.count(id))++id;
                    if(id<=65535) {
                        if(!root_.contains("images"))
                            root_["images"]=Document::array();
                        root_["images"].push_back({
                            {"id",id},{"file","image_"+std::to_string(id)+".png"}});
                        changedPack("images");
                    }
                }
                ImGui::TextDisabled("IDs: unique integers 0-65535.");
            }
        }
        ImGui::TextDisabled("Unknown fields and MAT signatures are preserved.");
    }
    void renderProperties() {
        ImGui::Begin("Material Properties");
        if(!loaded_)
            ImGui::TextWrapped("Open a compiled pack to inspect material properties.");
        else
            renderMaterialProperties();
        ImGui::End();
    }
    void renderMaterialProperties() {
        if(!validSelection()) {
            ImGui::TextWrapped("Select a material to edit its properties.");
            return;
        }
        const std::string name=stringValue(selected(),"name");
        ImGui::TextWrapped("Material: %s",name.c_str());
        ImGui::TextDisabled("Changes update the preview immediately; save metadata separately.");
        if(ImGui::CollapsingHeader("Albedo",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto v=rgba(selected(),"albedo_factor",{1,1,1,1});
            colorField("Tint / alpha","albedo_factor",v.data(),4);
            imageInfo("Albedo texture","albedo_map");
            bool keeps=selected().value("keep_albedo",false)||
                       selected().value("use_original_albedo",false);
            if(keeps) {
                ImGui::Separator();
                ImGui::TextUnformatted("Original JK MAT albedo (preview only)");
                if(ImGui::Button("Choose game assets folder...")) {
                    std::string directory=chooseDirectory(window_);
                    if(!directory.empty()) {
                        nativeSource_.setRoot(directory);
                        nativeCacheKey_.clear();
                        syncPreview();
                    }
                }
                ImGui::SameLine();
                if(ImGui::Button("Choose CMP...")) {
                    std::string cmp=choosePaletteFile(window_);
                    if(!cmp.empty()) {
                        nativeSource_.setPalette(cmp);
                        nativeCacheKey_.clear();
                        syncPreview();
                    }
                }
                if(!nativeSource_.root().empty()) {
                    ImGui::TextWrapped("Assets: %s (%zu files)",
                        nativeSource_.root().c_str(),nativeSource_.indexedFiles());
                }
                if(!nativeSource_.palette().empty())
                    ImGui::TextWrapped("CMP: %s",nativeSource_.palette().c_str());
                if(nativeLoaded_.width>0)
                    ImGui::TextWrapped("Using: %s",
                                       nativeLoaded_.description.c_str());
                else if(!nativeStatus_.empty())
                    ImGui::TextWrapped("%s",nativeStatus_.c_str());
                ImGui::TextDisabled("MAT/CMP preview does not alter metadata.");
            }
            ImGui::TextDisabled("Existing compiled PNG textures are read-only.");
        }
        if(ImGui::CollapsingHeader("Emissive",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto v=rgb(selected(),"emissive_factor",{0,0,0});
            if(ImGui::DragFloat3("Emissive RGB",v.data(),.05f,0,32,"%.2f")) {
                setArray(selected(),"emissive_factor",v.data(),3);
                changed("emissive_factor",ImGui::IsItemActive());
            }
            imageInfo("Emissive map","emissive_map");
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
        if(ImGui::CollapsingHeader("Parallax mapping",ImGuiTreeNodeFlags_DefaultOpen)) {
            imageInfo("Displacement map","displacement_map");
            ImGui::TextUnformatted("Displacement factor");
            ImGui::PushID("displacement_factor");
            float factor=number(selected(),"displacement_factor",0.0f);
            const bool overridden=selected().contains("displacement_factor");
            ImGui::SetNextItemWidth(std::max(110.0f,
                ImGui::GetContentRegionAvail().x-(overridden?32.0f:0.0f)));
            // The compiler accepts signed, unbounded finite values.
            // Negative factors are routinely used for relief maps.
            if(ImGui::DragFloat("##factor",&factor,0.002f,0,0,"%.4f") &&
               std::isfinite(factor)) {
                selected()["displacement_factor"]=factor;
                changed("displacement_factor",ImGui::IsItemActive());
            }
            if(overridden) {
                ImGui::SameLine();
                if(ImGui::SmallButton("x")) {
                    selected().erase("displacement_factor");
                    changed("displacement_factor");
                }
            }
            ImGui::PopID();
            if(!selected().contains("displacement_map"))
                ImGui::TextDisabled("A displacement texture is required.");
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
    NativeMatSource nativeSource_;
    NativeMatImage nativeLoaded_;
    std::string nativeCacheKey_,nativeStatus_;
    bool loaded_=false,dirty_=false,groupActive_=false,
         layoutConfigured_=false,openUnsavedDialog_=false,exitApproved_=false;
    bool showDemo_=false,showMetrics_=false,showStyleEditor_=false;
    PendingAction pendingAction_=PendingAction::None;
    int selection_=-1,environment_=0;
    PreviewShape shape_=PreviewShape::Sphere;
    bool envFresnel_=false;
    float orbitYaw_=0.35f,orbitPitch_=-0.15f,orbitDistance_=3.5f;
    std::array<float,3> orbitTarget_{0.0f,0.0f,0.0f};
    std::array<float,3> dragStartTarget_{0.0f,0.0f,0.0f};
    std::array<float,3> dragRight_{1.0f,0.0f,0.0f};
    std::array<float,3> dragUp_{0.0f,1.0f,0.0f};
    float dragPanUnitsPerPixel_=0.0f;
    float dragStartYaw_=0.35f,dragStartPitch_=-0.15f;
    ImGuiMouseButton dragButton_=ImGuiMouseButton_Left;
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
        while(true) {
            glfwPollEvents();
            if(glfwWindowShouldClose(window)) {
                if(editor.exitApproved())break;
                glfwSetWindowShouldClose(window,GLFW_FALSE);
                editor.requestExit();
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
