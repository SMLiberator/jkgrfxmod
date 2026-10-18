#include "base/file_block.hpp"
#include "base/filesystem.hpp"
#include "base/input_stream.hpp"
#include "base/log.hpp"
#include "base/md5.hpp"
#include "base/memory_block.hpp"
#include "base/std_input_stream.hpp"
#include "colormap.hpp"
#include "common/image.hpp"
#include "common/json_incl.hpp"
#include "gob_virtual_container.hpp"
#include "jk_virtual_file_system.hpp"
#include "math/color.hpp"
#include "math/color_conv.hpp"
#include "math/point.hpp"
#include "program/program.hpp"
#include "raw_material.hpp"
#include <bitset>
#include <cmath>
#include <set>

namespace jkgm {
    enum class out_material_alpha_mode { blend, mask };

    class out_material_replacement {
    public:
        std::string name;
        int cel;

        out_material_replacement(std::string name, int cel)
            : name(std::move(name))
            , cel(cel)
        {
        }
    };

    class out_material {
    public:
        std::string name;

        std::vector<out_material_replacement> replaces;
        std::vector<md5> replaces_signatures;

        std::optional<std::string> albedo_map;
        bool keep_albedo = false;
        std::optional<color> albedo_factor;

        std::optional<std::string> emissive_map;
        std::optional<color_rgb> emissive_factor;
        std::optional<point<2, float>> albedo_scroll;
        std::optional<point<2, float>> emissive_scroll;
        std::optional<json::json> emissive_pulse;

        std::optional<std::string> normal_map;
        std::optional<std::string> normal_map2;
        std::optional<float> normal_strength;
        std::optional<float> normal_strength2;
        std::optional<point<2, float>> normal_scroll;
        std::optional<point<2, float>> normal_scroll2;

        std::optional<std::string> displacement_map;
        std::optional<float> displacement_factor;

        std::optional<std::string> surface_map;
        std::optional<float> reflection_strength;
        std::optional<float> roughness;
        std::optional<float> metalness;
        std::optional<std::string> env_map;
        std::optional<std::string> reflection_mode;
        std::optional<bool> texture_filtering;

        std::optional<out_material_alpha_mode> alpha_mode;
        std::optional<std::string> blend_mode;
        std::optional<float> fluid_thickness;
        std::optional<bool> depth_clip;
        std::optional<float> alpha_cutoff;
    };

    class out_material_pack {
    public:
        std::string name;
        std::string description;
        std::optional<std::string> comment;
        std::optional<std::string> environment_map;

        // Images remain addressable by pack-local COG integer IDs even if no
        // compiled material refers to them.
        std::map<int, std::string> images;
        std::vector<std::unique_ptr<out_material>> materials;
    };

    class processed_names_map {
    public:
        std::set<std::string> seen_material_names;
        std::map<fs::path, std::string> srgb_map;
        std::map<fs::path, std::string> linear_map;
    };

    bool is_pow2(int dim)
    {
        std::bitset<sizeof(dim) * 8> bs(dim);
        return bs.count() == 1;
    }

    std::string get_convert_linear_image(processed_names_map *map,
                                         fs::path const &in_path,
                                         std::string const &desired_name,
                                         fs::path const &desired_out_path)
    {
        auto it = map->linear_map.find(in_path);
        if(it != map->linear_map.end()) {
            // File has already been processed. Reuse the same file.
            return it->second;
        }

        diagnostic_context dc(in_path.generic_string());

        // File has not been seen before.
        try {
            auto is = make_file_input_block(in_path);
            auto img = load_image(is.get());

            if(!is_pow2(get<x>(img->dimensions)) || !is_pow2(get<y>(img->dimensions))) {
                LOG_ERROR("Image dimensions are not power of two");
                throw std::runtime_error("invalid image");
            }

            auto os = make_file_output_block(desired_out_path);
            store_image_png(os.get(), *img);

            // Add to map
            map->linear_map.emplace(in_path, desired_name);
            return desired_name;
        }
        catch(std::exception const &e) {
            LOG_ERROR("Failed to load image file: ", e.what());
            throw;
        }
    }

    std::string get_convert_image(processed_names_map *map,
                                  fs::path const &in_path,
                                  std::string const &desired_name,
                                  fs::path const &desired_out_path)
    {
        auto it = map->srgb_map.find(in_path);
        if(it != map->srgb_map.end()) {
            // File has already been processed. Reuse the same file.
            return it->second;
        }

        diagnostic_context dc(in_path.generic_string());

        // File has not been seen before.
        try {
            auto is = make_file_input_block(in_path);
            auto img = load_image(is.get());

            if(!is_pow2(get<x>(img->dimensions)) || !is_pow2(get<y>(img->dimensions))) {
                LOG_ERROR("Image dimensions are not power of two");
                throw std::runtime_error("invalid image");
            }

            // Premultiply alpha
            for(auto &em : img->data) {
                auto linear_col = srgb_to_linear(to_float_color(em));
                auto pma_col =
                    extend(get<rgb>(linear_col) * get<a>(linear_col), get<a>(linear_col));
                em = to_discrete_color(linear_to_srgb(pma_col));
            }

            auto os = make_file_output_block(desired_out_path);
            store_image_png(os.get(), *img);

            // Add to map
            map->srgb_map.emplace(in_path, desired_name);
            return desired_name;
        }
        catch(std::exception const &e) {
            LOG_ERROR("Failed to load image file: ", e.what());
            throw;
        }
    }

