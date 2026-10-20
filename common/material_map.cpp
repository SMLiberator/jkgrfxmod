#include "material_map.hpp"
#include "base/file_block.hpp"
#include "base/log.hpp"
#include "base/memory_block.hpp"
#include "base/string_search.hpp"
#include "error_reporter.hpp"
#include "json_incl.hpp"
#include <cmath>

void jkgm::material_map::add_metadata(fs::path const &metadata_file)
{
    fs::path data_dir = metadata_file.parent_path();

    auto fs = make_file_input_block(metadata_file);
    memory_block mb;
    memory_output_block mob(&mb);
    fs->copy_to(&mob);

    auto doc = json::json::parse(mb.str());

    LOG_DEBUG("Mapping materials from ",
              static_cast<std::string>(doc["name"]),
              " (",
              metadata_file.generic_string(),
              ")");

    // Older compiled packs have no images property; runtime IDs are optional.
    // Validate paths relative to this pack, never allow an ID to address files
    // outside of its originating pack.
    if(doc.contains("images")) {
        if(!doc["images"].is_array()) {
            throw std::runtime_error("Compiled pack images must be an array");
        }
        auto &registered = pack_images[data_dir];
        for(auto const &img : doc["images"]) {
            if(!img.is_object() || !img.contains("id") ||
               !img["id"].is_number_integer() || !img.contains("file") ||
               !img["file"].is_string()) {
                throw std::runtime_error("Invalid compiled pack image entry");
            }
            int const id = img["id"].get<int>();
            fs::path const rel(img["file"].get<std::string>());
            if(id < 0 || id > 65535 || rel.empty() || rel.is_absolute() ||
               rel.has_parent_path() || !registered.emplace(id, data_dir / rel).second) {
                throw std::runtime_error("Invalid or duplicate compiled pack image ID/path");
            }
        }
        LOG_DEBUG("Registered ", registered.size(), " pack-local images from ",
                  data_dir.generic_string());
    }

    // The pack's default environment image applies to this pack's materials only
    std::optional<fs::path> pack_env_map;
    if(doc.contains("environment_map")) {
        pack_env_map = data_dir / std::string(doc["environment_map"]);
    }

    for(auto const &em : doc["materials"]) {
        auto mat = std::make_unique<material>();
        mat->pack_root = data_dir;
        mat->source_name = em.value("name", std::string());

        if(em.contains("albedo_map")) {
            mat->albedo_map = data_dir / std::string(em["albedo_map"]);
        }

        if(em.contains("keep_albedo") && em.contains("use_original_albedo")) {
            throw std::runtime_error("Material cannot specify both keep_albedo and "
                                     "use_original_albedo in metadata.json");
        }
        char const *keep_key = em.contains("keep_albedo")
            ? "keep_albedo" : "use_original_albedo";
        if(em.contains(keep_key)) {
            if(!em[keep_key].is_boolean()) {
                throw std::runtime_error("Original albedo flag must be boolean");
            }
            mat->use_original_albedo = static_cast<bool>(em[keep_key]);
        }

        if(em.contains("albedo_factor")) {
            auto const &af = em["albedo_factor"];
            mat->albedo_factor = color(static_cast<float>(af[0]),
                                       static_cast<float>(af[1]),
                                       static_cast<float>(af[2]),
                                       static_cast<float>(af[3]));
        }

        if(em.contains("emissive_map")) {
            mat->emissive_map = data_dir / std::string(em["emissive_map"]);
        }

        if(em.contains("emissive_factor")) {
            auto const &af = em["emissive_factor"];
            mat->emissive_factor = color_rgb(
                static_cast<float>(af[0]), static_cast<float>(af[1]), static_cast<float>(af[2]));
        }

        if(em.contains("albedo_scroll")) {
            auto const &s = em["albedo_scroll"];
            mat->albedo_scroll = make_point(static_cast<float>(s[0]),
                                            static_cast<float>(s[1]));
        }
        if(em.contains("emissive_scroll")) {
            auto const &s = em["emissive_scroll"];
            mat->emissive_scroll = make_point(static_cast<float>(s[0]),
                                              static_cast<float>(s[1]));
        }
        if(em.contains("emissive_pulse")) {
            auto const &p = em["emissive_pulse"];
            material_emissive_pulse pulse;
            auto const wave = static_cast<std::string>(p.at("wave"));
            if(wave == "square") {
                pulse.wave = material_pulse_wave::square;
            }
            else if(wave == "flicker") {
                pulse.wave = material_pulse_wave::flicker;
            }
            else if(wave == "random") {
                pulse.wave = material_pulse_wave::random;
            }
            else if(wave != "sine") {
                throw std::runtime_error("Unknown emissive_pulse wave in metadata.json");
            }
            pulse.rate = static_cast<float>(p.at("rate"));
            pulse.min = p.contains("min") ? static_cast<float>(p["min"]) : 0.0f;
            pulse.max = p.contains("max") ? static_cast<float>(p["max"]) : 1.0f;
            pulse.phase = p.contains("phase") ? static_cast<float>(p["phase"]) : 0.0f;
            mat->emissive_pulse = pulse;
        }

        if(em.contains("normal_map")) {
            mat->normal_map = data_dir / std::string(em["normal_map"]);
        }
        if(em.contains("normal_map2")) {
            mat->normal_map2 = data_dir / std::string(em["normal_map2"]);
        }
        if(em.contains("normal_strength")) {
            mat->normal_strength = static_cast<float>(em["normal_strength"]);
        }
        if(em.contains("normal_strength2")) {
            mat->normal_strength2 = static_cast<float>(em["normal_strength2"]);
        }
        if(em.contains("normal_scroll")) {
            auto const &s = em["normal_scroll"];
            mat->normal_scroll = make_point(static_cast<float>(s[0]),
                                             static_cast<float>(s[1]));
        }
        if(em.contains("normal_scroll2")) {
            auto const &s = em["normal_scroll2"];
            mat->normal_scroll2 = make_point(static_cast<float>(s[0]),
                                              static_cast<float>(s[1]));
        }

        if(em.contains("displacement_map")) {
            mat->displacement_map = data_dir / std::string(em["displacement_map"]);
        }

        if(em.contains("displacement_factor")) {
            mat->displacement_factor = em["displacement_factor"];
        }

        // Experimental neutral dielectric defaults on ordinary materials.
        // Authored surface maps still default to unity multipliers, since
        // their R/G/B channels explicitly define the per-pixel properties.
        bool const has_surface_map = em.contains("surface_map");
        if(has_surface_map) {
            mat->surface_map = data_dir / std::string(em["surface_map"]);
        }

        mat->reflection_strength =
            em.contains("reflection_strength") ? static_cast<float>(em["reflection_strength"])
                                                : (has_surface_map ? 1.0f : 0.1f);
        mat->roughness = em.contains("roughness") ? static_cast<float>(em["roughness"])
                                                   : (has_surface_map ? 1.0f : 0.9f);
        mat->metalness = em.contains("metalness") ? static_cast<float>(em["metalness"])
                                                   : (has_surface_map ? 1.0f : 0.0f);

        if(em.contains("environment_map")) {
            mat->has_explicit_env_map = true;
            mat->env_map = data_dir / std::string(em["environment_map"]);
        }
        else if(pack_env_map.has_value()) {
            mat->env_map = pack_env_map;
        }

        if(em.contains("reflection_mode")) {
            auto const &v = em["reflection_mode"];
            if(v == "screen") {
                mat->reflection_mode = material_reflection_mode::screen;
            }
            else if(v == "fixed") {
                mat->reflection_mode = material_reflection_mode::fixed;
            }
            else if(v == "matcap") {
                mat->reflection_mode = material_reflection_mode::matcap;
            }
            else if(v != "envmap") {
                throw std::runtime_error("Unknown reflection_mode in metadata.json");
            }
        }

        if(em.contains("texture_filtering")) {
            mat->texture_filtering = static_cast<bool>(em["texture_filtering"]);
        }

        if(em.contains("alpha_mode")) {
            auto const &am = em["alpha_mode"];
            if(am == "mask") {
                mat->alpha_mode = material_alpha_mode::mask;
            }
            else if(am == "blend") {
                mat->alpha_mode = material_alpha_mode::blend;
            }
            else if(am == "dithered") {
                mat->alpha_mode = material_alpha_mode::dithered;
            }
            else {
                throw std::runtime_error("Unknown alpha_mode in metadata.json");
            }
        }

        if(em.contains("blend_mode")) {
            auto const &mode = em["blend_mode"];
            if(mode == "normal") {
                mat->blend_mode = material_blend_mode::normal;
            }
            else if(mode == "additive") {
                mat->blend_mode = material_blend_mode::additive;
            }
            else if(mode == "multiply") {
                mat->blend_mode = material_blend_mode::multiply;
            }
            else if(mode == "fluid") {
                mat->blend_mode = material_blend_mode::fluid;
            }
            else {
                throw std::runtime_error("Unknown blend_mode in metadata.json");
            }
        }

        if(em.contains("fluid_thickness")) {
            float const value = static_cast<float>(em["fluid_thickness"]);
            if(!std::isfinite(value) || value < 0.0f) {
                throw std::runtime_error("Invalid fluid_thickness in metadata.json");
            }
            mat->fluid_thickness = value;
        }

        if(em.contains("depth_clip")) {
            if(!em["depth_clip"].is_boolean()) {
                throw std::runtime_error("depth_clip in metadata.json must be a boolean");
            }
            mat->depth_clip = static_cast<bool>(em["depth_clip"]);
        }

        if(em.contains("alpha_cutoff")) {
            mat->alpha_cutoff = em["alpha_cutoff"];
        }

        auto mat_index = materials.size();
        materials.push_back(std::move(mat));

        for(auto const &sig : em["replaces_signatures"]) {
            signature_map.emplace(md5(static_cast<std::string>(sig)), mat_index);
        }
    }
}

