#include "source_pack.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using editor_source::Document;
using editor_source::Kind;
void check(bool ok,const char *msg) {
    if(!ok)throw std::runtime_error(msg);
}
void testSourceInheritance() {
    Document source={
        {"name","Cars"},{"description","Source pack"},
        {"keep_albedo",true},{"albedo_factor",{1,0.5,1,1}},
        {"roughness",0.7},{"reflection_strength",0.6},
        {"alpha_mode","mask"},{"alpha_cutoff",0.42},
        {"texture_filtering",false},
        {"depth_bias",-0.5},
        {"materials",Document::array({
            {{"name","simple"},{"replaces",Document::array({"mat/car.mat"})}},
            {{"name","mapped"},{"replaces",Document::array({Document::array({"mat/car.mat",1})})},
             {"albedo_map","body.png"},{"alpha_mode","blend"}},
            {{"name","keep"},{"replaces",Document::array({"mat/body.mat"})},
             {"keep_albedo",true},{"alpha_mode","blend"}}
        })}
    };
    const auto original=source;
    check(editor_source::detect("script.json",source)==Kind::Source,"script detection");
    const auto first=editor_source::effectiveMaterial(source,source["materials"][0],Kind::Source);
    check(first["keep_albedo"]==true,"pack keep_albedo default");
    check(first["roughness"]==0.7,"pack roughness default");
    check(first["depth_bias"]==-0.5,"pack depth bias default");
    check(first["alpha_cutoff"]==0.42,"inherited mask cutoff");
    const auto mapped=editor_source::effectiveMaterial(source,source["materials"][1],Kind::Source);
    check(mapped["keep_albedo"]==false,"source albedo overrides inherited keep");
    check(mapped["albedo_map"]=="body.png","explicit image remains");
    check(!mapped.contains("alpha_cutoff"),"non-mask drops inherited cutoff");
    check(!source["materials"][1].contains("keep_albedo"),"source untouched");
    const auto keep=editor_source::effectiveMaterial(source,source["materials"][2],Kind::Source);
    check(keep["keep_albedo"]==true,"explicit keep applies");
    check(editor_source::isInherited(source,source["materials"][0],"roughness",Kind::Source),
          "inheritance detection");
    check(!editor_source::isInherited(source,source["materials"][1],"albedo_map",Kind::Source),
          "own value is not inherited");
    editor_source::validate(source,Kind::Source);
    check(source==original,"resolver must never flatten the source");
    Document roundtrip=Document::parse(source.dump(4));
    check(roundtrip==source,"script serialization preserves source structure");
}
void testPackAlbedoAndAlphaOverrides() {
    Document doc={
        {"name","A"},{"description","B"},{"albedo_map","pack.png"},
        {"use_original_albedo",false},{"alpha_mode","mask"},{"alpha_cutoff",0.23},
        {"materials",Document::array({
            {{"name","paint"},{"replaces",Document::array({"mat/a.mat"})},
             {"use_original_albedo",true}},
            {{"name","alpha"},{"replaces",Document::array({"mat/b.mat"})},
             {"alpha_mode","blend"},{"alpha_cutoff",0.9}}
        })}
    };
    const auto a=editor_source::effectiveMaterial(doc,doc["materials"][0],Kind::Source);
    check(!a.contains("albedo_map"),"explicit keep removes inherited albedo map");
    check(a["keep_albedo"]==true,"alias translates for display only");
    const auto b=editor_source::effectiveMaterial(doc,doc["materials"][1],Kind::Source);
    check(b["alpha_cutoff"]==0.9,"explicit cutoff is never silently removed");
    check(!doc["materials"][0].contains("keep_albedo"),"alias must be retained in source");
}
void testCompiledAndValidation() {
    Document compiled={
        {"name","Compiled"},{"description","Existing"},
        {"images",Document::array({{{"id",1},{"file","image_1.png"}}})},
        {"materials",Document::array({{{"name","body"},
          {"replaces",Document::array({{{"name","mat/car.mat"},{"cel",0}}})},
          {"replaces_signatures",Document::array({"hash"})}}})}
    };
    check(editor_source::detect("custom.json",compiled)==Kind::Compiled,
          "compiled signatures detected");
    editor_source::validate(compiled,Kind::Compiled);
    check(editor_source::effectiveMaterial(compiled,compiled["materials"][0],
            Kind::Compiled)==compiled["materials"][0],"compiled is not resolved");
    auto source=compiled;
    source["materials"][0]["replaces"]=Document::array({"mat/a.mat"});
    source["materials"][0].erase("replaces_signatures");
    source["images"][0]["file"]="subdir/image.png";
    editor_source::validate(source,Kind::Source);
    bool rejected=false;
    try{editor_source::validate(source,Kind::Compiled);}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected,"compiled images cannot be subdirectories");
    source["materials"][0]["unrecognized"]={{"key","value"}};
    source["custom_value"]={{"foo",7}};
    editor_source::validate(source,Kind::Source);
    check(Document::parse(source.dump(4))==source,"unknown fields survive");
}
void testRepositorySourcePack() {
    // Verify the compiler's real sample input is accepted verbatim and can
    // be serialized after a material override without flattening defaults.
    std::ifstream file("assets/sample_matpack/script.json");
    if(!file)throw std::runtime_error("Missing repository source pack fixture");
    Document source;
    file>>source;
    check(editor_source::detect("assets/sample_matpack/script.json",source)==Kind::Source,
          "repository source mode");
    editor_source::validate(source,Kind::Source);
    check(source["materials"].size()>10,"repository material count");
    const auto effective=editor_source::effectiveMaterial(
        source,source["materials"][2],Kind::Source);
    check(effective["displacement_factor"]==-0.2,
          "source displacement value preserved");
    const auto before=source;
    source["materials"][2]["roughness"]=0.37;
    check(source["materials"][2]["replaces"]==before["materials"][2]["replaces"],
          "MAT replacement preserved while editing source properties");
    source["materials"][2].erase("roughness");
    check(source==before,"source override rollback restores raw document");
}
void testRevertMaterialAndPack() {
    namespace fs=std::filesystem;
    // Use the real source document to guard its inherited defaults and
    // replacement definitions against accidental changes during a revert.
    const auto filename=fs::path("assets/sample_matpack/script.json");
    const Document saved=editor_source::readPack(filename);
    Document working=saved;
    const auto originalSecond=saved["materials"][1];
    working["materials"][0]["roughness"]=0.96;
    working["materials"][1]["metalness"]=0.42;
    working["description"]="unsaved pack description";
    check(editor_source::revertMaterial(working,saved,0),
          "Revert Material must report a change");
    check(working["materials"][0]==saved["materials"][0],
          "Revert Material must restore exact saved material properties");
    check(working["materials"][1]!=originalSecond,
          "Revert Material must preserve unrelated unsaved material changes");
    check(working["description"]=="unsaved pack description",
          "Revert Material must preserve unsaved pack defaults and metadata");
    check(!editor_source::revertMaterial(working,saved,0),
          "Reverting an unchanged material must be a no-op");
    check(!editor_source::revertMaterial(working,saved,working["materials"].size()),
          "Out-of-range revert must not touch the document");

    // File Revert reads the latest on-disk JSON, not an in-memory baseline.
    const Document disk=editor_source::readPack(filename);
    check(disk==saved,"File Revert must read the original disk document");
    working=disk;
    check(working==saved,"File Revert must restore the complete saved pack");
    bool rejected=false;
    try {
        editor_source::readPack(
            filename.parent_path()/"nonexistent_revert_fixture.json");
    } catch(const std::exception&) {rejected=true;}
    check(rejected,"Missing file must be reported instead of changing state");

    const auto invalid=fs::temp_directory_path()/
        ("jkgm_invalid_revert_"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    {
        std::ofstream out(invalid,std::ios::binary);
        if(!out)throw std::runtime_error("Could not write invalid revert fixture");
        out<<"{ broken json";
    }
    rejected=false;
    try {editor_source::readPack(invalid);}
    catch(const std::exception&) {rejected=true;}
    std::error_code ec;
    fs::remove(invalid,ec);
    check(rejected,"Malformed disk JSON must be rejected before activation");
}
int main(){
    try {
        testSourceInheritance();testPackAlbedoAndAlphaOverrides();
        testCompiledAndValidation();testRepositorySourcePack();
        testRevertMaterialAndPack();
        std::cout<<"Source pack tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Source pack tests failed: "<<e.what()<<"\n";
        return 1;
    }
}
