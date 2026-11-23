#include "editor_ini_settings.hpp"
#include "native_mat.hpp"
#include "material_signatures.hpp"
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

void putWord(std::vector<unsigned char> &b,size_t pos,std::uint32_t value) {
    if(pos+4>b.size())throw std::runtime_error("Test fixture overflow");
    for(int i=0;i<4;++i)b[pos+i]=static_cast<unsigned char>(value>>(i*8));
}
void writeTexturedMat(const fs::path &file,bool indexed) {
    // One texture cel and one 2x2 MAT with two mips for the RGB565 case.
    // The exact file offsets are those in compile/raw_material.cpp.
    std::vector<unsigned char> b(indexed?144:150,0);
    std::memcpy(b.data(),"MAT ",4);
    putWord(b,4,0x32);putWord(b,12,1);putWord(b,16,1);
    putWord(b,24,indexed?8:16);
    putWord(b,76,8);   // Cel type 8: texture
    putWord(b,112,0);  // Texture record index
    putWord(b,116,2);putWord(b,120,2);
    putWord(b,136,indexed?1:2);
    if(indexed) {
        b[140]=0;b[141]=1;b[142]=2;b[143]=3;
    } else {
        const std::uint16_t pixels[]={0xf800,0x07e0,0x001f,0xffff,0xaaaa};
        for(size_t i=0;i<5;++i) {
            b[140+i*2]=static_cast<unsigned char>(pixels[i]);
            b[141+i*2]=static_cast<unsigned char>(pixels[i]>>8);
        }
    }
    std::ofstream stream(file,std::ios::binary);
    stream.write(reinterpret_cast<const char *>(b.data()),b.size());
}
void writePalette(const fs::path &file,int r,int g,int b) {
    // CMP header + direct RGB palette; full light tables are not used when
    // generating signatures, which mirrors compile/colormap.cpp.
    std::vector<unsigned char> bytes(64+768,0);
    std::memcpy(bytes.data(),"CMP ",4);
    putWord(bytes,4,0x1e);
    for(int i=0;i<256;++i) {
        bytes[64+i*3]=static_cast<unsigned char>(i*r);
        bytes[64+i*3+1]=static_cast<unsigned char>(i*g);
        bytes[64+i*3+2]=static_cast<unsigned char>(i*b);
    }
    std::ofstream stream(file,std::ios::binary);
    stream.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());
}
void testCompiledMatSignatures(const fs::path &base) {
    const auto root=base/"signatures";
    fs::create_directories(root);
    writeTexturedMat(root/"sixteen.mat",false);
    writeTexturedMat(root/"eight.mat",true);
    writePalette(root/"colors_a.cmp",3,5,7);
    writePalette(root/"colors_b.cmp",17,11,13);
    NativeMatSource source;
    source.setRoot(root.u8string());
    std::vector<std::string> hashes;
    std::string error;
    check(source.signatures("sixteen.mat",0,hashes,error),
          "Cannot generate RGB565 signatures");
    check(hashes.size()==2&&
          hashes[0]=="038e70756812f6a097bf57e40b8bb25d"&&
          hashes[1]=="5c704567db2cb1883f223e860a687863",
          "RGB565 signatures do not match compiler MD5 inputs");
    check(source.signatures("eight.mat",0,hashes,error),
          "Cannot generate indexed MAT signatures");
    check(hashes.size()==2&&
          hashes[0]=="2457ef669640458f4c55ffeb881c493e"&&
          hashes[1]=="dcb52315e8fc60cfd841e5d52512e4f5",
          "Indexed MAT signatures must cover BOTH CMP palettes");
    check(!source.signatures("eight.mat",9,hashes,error),
          "Invalid MAT cel must be rejected");
    check(hashes.empty(),"Failure must not expose partial signature sets");

    editor_source::Document compiled={
        {"name","Signatures"},{"description","CI fixture"},
        {"materials",editor_source::Document::array({{
            {"name","test"},
            {"replaces",editor_source::Document::array({
                {{"name","sixteen.mat"},{"cel",0}}
            })},
            {"replaces_signatures",editor_source::Document::array({
                "038e70756812f6a097bf57e40b8bb25d",
                "5c704567db2cb1883f223e860a687863"
            })}
        }})}
    };
    const auto saved=compiled;
    // An unchanged pack must save even without any configured assets.
    NativeMatSource offline;
    check(editor_signatures::refreshChanged(compiled,saved,offline)==0,
          "Unchanged signatures must survive offline save");
    compiled["materials"][0]["name"]="renamed";
    check(editor_signatures::refreshChanged(compiled,saved,offline)==0,
          "Renaming must preserve valid unchanged signatures");
    compiled["materials"][0]["replaces"][0]["name"]="eight.mat";
    check(editor_signatures::refreshChanged(compiled,saved,source)==1,
          "Changing MAT must regenerate signatures");
    check(compiled["materials"][0]["replaces_signatures"].size()==2&&
          compiled["materials"][0]["replaces_signatures"][0]==
             "2457ef669640458f4c55ffeb881c493e",
          "Regenerated signatures must match new target + colormaps");
    editor_source::validate(compiled,editor_source::Kind::Compiled);
    compiled["materials"][0]["replaces"][0]["name"]="missing.mat";
    bool threw=false;
    try {editor_signatures::refreshChanged(compiled,saved,source);}
    catch(const std::exception &) {threw=true;}
    check(threw,"Missing replacement MAT must fail closed");
    const auto onlyMat=base/"no_colormaps";
    fs::create_directories(onlyMat);
    writeTexturedMat(onlyMat/"eight.mat",true);
    NativeMatSource noCmp;
    noCmp.setRoot(onlyMat.u8string());
    check(!noCmp.signatures("eight.mat",0,hashes,error),
          "8-bit signatures cannot use only the preview palette");
}
int main() {
    try {
        Sandbox temp;
        testPortableIni(temp.base);
        testRootPriority(temp.base);
        testCompiledMatSignatures(temp.base);
        std::cout<<"Portable INI and multi-root MAT tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Portable editor settings tests failed: "<<e.what()<<"\n";
        return 1;
    }
}