void jkgm::material_map::create_map(fs::path const &materials_dir)
{
    LOG_DEBUG("Loading material map...");
    // Search for metadata.json files in subdirectories
    for(fs::directory_iterator dir_iter(materials_dir); dir_iter != fs::directory_iterator();
        ++dir_iter) {
        if(fs::is_directory(dir_iter->status())) {
            auto md_path = dir_iter->path() / "metadata.json";
            try {
                add_metadata(md_path);
            }
            catch(std::exception const &e) {
                LOG_WARNING(
                    "Failed to load materials from ", md_path.generic_string(), ": ", e.what());
                report_warning_message(
                    str(fmt("JkGfxMod could not load the material pack \"",
                            dir_iter->path().filename().generic_string(),
                            "\". The directory either does not contain a material pack, or the "
                            "material pack is corrupt.\n\nDetails: ",
                            e.what())));
            }
        }
    }

    LOG_DEBUG("Material map loaded");
}

std::optional<jkgm::material const *> jkgm::material_map::get_material(md5 const &sig) const
{
    auto it = signature_map.find(sig);
    if(it != signature_map.end()) {
        return materials.at(it->second).get();
    }

    return std::nullopt;
}

std::vector<std::unique_ptr<jkgm::material>> const &jkgm::material_map::get_materials() const
{
    return materials;
}
std::optional<fs::path> jkgm::material_map::get_pack_image(
    fs::path const &pack, int image_id) const
{
    auto const p = pack_images.find(pack);
    if(p == pack_images.end()) {
        return std::nullopt;
    }
    auto const i = p->second.find(image_id);
    return i != p->second.end() ? std::optional<fs::path>(i->second) : std::nullopt;
}
