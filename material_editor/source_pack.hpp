#pragma once

// Source-pack semantics shared by the editor and its standalone tests.
// Mirrors compile/main.cpp's defaults resolution without mutating script.json.
// This intentionally has no GUI, OpenGL, or game-runtime dependencies.
#include "common/json_incl.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

namespace editor_source {
using Document = json::json;
enum class Kind { Source, Compiled };

// File and per-material revert share the same document semantics as Open.
 // Parse into a temporary document first: malformed or missing disk data
 // must not change the editor's unsaved in-memory state.
inline Document readPack(const std::filesystem::path &filename) {
    std::ifstream stream(filename,std::ios::binary);
    if(!stream)throw std::runtime_error("Unable to open material pack.");
    Document doc;
    stream>>doc;
    if(!doc.is_object()||!doc.contains("materials")||
       !doc["materials"].is_array())
        throw std::runtime_error(
            "Not a material pack (missing materials array).");
    return doc;
}

// Restore exactly one raw material object; never flatten source inheritance
// or affect other materials and pack-wide defaults.
inline bool revertMaterial(Document &current,const Document &saved,
                           size_t index) {
    if(!current.is_object()||!saved.is_object()||
       !current.contains("materials")||!saved.contains("materials")||
       !current["materials"].is_array()||!saved["materials"].is_array()||
       index>=current["materials"].size()||
       index>=saved["materials"].size())
        return false;
    if(current["materials"][index]==saved["materials"][index])
        return false;
    current["materials"][index]=saved["materials"][index];
    return true;
}

inline std::string lowerAscii(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

// Keep this list in step with the defaults list in compile/main.cpp.
// Environment is handled as pack fallback, not material inheritance.
inline constexpr std::array<const char *,35> inheritedProperties{{
    "albedo_map", "keep_albedo", "albedo_factor", "emissive_map",
    "emissive_factor", "albedo_scroll", "emissive_scroll", "emissive_pulse",
    "normal_map", "normal_map2", "normal_strength", "normal_strength2",
    "normal_scroll", "normal_scroll2", "displacement_map",
    "displacement_factor", "surface_map", "matcap_map", "matcap_surface_map",
    "matcap_roughness", "matcap_factor", "clearcoat_surface_map",
    "clearcoat_normal_map", "clearcoat_strength", "clearcoat_roughness",
    "clearcoat_transmission", "reflection_strength", "roughness",
    "metalness", "reflection_mode", "texture_filtering", "alpha_mode",
    "blend_mode", "fluid_thickness", "alpha_cutoff"
}};
// depth_bias is a separate final compiler default, included as well.
inline constexpr const char *extraInheritedProperty="depth_bias";

inline Kind detect(const std::filesystem::path &filename,const Document &doc) {
    const auto name=lowerAscii(filename.filename().u8string());
    if(name=="script.json")return Kind::Source;
    if(name=="metadata.json")return Kind::Compiled;
    auto materials=doc.find("materials");
    if(materials!=doc.end()&&materials->is_array()) {
        for(const auto &mat:*materials) {
            if(!mat.is_object())continue;
            if(mat.contains("replaces_signatures"))return Kind::Compiled;
            auto replaces=mat.find("replaces");
            if(replaces==mat.end()||!replaces->is_array())continue;
            for(const auto &replacement:*replaces) {
                if(replacement.is_object())return Kind::Compiled;
                if(replacement.is_string()||replacement.is_array())
                    return Kind::Source;
            }
        }
    }
    return Kind::Source;
}

inline Document effectiveMaterial(const Document &root,const Document &raw,Kind kind) {
    if(kind==Kind::Compiled)return raw;
    Document effective=raw;
    // Source alias values are previewed, but never normalized on disk.
    if(raw.contains("use_original_albedo")&&!raw.contains("keep_albedo"))
        effective["keep_albedo"]=raw["use_original_albedo"];
    if(!effective.contains("keep_albedo")&&!root.contains("keep_albedo")&&
       root.contains("use_original_albedo"))
        effective["keep_albedo"]=root["use_original_albedo"];
    for(const char *key:inheritedProperties)
        if(!effective.contains(key)&&root.contains(key))
            effective[key]=root[key];
    if(!effective.contains(extraInheritedProperty)&&
       root.contains(extraInheritedProperty))
        effective[extraInheritedProperty]=root[extraInheritedProperty];

    // Pack-wide keep_albedo and albedo_map are mutually exclusive at the
    // effective material level. Explicit material choices win.
    const bool explicitKeep=raw.contains("keep_albedo")||
                            raw.contains("use_original_albedo");
    if(raw.contains("albedo_map")&&!explicitKeep)
        effective["keep_albedo"]=false;
    if(explicitKeep&&effective.value("keep_albedo",false)==true &&
       !raw.contains("albedo_map")&&root.contains("albedo_map"))
        effective.erase("albedo_map");

    // Inherited cutoffs only apply to a material that remains in mask mode.
    if(!raw.contains("alpha_cutoff")&&effective.contains("alpha_cutoff")&&
       effective.value("alpha_mode",std::string{})!="mask")
        effective.erase("alpha_cutoff");
    return effective;
}

inline bool isInherited(const Document &root,const Document &material,
                        const char *key,Kind kind) {
    if(kind!=Kind::Source||material.contains(key))return false;
    if(std::string(key)=="keep_albedo")
        return root.contains(key)||root.contains("use_original_albedo");
    return root.contains(key);
}

inline void validate(const Document &doc,Kind kind) {
    if(!doc.is_object()||!doc.contains("materials")||
       !doc["materials"].is_array())
        throw std::runtime_error("Material pack must have a materials array.");
    for(const char *key:{"name","description"}) {
        if(!doc.contains(key)||!doc[key].is_string())
            throw std::runtime_error(std::string("Pack ")+key+" must be a string.");
    }
    if(doc["name"].get<std::string>().empty())
        throw std::runtime_error("Pack name cannot be empty.");
    for(const char *key:{"comment","environment_map"}) {
        if(doc.contains(key)&&!doc[key].is_string())
            throw std::runtime_error(std::string("Pack ")+key+" must be a string.");
    }
    if(doc.contains("images")) {
        if(!doc["images"].is_array())
            throw std::runtime_error("Pack images must be an array.");
        std::set<int> used;
        for(const auto &image:doc["images"]) {
            if(!image.is_object()||!image.contains("id")||
               !image["id"].is_number_integer()||
               !image.contains("file")||!image["file"].is_string())
                throw std::runtime_error("Image registry requires {id, file} records.");
            const int id=image["id"].get<int>();
            if(id<0||id>65535||!used.insert(id).second)
                throw std::runtime_error("Image IDs must be unique and 0..65535.");
            const auto name=image["file"].get<std::string>();
            if(name.empty())
                throw std::runtime_error("Registered image filenames cannot be empty.");
            if(kind==Kind::Compiled) {
                const auto path=std::filesystem::u8path(name);
                if(path.is_absolute()||path.has_parent_path()||
                   path.filename().empty()||name=="."||name=="..")
                    throw std::runtime_error(
                        "Compiled image files must be directly pack-local.");
            }
        }
    }
    if(kind!=Kind::Source)return;

    std::set<std::string> names;
    for(const auto &material:doc["materials"]) {
        if(!material.is_object()||!material.contains("name")||
           !material["name"].is_string())
            throw std::runtime_error("Every source material needs a name.");
        const auto name=material["name"].get<std::string>();
        if(name.empty()||name.find_first_of("<>:\"/\\|?*")!=std::string::npos)
            throw std::runtime_error("Invalid source material name: "+name);
        if(!names.insert(lowerAscii(name)).second)
            throw std::runtime_error("Duplicate source material name: "+name);
        if(!material.contains("replaces")||!material["replaces"].is_array()||
           material["replaces"].empty())
            throw std::runtime_error("Source material "+name+" needs MAT replacements.");
        for(const auto &entry:material["replaces"]) {
            if(entry.is_string()&&!entry.get<std::string>().empty())continue;
            if(entry.is_array()&&entry.size()==2&&entry[0].is_string()&&
               !entry[0].get<std::string>().empty()&&
               entry[1].is_number_integer()&&entry[1].get<int>()>=0)
                continue;
            throw std::runtime_error("Invalid MAT replacement in "+name+
                                     ": expected filename or [filename, cel].");
        }
    }
}
} // namespace editor_source