    std::vector<md5>
        get_8bit_mat_cel_signatures(raw_material const &mat,
                                    int celnum,
                                    std::vector<std::unique_ptr<colormap>> const &colormaps)
    {
        std::vector<md5> rv;

        auto const &cel = mat.cel_records.at(celnum);
        auto const &tex = mat.texture_records.at(cel.texture_index);

        uint32_t next_width = tex.width;
        uint32_t next_height = tex.height;
        for(auto const &data : tex.image_data) {
            for(auto const &cmp : colormaps) {
                md5_hasher mh;
                mh.add(make_span(&next_width, 1).as_const_bytes());
                mh.add(make_span(&next_height, 1).as_const_bytes());

                for(auto const &px : data) {
                    auto cmp_color = cmp->get_direct_color(px);
                    auto conv_color = extend(cmp_color, uint8_t(255));

                    if(tex.uses_transparency && (px == mat.transparency)) {
                        conv_color = color_rgba8::zero();
                    }

                    uint16_t conv_value;
                    if(tex.uses_transparency) {
                        conv_value = srgb_a8_to_rgba5551(conv_color);
                    }
                    else {
                        conv_value = srgb_a8_to_rgb565(conv_color);
                    }

                    mh.add(make_span(&conv_value, 1).as_const_bytes());
                }

                rv.push_back(mh.finish());
            }

            next_width >>= 1;
            next_height >>= 1;
        }

        return rv;
    }

    std::vector<md5> get_16bit_mat_cel_signatures(raw_material const &mat, int celnum)
    {
        std::vector<md5> rv;

        auto const &cel = mat.cel_records.at(celnum);
        auto const &tex = mat.texture_records.at(cel.texture_index);

        uint32_t next_width = tex.width;
        uint32_t next_height = tex.height;
        for(auto const &data : tex.image_data) {
            md5_hasher mh;
            mh.add(make_span(&next_width, 1).as_const_bytes());
            mh.add(make_span(&next_height, 1).as_const_bytes());
            mh.add(make_span(data).as_const_bytes());

            rv.push_back(mh.finish());

            next_width >>= 1;
            next_height >>= 1;
        }

        return rv;
    }

    std::vector<md5> get_mat_cel_signatures(std::string const &mat_filename,
                                            int celnum,
                                            virtual_file_system *vfs,
                                            std::vector<std::unique_ptr<colormap>> const &colormaps)
    {
        auto mf = vfs->open(mat_filename);
        raw_material mat(mf.get());

        if(mat.bitdepth == 16) {
            return get_16bit_mat_cel_signatures(mat, celnum);
        }
        else {
            return get_8bit_mat_cel_signatures(mat, celnum, colormaps);
        }
    }

