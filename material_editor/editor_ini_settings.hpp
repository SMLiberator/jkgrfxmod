#pragma once
// Portable application preferences stored in Dear ImGui's existing imgui.ini.
// No second settings file is created (legacy AppData JSON is import-only).
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <string>
#include <vector>
#include <cstring>

namespace editor_ini {
namespace fs=std::filesystem;
struct Settings {
    fs::path exeDirectory;
    std::vector<std::string> assetRoots; // first path has highest priority
    std::string palette;
    bool hasIniSection=false;
};

// Keep paths relative to the executable when the assets are inside its tree.
// Paths elsewhere retain an absolute UTF-8 representation. No dependence on
// the Wine/Windows current working directory when reopening the editor.
inline std::string encodePath(const fs::path &exeFolder,const std::string &path) {
    if(path.empty())return {};
    const auto absolute=fs::absolute(fs::u8path(path)).lexically_normal();
    const auto relative=absolute.lexically_relative(exeFolder);
    if(!relative.empty()&&!relative.is_absolute()&&
       *relative.begin()!="..")
        return "relative:"+relative.generic_u8string();
    return "absolute:"+absolute.u8string();
}
inline std::string decodePath(const fs::path &exeFolder,std::string text) {
    if(text.rfind("relative:",0)==0)
        return (exeFolder/fs::u8path(text.substr(9))).lexically_normal().u8string();
    if(text.rfind("absolute:",0)==0)
        return fs::u8path(text.substr(9)).lexically_normal().u8string();
    // Allow future/older plain INI path entries.
    return fs::u8path(text).lexically_normal().u8string();
}
inline void* readOpen(ImGuiContext*,ImGuiSettingsHandler *handler,const char *name) {
    if(std::strcmp(name,"Config")!=0)return nullptr;
    auto *settings=static_cast<Settings *>(handler->UserData);
    settings->hasIniSection=true;
    return settings;
}
inline void readInit(ImGuiContext*,ImGuiSettingsHandler *handler) {
    auto *settings=static_cast<Settings *>(handler->UserData);
    settings->assetRoots.clear();
    settings->palette.clear();
    settings->hasIniSection=false;
}
inline void readLine(ImGuiContext*,ImGuiSettingsHandler*,void *entry,const char *line) {
    auto *settings=static_cast<Settings *>(entry);
    const std::string value(line);
    if(value.rfind("Root=",0)==0)
        settings->assetRoots.push_back(
            decodePath(settings->exeDirectory,value.substr(5)));
    else if(value.rfind("Palette=",0)==0)
        settings->palette=decodePath(settings->exeDirectory,value.substr(8));
}
inline void writeAll(ImGuiContext*,ImGuiSettingsHandler *handler,
                     ImGuiTextBuffer *out) {
    auto *settings=static_cast<Settings *>(handler->UserData);
    out->appendf("[%s][Config]\n",handler->TypeName);
    for(const auto &root:settings->assetRoots) {
        // Windows filenames may contain '=', spaces and non-ASCII characters.
        // Keep the full text following the first '=', with one entry per root.
        if(!root.empty())
            out->appendf("Root=%s\n",encodePath(settings->exeDirectory,root).c_str());
    }
    if(!settings->palette.empty())
        out->appendf("Palette=%s\n",
            encodePath(settings->exeDirectory,settings->palette).c_str());
    out->append("\n");
}
inline void install(Settings &settings) {
    ImGuiSettingsHandler handler;
    handler.TypeName="JkGfxModEditor";
    handler.TypeHash=ImHashStr("JkGfxModEditor");
    handler.UserData=&settings;
    handler.ReadInitFn=readInit;
    handler.ReadOpenFn=readOpen;
    handler.ReadLineFn=readLine;
    handler.WriteAllFn=writeAll;
    ImGui::AddSettingsHandler(&handler);
}
} // namespace editor_ini
