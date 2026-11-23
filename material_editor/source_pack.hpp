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
#include <cstdint>
#include <limits>
#include <utility>
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


struct MatReplacement {
    std::string name;
    int cel=0;
    // Preserve the author's short "mat/foo.mat" form for cel zero.
    bool specificCel=false;
};

// Source replacements are a filename (cel 0) or [filename, cel] tuple.
// Compiled replacements use {"name":..., "cel":...}. The editor can
// regenerate their signatures on Save using the configured game assets.
inline bool readMatReplacement(const Document &value,Kind kind,
                               MatReplacement &out) {
    MatReplacement result;
    if(kind==Kind::Source && value.is_string()) {
        result.name=value.get<std::string>();
    } else if(kind==Kind::Source && value.is_array()&&value.size()==2&&
              value[0].is_string()&&value[1].is_number_integer()) {
        result.name=value[0].get<std::string>();
        const auto cel=value[1].get<std::int64_t>();
        if(cel<0||cel>std::numeric_limits<int>::max())return false;
        result.cel=static_cast<int>(cel);
        result.specificCel=true;
    } else if(kind==Kind::Compiled && value.is_object()&&
              value.contains("name")&&value["name"].is_string()&&
              value.contains("cel")&&value["cel"].is_number_integer()) {
        result.name=value["name"].get<std::string>();
        const auto cel=value["cel"].get<std::int64_t>();
        if(cel<0||cel>std::numeric_limits<int>::max())return false;
        result.cel=static_cast<int>(cel);
        result.specificCel=true;
    } else return false;
    out=std::move(result);
    return true;
}

inline Document writeSourceMatReplacement(const MatReplacement &replacement) {
    if(!replacement.specificCel&&replacement.cel==0)
        return replacement.name;
    return Document::array({replacement.name,replacement.cel});
}

// Material names are unique within a pack; keep add/remove operations
// independent of the GUI so structural Undo/Redo can restore entire arrays.
inline std::string nextMaterialName(const Document &root) {
    std::set<std::string> names;
    if(root.contains("materials")&&root["materials"].is_array())
        for(const auto &entry:root["materials"])
            if(entry.is_object()&&entry.contains("name")&&entry["name"].is_string())
                names.insert(lowerAscii(entry["name"].get<std::string>()));
    for(unsigned index=1;index<1000000;++index) {
        const std::string candidate="material_"+std::to_string(index);
        if(!names.count(lowerAscii(candidate)))return candidate;
    }
    throw std::runtime_error("Too many materials");
}
inline size_t addMaterial(Document &root,Kind kind) {
    const std::string name=nextMaterialName(root);
    Document entry={{"name",name}};
    if(kind==Kind::Source)
        entry["replaces"]=Document::array({""});
    else {
        entry["replaces"]=Document::array({
            {{"name",""},{"cel",0}}
        });
        entry["replaces_signatures"]=Document::array();
    }
    root["materials"].push_back(std::move(entry));
    return root["materials"].size()-1;
}
// Clone a material's complete raw properties, including replacement and
// compiled signature metadata. A distinct name is required by pack validation.
// Any replacement edits made afterwards trigger the normal signature refresh.
inline size_t duplicateMaterial(Document &root,size_t index) {
    if(!root.contains("materials")||!root["materials"].is_array()||
       index>=root["materials"].size()||
       !root["materials"][index].is_object())
        throw std::runtime_error("Cannot duplicate an invalid material.");
    const Document original=root["materials"][index];
    const std::string base=original.value("name",std::string("material"));
    std::set<std::string> occupied;
    for(const auto &entry:root["materials"]) {
        if(entry.is_object()&&entry.contains("name")&&
           entry["name"].is_string())
            occupied.insert(lowerAscii(entry["name"].get<std::string>()));
    }
    std::string name=base+" (copy)";
    for(unsigned suffix=2;occupied.count(lowerAscii(name));++suffix) {
        if(suffix>=1000000)
            throw std::runtime_error("Too many copies of this material.");
        name=base+" (copy "+std::to_string(suffix)+")";
    }
    Document clone=original;
    clone["name"]=name;
    root["materials"].push_back(std::move(clone));
    return root["materials"].size()-1;
}
inline bool removeMaterial(Document &root,size_t index) {
    if(!root.contains("materials")||!root["materials"].is_array()||
       index>=root["materials"].size())return false;
    root["materials"].erase(index);
    return true;
}

// These helpers keep the albedo source mutually exclusive. A material
// override of keep_albedo=true suppresses a pack-level albedo_map too.
inline void chooseOriginalMatAlbedo(Document &entry,bool keep) {
    entry["keep_albedo"]=keep;
    entry.erase("use_original_albedo");
    if(keep)entry.erase("albedo_map");
}
inline void chooseAlbedoImage(Document &entry,const std::string &image) {
    entry["albedo_map"]=image;
    entry["keep_albedo"]=false;
    entry.erase("use_original_albedo");
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
    std::set<std::string> names;
    for(const auto &material:doc["materials"]) {
        if(!material.is_object()||!material.contains("name")||
           !material["name"].is_string())
            throw std::runtime_error("Every material needs a name.");
        const auto name=material["name"].get<std::string>();
        if(name.empty()||name.find_first_of("<>:\"/\\|?*")!=std::string::npos)
            throw std::runtime_error("Invalid material name: "+name);
        if(!names.insert(lowerAscii(name)).second)
            throw std::runtime_error("Duplicate material name: "+name);
        if(!material.contains("replaces")||!material["replaces"].is_array()||
           material["replaces"].empty())
            throw std::runtime_error("Material "+name+" needs MAT replacements.");
        for(const auto &entry:material["replaces"]) {
            MatReplacement replacement;
            if(readMatReplacement(entry,kind,replacement)&&
               !replacement.name.empty())continue;
            throw std::runtime_error("Invalid MAT replacement in "+name+
                                     ": expected a nonempty filename and valid cel.");
        }
        if(kind==Kind::Compiled) {
            auto it=material.find("replaces_signatures");
            if(it==material.end()||!it->is_array()||it->empty())
                throw std::runtime_error(
                    "Compiled material "+name+" needs generated MAT signatures.");
            for(const auto &signature:*it) {
                if(!signature.is_string()||signature.get<std::string>().size()!=32)
                    throw std::runtime_error(
                        "Invalid compiled MAT signature for "+name);
            }
            continue;
        }
        // Mirror the compiler's final, resolved mutually exclusive inputs.
        // Material-level original-MAT choices can legitimately suppress a
        // pack-level PNG; reject only the actual effective conflict.
        const Document effective=effectiveMaterial(doc,material,Kind::Source);
        if(effective.value("keep_albedo",false)&&
           effective.contains("albedo_map"))
            throw std::runtime_error(
                "Material "+name+": keep_albedo conflicts with albedo_map.");
    }
}
} // namespace editor_source