    std::unique_ptr<out_material>
        process_material(json::json const &doc,
                         processed_names_map *img_map,
                         virtual_file_system *vfs,
                         std::vector<std::unique_ptr<colormap>> const &colormaps,
                         fs::path const &script_path,
                         fs::path const &output_path)
    {
        auto rv = std::make_unique<out_material>();

        if(!doc.contains("name")) {
            LOG_ERROR("Material does not define a name");
            throw std::runtime_error("Invalid script");
        }

        rv->name = doc["name"];

        std::string lc_name;
        lc_name.reserve(rv->name.size());
        std::transform(rv->name.begin(), rv->name.end(), std::back_inserter(lc_name), to_lower);
        if(img_map->seen_material_names.find(lc_name) != img_map->seen_material_names.end()) {
            LOG_ERROR("Material name '", rv->name, "' defined multiple times");
            throw std::runtime_error("Invalid script");
        }

        img_map->seen_material_names.insert(std::move(lc_name));

        LOG_INFO(rv->name);

        if(rv->name.find_first_of("<>:\"/\\|?*") != std::string::npos) {
            LOG_ERROR("Material '", rv->name, "' name contains invalid characters");
            throw std::runtime_error("Invalid script");
        }

        if(doc.contains("albedo_map")) {
            auto map_filename = str(fmt(rv->name, ".albedo.png"));
            auto real_map_filename =
                get_convert_image(img_map,
                                  /*input path*/ script_path.parent_path() /
                                      static_cast<std::string>(doc["albedo_map"]),
                                  /*desired name*/ map_filename,
                                  /*desired out path*/ output_path / map_filename);
            rv->albedo_map = real_map_filename;
        }

        // "use_original_albedo" is a descriptive synonym of the original
        // "keep_albedo" option. Compiled metadata continues to write keep_albedo
        // so older packs and renderers retain their existing behavior.
        if(doc.contains("keep_albedo") && doc.contains("use_original_albedo")) {
            LOG_ERROR("Material '", rv->name, "' specifies both keep_albedo and "
                      "use_original_albedo");
            throw std::runtime_error("Invalid script");
        }
        char const *keep_key = doc.contains("keep_albedo")
            ? "keep_albedo" : "use_original_albedo";
        if(doc.contains(keep_key)) {
            auto const &v = doc[keep_key];
            if(!v.is_boolean()) {
                LOG_ERROR("Material '", rv->name, "' ", keep_key, " must be true or false");
                throw std::runtime_error("Invalid script");
            }
            rv->keep_albedo = static_cast<bool>(v);
        }

        if(rv->keep_albedo && rv->albedo_map.has_value()) {
            LOG_ERROR("Material '",
                      rv->name,
                      "' cannot have both an albedo map and keep_albedo");
            throw std::runtime_error("Invalid script");
        }

        if(doc.contains("albedo_factor")) {
            auto const &af = doc["albedo_factor"];
            if(!af.is_array() || af.size() != 4) {
                LOG_ERROR("Material '", rv->name, "' albedo factor must be an RGBA tuple");
                throw std::runtime_error("Invalid script");
            }

            rv->albedo_factor = color(static_cast<float>(af[0]),
                                      static_cast<float>(af[1]),
                                      static_cast<float>(af[2]),
                                      static_cast<float>(af[3]));
        }

        if(doc.contains("emissive_map")) {
            auto map_filename = str(fmt(rv->name, ".emissive.png"));
            auto real_map_filename =
                get_convert_image(img_map,
                                  /*input path*/ script_path.parent_path() /
                                      static_cast<std::string>(doc["emissive_map"]),
                                  /*desired name*/ map_filename,
                                  /*desired out path*/ output_path / map_filename);
            rv->emissive_map = real_map_filename;
        }

        if(doc.contains("emissive_factor")) {
            auto const &ef = doc["emissive_factor"];
            if(!ef.is_array() || ef.size() != 3) {
                LOG_ERROR("Material '", rv->name, "' emissive factor must be an RGB tuple");
                throw std::runtime_error("Invalid script");
            }

            rv->emissive_factor = color_rgb(
                static_cast<float>(ef[0]), static_cast<float>(ef[1]), static_cast<float>(ef[2]));
        }

        // Normal maps are DATA, never sRGB color: keep encoded XYZ intact.
        auto import_normal_map = [&](char const *key, char const *suffix)
            -> std::optional<std::string> {
            if(!doc.contains(key)) {
                return std::nullopt;
            }
            if(!doc[key].is_string()) {
                LOG_ERROR("Material '", rv->name, "' ", key, " must be a filename");
                throw std::runtime_error("Invalid script");
            }
            auto map_filename = str(fmt(rv->name, suffix));
            return get_convert_linear_image(img_map,
                                            script_path.parent_path() /
                                                static_cast<std::string>(doc[key]),
                                            map_filename,
                                            output_path / map_filename);
        };
        rv->normal_map = import_normal_map("normal_map", ".normal.png");
        rv->normal_map2 = import_normal_map("normal_map2", ".normal2.png");

        if(doc.contains("normal_map2") && !doc.contains("normal_map")) {
            LOG_ERROR("Material '", rv->name,
                      "' normal_map2 requires normal_map as the first layer");
            throw std::runtime_error("Invalid script");
        }

        auto parse_normal_strength = [&](char const *key) -> std::optional<float> {
            if(!doc.contains(key)) {
                return std::nullopt;
            }
            if(!doc[key].is_number() ||
               !std::isfinite(static_cast<double>(doc[key]))) {
                LOG_ERROR("Material '", rv->name, "' ", key, " must be finite");
                throw std::runtime_error("Invalid script");
            }
            float s = static_cast<float>(doc[key]);
            if(!std::isfinite(s) || s < 0.0f || s > 4.0f) {
                LOG_ERROR("Material '", rv->name, "' ", key, " must be in [0, 4]");
                throw std::runtime_error("Invalid script");
            }
            return s;
        };
        rv->normal_strength = parse_normal_strength("normal_strength");
        rv->normal_strength2 = parse_normal_strength("normal_strength2");

        if(doc.contains("displacement_map")) {
            auto map_filename = str(fmt(rv->name, ".displacement.png"));
            auto real_map_filename =
                get_convert_linear_image(img_map,
                                         /*input path*/ script_path.parent_path() /
                                             static_cast<std::string>(doc["displacement_map"]),
                                         /*desired name*/ map_filename,
                                         /*desired out path*/ output_path / map_filename);
            rv->displacement_map = real_map_filename;
        }

        if(doc.contains("displacement_factor")) {
            auto const &ef = doc["displacement_factor"];
            if(!ef.is_number()) {
                LOG_ERROR("Material '", rv->name, "' displacement factor must be a number");
                throw std::runtime_error("Invalid script");
            }

            rv->displacement_factor = static_cast<float>(ef);
        }

        if(doc.contains("surface_map")) {
            auto map_filename = str(fmt(rv->name, ".surface.png"));
            auto real_map_filename =
                get_convert_linear_image(img_map,
                                         /*input path*/ script_path.parent_path() /
                                             static_cast<std::string>(doc["surface_map"]),
                                         /*desired name*/ map_filename,
                                         /*desired out path*/ output_path / map_filename);
            rv->surface_map = real_map_filename;
        }

        if(doc.contains("reflection_strength")) {
            auto const &v = doc["reflection_strength"];
            if(!v.is_number() || static_cast<float>(v) < 0.0f) {
                LOG_ERROR("Material '", rv->name, "' reflection strength must be a number >= 0");
                throw std::runtime_error("Invalid script");
            }

            rv->reflection_strength = static_cast<float>(v);
        }

        if(doc.contains("roughness")) {
            auto const &v = doc["roughness"];
            if(!v.is_number() || static_cast<float>(v) < 0.0f || static_cast<float>(v) > 1.0f) {
                LOG_ERROR("Material '", rv->name, "' roughness must be a number from 0 to 1");
                throw std::runtime_error("Invalid script");
            }

            rv->roughness = static_cast<float>(v);
        }

        if(doc.contains("metalness")) {
            auto const &v = doc["metalness"];
            if(!v.is_number() || static_cast<float>(v) < 0.0f || static_cast<float>(v) > 1.0f) {
                LOG_ERROR("Material '", rv->name, "' metalness must be a number from 0 to 1");
                throw std::runtime_error("Invalid script");
            }

            rv->metalness = static_cast<float>(v);
        }

        if(doc.contains("reflection_mode")) {
            auto const &v = doc["reflection_mode"];
            if(!v.is_string() ||
               (v != "envmap" && v != "screen" && v != "fixed" && v != "matcap")) {
                LOG_ERROR("Material '", rv->name,
                          "' reflection_mode must be envmap, screen, fixed, or matcap");
                throw std::runtime_error("Invalid script");
            }
            rv->reflection_mode = static_cast<std::string>(v);
        }

        if(doc.contains("texture_filtering")) {
            auto const &v = doc["texture_filtering"];
            if(!v.is_boolean()) {
                LOG_ERROR("Material '", rv->name, "' texture_filtering must be true or false");
                throw std::runtime_error("Invalid script");
            }
            rv->texture_filtering = static_cast<bool>(v);
        }

        if(doc.contains("environment_map")) {
            auto map_filename = str(fmt(rv->name, ".env.png"));
            auto real_map_filename =
                get_convert_image(img_map,
                                  /*input path*/ script_path.parent_path() /
                                      static_cast<std::string>(doc["environment_map"]),
                                  /*desired name*/ map_filename,
                                  /*desired out path*/ output_path / map_filename);
            rv->env_map = real_map_filename;
        }

        // Animated texture speeds (UV repeats per second).
        auto parse_scroll = [&](char const *key) -> std::optional<point<2, float>> {
            if(!doc.contains(key)) {
                return std::nullopt;
            }
            auto const &v = doc[key];
            if(!v.is_array() || v.size() != 2 ||
               !v[0].is_number() || !v[1].is_number() ||
               !std::isfinite(static_cast<double>(v[0])) ||
               !std::isfinite(static_cast<double>(v[1])) ||
               !std::isfinite(static_cast<float>(v[0])) ||
               !std::isfinite(static_cast<float>(v[1]))) {
                LOG_ERROR("Material '", rv->name, "' ", key,
                          " must be two finite UV speeds [u, v]");
                throw std::runtime_error("Invalid script");
            }
            return make_point(static_cast<float>(v[0]), static_cast<float>(v[1]));
        };
        rv->albedo_scroll = parse_scroll("albedo_scroll");
        rv->emissive_scroll = parse_scroll("emissive_scroll");
        rv->normal_scroll = parse_scroll("normal_scroll");
        rv->normal_scroll2 = parse_scroll("normal_scroll2");

        if(doc.contains("emissive_pulse")) {
            auto const &v = doc["emissive_pulse"];
            if(!v.is_object() || !v.contains("wave") || !v["wave"].is_string() ||
               (v["wave"] != "sine" && v["wave"] != "square" &&
                v["wave"] != "flicker" && v["wave"] != "random") ||
               !v.contains("rate") || !v["rate"].is_number()) {
                LOG_ERROR("Material '", rv->name,
                          "' emissive_pulse needs wave (sine/square/flicker/random) "
                          "and positive rate (seconds)");
                throw std::runtime_error("Invalid script");
            }

            for(char const *key : {"rate", "min", "max", "phase"}) {
                if(!v.contains(key)) {
                    continue;
                }
                if(!v[key].is_number() ||
                   !std::isfinite(static_cast<double>(v[key])) ||
                   !std::isfinite(static_cast<float>(v[key]))) {
                    LOG_ERROR("Material '", rv->name,
                              "' emissive_pulse.", key, " must be a finite number");
                    throw std::runtime_error("Invalid script");
                }
            }

            float const rate = static_cast<float>(v["rate"]);
            float const min_value = v.contains("min") ? static_cast<float>(v["min"]) : 0.0f;
            float const max_value = v.contains("max") ? static_cast<float>(v["max"]) : 1.0f;
            if(rate <= 0.0f || min_value < 0.0f || max_value < min_value) {
                LOG_ERROR("Material '", rv->name,
                          "' emissive_pulse requires rate > 0 and 0 <= min <= max");
                throw std::runtime_error("Invalid script");
            }

            rv->emissive_pulse = v;
        }

        if(doc.contains("alpha_mode")) {
            auto const &am = doc["alpha_mode"];
            if(am == "mask") {
                rv->alpha_mode = out_material_alpha_mode::mask;
            }
            else if(am == "blend") {
                rv->alpha_mode = out_material_alpha_mode::blend;
            }
            else {
                LOG_ERROR("Material '",
                          rv->name,
                          "' unknown alpha_mode '",
                          static_cast<std::string>(am),
                          "'");
                throw std::runtime_error("Invalid script");
            }
        }

        if(doc.contains("blend_mode")) {
            auto const &mode = doc["blend_mode"];
            if(!mode.is_string() ||
               (mode != "normal" && mode != "additive" &&
                mode != "multiply" && mode != "fluid")) {
                LOG_ERROR("Material '", rv->name,
                          "' blend_mode must be normal, additive, multiply, or fluid");
                throw std::runtime_error("Invalid script");
            }
            rv->blend_mode = static_cast<std::string>(mode);
        }

        if(doc.contains("fluid_thickness")) {
            auto const &v = doc["fluid_thickness"];
            if(!v.is_number() || !std::isfinite(static_cast<double>(v)) ||
               !std::isfinite(static_cast<float>(v)) ||
               static_cast<float>(v) < 0.0f) {
                LOG_ERROR("Material '", rv->name,
                          "' fluid_thickness must be a finite, nonnegative number");
                throw std::runtime_error("Invalid script");
            }
            rv->fluid_thickness = static_cast<float>(v);
        }

        if(doc.contains("depth_clip")) {
            auto const &v = doc["depth_clip"];
            if(!v.is_boolean()) {
                LOG_ERROR("Material '", rv->name, "' depth_clip must be true or false");
                throw std::runtime_error("Invalid script");
            }
            rv->depth_clip = static_cast<bool>(v);
        }

        // Fluid absorption and decal plane clipping are different modes:
        // fluid uses empty opaque-depth pixels as fully deep, while decals
        // discard them. Reject ambiguous combinations.
        if(rv->blend_mode == "fluid" && rv->depth_clip.value_or(false)) {
            LOG_ERROR("Material '", rv->name,
                      "' cannot combine fluid blend_mode with depth_clip");
            throw std::runtime_error("Invalid script");
        }

        if(doc.contains("alpha_cutoff")) {
            if(rv->alpha_mode != out_material_alpha_mode::mask) {
                LOG_ERROR(
                    "Material '", rv->name, "' specifies alpha cutoff without alpha mode mask");
                throw std::runtime_error("Invalid script");
            }

            rv->alpha_cutoff = doc["alpha_cutoff"];
        }

        // Get signatures for all replaced materials
        std::set<md5> signatures;

        if(!doc.contains("replaces")) {
            LOG_ERROR("Material '", rv->name, "' does not define any replacements");
            throw std::runtime_error("Invalid script");
        }

        for(auto const &em : doc["replaces"]) {
            std::string mat_filename;
            int celnum = 0;

            if(em.is_string()) {
                // Simple replacement: material only, cel 0
                mat_filename = em;
            }
            else if(em.is_array() && em.size() == 2) {
                // Brief replacement: material and cel tuple
                mat_filename = em[0];
                celnum = em[1];
            }
            else {
                LOG_ERROR("Material '", rv->name, "' contains an unreadable replacement");
                throw std::runtime_error("Invalid script");
            }

            rv->replaces.emplace_back(mat_filename, celnum);

            diagnostic_context dc(mat_filename);
            try {
                auto new_sigs = get_mat_cel_signatures(mat_filename, celnum, vfs, colormaps);
                std::copy(new_sigs.begin(),
                          new_sigs.end(),
                          std::inserter(signatures, signatures.begin()));
            }
            catch(std::exception const &e) {
                LOG_ERROR("Failed to load MAT file: ", e.what());
                throw std::runtime_error("Invalid script");
            }
        }

        rv->replaces_signatures.reserve(signatures.size());
        std::copy(
            signatures.begin(), signatures.end(), std::back_inserter(rv->replaces_signatures));

        return rv;
    }

