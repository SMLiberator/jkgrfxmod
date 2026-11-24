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
          {"replaces_signatures",Document::array({"0123456789abcdef0123456789abcdef"})}}})}
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
void testMatReplacementEditing() {
    using editor_source::MatReplacement;
    const auto source=editor_source::Kind::Source;
    const auto compiled=editor_source::Kind::Compiled;
    MatReplacement repl;
    check(editor_source::readMatReplacement("mat/door.mat",source,repl),
          "plain source MAT path is readable");
    check(repl.name=="mat/door.mat"&&repl.cel==0&&!repl.specificCel,
          "plain MAT uses cel zero without changing serialized form");
    repl.name="mat/newdoor.mat";
    check(editor_source::writeSourceMatReplacement(repl)=="mat/newdoor.mat",
          "editing a plain MAT path keeps its short syntax");
    const Document tuple=Document::array({"mat/door.mat",3});
    check(editor_source::readMatReplacement(tuple,source,repl),
          "source MAT tuple is readable");
    check(repl.name=="mat/door.mat"&&repl.cel==3&&repl.specificCel,
          "source MAT tuple retains cel index");
    repl.name="mat/animated.mat";repl.cel=0;
    check(editor_source::writeSourceMatReplacement(repl)==
          Document::array({"mat/animated.mat",0}),
          "explicit cel stays a tuple when changed to zero");
    const Document object={{"name","mat/door.mat"},{"cel",4}};
    check(editor_source::readMatReplacement(object,compiled,repl)&&
          repl.name=="mat/door.mat"&&repl.cel==4,
          "compiled MAT mapping can be inspected read-only");
    check(!editor_source::readMatReplacement(object,source,repl),
          "compiled object cannot silently be serialized as source syntax");
    check(!editor_source::readMatReplacement(
          Document::array({"mat/bad.mat",-1}),source,repl),
          "negative MAT cel must be rejected");
    check(!editor_source::readMatReplacement(
          Document::array({"mat/bad.mat",2147483648LL}),source,repl),
          "out-of-range MAT cel must be rejected");
}
void testExclusiveAlbedoSource() {
    Document root={
        {"name","Pack"},{"description","Material sample"},
        {"keep_albedo",true},
        {"materials",Document::array({{
            {"name","body"},
            {"replaces",Document::array({"mat/body.mat"})}
        }})}
    };
    auto &material=root["materials"][0];
    editor_source::chooseAlbedoImage(material,"paint.png");
    check(material["albedo_map"]=="paint.png"&&material["keep_albedo"]==false,
          "loading PNG overrides inherited original MAT policy");
    auto imageEffective=editor_source::effectiveMaterial(
        root,material,editor_source::Kind::Source);
    check(imageEffective["keep_albedo"]==false&&
          imageEffective["albedo_map"]=="paint.png",
          "replacement albedo wins over pack default keep");
    editor_source::chooseOriginalMatAlbedo(material,true);
    check(material["keep_albedo"]==true&&!material.contains("albedo_map"),
          "choosing original MAT removes conflicting local albedo map");
    auto nativeEffective=editor_source::effectiveMaterial(
        root,material,editor_source::Kind::Source);
    check(nativeEffective["keep_albedo"]==true&&
          !nativeEffective.contains("albedo_map"),
          "original MAT policy resolves without imported texture");
    editor_source::chooseAlbedoImage(root,"pack-albedo.png");
    check(root["keep_albedo"]==false&&
          root["albedo_map"]=="pack-albedo.png",
          "pack default image selection disables pack original albedo");
    editor_source::chooseOriginalMatAlbedo(material,true);
    check(!editor_source::effectiveMaterial(
         root,material,editor_source::Kind::Source).contains("albedo_map"),
         "material original MAT suppresses inherited pack PNG");
    material.erase("keep_albedo");
    auto inherited=editor_source::effectiveMaterial(
        root,material,editor_source::Kind::Source);
    check(inherited["albedo_map"]=="pack-albedo.png"&&
          inherited["keep_albedo"]==false,
          "removing MAT override restores image pack defaults");
    material["use_original_albedo"]=true;
    editor_source::chooseAlbedoImage(material,"second.png");
    check(!material.contains("use_original_albedo")&&
          material["keep_albedo"]==false,
          "choosing image removes legacy keep alias");
    editor_source::validate(root,editor_source::Kind::Source);
    check(root["materials"][0]["replaces"][0]=="mat/body.mat",
          "albedo selection leaves MAT replacement targets untouched");
    // Validating resolved settings catches a legacy file with both image
    // sources selected, while respecting explicit overrides of pack defaults.
    material["keep_albedo"]=true;
    material["albedo_map"]="bad.png";
    bool rejected=false;
    try {editor_source::validate(root,editor_source::Kind::Source);}
    catch(const std::runtime_error &) {rejected=true;}
    check(rejected,"conflicting local image and original MAT must be rejected");
    editor_source::chooseOriginalMatAlbedo(material,true);
    editor_source::validate(root,editor_source::Kind::Source);
}

