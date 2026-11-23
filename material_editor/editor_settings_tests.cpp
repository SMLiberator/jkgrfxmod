#include "editor_ini_settings.hpp"
#include "native_mat.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
void check(bool condition,const char *description) {
    if(!condition)throw std::runtime_error(description);
}
struct Sandbox {
    fs::path base;
    Sandbox() {
        base=fs::temp_directory_path()/
             ("jkgm_editor_settings_"+
              std::to_string(std::chrono::steady_clock::now()
                             .time_since_epoch().count()));
        fs::create_directories(base);
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(base,ec);
    }
};
void testPortableIni(const fs::path &base) {
    const auto exe=base/"editor";
    fs::create_directories(exe/"assets"/"JediKnight");
    fs::create_directories(base/"external");
    const auto a=(exe/"assets"/"JediKnight").u8string();
    const auto b=(base/"external").u8string();
    const auto cmp=(exe/"assets"/"levels.cmp").u8string();

    editor_ini::Settings original;
    original.exeDirectory=exe;
    original.assetRoots={a,b};
    original.palette=cmp;

    ImGui::CreateContext();
    editor_ini::install(original);
    const std::string ini=ImGui::SaveIniSettingsToMemory();
    check(ini.find("[JkGfxModEditor][Config]")!=std::string::npos,
          "Custom INI section is missing");
    check(ini.find("Root=relative:assets/JediKnight")!=std::string::npos,
          "Subdirectories of EXE should save as relative");
    check(ini.find("Root=absolute:")!=std::string::npos,
          "Outside paths should save as absolute");
    check(ini.find("Palette=relative:assets/levels.cmp")!=std::string::npos,
          "CMP under EXE should save as relative");
    ImGui::DestroyContext();

    editor_ini::Settings loaded;
    loaded.exeDirectory=exe;
    ImGui::CreateContext();
    editor_ini::install(loaded);
    ImGui::LoadIniSettingsFromMemory(ini.c_str(),ini.size());
    check(loaded.hasIniSection,"INI custom section should suppress legacy migration");
    check(loaded.assetRoots.size()==2,"INI must preserve every root");
    check(loaded.assetRoots[0]==a,"first root changed");
    check(loaded.assetRoots[1]==b,"second root changed");
    check(loaded.palette==cmp,"INI palette roundtrip failed");
    const std::string restored=ImGui::SaveIniSettingsToMemory();
    check(restored.find("Root=relative:assets/JediKnight")!=std::string::npos,
          "INI save after load dropped a root");
    ImGui::DestroyContext();
}
void writeColorMat(const fs::path &path) {
    // Small valid 16-bit MAT: type-0 solid cel (no image records).
    // It can be decoded without a CMP, making search precedence testable.
    std::vector<unsigned char> bytes(100,0);
    std::memcpy(bytes.data(),"MAT ",4);
    bytes[4]=0x32; // MAT format version
    bytes[12]=1;   // one cel
    bytes[24]=16;  // RGB565 direct color
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
void testRootPriority(const fs::path &base) {
    const auto first=base/"priority_1";
    const auto second=base/"priority_2";
    fs::create_directories(first);
    fs::create_directories(second);
    writeColorMat(first/"shared.mat");
    writeColorMat(second/"shared.mat");
    writeColorMat(second/"only_second.mat");

    NativeMatSource source;
    source.setRoots({first.u8string(),second.u8string()});
    check(source.roots().size()==2,"Failed to store multiple roots");
    NativeMatImage image;
    std::string error;
    check(source.load("shared.mat",0,image,error),"Cannot decode shared MAT");
    check(image.description.find(first.u8string())!=std::string::npos,
          "First root must win on duplicate MAT");
    check(source.load("only_second.mat",0,image,error),
          "Lower-priority root is not searched");
    check(image.description.find(second.u8string())!=std::string::npos,
          "Fallback MAT came from wrong root");

    source.setRoots({second.u8string(),first.u8string()});
    check(source.load("shared.mat",0,image,error),"Cannot decode reordered MAT");
    check(image.description.find(second.u8string())!=std::string::npos,
          "Reordering roots must change search priority");
    source.setRoots({first.u8string(),first.u8string()});
    check(source.roots().size()==1,"Duplicate roots should be ignored");
}
int main() {
    try {
        Sandbox temp;
        testPortableIni(temp.base);
        testRootPriority(temp.base);
        std::cout<<"Portable INI and multi-root MAT tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Portable editor settings tests failed: "<<e.what()<<"\n";
        return 1;
    }
}