    json::json write_packfile_material(out_material const &mat)
    {
        json::json rv;

        rv["name"] = mat.name;

        for(auto const &repl : mat.replaces) {
            rv["replaces"].push_back({{"name", repl.name}, {"cel", repl.cel}});
        }

        for(auto const &repl : mat.replaces_signatures) {
            rv["replaces_signatures"].push_back(static_cast<std::string>(repl));
        }

        if(mat.albedo_map.has_value()) {
            rv["albedo_map"] = *mat.albedo_map;
        }

        if(mat.keep_albedo) {
            rv["keep_albedo"] = true;
        }

        if(mat.albedo_factor.has_value()) {
            auto c = *mat.albedo_factor;
            rv["albedo_factor"] = {get<0>(c), get<1>(c), get<2>(c), get<3>(c)};
        }

        if(mat.emissive_map.has_value()) {
            rv["emissive_map"] = *mat.emissive_map;
        }

        if(mat.emissive_factor.has_value()) {
            auto c = *mat.emissive_factor;
            rv["emissive_factor"] = {get<0>(c), get<1>(c), get<2>(c)};
        }

        if(mat.albedo_scroll.has_value()) {
            auto s = *mat.albedo_scroll;
            rv["albedo_scroll"] = {get<0>(s), get<1>(s)};
        }
        if(mat.emissive_scroll.has_value()) {
            auto s = *mat.emissive_scroll;
            rv["emissive_scroll"] = {get<0>(s), get<1>(s)};
        }
        if(mat.emissive_pulse.has_value()) {
            rv["emissive_pulse"] = *mat.emissive_pulse;
        }

        if(mat.normal_map.has_value()) {
            rv["normal_map"] = *mat.normal_map;
        }
        if(mat.normal_map2.has_value()) {
            rv["normal_map2"] = *mat.normal_map2;
        }
        if(mat.normal_strength.has_value()) {
            rv["normal_strength"] = *mat.normal_strength;
        }
        if(mat.normal_strength2.has_value()) {
            rv["normal_strength2"] = *mat.normal_strength2;
        }
        if(mat.normal_scroll.has_value()) {
            auto s = *mat.normal_scroll;
            rv["normal_scroll"] = {get<0>(s), get<1>(s)};
        }
        if(mat.normal_scroll2.has_value()) {
            auto s = *mat.normal_scroll2;
            rv["normal_scroll2"] = {get<0>(s), get<1>(s)};
        }

        if(mat.displacement_map.has_value()) {
            rv["displacement_map"] = *mat.displacement_map;
        }

        if(mat.displacement_factor.has_value()) {
            rv["displacement_factor"] = *mat.displacement_factor;
        }

        if(mat.surface_map.has_value()) {
            rv["surface_map"] = *mat.surface_map;
        }

        if(mat.reflection_strength.has_value()) {
            rv["reflection_strength"] = *mat.reflection_strength;
        }

        if(mat.roughness.has_value()) {
            rv["roughness"] = *mat.roughness;
        }

        if(mat.metalness.has_value()) {
            rv["metalness"] = *mat.metalness;
        }

        if(mat.env_map.has_value()) {
            rv["environment_map"] = *mat.env_map;
        }

        if(mat.reflection_mode.has_value()) {
            rv["reflection_mode"] = *mat.reflection_mode;
        }

        if(mat.texture_filtering.has_value()) {
            rv["texture_filtering"] = *mat.texture_filtering;
        }

        if(mat.alpha_mode.has_value()) {
            switch(*mat.alpha_mode) {
            default:
            case out_material_alpha_mode::blend:
                rv["alpha_mode"] = "blend";
                break;

            case out_material_alpha_mode::mask:
                rv["alpha_mode"] = "mask";
                break;
            }
        }

        if(mat.blend_mode.has_value()) {
            rv["blend_mode"] = *mat.blend_mode;
        }
        if(mat.fluid_thickness.has_value()) {
            rv["fluid_thickness"] = *mat.fluid_thickness;
        }

        if(mat.depth_clip.has_value()) {
            rv["depth_clip"] = *mat.depth_clip;
        }

        if(mat.alpha_mode.has_value() && mat.alpha_cutoff.has_value()) {
            rv["alpha_cutoff"] = *mat.alpha_cutoff;
        }

        return rv;
    }

