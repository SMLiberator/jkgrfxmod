#include "source_pack.hpp"
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
    source["images"][0]["file"]="subdir/image.png";
    editor_source::validate(source,Kind::Source);
    bool rejected=false;
    try{editor_source::validate(source,Kind::Compiled);}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected,"compiled images cannot be subdirectories");
    auto original=compiled;
    original["materials"][0]["replaces"]=Document::array({"mat/a.mat"});
    original["materials"][0]["name"]="body";
    original["materials"][0].erase("replaces_signatures");
    original["materials"][0]["unrecognized"]={{"key","value"}};
    original["custom_value"]={{"foo",7}};
    editor_source::validate(original,Kind::Source);
    check(Document::parse(original.dump(4))==original,"unknown fields survive");
}
int main(){
    try {
        testSourceInheritance();testPackAlbedoAndAlphaOverrides();
        testCompiledAndValidation();
        std::cout<<"Source pack tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Source pack tests failed: "<<e.what()<<"\n";
        return 1;
    }
}