void testDuplicateMaterial() {
    Document source={
        {"name","Variants"},{"description","Material copies"},
        {"roughness",0.6f},
        {"materials",Document::array({
            {{"name","Vehicle paint"},
             {"replaces",Document::array({"mat/car.mat"})},
             {"albedo_map","body.png"},
             {"normal_strength",0.75f}},
            {{"name","VEHICLE PAINT (COPY)"},
             {"replaces",Document::array({"mat/other.mat"})}}
        })}
    };
    const auto unchanged=source["materials"][0];
    const size_t first=editor_source::duplicateMaterial(source,0);
    check(first==2,"duplicate must append the copy");
    check(source["materials"][first]["name"]=="Vehicle paint (copy 2)",
          "copy name must avoid case-insensitive collisions");
    Document cloned=source["materials"][first];
    cloned["name"]=unchanged["name"];
    check(cloned==unchanged,
          "duplicate must preserve all source material fields");
    source["materials"][first]["albedo_map"]="variant.png";
    check(source["materials"][0]==unchanged,
          "editing a duplicate must not modify its source");
    editor_source::validate(source,Kind::Source);
    const size_t next=editor_source::duplicateMaterial(source,0);
    check(source["materials"][next]["name"]=="Vehicle paint (copy 3)",
          "repeated copies need unique suffixes");
    editor_source::validate(source,Kind::Source);
    const Document afterCopy=source["materials"];
    source["materials"].erase(first);
    source["materials"]=afterCopy; // Structural Undo restores the whole list.
    check(source["materials"][first]["albedo_map"]=="variant.png",
          "structural Undo must restore the duplicate's edited state");

    Document compiled={
        {"name","Compiled"},{"description","Compiled copies"},
        {"materials",Document::array({{
            {"name","Body"},{"roughness",0.3f},
            {"replaces",Document::array({
                {{"name","mat/car.mat"},{"cel",2}}})},
            {"replaces_signatures",Document::array({
                "0123456789abcdef0123456789abcdef"})}
        }})}
    };
    const auto compiledOriginal=compiled["materials"][0];
    const size_t compiledCopy=editor_source::duplicateMaterial(compiled,0);
    check(compiled["materials"][compiledCopy]["name"]=="Body (copy)",
          "compiled material duplicate gets an independent name");
    cloned=compiled["materials"][compiledCopy];
    cloned["name"]=compiledOriginal["name"];
    check(cloned==compiledOriginal,
          "compiled duplicate must preserve MAT signatures and properties");
    editor_source::validate(compiled,Kind::Compiled);
    bool rejected=false;
    try {editor_source::duplicateMaterial(compiled,999);}
    catch(const std::runtime_error &) {rejected=true;}
    check(rejected,"out-of-range duplicates must fail without mutation");
}