    void write_packfile(fs::path const &output_path, out_material_pack const &pack)
    {
        json::json doc;

        // Add author metadata
        doc["name"] = pack.name;
        doc["description"] = pack.description;

        if(pack.comment.has_value()) {
            doc["comment"] = *pack.comment;
        }

        if(pack.environment_map.has_value()) {
            doc["environment_map"] = *pack.environment_map;
        }

        // Optional: absent in legacy packs, whose metadata stays valid.
        if(!pack.images.empty()) {
            doc["images"] = json::json::array();
            for(auto const &entry : pack.images) {
                doc["images"].push_back({{"id", entry.first}, {"file", entry.second}});
            }
        }

        // Add material mappings
        for(auto const &em : pack.materials) {
            doc["materials"].push_back(write_packfile_material(*em));
        }

        auto out_fs = make_file_output_block(output_path / "metadata.json");
        out_fs->write(make_span(doc.dump(4)));
    }

    class compile_program : public program {
    public:
        fs::path script_path;
        fs::path output_path;

        fs::path jk_resource;
        fs::path jk_episode;
        fs::path jk_game;

        virtual void create_options(options &opts) override
        {
            opts.insert(make_value_option("script", script_path));
            opts.insert(make_value_option("output", output_path));

            opts.insert(make_value_option("game", jk_game));
            opts.insert(make_value_option("episode", jk_episode));
            opts.insert(make_value_option("resource", jk_resource));

            opts.emplace_constraint<required_option>(
                std::vector<std::string>{"script", "output", "resource"});
        }

