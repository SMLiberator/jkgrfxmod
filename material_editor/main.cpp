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
#include "common/stb_image.h"
#include "preview.hpp"
#include "editor_image.hpp"
#include "source_pack.hpp"
#include "editor_ini_settings.hpp"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <map>
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
// One-time migration source for settings created before portable imgui.ini.
fs::path editorSettingsFile() {
    wchar_t roaming[MAX_PATH]={};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_APPDATA|CSIDL_FLAG_CREATE,
                                nullptr,SHGFP_TYPE_CURRENT,roaming)))
        return {};
    return fs::path(roaming)/L"JkGfxMod"/L"material-editor.json";
}
fs::path executableDirectory() {
    std::wstring buffer(32768,L'\0');
    const DWORD length=GetModuleFileNameW(
        nullptr,buffer.data(),DWORD(buffer.size()));
    if(length==0||length>=buffer.size())
        return fs::current_path();
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}
std::string readableFileSize(std::uintmax_t bytes) {
    if(bytes>=1024*1024)
        return std::to_string((bytes+512*1024)/(1024*1024))+" MiB";
    if(bytes>=1024)
        return std::to_string((bytes+512)/1024)+" KiB";
    return std::to_string(bytes)+" B";
}
std::string imageFormatLabel(const std::string &filename) {
    std::string ext=fs::u8path(filename).extension().u8string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return ext.empty()?"Image":ext.substr(1);
}
void validateSelectedImage(const std::string &filename) {
    std::ifstream file(fs::u8path(filename),std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open selected image.");
    const auto length=file.tellg();
    if(length<=0||static_cast<std::uint64_t>(length)>256ull*1024*1024)
        throw std::runtime_error("Image is empty or too large to import.");
    file.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    file.read(reinterpret_cast<char*>(bytes.data()),length);
    if(!file)throw std::runtime_error("Could not read the image.");
    int width=0,height=0,channels=0;
    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),
                              &width,&height,&channels)||
       width<=0||height<=0||width>16384||height>16384||
       std::uint64_t(width)*std::uint64_t(height)>16777216ull)
        throw std::runtime_error("Unsupported image file or dimensions.");
}
std::string referencedImagePath(const std::string &script,const std::string &chosen) {
    // A filename inside the script's tree becomes a portable relative path.
    // Other locations stay absolute and are referenced without copying.
    const fs::path absolute=fs::absolute(fs::u8path(chosen));
    const fs::path base=fs::absolute(fs::u8path(script).parent_path());
    std::error_code ec;
    fs::path relative=fs::relative(absolute,base,ec);
    if(!ec&&!relative.empty()&&!relative.is_absolute()) {
        const auto first=*relative.begin();
        if(first!="..")
            return relative.generic_u8string();
    }
    return absolute.u8string();
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
std::string chooseFile(GLFWwindow *window,bool image,
                       const std::string &initialDirectory={}) {
    wchar_t path[32768] = {};
    const wchar_t *filter=image
        ? L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr\0All files\0*.*\0\0"
        : L"Material packs (script.json, metadata.json)\0*.json\0All files\0*.*\0\0";
    OPENFILENAMEW dlg{};
    dlg.lStructSize=sizeof(dlg);
    dlg.hwndOwner=glfwGetWin32Window(window);
    dlg.lpstrFilter=filter;
    dlg.lpstrFile=path;
    dlg.nMaxFile=DWORD(sizeof(path)/sizeof(path[0]));
    const std::wstring initial=initialDirectory.empty()?std::wstring{}:
        fs::u8path(initialDirectory).wstring();
    dlg.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
    dlg.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dlg)?toUtf8(path):std::string{};
}
std::string chooseSaveMetadataFile(GLFWwindow *window,const std::string &current,
                                   bool sourceMode) {
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
    dlg.lpstrFilter=sourceMode
        ?L"Source material pack (*.json)\0*.json\0All files\0*.*\0\0"
        :L"Compiled material metadata (*.json)\0*.json\0All files\0*.*\0\0";
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
std::array<float,2> scrollSpeed(const Document &entry,const char *key) {
    const auto it=entry.find(key);
    if(it==entry.end()||!it->is_array()||it->size()!=2)return {};
    std::array<float,2> result{};
    for(int i=0;i<2;++i) {
        if(!(*it)[i].is_number())return {};
        try {
            result[i]=(*it)[i].get<float>();
            if(!std::isfinite(result[i]))return {};
        } catch(...) {return {}; }
    }
    return result;
}
editor_animation::Pulse pulseSettings(const Document &entry) {
    editor_animation::Pulse result;
    auto it=entry.find("emissive_pulse");
    if(it==entry.end()||!it->is_object())return result;
    const std::string wave=stringValue(*it,"wave");
    if(wave=="square")result.wave=editor_animation::Wave::Square;
    else if(wave=="flicker")result.wave=editor_animation::Wave::Flicker;
    else if(wave=="random")result.wave=editor_animation::Wave::Random;
    result.rate=number(*it,"rate",1.0f);
    result.min=number(*it,"min",0.0f);
    result.max=number(*it,"max",1.0f);
    result.phase=number(*it,"phase",0.0f);
    result.enabled=result.rate>0.0f&&result.min>=0.0f&&
                   result.max>=result.min;
    return result;
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
    p.albedoScroll=scrollSpeed(entry,"albedo_scroll");
    p.emissiveScroll=scrollSpeed(entry,"emissive_scroll");
    p.normalScroll=scrollSpeed(entry,"normal_scroll");
    p.normalScroll2=scrollSpeed(entry,"normal_scroll2");
    p.emissivePulse=pulseSettings(entry);
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
    p.alphaMode=alpha=="mask"?1:((alpha=="dithered"||alpha=="dither")?2:0);
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
    explicit Editor(GLFWwindow *window,editor_ini::Settings &settings)
        :window_(window),settings_(settings) {
        preview_=std::make_unique<MaterialPreview>();
        resetView();
        status_="Open script.json or metadata.json to begin.";
        loadUserSettings();
    }
    void frame() {
        if(animationPlaying_)
            animationSeconds_+=std::max(0.0f,ImGui::GetIO().DeltaTime);
        // Let ImGui''s own editable text boxes handle Ctrl+Z while typing.
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
                if(ImGui::MenuItem("Open material pack...","Ctrl+O"))open();
                if(ImGui::MenuItem(isSource()?"Save source pack":"Save metadata",
                                   "Ctrl+S",false,loaded_))save();
                if(ImGui::MenuItem("Save As...","Ctrl+Shift+S",false,loaded_))
                    saveAs();
                if(ImGui::MenuItem("Revert",nullptr,false,loaded_))
                    requestRevertPack();
                ImGui::Separator();
                if(ImGui::BeginMenu("JK / MotS game assets")) {
                    renderGameAssetMenu();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
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
                ImGui::Separator();
                if(ImGui::MenuItem("Revert Material",nullptr,false,
                                   canRevertMaterial()))
                    revertSelectedMaterial();
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
        renderRevertDialog();
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
    void loadUserSettings() {
        // The INI handler was already registered and loaded before the
        // Editor was constructed. Only migrate the old AppData JSON if there
        // is no custom section yet; do not remove that file.
        bool migrated=false;
        if(!settings_.hasIniSection) {
            const auto oldPath=editorSettingsFile();
            if(!oldPath.empty()) {
                try {
                    std::ifstream file(oldPath,std::ios::binary);
                    if(file) {
                        Document doc;file>>doc;
                        if(doc.is_object()) {
                            const auto folder=stringValue(doc,"game_assets_folder");
                            if(!folder.empty())
                                settings_.assetRoots.push_back(folder);
                            settings_.palette=stringValue(doc,"colormap_file");
                            migrated=true;
                        }
                    }
                } catch(const std::exception &e) {
                    status_=std::string("Legacy settings import failed: ")+e.what();
                }
            }
        }
        nativeSource_.setRoots(settings_.assetRoots);
        settings_.assetRoots=nativeSource_.roots();
        nativeSource_.setPalette(settings_.palette);
        if(migrated) {
            settings_.hasIniSection=true;
            ImGui::MarkIniSettingsDirty();
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
            status_="Migrated old game paths to portable imgui.ini.";
        }
    }
    void persistGameSettings(bool indexRoots=true) {
        if(indexRoots) {
            nativeSource_.setRoots(settings_.assetRoots);
            settings_.assetRoots=nativeSource_.roots();
        }
        nativeSource_.setPalette(settings_.palette);
        nativeCacheKey_.clear();
        nativeLoaded_={};
        nativeStatus_.clear();
        if(loaded_)syncPreview();
        settings_.hasIniSection=true;
        // Use Dear ImGui's own serializer; it retains docking/window/table
        // state plus our custom [JkGfxModEditor][Config] section.
        ImGui::MarkIniSettingsDirty();
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        status_="Game asset settings saved to portable imgui.ini.";
    }
    void chooseGameAssetsFolder() {
        const std::string directory=chooseDirectory(window_);
        if(directory.empty())return;
        const std::string absolute=
            fs::absolute(fs::u8path(directory)).lexically_normal().u8string();
        if(std::find(settings_.assetRoots.begin(),settings_.assetRoots.end(),
                     absolute)!=settings_.assetRoots.end()) {
            status_="That game assets folder is already in the list.";
            return;
        }
        settings_.assetRoots.push_back(absolute);
        persistGameSettings();
    }
    void chooseGamePalette() {
        const std::string path=choosePaletteFile(window_);
        if(path.empty())return;
        settings_.palette=
            fs::absolute(fs::u8path(path)).lexically_normal().u8string();
        persistGameSettings(false);
    }
    void renderGameAssetMenu() {
        if(ImGui::MenuItem("Add assets folder..."))
            chooseGameAssetsFolder();
        ImGui::TextDisabled("Earlier folders override later folders");
        if(settings_.assetRoots.empty())
            ImGui::TextDisabled("No game asset folders configured");
        for(size_t i=0;i<settings_.assetRoots.size();++i) {
            ImGui::PushID(int(i));
            const auto &full=settings_.assetRoots[i];
            const std::string title=std::to_string(i+1)+". "+
                fs::u8path(full).filename().u8string();
            bool changed=false;
            if(ImGui::BeginMenu(title.c_str())) {
                ImGui::TextWrapped("%s",full.c_str());
                ImGui::Separator();
                std::error_code ec;
                if(!fs::is_directory(fs::u8path(full),ec))
                    ImGui::TextDisabled("Folder is currently unavailable");
                if(ImGui::MenuItem("Move up (higher priority)",nullptr,false,i>0)){
                    std::swap(settings_.assetRoots[i],settings_.assetRoots[i-1]);
                    changed=true;
                }
                if(ImGui::MenuItem("Move down (lower priority)",nullptr,false,
                                    i+1<settings_.assetRoots.size())){
                    std::swap(settings_.assetRoots[i],settings_.assetRoots[i+1]);
                    changed=true;
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Remove this folder")){
                    settings_.assetRoots.erase(settings_.assetRoots.begin()+i);
                    changed=true;
                }
                ImGui::EndMenu();
            }
            ImGui::PopID();
            if(changed) {
                persistGameSettings();
                break;
            }
        }
        if(ImGui::MenuItem("Remove all asset folders",nullptr,false,
                            !settings_.assetRoots.empty())) {
            settings_.assetRoots.clear();
            persistGameSettings();
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Choose colormap (CMP)..."))
            chooseGamePalette();
        if(!settings_.palette.empty()) {
            ImGui::TextWrapped("CMP: %s",
                              fs::u8path(settings_.palette).filename().u8string().c_str());
            if(ImGui::MenuItem("Clear CMP")) {
                settings_.palette.clear();
                persistGameSettings(false);
            }
        } else {
            ImGui::TextDisabled("CMP: automatic dflt.cmp lookup");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Stored in imgui.ini beside the executable");
    }
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
    void renderRevertDialog() {
        if(openRevertDialog_) {
            ImGui::OpenPopup("Revert pack##confirmation");
            openRevertDialog_=false;
        }
        if(!ImGui::BeginPopupModal("Revert pack##confirmation",nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped("Reload %s from disk?",
                           fs::u8path(file_).filename().u8string().c_str());
        ImGui::TextWrapped("All unsaved material and pack changes will be lost.");
        ImGui::TextWrapped("The pack's Undo/Redo history will be cleared.");
        ImGui::Separator();
        if(ImGui::Button("Revert changes",ImVec2(150,0))) {
            if(revertPack())ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel",ImVec2(90,0))||
           ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
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
        // Each shape starts with a useful upright orientation and fixed zoom.
        // Do not manually offset Cube in world Y: its perspective silhouette
        // has an off-center screen-space bounding rectangle even at origin.
        orbitYaw_=shape_==PreviewShape::Plane?0.0f:
                  shape_==PreviewShape::Cube?0.55f:0.35f;
        orbitPitch_=shape_==PreviewShape::Plane?0.0f:
                    shape_==PreviewShape::Cube?0.38f:-0.15f;
        orbitDistance_=shape_==PreviewShape::Cube?4.4f:3.5f;
        orbitTarget_=shape_==PreviewShape::Cube
            ?cubeCenteredTarget(orthographic_)
            :std::array<float,3>{0.0f,0.0f,0.0f};
        dragStartYaw_=orbitYaw_;
        dragStartPitch_=orbitPitch_;
        dragStartTarget_=orbitTarget_;
    }
    std::array<float,3> cubeCenteredTarget(bool orthographic) const {
        std::array<float,3> target={0.0f,0.0f,0.0f};
        // Center the *projected* eight cube corners, not the raw world-space
        // center. In perspective the visible bounding rectangle is offset
        // towards nearer corners; symmetric geometry isn't screen-centered.
        const float cy=std::cos(orbitYaw_),sy=std::sin(orbitYaw_);
        const float cp=std::cos(orbitPitch_),sp=std::sin(orbitPitch_);
        const std::array<float,3> right={cy,0.0f,-sy};
        const std::array<float,3> up={-sy*sp,cp,-cy*sp};
        const std::array<float,3> back={sy*cp,sp,cy*cp};
        const auto dot=[](const std::array<float,3> &a,
                          const std::array<float,3> &b) {
            return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
        };
        constexpr float h=kPreviewCubeHalfExtent;
        // A few fixed iterations converge to the optical center without a
        // magic offset and without depending on viewport width or height.
        for(int iteration=0;iteration<3;++iteration) {
            float xmin=std::numeric_limits<float>::infinity();
            float xmax=-xmin,ymin=xmin,ymax=-xmin;
            for(float x:{-h,h})for(float y:{-h,h})for(float z:{-h,h}) {
                const std::array<float,3> delta={
                    x-target[0],y-target[1],z-target[2]};
                const float depth=orthographic?orbitDistance_:
                    orbitDistance_-dot(delta,back);
                const float sx=dot(delta,right)/depth;
                const float sy=dot(delta,up)/depth;
                xmin=std::min(xmin,sx);xmax=std::max(xmax,sx);
                ymin=std::min(ymin,sy);ymax=std::max(ymax,sy);
            }
            const float shiftX=(xmin+xmax)*0.5f*orbitDistance_;
            const float shiftY=(ymin+ymax)*0.5f*orbitDistance_;
            for(size_t axis=0;axis<3;++axis)
                target[axis]+=shiftX*right[axis]+shiftY*up[axis];
        }
        return target;
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
        for(const char *key:editor_source::inheritedProperties) {
            auto it=doc.find(key);
            if(it!=doc.end())state[key]=*it;
        }
        for(const char *key:{"depth_bias","use_original_albedo"}) {
            auto it=doc.find(key);
            if(it!=doc.end())state[key]=*it;
        }
        return state;
    }
    static void restorePackSnapshot(Document &doc,const Document &state) {
        // A pack undo must not replace unrelated material changes.
        const auto restore=[&](const char *key) {
            auto it=state.find(key);
            if(it!=state.end())doc[key]=*it;
            else doc.erase(key);
        };
        for(const char *key:{"name","description","comment","environment_map","images"})
            restore(key);
        for(const char *key:editor_source::inheritedProperties)
            restore(key);
        restore("depth_bias");
        restore("use_original_albedo");
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
    bool isSource() const {return mode_==editor_source::Kind::Source;}
    const Document &effectiveSelected() const {return effectiveEntry_;}
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
    bool canRevertMaterial() const {
        return validSelection()&&savedRoot_.contains("materials")&&
               savedRoot_["materials"].is_array()&&
               size_t(selection_)<savedRoot_["materials"].size()&&
               selected()!=savedRoot_["materials"][size_t(selection_)];
    }
    void revertSelectedMaterial() {
        if(!canRevertMaterial())return;
        const auto index=size_t(selection_);
        if(!editor_source::revertMaterial(root_,savedRoot_,index))return;
        // Record as one ordinary material edit, so Ctrl+Z restores the
        // previous unsaved state without affecting other materials or defaults.
        changed("Revert Material");
        status_="Restored material from last save (Undo available).";
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
        if(!validSelection()) {
            effectiveEntry_=Document::object();
            preview_->setMaterial(PreviewMaterial{});
            return;
        }
        effectiveEntry_=editor_source::effectiveMaterial(root_,selected(),mode_);
        PreviewMaterial p=fromMetadata(effectiveEntry_,root_,
                                     fs::u8path(file_).parent_path());
        for(size_t i=0;i<TextureCount;++i) {
            const fs::path assigned=fs::u8path(p.files[i]).lexically_normal();
            const auto name=assigned.filename().u8string();
            const auto found=stagedImages_.find(name);
            if(found==stagedImages_.end())continue;
            const auto expected=(fs::u8path(file_).parent_path()/
                                 fs::u8path(name)).lexically_normal();
            // A user may reference an external texture with the same
            // basename as a draft. Only substitute a draft for its exact
            // reserved location beside this pack's JSON.
            if(assigned==expected)
                p.stagedImages[i]=found->second;
        }
        // Source textures are straight-alpha input; compiled color PNGs
        // have been premultiplied by the pack compiler.
        p.albedoPremultiplied=!isSource();
        // Game material-instance IDs can differ, but the preview's seed
        // stays stable across edits and mirrors the same random waveform.
        p.pulseSeed=std::uint64_t(selection_)+1;
        const Document &entry=effectiveEntry_;
        bool keep=entry.value("keep_albedo",false)||
                  entry.value("use_original_albedo",false);
        if(keep) {
            p.files[BaseMap].clear();
            p.stagedImages[BaseMap].reset();

            std::string matName;
            int cel=0;
            auto it=entry.find("replaces");
            if(it!=entry.end() && it->is_array() && !it->empty()) {
                const Document &repl=(*it)[0];
                if(repl.is_object()) {
                    matName=stringValue(repl,"name");
                    cel=repl.value("cel",0);
                } else if(repl.is_string()) {
                    matName=repl.get<std::string>();
                } else if(repl.is_array()&&repl.size()==2&&
                          repl[0].is_string()&&repl[1].is_number_integer()) {
                    matName=repl[0].get<std::string>();
                    cel=repl[1].get<int>();
                }
            }
            std::string key=nativeSource_.palette();
            for(const auto &root:nativeSource_.roots())
                key+="|"+root;
            key+="|"+matName+"|"+std::to_string(cel);
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
    void activatePack(Document doc,const std::string &path,
                      int preferredIndex=-1,const std::string &preferredName={}) {
        const auto kind=editor_source::detect(fs::u8path(path),doc);
        int target=doc["materials"].empty()?-1:0;
        if(target>=0&&preferredIndex>=0) {
            target=std::min(preferredIndex,int(doc["materials"].size())-1);
            if(!preferredName.empty()) {
                for(size_t i=0;i<doc["materials"].size();++i) {
                    const auto &mat=doc["materials"][i];
                    if(mat.is_object()&&stringValue(mat,"name")==preferredName) {
                        target=int(i);
                        break;
                    }
                }
            }
        }
        root_=std::move(doc);
        stagedImages_.clear();
        committedImageNames_.clear();
        mode_=kind;
        observedRoot_=root_;savedRoot_=root_;
        undo_.clear();redo_.clear();groupActive_=false;
        sectionSelection_=-2;
        sectionSeen_.fill(false);
        sectionActive_.fill(false);
        inspectedChannel_=0;
        file_=path;loaded_=true;dirty_=false;
        selection_=target;
        nativeCacheKey_.clear();
        nativeLoaded_={};nativeStatus_.clear();
        syncPreview();
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
            Document doc=editor_source::readPack(fs::u8path(path));
            activatePack(std::move(doc),path);
            status_=std::string(isSource()?"Opened source script.json: ":
                                 "Opened compiled metadata.json: ")+
                    std::to_string(root_["materials"].size())+" materials.";
        } catch(const std::exception &e) {
            status_=std::string("Open failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),
                        "Material editor",MB_ICONERROR);
        }
    }
    void requestRevertPack() {
        if(!loaded_)return;
        if(dirty_)openRevertDialog_=true;
        else revertPack(); // Also useful to reload external disk edits.
    }
    bool revertPack() {
        if(!loaded_)return false;
        try {
            // Read before touching the current document. A missing, locked,
            // or malformed disk file must leave all unsaved edits intact.
            Document disk=editor_source::readPack(fs::u8path(file_));
            const int index=selection_;
            const std::string name=validSelection()?
                stringValue(selected(),"name"):std::string{};
            activatePack(std::move(disk),file_,index,name);
            status_="Reloaded pack from disk; Undo/Redo history cleared.";
            return true;
        } catch(const std::exception &e) {
            status_=std::string("Revert failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),
                        "Revert failed",MB_ICONERROR);
            return false;
        }
    }
    void save() {writeMetadata(file_,false);}
    bool saveAs() {
        if(!loaded_)return false;
        const std::string destination=chooseSaveMetadataFile(window_,file_,isSource());
        return !destination.empty()&&writeMetadata(destination,true);
    }
    static bool documentReferences(const Document &node,
                                   const std::string &filename) {
        if(node.is_string())return node.get<std::string>()==filename;
        if(node.is_object()||node.is_array())
            for(const auto &member:node)
                if(documentReferences(member,filename))return true;
        return false;
    }
    bool writeMetadata(const std::string &path,bool switchToCopy) {
        if(!loaded_||path.empty())return false;
        std::vector<fs::path> temporaryFiles;
        std::vector<fs::path> newlyWrittenFiles;
        std::vector<std::string> writtenNames;
        try {
            // Source save must keep compiler-relative asset paths intact.
            // Copying only script.json to another directory silently breaks
            // map/environment/registry resolution: refuse such a Save As.
            if(isSource()&&switchToCopy) {
                const auto originalDir=editor_source::lowerAscii(
                    fs::weakly_canonical(fs::u8path(file_).parent_path())
                        .generic_u8string());
                const auto copiedDir=editor_source::lowerAscii(
                    fs::weakly_canonical(fs::u8path(path).parent_path())
                        .generic_u8string());
                if(originalDir!=copiedDir)
                    throw std::runtime_error(
                        "Source Save As must stay in the same folder as the "
                        "original script and textures. Choose another filename "
                        "in that folder to preserve relative asset paths.");
            }
            editor_source::validate(root_,mode_);
            const fs::path destination=fs::u8path(path);
            const fs::path directory=destination.parent_path();
            fs::path temp=destination;
            temp+=L".tmp";
            // Only referenced drafts are materialized. Prepare PNGs and JSON
            // before changing any final file; commit the JSON last.
            for(const auto &draft:stagedImages_) {
                if(!documentReferences(root_,draft.first)||
                   committedImageNames_.count(draft.first))
                    continue;
                const fs::path output=directory/fs::u8path(draft.first);
                if(fs::exists(output))
                    throw std::runtime_error(
                        "Imported filename already exists: "+draft.first);
                fs::path staging=output;
                staging+=L".jkgm-tmp";
                if(fs::exists(staging))
                    throw std::runtime_error(
                        "Temporary import file already exists: "+draft.first);
                const auto encoded=editor_images::png(*draft.second);
                {
                    std::ofstream stream(staging,std::ios::binary|std::ios::trunc);
                    if(!stream)
                        throw std::runtime_error("Cannot stage image: "+draft.first);
                    temporaryFiles.push_back(staging);
                    stream.write(reinterpret_cast<const char *>(encoded.data()),
                                 std::streamsize(encoded.size()));
                    stream.flush();
                    if(!stream)
                        throw std::runtime_error("Cannot write PNG: "+draft.first);
                }
                writtenNames.push_back(draft.first);
            }
            {
                std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
                if(!stream)throw std::runtime_error("Cannot write temporary JSON.");
                temporaryFiles.push_back(temp);
                stream<<root_.dump(4)<<"\n";
                stream.flush();
                if(!stream)throw std::runtime_error("Failed to write JSON.");
            }
            for(const auto &filename:writtenNames) {
                const fs::path output=directory/fs::u8path(filename);
                fs::path staging=output;staging+=L".jkgm-tmp";
                if(!MoveFileExW(staging.c_str(),output.c_str(),
                                MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error(
                        "Could not commit PNG: "+filename);
                newlyWrittenFiles.push_back(output);
            }
            if(!MoveFileExW(temp.c_str(),destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error(
                    "Cannot replace the JSON file. Close other programs using it.");
            // JSON now references only images confirmed present on disk.
            newlyWrittenFiles.clear();
            temporaryFiles.clear();
            for(const auto &name:writtenNames)
                committedImageNames_.insert(name);
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
                :isSource()?"Saved script.json (source; not yet compiled)."
                           :"Saved metadata.json. No MAT signatures were regenerated.";
            return true;
        } catch(const std::exception &e) {
            std::error_code ec;
            for(const auto &tmp:temporaryFiles)fs::remove(tmp,ec);
            for(const auto &created:newlyWrittenFiles)fs::remove(created,ec);
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
            ImGui::TextColored(
                isSource()?ImVec4(.55f,.88f,.72f,1):ImVec4(.68f,.75f,1,1),
                "%s",isSource()?"SOURCE PACK":"COMPILED PACK");
            ImGui::SameLine();
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
            ImGui::TextWrapped("Open script.json for source editing or metadata.json to edit an existing compiled material pack.");
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
        // Group related controls into responsive columns rather than a long,
        // irregular sequence of left-aligned rows. Narrow dock layouts
        // automatically stack groups without squeezing the preview buttons.
        const float toolbarWidth=ImGui::GetContentRegionAvail().x;
        const int toolbarColumns=toolbarWidth>=980.0f?3:
                                 toolbarWidth>=610.0f?2:1;
        if(ImGui::BeginTable("##previewControls",toolbarColumns,
                             ImGuiTableFlags_SizingStretchSame|
                             ImGuiTableFlags_PadOuterX)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("SHAPE");
            shapeButton("Sphere",PreviewShape::Sphere);
            ImGui::SameLine();
            shapeButton("Cube",PreviewShape::Cube);
            ImGui::SameLine();
            shapeButton("Plane",PreviewShape::Plane);
            if(ImGui::Button("Reset view (Space)"))resetView();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("ENVIRONMENT");
            ImGui::SetNextItemWidth(150);
            const char *options[]={"Studio","Outdoor","Night"};
            if(ImGui::Combo("Fallback##environment",&environment_,options,3))
                preview_->setPreset(environment_);
            if(ImGui::Button("Override envmap...")) {
                std::string path=chooseFile(window_,true);
                if(!path.empty()) {
                    preview_->setEnvironmentOverride(path);override_=path;
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Use pack envmap")) {
                override_.clear();preview_->setEnvironmentOverride({});
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("ANIMATION & VIEW");
            ImGui::Checkbox("Animate",&animationPlaying_);
            ImGui::SameLine();
            if(ImGui::Button("Restart animation"))animationSeconds_=0.0;
            ImGui::Checkbox("Fresnel",&envFresnel_);
            ImGui::SameLine();
            if(ImGui::Checkbox("Orthographic",&orthographic_) &&
               shape_==PreviewShape::Cube) {
                // Retain pan when changing the cube's optical projection.
                const auto previous=cubeCenteredTarget(!orthographic_);
                const auto next=cubeCenteredTarget(orthographic_);
                for(size_t axis=0;axis<3;++axis)
                    orbitTarget_[axis]+=next[axis]-previous[axis];
                dragStartTarget_=orbitTarget_;
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImVec2 area=ImGui::GetContentRegionAvail();
        area.x=std::max(area.x,64.0f);
        area.y=std::max(area.y-35.0f,64.0f);
        if(preview_->valid()) {
            preview_->draw(int(area.x),int(area.y),orbitYaw_,orbitPitch_,
                           orbitDistance_,orbitTarget_,shape_,envFresnel_,
                           orthographic_,animationSeconds_);
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
        float v=number(effectiveSelected(),key,fallback);
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
    // A single compact signed UV speed pair. Removing the local value
    // restores an inherited pack speed in source mode.
    void scrollField(const char *label,const char *key,bool packField=false) {
        Document &obj=packField?root_:selected();
        const Document &resolved=packField?root_:effectiveSelected();
        auto speed=scrollSpeed(resolved,key);
        const bool own=obj.contains(key);
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(std::max(100.0f,
            ImGui::GetContentRegionAvail().x-(own?32.0f:0.0f)));
        if(ImGui::DragFloat2("##speed",speed.data(),0.01f,0,0,"%.3f")&&
           std::isfinite(speed[0])&&std::isfinite(speed[1])) {
            setArray(obj,key,speed.data(),2);
            if(packField)changedPack(key,ImGui::IsItemActive());
            else changed(key,ImGui::IsItemActive());
        }
        if(own) {
            ImGui::SameLine();
            if(ImGui::SmallButton("x")) {
                obj.erase(key);
                if(packField)changedPack(key);
                else changed(key);
            }
        }
        ImGui::PopID();
    }
    void pulseField(bool packField=false) {
        constexpr const char *key="emissive_pulse";
        Document &obj=packField?root_:selected();
        // Snapshot resolved settings before calling changed()/changedPack(),
        // which refreshes the effective material immediately.
        const Document &effective=packField?root_:effectiveSelected();
        Document pulse=effective.value(key,Document());
        if(!pulse.is_object())pulse=Document::object();
        const bool active=pulse.contains("wave");
        const bool own=obj.contains(key);
        ImGui::PushID(key);
        ImGui::TextUnformatted("Emissive pulse");
        if(!active) {
            if(ImGui::SmallButton("Add pulse")) {
                obj[key]={{"wave","sine"},{"rate",1.0f},
                          {"min",0.0f},{"max",1.0f},{"phase",0.0f}};
                if(packField)changedPack(key);
                else changed(key);
            }
            ImGui::PopID();
            return;
        }
        if(own) {
            ImGui::SameLine();
            if(ImGui::SmallButton("Remove pulse")) {
                obj.erase(key);
                if(packField)changedPack(key);
                else changed(key);
                ImGui::PopID();
                return;
            }
        } else if(!packField&&isSource()&&root_.contains(key)) {
            ImGui::SameLine();
            if(ImGui::SmallButton("Neutralize inherited pulse")) {
                obj[key]={{"wave","sine"},{"rate",1.0f},
                          {"min",1.0f},{"max",1.0f},{"phase",0.0f}};
                changed(key);
                ImGui::PopID();
                return;
            }
        }
        const char *waves[]={"Sine","Square","Flicker","Random"};
        const char *values[]={"sine","square","flicker","random"};
        int chosen=0;
        const std::string wave=stringValue(pulse,"wave");
        for(int i=0;i<4;++i)if(wave==values[i])chosen=i;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if(ImGui::Combo("Wave",&chosen,waves,4)) {
            pulse["wave"]=values[chosen];
            obj[key]=pulse;
            if(packField)changedPack(key);
            else changed(key);
        }
        // Inputs are committed only when valid under compile/main.cpp's
        // contract: rate > 0, 0 <= min <= max, and finite phase.
        const auto editNumber=[&](const char *label,const char *field,
                                  float fallback) {
            float value=number(pulse,field,fallback);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if(!ImGui::DragFloat(label,&value,0.01f,0,0,"%.3f")||
               !std::isfinite(value))return;
            const std::string name(field);
            if(name=="rate"&&value<=0.0f)value=0.001f;
            if(name=="min") {
                value=std::max(0.0f,value);
                pulse["max"]=std::max(value,number(pulse,"max",1.0f));
            }
            if(name=="max")value=std::max(value,number(pulse,"min",0.0f));
            pulse[field]=value;
            obj[key]=pulse;
            if(packField)changedPack(key,ImGui::IsItemActive());
            else changed(key,ImGui::IsItemActive());
        };
        editNumber("Rate (seconds / cycle)","rate",1.0f);
        editNumber("Minimum multiplier","min",0.0f);
        editNumber("Maximum multiplier","max",1.0f);
        editNumber("Phase (cycles)","phase",0.0f);
        ImGui::PopID();
    }
    static int imageSlot(const char *key) {
        const std::array<std::pair<const char*,PreviewTexture>,11> slots{{
            {"albedo_map",BaseMap},{"surface_map",SurfaceMap},
            {"environment_map",EnvironmentMap},{"normal_map",NormalMap},
            {"normal_map2",NormalMap2},{"emissive_map",EmissiveMap},
            {"matcap_map",MatcapMap},{"matcap_surface_map",MatcapSurfaceMap},
            {"clearcoat_surface_map",CoatSurfaceMap},
            {"clearcoat_normal_map",CoatNormalMap},
            {"displacement_map",DisplacementMap}
        }};
        for(const auto &item:slots)
            if(std::string(key)==item.first)return int(item.second);
        return -1;
    }
    static editor_images::Role textureRole(const char *key) {
        const std::string field(key);
        if(field=="albedo_map")
            return editor_images::Role::Albedo;
        if(field=="displacement_map")
            return editor_images::Role::Height;
        if(field=="emissive_map"||field=="matcap_map"||
           field=="environment_map")
            return editor_images::Role::Color;
        return editor_images::Role::Data;
    }
    std::string reservedTextureName(const std::string &selectedFile) const {
        const fs::path original=fs::u8path(selectedFile);
        std::string name=original.stem().u8string();
        if(name.empty())name="texture";
        // Avoid invalid Windows filename chars in imported output names.
        for(char &ch:name)
            if(ch=='<'||ch=='>'||ch==':'||ch=='"'||ch=='/'||
               ch=='\\'||ch=='|'||ch=='?'||ch=='*')
                ch='_';
        const auto directory=fs::u8path(file_).parent_path();
        for(unsigned suffix=1;suffix<100000;++suffix) {
            const auto proposed=name+"-import-"+std::to_string(suffix)+".png";
            if(stagedImages_.count(proposed))continue;
            std::error_code ec;
            if(!fs::exists(directory/fs::u8path(proposed),ec))
                return proposed;
        }
        throw std::runtime_error("Could not reserve a unique texture filename.");
    }
    void stageTexture(const char *key,bool packField) {
        if(!loaded_)return;
        const std::string chosen=chooseFile(
            window_,true,fs::u8path(file_).parent_path().u8string());
        if(chosen.empty())return;
        try {
            const auto role=textureRole(key);
            auto image=std::make_shared<const editor_images::Image>(
                editor_images::load(fs::u8path(chosen),role,isSource()));
            const std::string name=reservedTextureName(chosen);
            // Reference the eventual pack-local PNG without writing it yet.
            stagedImages_[name]=std::move(image);
            Document &obj=packField?root_:selected();
            if(std::string(key)=="albedo_map")
                editor_source::chooseAlbedoImage(obj,name);
            else obj[key]=name;
            if(packField)changedPack(key);
            else changed(key);
            status_="Staged texture in memory ("+std::to_string(
                stagedImages_[name]->channels)+" channels). Save to write PNG.";
        } catch(const std::exception &e) {
            status_=std::string("Texture import failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),
                        "Material texture",MB_ICONERROR);
        }
    }
    void referenceSourceTexture(const char *key,bool packField) {
        if(!isSource())return;
        const auto chosen=chooseFile(
            window_,true,fs::u8path(file_).parent_path().u8string());
        if(chosen.empty())return;
        try {
            validateSelectedImage(chosen);
            const std::string stored=referencedImagePath(file_,chosen);
            Document &obj=packField?root_:selected();
            if(std::string(key)=="albedo_map")
                editor_source::chooseAlbedoImage(obj,stored);
            else obj[key]=stored;
            if(packField)changedPack(key);
            else changed(key);
            status_="Referenced source texture without copying.";
        } catch(const std::exception &e) {
            status_=std::string("Reference failed: ")+e.what();
            MessageBoxA(glfwGetWin32Window(window_),status_.c_str(),
                        "Material texture",MB_ICONERROR);
        }
    }
    // Material slots are intentionally compact: thumbnail + Load / Remove.
    // Details and manual source references live behind the thumbnail instead
    // of occupying several lines for every texture.
    void sourceTextureActions(const char *key,bool packField) {
        if(!loaded_)return;
        ImGui::PushID("##textureActions");
        if(ImGui::SmallButton("Load"))
            stageTexture(key,packField);
        ImGui::SameLine();
        Document &obj=packField?root_:selected();
        const bool removable=obj.contains(key);
        ImGui::BeginDisabled(!removable);
        if(ImGui::SmallButton("Remove")&&removable) {
            obj.erase(key);
            if(packField)changedPack(key);
            else changed(key);
        }
        ImGui::EndDisabled();
        if(!removable&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(isSource()
                ?"No local override to remove (pack default is preserved)."
                :"No assigned texture to remove.");
        ImGui::PopID();
    }
    void sourceTextureContextMenu(const char *key,bool packField) {
        if(!isSource())return;
        if(ImGui::BeginPopupContextItem("##sourceTextureContext")) {
            if(ImGui::MenuItem("Reference existing file..."))
                referenceSourceTexture(key,packField);
            Document &obj=packField?root_:selected();
            std::string raw=stringValue(obj,key);
            if(raw.empty()&&!packField)
                raw=stringValue(effectiveSelected(),key);
            ImGui::SetNextItemWidth(260.0f);
            if(ImGui::InputText("Source path",&raw)) {
                if(raw.empty())obj.erase(key);
                else if(std::string(key)=="albedo_map")
                    editor_source::chooseAlbedoImage(obj,raw);
                else obj[key]=raw;
                if(packField)changedPack(key,ImGui::IsItemActive());
                else changed(key,ImGui::IsItemActive());
            }
            ImGui::TextDisabled("Relative paths resolve from the source pack.");
            ImGui::EndPopup();
        }
    }
    void showTextureDetails(const char *key,const std::string &storedFile,
                            bool previewMatches=true,bool packField=false) {
        const int index=imageSlot(key);
        if(index<0)return;
        const auto slot=static_cast<PreviewTexture>(index);
        // A pack-wide default may not be the selected material's current
        // map. Never display an unrelated thumbnail as that default.
        const bool canPreview=previewMatches&&
            !(slot==EnvironmentMap&&!override_.empty());
        const auto &info=preview_->imageInfo(slot);
        const GLuint image=canPreview?preview_->imageTexture(slot):0;
        const bool hasImage=image!=0&&info.valid();
        constexpr float thumb=52.0f;
        if(hasImage) {
            const float ratio=float(info.width)/float(info.height);
            const ImVec2 size(ratio>=1.0f?thumb:thumb*ratio,
                              ratio>=1.0f?thumb/ratio:thumb);
            if(ImGui::ImageButton("##textureThumbnail",
                                 ImTextureRef((ImTextureID)image),size,
                                 ImVec2(0,1),ImVec2(1,0)))
                ImGui::OpenPopup("##textureChannels");
        } else {
            if(ImGui::Button(storedFile.empty()?"No map":"No preview",
                             ImVec2(thumb+7.0f,thumb+4.0f)))
                if(!storedFile.empty())
                    ImGui::OpenPopup("##textureChannels");
        }
        if(ImGui::IsItemHovered()) {
            if(!storedFile.empty())
                ImGui::SetTooltip("%s%s",storedFile.c_str(),
                    isSource()?"\nRight-click for source path options.":"");
            else if(isSource())
                ImGui::SetTooltip("Right-click to reference a source image.");
            else
                ImGui::SetTooltip("Load an image to assign this map.");
        }
        sourceTextureContextMenu(key,packField);
        if(!ImGui::BeginPopup("##textureChannels"))return;
        if(!storedFile.empty())
            ImGui::TextWrapped("%s",storedFile.c_str());
        if(!hasImage) {
            ImGui::TextDisabled(canPreview
                ?"Image missing or unsupported.":"Not active in current preview.");
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("%d x %d | %s | %d channels",
                    info.width,info.height,
                    storedFile.empty()?"MAT":imageFormatLabel(storedFile).c_str(),
                    info.channels);
        ImGui::TextDisabled("%s",readableFileSize(info.bytes).c_str());
        ImGui::Separator();
        const char *labels[]={"RGBA","R","G","B","A"};
        for(int c=0;c<5;++c) {
            if(c)ImGui::SameLine();
            const bool unavailable=c==4&&info.channels!=2&&info.channels!=4;
            ImGui::BeginDisabled(unavailable);
            if(ImGui::RadioButton(labels[c],inspectedChannel_==c))
                inspectedChannel_=c;
            ImGui::EndDisabled();
        }
        if(inspectedChannel_==4&&info.channels!=2&&info.channels!=4)
            inspectedChannel_=0;
        GLuint channel=inspectedChannel_==0?image:
            preview_->channelTexture(slot,inspectedChannel_-1);
        if(channel) {
            const float scale=std::min(1.0f,
                320.0f/float(std::max(info.width,info.height)));
            ImGui::Image(ImTextureRef((ImTextureID)channel),
                ImVec2(std::max(1.0f,info.width*scale),
                       std::max(1.0f,info.height*scale)),
                ImVec2(0,1),ImVec2(1,0));
        }
        if(slot==SurfaceMap)
            ImGui::TextDisabled("Surface: R=reflection, G=roughness, B=metalness");
        else if(slot==CoatSurfaceMap)
            ImGui::TextDisabled("Clearcoat: R=coverage, G=roughness, B=transmission");
        else if(slot==MatcapSurfaceMap)
            ImGui::TextDisabled("MatCap mask: R=intensity, G=roughness");
        else if(slot==DisplacementMap)
            ImGui::TextDisabled("Displacement: R=height");
        const bool color=slot==BaseMap||slot==EmissiveMap||
                         slot==MatcapMap||slot==EnvironmentMap;
        ImGui::TextDisabled(color?"sRGB color texture":"Linear data texture");
        ImGui::EndPopup();
    }
    void imageInfo(const char *label,const char *key) {
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        if(isSource()&&
           editor_source::isInherited(root_,selected(),key,mode_)&&
           ImGui::IsItemHovered())
            ImGui::SetTooltip("Inherited from pack defaults.");
        std::string value=stringValue(effectiveSelected(),key);
        if(isSource()&&value.empty()&&std::string(key)=="environment_map")
            value=stringValue(root_,"environment_map");
        showTextureDetails(key,value);
        ImGui::SameLine();
        ImGui::BeginGroup();
        sourceTextureActions(key,false);
        ImGui::EndGroup();
        ImGui::PopID();
    }
    void packTextureField(const char *label,const char *key) {
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        const auto file=stringValue(root_,key);
        bool matches=false;
        if(validSelection()) {
            const auto selectedFile=stringValue(effectiveSelected(),key);
            matches=selectedFile==file;
            if(std::string(key)=="environment_map"&&selectedFile.empty())
                matches=true; // The preview falls back to the pack EnvMap.
        }
        showTextureDetails(key,file,matches,true);
        ImGui::SameLine();
        ImGui::BeginGroup();
        sourceTextureActions(key,true);
        ImGui::EndGroup();
        ImGui::PopID();
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
        ImGui::TextDisabled(isSource()
            ?"Source pack (script.json) — editable defaults and metadata"
            :"Top-level compiled pack metadata");
        packTextField("Name","name");
        packTextField("Description","description",true);
        packTextField("Comment (optional)","comment",true,true);
        packTextureField("Default environment map","environment_map");
        const std::string env=stringValue(root_,"environment_map");
        if(!env.empty()&&!stagedImages_.count(env)) {
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
        if(isSource())
            renderSourcePackDefaults();
        ImGui::TextDisabled(isSource()
            ?"Source textures, inheritance and MAT replacements are preserved."
            :"Unknown fields and MAT signatures are preserved.");
    }
    void packDefaultFloat(const char *label,const char *key,float fallback) {
        ImGui::PushID(key);
        const bool has=root_.contains(key);
        ImGui::TextUnformatted(label);
        float value=number(root_,key,fallback);
        ImGui::SetNextItemWidth(std::max(90.0f,
            ImGui::GetContentRegionAvail().x-(has?27.0f:0.0f)));
        if(ImGui::DragFloat("##packValue",&value,0.01f,0,0,"%.3f")&&
           std::isfinite(value)) {
            // Respect the same numeric domains as the material compiler.
            const std::string field(key);
            if(field=="normal_strength"||field=="normal_strength2")
                value=std::clamp(value,0.0f,4.0f);
            else if(field=="depth_bias")
                value=std::clamp(value,-16.0f,16.0f);
            else if(field=="roughness"||field=="metalness"||
                    field=="clearcoat_strength"||field=="clearcoat_roughness"||
                    field=="clearcoat_transmission"||field=="matcap_roughness")
                value=std::clamp(value,0.0f,1.0f);
            else if(field=="reflection_strength"||field=="fluid_thickness")
                value=std::max(0.0f,value);
            root_[key]=value;
            changedPack(key,ImGui::IsItemActive());
        }
        if(has) {
            ImGui::SameLine();
            if(ImGui::SmallButton("x")){root_.erase(key);changedPack(key);}
        }
        ImGui::PopID();
    }
    void packDefaultColor(const char *label,const char *key,int count) {
        ImGui::PushID(key);
        const bool has=root_.contains(key);
        ImGui::TextUnformatted(label);
        auto v=count==4?rgba(root_,key,{1,1,1,1}):
            std::array<float,4>{0,0,0,1};
        if(count==3) {
            const auto color=rgb(root_,key,std::string(key)=="matcap_factor"
                                              ?std::array<float,3>{1,1,1}
                                              :std::array<float,3>{0,0,0});
            std::copy(color.begin(),color.end(),v.begin());
        }
        ImGui::SetNextItemWidth(std::max(90.0f,
            ImGui::GetContentRegionAvail().x-(has?27.0f:0.0f)));
        const auto flags=ImGuiColorEditFlags_NoLabel|ImGuiColorEditFlags_Float|
                         ImGuiColorEditFlags_HDR;
        const bool edited=count==4
            ?ImGui::ColorEdit4("##packColor",v.data(),flags)
            :ImGui::ColorEdit3("##packColor",v.data(),flags);
        if(edited) {
            setArray(root_,key,v.data(),count);
            changedPack(key,ImGui::IsItemActive());
        }
        if(has) {
            ImGui::SameLine();
            if(ImGui::SmallButton("x")){root_.erase(key);changedPack(key);}
        }
        ImGui::PopID();
    }
    void packDefaultBool(const char *label,const char *key) {
        ImGui::PushID(key);
        const bool alias=std::string(key)=="keep_albedo";
        const bool has=root_.contains(key)||
            (alias&&root_.contains("use_original_albedo"));
        bool value=false;
        auto it=root_.find(key);
        if(it==root_.end()&&alias)it=root_.find("use_original_albedo");
        if(it!=root_.end()&&it->is_boolean())value=it->get<bool>();
        if(ImGui::Checkbox(label,&value)) {
            if(alias)editor_source::chooseOriginalMatAlbedo(root_,value);
            else root_[key]=value;
            changedPack(key);
        }
        if(has) {
            ImGui::SameLine();
            if(ImGui::SmallButton("x")) {
                root_.erase(key);
                if(alias)root_.erase("use_original_albedo");
                changedPack(key);
            }
        }
        ImGui::PopID();
    }
    void packDefaultChoice(const char *label,const char *key,
                           const char *const *values,int count) {
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        const std::string current=stringValue(root_,key);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if(ImGui::BeginCombo("##packChoice",
                             current.empty()?"(no pack default)":current.c_str())) {
            if(ImGui::Selectable("Use renderer default",current.empty())) {
                if(root_.contains(key)){root_.erase(key);changedPack(key);}
            }
            for(int i=0;i<count;++i)
                if(ImGui::Selectable(values[i],current==values[i])) {
                    root_[key]=values[i];changedPack(key);
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    void renderSourcePackDefaults() {
        ImGui::Separator();
        if(!ImGui::CollapsingHeader("Source material defaults"))return;
        ImGui::TextDisabled("Defaults apply where a material has no override.");
        if(ImGui::TreeNode("Default textures")) {
            for(const auto &entry:std::initializer_list<std::pair<const char*,const char*>>{
                {"Albedo map","albedo_map"},{"Emissive map","emissive_map"},
                {"Normal map","normal_map"},{"Normal map 2","normal_map2"},
                {"Displacement map","displacement_map"},
                {"Surface map","surface_map"},{"MatCap map","matcap_map"},
                {"MatCap mask","matcap_surface_map"},
                {"Clearcoat surface","clearcoat_surface_map"},
                {"Clearcoat normal","clearcoat_normal_map"}})
                packTextureField(entry.first,entry.second);
            ImGui::TreePop();
        }
        if(ImGui::TreeNode("Colors and surface properties")) {
            packDefaultColor("Albedo RGBA","albedo_factor",4);
            packDefaultColor("Emissive RGB","emissive_factor",3);
            packDefaultColor("MatCap RGB","matcap_factor",3);
            packDefaultFloat("Reflection strength","reflection_strength",0);
            packDefaultFloat("Roughness","roughness",0);
            packDefaultFloat("Metalness","metalness",0);
            packDefaultFloat("Normal strength","normal_strength",1);
            packDefaultFloat("Normal strength 2","normal_strength2",1);
            packDefaultFloat("Displacement factor","displacement_factor",0);
            packDefaultFloat("Clearcoat strength","clearcoat_strength",0);
            packDefaultFloat("Clearcoat roughness","clearcoat_roughness",0.1f);
            packDefaultFloat("Clearcoat transmission","clearcoat_transmission",1);
            packDefaultFloat("MatCap roughness","matcap_roughness",0);
            ImGui::TreePop();
        }
        if(ImGui::TreeNode("Rendering defaults")) {
            packDefaultBool("Keep original MAT albedo","keep_albedo");
            packDefaultBool("Texture filtering","texture_filtering");
            const char *refModes[]={"envmap","screen","fixed","planar",
                                    "hemi","cube","shared"};
            const char *alphaModes[]={"blend","mask","dithered"};
            const char *blendModes[]={"normal","additive","multiply","fluid"};
            packDefaultChoice("Reflection mode","reflection_mode",refModes,7);
            packDefaultChoice("Alpha mode","alpha_mode",alphaModes,3);
            packDefaultChoice("Blend mode","blend_mode",blendModes,4);
            if(stringValue(root_,"alpha_mode")=="mask")
                packDefaultFloat("Alpha cutoff","alpha_cutoff",0.5f);
            packDefaultFloat("Depth bias","depth_bias",0);
            packDefaultFloat("Fluid thickness","fluid_thickness",0);
            ImGui::TreePop();
        }
        if(root_.value("keep_albedo",false)&&root_.contains("albedo_map"))
            ImGui::TextColored(ImVec4(1,.6f,.3f,1),
                "Warning: keep_albedo and albedo_map defaults conflict.");
        if(ImGui::TreeNode("Animation defaults")) {
            scrollField("Albedo scroll (U,V) / sec","albedo_scroll",true);
            scrollField("Emissive scroll (U,V) / sec","emissive_scroll",true);
            scrollField("Normal 1 scroll (U,V) / sec","normal_scroll",true);
            scrollField("Normal 2 scroll (U,V) / sec","normal_scroll2",true);
            pulseField(true);
            ImGui::TreePop();
        }
    }
    void renderProperties() {
        ImGui::Begin("Material Properties");
        if(!loaded_)
            ImGui::TextWrapped("Open a compiled pack to inspect material properties.");
        else
            renderMaterialProperties();
        ImGui::End();
    }
    // A section is initially open only when its material feature is in use.
    // Each material has independent ImGui open/closed state, so switching
    // selection doesn't discard manual expansion choices.
    bool materialSection(const char *label,int index,bool active) {
        if(index<0||index>=int(sectionSeen_.size()))
            return ImGui::CollapsingHeader(label);
        if(!sectionSeen_[index]) {
            ImGui::SetNextItemOpen(active,ImGuiCond_Once);
            sectionSeen_[index]=true;
        } else if(active&&!sectionActive_[index]) {
            // A newly assigned map/factor automatically exposes its section.
            // Never auto-collapse when a map is cleared or when user toggles.
            ImGui::SetNextItemOpen(true,ImGuiCond_Always);
        }
        sectionActive_[index]=active;
        return ImGui::CollapsingHeader(label);
    }
    void renderMatReplacements() {
        if(!ImGui::CollapsingHeader("MAT replacements",
                                  ImGuiTreeNodeFlags_DefaultOpen))return;
        const Document entries=selected().value("replaces",Document::array());
        if(!entries.is_array()) {
            ImGui::TextDisabled("Invalid MAT replacement list.");
            return;
        }
        for(size_t i=0;i<entries.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            editor_source::MatReplacement replacement;
            const bool valid=editor_source::readMatReplacement(
                entries[i],mode_,replacement);
            if(!valid)ImGui::TextDisabled("Unsupported replacement record");
            if(isSource()) {
                // Preserve the author's short-string vs [path,cel] form
                // unless explicitly selecting a nonzero cel.
                std::string path=replacement.name;
                ImGui::TextDisabled("MAT %zu",i+1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                if(ImGui::InputTextWithHint("##matPath","mat/example.mat",&path)) {
                    replacement.name=path;
                    selected()["replaces"][i]=
                        editor_source::writeSourceMatReplacement(replacement);
                    changed("replaces",ImGui::IsItemActive());
                }
                int cel=replacement.cel;
                ImGui::SetNextItemWidth(120.0f);
                if(ImGui::InputInt("Cel",&cel)) {
                    if(cel>=0) {
                        replacement.cel=cel;
                        replacement.specificCel|=cel!=0;
                        selected()["replaces"][i]=
                            editor_source::writeSourceMatReplacement(replacement);
                        changed("replaces",ImGui::IsItemActive());
                    }
                }
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cel 0 is used for plain MAT filenames.");
                ImGui::SameLine();
                ImGui::BeginDisabled(entries.size()<=1);
                if(ImGui::SmallButton("Remove")&&entries.size()>1) {
                    selected()["replaces"].erase(i);
                    changed("replaces");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    break;
                }
                ImGui::EndDisabled();
                if(path.empty())
                    ImGui::TextColored(ImVec4(1.0f,.65f,.35f,1.0f),
                                       "Enter a MAT filename before saving.");
            } else {
                if(valid)
                    ImGui::TextWrapped("%zu. %s  (cel %d)",
                        i+1,replacement.name.c_str(),replacement.cel);
            }
            ImGui::PopID();
            if(i+1<entries.size())ImGui::Separator();
        }
        if(isSource()) {
            if(ImGui::Button("Add MAT replacement")) {
                // Empty until authored; source validation prevents saving an
                // unfinished entry instead of fabricating a real MAT path.
                selected()["replaces"].push_back("");
                changed("replaces");
            }
        } else {
            ImGui::TextDisabled("Read-only: edit script.json and recompile to update MAT signatures.");
        }
    }
    void renderMaterialProperties() {
        if(!validSelection()) {
            ImGui::TextWrapped("Select a material to edit its properties.");
            return;
        }
        if(sectionSelection_!=selection_) {
            sectionSelection_=selection_;
            sectionSeen_.fill(false);
            sectionActive_.fill(false);
        }
        ImGui::PushID(file_.c_str());
        ImGui::PushID(selection_);
        const Document &effective=effectiveSelected();
        const auto usesMap=[&](const char *key) {
            return !stringValue(effective,key).empty();
        };
        const auto emissive=rgb(effective,"emissive_factor",{0,0,0});
        const bool emits=usesMap("emissive_map")||
            std::any_of(emissive.begin(),emissive.end(),
                        [](float value){return value>0.00001f;})||
            effective.contains("emissive_pulse");
        const bool reflects=usesMap("surface_map")||
            number(effective,"reflection_strength",0.0f)>0.00001f;
        const bool normals=usesMap("normal_map")||usesMap("normal_map2");
        const bool parallax=usesMap("displacement_map");
        const bool coat=usesMap("clearcoat_surface_map")||
            usesMap("clearcoat_normal_map")||
            number(effective,"clearcoat_strength",0.0f)>0.00001f;
        const bool matcap=usesMap("matcap_map");
        const std::string name=stringValue(selected(),"name");
        if(isSource()) {
            std::string updated=name;
            ImGui::TextUnformatted("Material name");
            if(ImGui::InputText("##materialName",&updated)) {
                selected()["name"]=updated;
                changed("name",ImGui::IsItemActive());
            }
        } else {
            ImGui::TextWrapped("Material: %s",name.c_str());
        }
        renderMatReplacements();
        if(materialSection("Albedo",0,true)) {
            auto v=rgba(effectiveSelected(),"albedo_factor",{1,1,1,1});
            colorField("Tint / alpha","albedo_factor",v.data(),4);
            // Native JK albedo and an imported albedo_map are alternate
            // sources, never simultaneous inputs. Toggling or loading a
            // replacement makes one explicit choice in a single Undo step.
            bool keep=effectiveSelected().value("keep_albedo",false);
            if(ImGui::Checkbox("Keep original MAT albedo",&keep)) {
                editor_source::chooseOriginalMatAlbedo(selected(),keep);
                changed("keep_albedo");
            }
            const bool ownKeep=selected().contains("keep_albedo")||
                               selected().contains("use_original_albedo");
            if(isSource()&&ownKeep) {
                ImGui::SameLine();
                if(ImGui::SmallButton("Use pack default##keep")) {
                    selected().erase("keep_albedo");
                    selected().erase("use_original_albedo");
                    changed("keep_albedo");
                }
            }
            // Resolve current value again after a checkbox change (syncPreview
            // refreshes effectiveEntry_ during changed()).
            keep=effectiveSelected().value("keep_albedo",false);
            if(keep) {
                editor_source::MatReplacement original;
                const Document replacements=selected().value(
                    "replaces",Document::array());
                const bool hasOriginal=replacements.is_array()&&
                    !replacements.empty()&&
                    editor_source::readMatReplacement(
                        replacements[0],mode_,original);
                ImGui::TextUnformatted("Original MAT albedo");
                if(hasOriginal)
                    ImGui::TextDisabled("%s (cel %d)",
                                        original.name.c_str(),original.cel);
                const auto &thumbnail=preview_->imageInfo(BaseMap);
                const GLuint image=preview_->imageTexture(BaseMap);
                if(image&&thumbnail.valid()) {
                    const float aspect=float(thumbnail.width)/thumbnail.height;
                    const ImVec2 size(aspect>=1.0f?64.0f:64.0f*aspect,
                                      aspect>=1.0f?64.0f/aspect:64.0f);
                    ImGui::Image(ImTextureRef((ImTextureID)image),size,
                                 ImVec2(0,1),ImVec2(1,0));
                } else if(isSource()) {
                    ImGui::TextDisabled("Preview unavailable; set game assets/CMP under File.");
                }
            } else {
                imageInfo("Albedo texture","albedo_map");
            }
            scrollField("Scroll (U,V) / sec","albedo_scroll");
            // Texture import, replacement, and removal are available above.
        }
        if(materialSection("Emissive",1,emits)) {
            auto v=rgb(effectiveSelected(),"emissive_factor",{0,0,0});
            if(ImGui::DragFloat3("Emissive RGB",v.data(),.05f,0,32,"%.2f")) {
                setArray(selected(),"emissive_factor",v.data(),3);
                changed("emissive_factor",ImGui::IsItemActive());
            }
            imageInfo("Emissive map","emissive_map");
            scrollField("Scroll (U,V) / sec","emissive_scroll");
            pulseField(false);
        }
        if(materialSection("Reflections",2,reflects)) {
            bool hasSurface=effectiveSelected().find("surface_map")!=effectiveSelected().end();
            floatField("Strength","reflection_strength",hasSurface?1.0f:0.0f,0,1,true);
            if(number(effectiveSelected(),"reflection_strength",hasSurface?1.0f:0.0f)>1.0f)
                ImGui::TextDisabled("Above 1: explicitly overridden in the numeric field.");
            floatField("Roughness","roughness",hasSurface?1.0f:0.0f,0,1);
            floatField("Metalness","metalness",hasSurface?1.0f:0.0f,0,1);
            static const char *modes[]={"envmap","screen","fixed","planar","hemi","cube","shared"};
            static const char *modeLabels[]={"EnvMap","Screen","Fixed","Planar",
                                            "Hemisphere","Cubemap","Shared"};
            comboField("Game reflection mode","reflection_mode",
                       modes,modeLabels,7);
            imageInfo("Surface map","surface_map");
        }
        if(materialSection("Normal maps",3,normals)) {
            imageInfo("Normal map 1","normal_map");
            floatField("Normal map 1 strength","normal_strength",1,0,4);
            scrollField("Normal map 1 scroll (U,V) / sec","normal_scroll");
            ImGui::Spacing();
            imageInfo("Normal map 2","normal_map2");
            floatField("Normal map 2 strength","normal_strength2",1,0,4);
            scrollField("Normal map 2 scroll (U,V) / sec","normal_scroll2");
        }
        if(materialSection("Parallax mapping",4,parallax)) {
            imageInfo("Displacement map","displacement_map");
            ImGui::TextUnformatted("Displacement factor");
            ImGui::PushID("displacement_factor");
            float factor=number(effectiveSelected(),"displacement_factor",0.0f);
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
            if(!effectiveSelected().contains("displacement_map"))
                ImGui::TextDisabled("A displacement texture is required.");
        }
        if(materialSection("Clearcoat",5,coat)) {
            bool map=effectiveSelected().find("clearcoat_surface_map")!=effectiveSelected().end();
            floatField("Coat strength","clearcoat_strength",map?1.0f:0.0f,0,1);
            floatField("Coat roughness","clearcoat_roughness",.1f,0,1);
            floatField("Coat transmission","clearcoat_transmission",1,0,1);
            imageInfo("Coat RGB","clearcoat_surface_map");
            imageInfo("Coat normal","clearcoat_normal_map");
        }
        if(materialSection("MatCap",6,matcap)) {
            floatField("MatCap roughness","matcap_roughness",0,0,1);
            auto v=rgb(effectiveSelected(),"matcap_factor",{1,1,1});
            if(ImGui::DragFloat3("MatCap RGB factor",v.data(),.02f,0,4,"%.2f")) {
                setArray(selected(),"matcap_factor",v.data(),3);
                changed("matcap_factor",ImGui::IsItemActive());
            }
            imageInfo("MatCap","matcap_map");
            imageInfo("MatCap mask","matcap_surface_map");
        }
        if(materialSection("Other settings",7,false)) {
            static const char *alphas[]={"blend","mask","dithered"};
            static const char *alphaLabels[]={"Blend (smooth alpha)","Mask (alpha cutoff)",
                                              "Dithered transparency"};
            static const char *blends[]={"normal","additive","multiply","fluid"};
            static const char *blendLabels[]={"Normal","Additive","Multiply","Fluid"};
            comboField("Alpha mode","alpha_mode",alphas,alphaLabels,3);
            comboField("Blend mode","blend_mode",blends,blendLabels,4);
            if(stringValue(effectiveSelected(),"alpha_mode")=="mask")
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
        if(isSource()) {
            if(materialSection("Material environment override",8,usesMap("environment_map")))
                imageInfo("Environment map","environment_map");
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    void comboField(const char *label,const char *key,
                    const char *const*values,const char *const*labels,int count) {
        Document &obj=selected();
        std::string value=stringValue(effectiveSelected(),key);
        if(std::string(key)=="alpha_mode"&&value=="dither")
            value="dithered";
        int current=0;
        for(int i=0;i<count;++i)if(value==values[i])current=i;
        ImGui::PushID(key);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        std::string display=labels[current];
        if(!obj.contains(key)) {
            const bool inherited=editor_source::isInherited(root_,obj,key,mode_);
            display+=inherited?" (pack default)":" (renderer default)";
        }
        if(ImGui::BeginCombo("##choice",display.c_str())) {
            if(ImGui::Selectable(isSource()?"Use pack default":"Use renderer default",
                                  !obj.contains(key))) {
                if(obj.contains(key)) {obj.erase(key);changed(key);}
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
    editor_ini::Settings &settings_;
    std::unique_ptr<MaterialPreview> preview_;
    Document root_=Document::object();
    Document savedRoot_=Document::object(), observedRoot_=Document::object();
    Document effectiveEntry_=Document::object();
    editor_source::Kind mode_=editor_source::Kind::Compiled;
    std::vector<Edit> undo_,redo_;
    std::string file_,status_,override_;
    // Drafts survive Undo/Redo until a pack is closed or reverted. An
    // unreferenced draft is never written to disk during Save.
    std::map<std::string,std::shared_ptr<const editor_images::Image>>
        stagedImages_;
    std::set<std::string> committedImageNames_;
    NativeMatSource nativeSource_;
    NativeMatImage nativeLoaded_;
    std::string nativeCacheKey_,nativeStatus_;
    bool loaded_=false,dirty_=false,groupActive_=false,
         layoutConfigured_=false,openUnsavedDialog_=false,
         openRevertDialog_=false,exitApproved_=false;
    bool showDemo_=false,showMetrics_=false,showStyleEditor_=false;
    PendingAction pendingAction_=PendingAction::None;
    int selection_=-1,environment_=0;
    PreviewShape shape_=PreviewShape::Sphere;
    bool envFresnel_=false,orthographic_=false;
    int inspectedChannel_=0;
    bool animationPlaying_=true;
    double animationSeconds_=0.0;
    float orbitYaw_=0.35f,orbitPitch_=-0.15f,orbitDistance_=3.5f;
    std::array<float,3> orbitTarget_{0.0f,0.0f,0.0f};
    std::array<float,3> dragStartTarget_{0.0f,0.0f,0.0f};
    std::array<float,3> dragRight_{1.0f,0.0f,0.0f};
    std::array<float,3> dragUp_{0.0f,1.0f,0.0f};
    float dragPanUnitsPerPixel_=0.0f;
    float dragStartYaw_=0.35f,dragStartPitch_=-0.15f;
    ImGuiMouseButton dragButton_=ImGuiMouseButton_Left;
    ImGuiTextFilter filter_;
    int sectionSelection_=-2;
    std::array<bool,10> sectionSeen_{};
    std::array<bool,10> sectionActive_{};
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
    // Portable: store the layout AND game asset roots in the same INI next
    // to the EXE, even when launched from a different working directory.
    editor_ini::Settings settings;
    settings.exeDirectory=executableDirectory();
    const fs::path iniPath=settings.exeDirectory/L"imgui.ini";
    const std::string iniFile=iniPath.u8string();
    io.IniFilename=iniFile.c_str();
    // Preserve an existing cwd imgui.ini layout on first portable launch.
    std::error_code iniEc;
    if(!fs::exists(iniPath,iniEc)) {
        const auto legacyLayout=fs::current_path()/L"imgui.ini";
        if(legacyLayout!=iniPath&&fs::is_regular_file(legacyLayout,iniEc))
            fs::copy_file(legacyLayout,iniPath,fs::copy_options::none,iniEc);
    }
    editor_ini::install(settings);
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    {
        Editor editor(window,settings);
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
