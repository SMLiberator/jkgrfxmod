#pragma once
// Portable application preferences stored in Dear ImGui's existing imgui.ini.
// No second settings file is created (legacy AppData JSON is import-only).
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <string>
#include <vector>
#include <cstring>

namespace editor_ini {
namespace fs=std::filesystem;
struct Settings {
    fs::path exeDirectory;
    std::vector<std::string> assetRoots; // first path has highest priority
    std::string palette;
    std::vector<std::string> recentPacks; // most recently opened first
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
constexpr size_t maxRecentPacks=10;

inline std::string normalizedRecentPath(const std::string &filename) {
    if(filename.empty())return {};
    try {
        // Native Windows pickers provide absolute paths. Resolve legacy
        // relative entries at the moment they are opened, never on re-launch.
        return fs::absolute(fs::u8path(filename)).lexically_normal().u8string();
    } catch(const fs::filesystem_error &) {
        return {};
    }
}
inline bool sameRecentPath(const std::string &left,const std::string &right) {
    // Windows filenames are case-insensitive for ordinary ASCII characters.
    // Keep the original spelling for display and non-ASCII UTF-8 paths.
    if(left.size()!=right.size())return false;
    for(size_t i=0;i<left.size();++i) {
        const auto a=static_cast<unsigned char>(left[i]);
        const auto b=static_cast<unsigned char>(right[i]);
        if(std::tolower(a)!=std::tolower(b))return false;
    }
    return true;
}
inline void rememberRecent(Settings &settings,const std::string &filename) {
    const std::string path=normalizedRecentPath(filename);
    if(path.empty())return;
    auto &recent=settings.recentPacks;
    recent.erase(std::remove_if(recent.begin(),recent.end(),
        [&](const std::string &item){return sameRecentPath(item,path);}),
        recent.end());
    recent.insert(recent.begin(),path);
    if(recent.size()>maxRecentPacks)recent.resize(maxRecentPacks);
}
inline void appendRecentFromIni(Settings &settings,const std::string &path) {
    const std::string normalized=normalizedRecentPath(path);
    if(normalized.empty()||settings.recentPacks.size()>=maxRecentPacks)return;
    for(const auto &item:settings.recentPacks)
        if(sameRecentPath(item,normalized))return;
    settings.recentPacks.push_back(normalized);
}
inline size_t removeMissingRecent(Settings &settings) {
    auto &recent=settings.recentPacks;
    const auto oldSize=recent.size();
    recent.erase(std::remove_if(recent.begin(),recent.end(),
        [](const std::string &path) {
            std::error_code ec;
            return !fs::is_regular_file(fs::u8path(path),ec)||bool(ec);
        }),recent.end());
    return oldSize-recent.size();
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
    settings->recentPacks.clear();
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
    else if(value.rfind("Recent=",0)==0)
        appendRecentFromIni(*settings,
            decodePath(settings->exeDirectory,value.substr(7)));
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
    for(const auto &path:settings->recentPacks)
        out->appendf("Recent=%s\n",
            encodePath(settings->exeDirectory,path).c_str());
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