        virtual int run() override
        {
            // Set up virtual file system from command line arguments
            std::unique_ptr<jk_virtual_file_system> vfs;
            if(jk_game.empty()) {
                vfs = std::make_unique<jk_virtual_file_system>(jk_resource);
            }
            else {
                vfs = std::make_unique<jk_virtual_file_system>(jk_resource, jk_game);
            }

            std::unique_ptr<virtual_container> episode_gob;
            if(!jk_episode.empty()) {
                episode_gob = std::make_unique<gob_virtual_container>(jk_episode);
                vfs->set_current_episode(*episode_gob);
            }

            // Load colormaps from virtual file system
            LOG_INFO("Loading colormaps...");
            std::vector<std::string> colormap_names;
            for(auto const &em : vfs->list_files()) {
                if(iends_with(std::get<0>(em), ".cmp")) {
                    colormap_names.push_back(std::get<0>(em));
                }
            }

            std::vector<std::unique_ptr<colormap>> colormaps;
            for(auto const &em : colormap_names) {
                LOG_INFO(em);
                diagnostic_context dc(em);
                try {
                    auto f = vfs->open(em);
                    colormaps.push_back(std::make_unique<colormap>(f.get()));
                }
                catch(std::exception const &e) {
                    LOG_ERROR("Failed to load colormap: ", e.what());
                    return EXIT_FAILURE;
                }
            }

            // Create output directory
            fs::create_directories(output_path);

            // Process script
            memory_block script_mb;

            try {
                auto script_fs = make_file_input_block(script_path);
                memory_output_block mob(&script_mb);
                script_fs->copy_to(&mob);
            }
            catch(std::exception const &e) {
                LOG_ERROR("Failed to load script file: ", e.what());
                return EXIT_FAILURE;
            }

            auto script_doc = json::json::parse(script_mb.str());

            // Assemble pack
            out_material_pack pack;

            if(!script_doc.contains("name")) {
                LOG_ERROR("Script does not contain a name field");
                return EXIT_FAILURE;
            }

            pack.name = script_doc["name"];

            if(!script_doc.contains("description")) {
                LOG_ERROR("Script does not contain a description field");
                return EXIT_FAILURE;
            }

            pack.description = script_doc["description"];

            if(script_doc.contains("comment")) {
                pack.comment = script_doc["comment"];
            }

            if(!script_doc.contains("materials")) {
                LOG_ERROR("Script does not contain a materials field");
                return EXIT_FAILURE;
            }

            processed_names_map img_map;

            if(script_doc.contains("environment_map")) {
                LOG_INFO("Processing default environment map...");
                try {
                    pack.environment_map = get_convert_image(
                        &img_map,
                        /*input path*/ script_path.parent_path() /
                            static_cast<std::string>(script_doc["environment_map"]),
                        /*desired name*/ "environment.png",
                        /*desired out path*/ output_path / "environment.png");
                }
                catch(std::exception const &e) {
                    LOG_ERROR("Failed to process the environment map: ", e.what());
                    return EXIT_FAILURE;
                }
            }

            // Pack-local image registry for runtime COG material overrides.
            // Images use the standard color pipeline (sRGB + premultiplied
            // alpha); they need not appear in any compiled material.
            if(script_doc.contains("images")) {
                if(!script_doc["images"].is_array()) {
                    LOG_ERROR("Pack images must be an array of {id, file} entries");
                    return EXIT_FAILURE;
                }
                for(auto const &img : script_doc["images"]) {
                    if(!img.is_object() || !img.contains("id") ||
                       !img["id"].is_number_integer() ||
                       !img.contains("file") || !img["file"].is_string()) {
                        LOG_ERROR("Invalid pack image; expected {id: integer, file: filename}");
                        return EXIT_FAILURE;
                    }
                    int id = img["id"].get<int>();
                    if(id < 0 || id > 65535 || pack.images.count(id)) {
                        LOG_ERROR("Duplicate or out-of-range pack image ID ", id);
                        return EXIT_FAILURE;
                    }
                    auto const filename = img["file"].get<std::string>();
                    try {
                        std::string const outname = str(fmt("image_", id, ".png"));
                        auto converted = get_convert_image(
                            &img_map, script_path.parent_path() / filename,
                            outname, output_path / outname);
                        pack.images.emplace(id, std::move(converted));
                    }
                    catch(std::exception const &e) {
                        LOG_ERROR("Cannot compile pack image ", id, ": ", e.what());
                        return EXIT_FAILURE;
                    }
                }
            }

            LOG_INFO("Compiling materials...");
            for(auto const &em : script_doc["materials"]) {
                try {
                    // Resolve per-pack defaults *before* importing textures or validating
                    // material parameters. The generated metadata.json stores resolved
                    // materials, so the runtime doesn't need a separate inheritance layer.
                    json::json effective = em;
                    // Normalize the two accepted spellings before applying
                    // pack-wide defaults and material-level overrides.
                    if(effective.contains("use_original_albedo") &&
                       !effective.contains("keep_albedo")) {
                        effective["keep_albedo"] = effective["use_original_albedo"];
                    }
                    if(!effective.contains("keep_albedo") &&
                       !script_doc.contains("keep_albedo") &&
                       script_doc.contains("use_original_albedo")) {
                        effective["keep_albedo"] = script_doc["use_original_albedo"];
                    }
                    for(char const *property : {
                    "albedo_map",
                    "keep_albedo",
                    "albedo_factor",
                    "emissive_map",
                    "emissive_factor",
                    "albedo_scroll",
                    "emissive_scroll",
                    "emissive_pulse",
                    "normal_map",
                    "normal_map2",
                    "normal_strength",
                    "normal_strength2",
                    "normal_scroll",
                    "normal_scroll2",
                    "displacement_map",
                    "displacement_factor",
                    "surface_map",
                    "reflection_strength",
                    "roughness",
                    "metalness",
                    "reflection_mode",
                    "texture_filtering",
                    "alpha_mode",
                    "blend_mode",
                    "fluid_thickness",
                    "depth_clip",
                    "alpha_cutoff",
                    }) {
                        if(!effective.contains(property) && script_doc.contains(property)) {
                            effective[property] = script_doc[property];
                        }
                    }

                    // A material-specific albedo map replaces pack-wide keep_albedo.
                    // Likewise, keep_albedo=true replaces a pack-wide albedo map.
                    // If *both* are specified on one material, keep the existing
                    // compiler error rather than silently choosing between them.
                    bool const explicit_keep = em.contains("keep_albedo") ||
                                               em.contains("use_original_albedo");
                    if(em.contains("albedo_map") && !explicit_keep) {
                        effective["keep_albedo"] = false;
                    }
                    if(explicit_keep && effective["keep_albedo"] == true &&
                       !em.contains("albedo_map")) {
                        // A pack-level albedo map is inherited only if the material
                        // doesn't explicitly elect to retain its original JK texture.
                        if(script_doc.contains("albedo_map")) {
                            effective.erase("albedo_map");
                        }
                    }

                    // An inherited alpha cutoff only applies in mask mode. A material
                    // that overrides a pack's mode to 'blend' shouldn't be rejected.
                    if(!em.contains("alpha_cutoff") && effective.contains("alpha_cutoff") &&
                       (!effective.contains("alpha_mode") ||
                        effective["alpha_mode"] != "mask")) {
                        effective.erase("alpha_cutoff");
                    }

                    pack.materials.push_back(process_material(
                        effective, &img_map, vfs.get(), colormaps, script_path, output_path));
                }
                catch(std::exception const &e) {
                    LOG_ERROR("Failed to process script: ", e.what());
                    return EXIT_FAILURE;
                }
            }

            LOG_INFO("Done");

            // Output final metadata file
            write_packfile(output_path, pack);

            return EXIT_SUCCESS;
        }
    };
}

MAKE_MAIN(jkgm::compile_program)