void testMaterialListEdits() {
    Document source={
        {"name","Editable"},{"description","Material management"},
        {"materials",Document::array({{
            {"name","material_1"},{"replaces",Document::array({"mat/one.mat"})}
        }})}
    };
    const auto original=source;
    const size_t second=editor_source::addMaterial(source,Kind::Source);
    check(second==1&&source["materials"][1]["name"]=="material_2",
          "new source material must get a unique name");
    check(source["materials"][1]["replaces"]==Document::array({""}),
          "new source material must wait for a MAT replacement");
    bool rejected=false;
    try {editor_source::validate(source,Kind::Source);}
    catch(const std::runtime_error &) {rejected=true;}
    check(rejected,"unfinished material must not save");
    source["materials"][1]["replaces"][0]="mat/two.mat";
    source["materials"][1]["name"]="Second Material";
    editor_source::validate(source,Kind::Source);
    const auto afterAdd=source["materials"];
    check(editor_source::removeMaterial(source,0),"remove first material");
    check(source["materials"].size()==1&&
          source["materials"][0]["name"]=="Second Material",
          "deleting one material must keep unaffected entries");
    // Simulate a single whole-array undo/redo snapshot, used by main.cpp.
    source["materials"]=afterAdd;
    check(source["materials"].size()==2,"undo add/remove snapshot");
    check(editor_source::removeMaterial(source,1),"remove added material");
    check(source==original,"structural undo can restore original document");
    check(!editor_source::removeMaterial(source,10),
          "invalid index must not remove any entry");

    Document compiled={
        {"name","Compiled"},{"description","Test"},
        {"materials",Document::array()}
    };
    editor_source::addMaterial(compiled,Kind::Compiled);
    check(compiled["materials"][0]["replaces"][0]["cel"]==0,
          "new compiled material uses object MAT replacement form");
    check(compiled["materials"][0]["replaces_signatures"].empty(),
          "new compiled material starts with no fabricated signatures");
    rejected=false;
    try {editor_source::validate(compiled,Kind::Compiled);}
    catch(const std::runtime_error &) {rejected=true;}
    check(rejected,"new compiled material cannot save before signature calculation");
    compiled["materials"][0]["replaces"][0]["name"]="mat/fresh.mat";
    compiled["materials"][0]["replaces_signatures"]=
        Document::array({"0123456789abcdef0123456789abcdef"});
    editor_source::validate(compiled,Kind::Compiled);
    compiled["materials"][0]["name"]="renamed";
    editor_source::validate(compiled,Kind::Compiled);
    editor_source::addMaterial(compiled,Kind::Compiled);
    compiled["materials"][1]["name"]="RENAMED";
    rejected=false;
    try {editor_source::validate(compiled,Kind::Compiled);}
    catch(const std::runtime_error &) {rejected=true;}
    check(rejected,"material names must be unique case-insensitively");
}
void testAnimationInheritance() {
    Document source={
        {"name","Animated"},{"description","Animation defaults"},
        {"albedo_scroll",{0.25,-0.5}},
        {"emissive_scroll",{0.0,1.0}},
        {"normal_scroll",{0.3,0.0}},
        {"normal_scroll2",{-0.1,0.2}},
        {"emissive_pulse",{{"wave","random"},{"rate",2.0},
                            {"min",0.2},{"max",1.4},{"phase",0.5}}},
        {"materials",Document::array({{
            {"name","neon"},{"replaces",Document::array({"mat/neon.mat"})}
        }})}
    };
    const auto original=source;
    auto inherited=editor_source::effectiveMaterial(
        source,source["materials"][0],Kind::Source);
    check(inherited["albedo_scroll"]==Document::array({0.25,-0.5}),
          "albedo scroll inherited");
    check(inherited["emissive_scroll"]==Document::array({0.0,1.0}),
          "emissive scroll inherited");
    check(inherited["normal_scroll"]==source["normal_scroll"]&&
          inherited["normal_scroll2"]==source["normal_scroll2"],
          "independent normal layer scrolls inherited");
    check(inherited["emissive_pulse"]==source["emissive_pulse"],
          "emissive pulse inherited intact");
    source["materials"][0]["albedo_scroll"]={0.0,0.0};
    source["materials"][0]["emissive_pulse"]={
        {"wave","sine"},{"rate",1.0},{"min",1.0},{"max",1.0},{"phase",0.0}};
    auto overridden=editor_source::effectiveMaterial(
        source,source["materials"][0],Kind::Source);
    check(overridden["albedo_scroll"]==Document::array({0.0,0.0}),
          "zero scroll overrides inherited motion");
    check(overridden["emissive_pulse"]["min"]==1.0&&
          overridden["emissive_pulse"]["max"]==1.0,
          "neutral pulse overrides inherited pulse");
    source["materials"][0].erase("albedo_scroll");
    source["materials"][0].erase("emissive_pulse");
    check(source==original,"remove animation override restores raw source");
    check(Document::parse(source.dump(4))==original,
          "animation defaults survive serialization");
}
void testRepositorySourcePack() {
    // Verify the compiler's real sample input is accepted verbatim and can
    // be serialized after a material override without flattening defaults.
    std::ifstream file("material_editor/fixtures/sample_matpack/script.json");
    if(!file)throw std::runtime_error("Missing repository source pack fixture");
    Document source;
    file>>source;
    check(editor_source::detect("material_editor/fixtures/sample_matpack/script.json",source)==Kind::Source,
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
    const auto filename=fs::path("material_editor/fixtures/sample_matpack/script.json");
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
        testAnimationInheritance();
        testMaterialListEdits();testDuplicateMaterial();
        testMatReplacementEditing();testExclusiveAlbedoSource();
        testCompiledAndValidation();testRepositorySourcePack();
        testRevertMaterialAndPack();
        std::cout<<"Source pack tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Source pack tests failed: "<<e.what()<<"\n";
        return 1;
    }
}
