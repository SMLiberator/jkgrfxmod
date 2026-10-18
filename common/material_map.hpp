#pragma once

#include "base/md5.hpp"
#include "json.hpp"
#include "material.hpp"
#include <unordered_map>

namespace jkgm {
    class material_map {
    private:
        std::vector<std::unique_ptr<material>> materials;
        std::unordered_map<md5, size_t> signature_map;
        std::map<fs::path, std::map<int, fs::path>> pack_images;

        void add_metadata(fs::path const &metadata_file);

    public:
        void create_map(fs::path const &materials_dir);

        std::optional<material const *> get_material(md5 const &sig) const;
        std::optional<fs::path> get_pack_image(fs::path const &pack, int image_id) const;
        std::vector<std::unique_ptr<material>> const &get_materials() const;
    };
}