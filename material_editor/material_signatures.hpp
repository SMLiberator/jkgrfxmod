#pragma once
#include "native_mat.hpp"
#include "source_pack.hpp"
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Refresh only mappings which are new or changed since the last saved pack.
// Existing compiled packs must still be editable even when game assets are
// unavailable, provided their replacement targets were not modified.
namespace editor_signatures {
using editor_source::Document;

inline bool matchesSavedReplacement(const Document &material,
                                    const Document &saved) {
    if(!material.is_object()||!material.contains("replaces")||
       !material.contains("replaces_signatures")||
       !saved.is_object()||!saved.contains("materials")||
       !saved["materials"].is_array())return false;
    for(const auto &prior:saved["materials"]) {
        if(prior.is_object()&&prior.contains("replaces")&&
           prior.contains("replaces_signatures")&&
           prior["replaces"]==material["replaces"]&&
           prior["replaces_signatures"]==material["replaces_signatures"]&&
           prior["replaces_signatures"].is_array()&&
           !prior["replaces_signatures"].empty())
            return true;
    }
    return false;
}

inline size_t refreshChanged(Document &toSave,const Document &saved,
                             const NativeMatSource &assets) {
    size_t updated=0;
    if(!toSave.contains("materials")||!toSave["materials"].is_array())
        throw std::runtime_error("Missing materials array");
    for(auto &material:toSave["materials"]) {
        if(matchesSavedReplacement(material,saved))continue;
        if(!material.is_object()||!material.contains("replaces")||
           !material["replaces"].is_array()||material["replaces"].empty())
            throw std::runtime_error("Material needs MAT replacements");
        std::set<std::string> allSignatures;
        for(const auto &raw:material["replaces"]) {
            editor_source::MatReplacement replacement;
            if(!editor_source::readMatReplacement(
                    raw,editor_source::Kind::Compiled,replacement)||
               replacement.name.empty())
                throw std::runtime_error("Invalid compiled MAT replacement");
            std::vector<std::string> signatures;
            std::string error;
            if(!assets.signatures(replacement.name,replacement.cel,
                                  signatures,error))
                throw std::runtime_error(error);
            allSignatures.insert(signatures.begin(),signatures.end());
        }
        if(allSignatures.empty())
            throw std::runtime_error("Cannot save material with zero MAT signatures");
        material["replaces_signatures"]=Document::array();
        for(const auto &hash:allSignatures)
            material["replaces_signatures"].push_back(hash);
        ++updated;
    }
    return updated;
}
} // namespace editor_signatures
