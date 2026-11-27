#include "texture_cache.hpp"
#include "base/file_block.hpp"
#include "base/file_stream.hpp"
#include "common/config.hpp"
#include "common/image.hpp"
#include "common/material_map.hpp"
#include "texture_alpha.hpp"
#include "base/log.hpp"
#include "glad/gl.h"
#include <array>
#include <chrono>
#include <set>
#include <algorithm>
#include <numeric>
#include <random>

namespace jkgm {
    namespace {
        struct srgb_texture {
            size<2, int> dims;
            gl::texture handle;
            int refct = 0;
            bool nonopaque_alpha = false;
            gl::texture_internal_format storage=gl::texture_internal_format::srgb_a8;
            bool native_albedo = false;
            uint64_t compressed_bytes = 0;
            std::optional<fs::path> origin_filename;

            explicit srgb_texture(size<2, int> dims)
                : dims(dims)
            {
            }
        };

        struct linear_texture {
            size<2, int> dims;
            gl::texture handle;
            int refct = 0;
            std::optional<fs::path> origin_filename;

            explicit linear_texture(size<2, int> dims)
                : dims(dims)
            {
            }
        };

        uint64_t dims_to_bytes(size<2, int> dims)
        {
            return static_cast<uint64_t>(volume(dims) * 4);
        }

        // Nominal complete RGBA8 mip chain (not driver allocation overhead).
        uint64_t rgba_mip_bytes(size<2,int> dims)
        {
            int w=get<x>(dims),h=get<y>(dims);
            uint64_t bytes=0;
            while(w>0 && h>0) {
                bytes+=uint64_t(w)*uint64_t(h)*4U;
                if(w==1 && h==1)break;
                w=std::max(1,w/2);
                h=std::max(1,h/2);
            }
            return bytes;
        }

        // Request a specific sRGB block format, not the driver's generic
        // "compressed RGB" preference. Validate EVERY mip before accepting
        // the texture; an unsupported encoder/mipmap path must fall back.
        // Called only on texture upload, never on the draw path.
        std::optional<uint64_t> upload_compressed_albedo(size<2,int> dims,span<char const> data,
                                                        gl::texture_internal_format storage)
        {
            gl::log_errors(); // attribute subsequent errors to this upload
            gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
                storage,dims,
                gl::texture_pixel_format::rgba,gl::texture_pixel_type::uint8,data);
            if(glGetError()!=GL_NO_ERROR) {
                gl::log_errors();
                return std::nullopt;
            }
            gl::generate_mipmap(gl::texture_bind_target::texture_2d);
            if(glGetError()!=GL_NO_ERROR) {
                gl::log_errors();
                return std::nullopt;
            }
            int w=get<x>(dims),h=get<y>(dims);
            uint64_t bytes=0;
            for(int level=0;;++level) {
                GLint compressed=0,format=0,width=0,height=0,level_bytes=0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED,&compressed);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_INTERNAL_FORMAT,&format);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&width);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&height);
                if(glGetError()!=GL_NO_ERROR || !compressed ||
                   format!=static_cast<GLint>(storage) ||
                   width!=w || height!=h) {
                    gl::log_errors();
                    return std::nullopt;
                }
                glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                                         &level_bytes);
                uint64_t const block_bytes=storage==gl::texture_internal_format::srgb_bc1?8U:16U;
                uint64_t const expected=((uint64_t(w)+3U)/4U)*((uint64_t(h)+3U)/4U)*block_bytes;
                if(glGetError()!=GL_NO_ERROR || level_bytes<=0 ||
                   uint64_t(level_bytes)!=expected) {
                    gl::log_errors();
                    return std::nullopt;
                }
                bytes+=uint64_t(level_bytes);
                if(w==1 && h==1)break;
                w=std::max(1,w/2);
                h=std::max(1,h/2);
            }
            return bytes;
        }

        class texture_cache_impl : public texture_cache {
        public:
            config const *the_config;
            material_map materials;

            std::vector<srgb_texture> srgb_textures;
            std::map<fs::path, size_t> file_to_srgb_texture_map;

            std::vector<linear_texture> linear_textures;
            std::map<fs::path, size_t> file_to_linear_texture_map;

            uint64_t current_capacity = 0U;
            bool bc1_supported=false,bc3_supported=false;
            std::set<fs::path> compression_albedo_paths;
            uint64_t bc1_uploads=0,bc1_fallbacks=0,bc1_upload_us=0;
            uint64_t native_bc1_uploads=0,native_bc1_fallbacks=0,native_bc1_upload_us=0;
            uint64_t bc3_uploads=0,bc3_fallbacks=0,bc3_upload_us=0;
            uint64_t native_bc3_uploads=0,native_bc3_fallbacks=0,native_bc3_upload_us=0;

            explicit texture_cache_impl(config const *the_config)
                : the_config(the_config)
            {
                materials.create_map(fs::path(the_config->data_path) / "materials");

                uint64_t preload_capacity =
                    the_config->vram_texture_preload_size * uint64_t(1048576U);

                auto const &raw_materials = materials.get_materials();

                if(the_config->experimental_texture_compression_bc1 ||
                   the_config->experimental_texture_compression_bc3) {
                    // A filename can serve several roles. Keep emission,
                    // environment, matcap and special-blend sources intact,
                    // even if another material uses that same file as albedo.
                    std::set<fs::path> excluded;
                    for(auto const &mat:raw_materials) {
                        if(mat->albedo_map) {
                            if(mat->blend_mode==material_blend_mode::normal)
                                compression_albedo_paths.insert(*mat->albedo_map);
                            else
                                excluded.insert(*mat->albedo_map);
                        }
                        if(mat->emissive_map)excluded.insert(*mat->emissive_map);
                        if(mat->env_map)excluded.insert(*mat->env_map);
                        if(mat->matcap_map)excluded.insert(*mat->matcap_map);
                    }
                    for(auto const &path:excluded)compression_albedo_paths.erase(path);
                }
                if(the_config->experimental_texture_compression_bc1 ||
                   the_config->experimental_native_texture_compression_bc1) {
                    // Verify actual driver encoding AND mip generation, not
                    // just extension names or a compressed-format advertisement.
                    gl::texture probe;
                    gl::bind_texture(gl::texture_bind_target::texture_2d,probe);
                    std::array<char,64> white;
                    white.fill(static_cast<char>(255));
                    bc1_supported=upload_compressed_albedo(make_size(4,4),
                        span<char const>(white.data(),white.size()),
                        gl::texture_internal_format::srgb_bc1).has_value();
                    if(!bc1_supported)
                        LOG_WARNING("TEXTURE BC1 driver upload/mipmap probe failed; using RGBA8");
                }
                LOG_INFO("TEXTURE BC1 requested=",
                    the_config->experimental_texture_compression_bc1?1:0,
                    " native_requested=",the_config->experimental_native_texture_compression_bc1?1:0,
                    " supported=",bc1_supported?1:0,
                    " eligible_albedo_paths=",compression_albedo_paths.size(),
                    " NOTE=opaque_albedo_only;driver_encoded;sRGB");

                if(the_config->experimental_texture_compression_bc3 ||
                   the_config->experimental_native_texture_compression_bc3) {
                    gl::texture probe;
                    gl::bind_texture(gl::texture_bind_target::texture_2d,probe);
                    std::array<char,64> rgba;
                    rgba.fill(static_cast<char>(255));
                    for(size_t i=3;i<rgba.size();i+=4)
                        rgba[i]=static_cast<char>((i/4)*17U);
                    bc3_supported=upload_compressed_albedo(make_size(4,4),
                        span<char const>(rgba.data(),rgba.size()),
                        gl::texture_internal_format::srgb_bc3).has_value();
                    if(!bc3_supported)
                        LOG_WARNING("TEXTURE BC3 driver upload/mipmap probe failed; using RGBA8");
                }
                LOG_INFO("TEXTURE BC3 requested=",
                    the_config->experimental_texture_compression_bc3?1:0,
                    " native_requested=",the_config->experimental_native_texture_compression_bc3?1:0,
                    " supported=",bc3_supported?1:0,
                    " eligible_albedo_paths=",compression_albedo_paths.size(),
                    " NOTE=alpha_bearing_albedo_only;driver_encoded;sRGB");

                std::vector<size_t> shuffled_materials;
                shuffled_materials.resize(raw_materials.size());
                std::iota(shuffled_materials.begin(), shuffled_materials.end(), size_t(0U));

                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled_materials.begin(), shuffled_materials.end(), g);

                for(size_t mat_index : shuffled_materials) {
                    if(current_capacity >= preload_capacity) {
                        break;
                    }

                    auto const &mat = raw_materials.at(mat_index);

                    if(mat->albedo_map.has_value()) {
                        get_srgb_texture_from_filename(*mat->albedo_map);
                    }

                    if(mat->emissive_map.has_value()) {
                        get_srgb_texture_from_filename(*mat->emissive_map);
                    }

                    if(the_config->enable_reflections && mat->reflection_strength > 0.0f) {
                        if(mat->normal_map.has_value()) {
                            get_linear_texture_from_filename(*mat->normal_map);
                        }
                        if(mat->normal_map2.has_value()) {
                            get_linear_texture_from_filename(*mat->normal_map2);
                        }
                        if(mat->surface_map.has_value()) {
                            get_linear_texture_from_filename(*mat->surface_map);
                        }
                        if(mat->env_map.has_value()) {
                            get_srgb_texture_from_filename(*mat->env_map);
                        }
                    }

                    if(the_config->enable_parallax && mat->displacement_factor != 0.0f &&
                       mat->displacement_map.has_value()) {
                        get_linear_texture_from_filename(*mat->displacement_map);
                    }
                }

                // Reset reference counts from preloaded textures
                for(auto &em : srgb_textures) {
                    em.refct = 0U;
                }

                for(auto &em : linear_textures) {
                    em.refct = 0U;
                }
            }

            std::optional<material const *> get_replacement_material(md5 const &sig) override
            {
                return materials.get_material(sig);
            }

            std::optional<fs::path> get_pack_image(fs::path const &pack,
                                                   int image_id) const override
            {
                return materials.get_pack_image(pack, image_id);
            }

            std::optional<srgb_texture_id> get_existing_free_srgb_texture(size<2, int> const &dims,
                                                                            gl::texture_internal_format storage)
            {
                for(size_t i = 0; i < srgb_textures.size(); ++i) {
                    auto &em = srgb_textures[i];
                    if(em.refct <= 0 && em.dims == dims && em.storage==storage) {
                        // Match both dimensions and storage class. Native
                        // RGBA updates must never refill a compressed slot.
                        if(em.origin_filename.has_value()) {
                            file_to_srgb_texture_map.erase(*em.origin_filename);
                            em.origin_filename.reset();
                        }

                        em.refct = 0;

                        return srgb_texture_id(i);
                    }
                }

                return std::nullopt;
            }

            srgb_texture_id create_srgb_texture_from_buffer(size<2, int> const &dims,
                                                            span<char const> data) override
            {
                // Generic buffers (including palette masks) stay uncompressed.
                return create_srgb_texture(dims,data,false);
            }

            srgb_texture_id create_native_albedo_texture(size<2,int> const &dims,
                                                        span<char const> data,
                                                        bool normal_blend) override
            {
                return create_srgb_texture(dims,data,normal_blend,true);
            }

            srgb_texture_id create_srgb_texture(size<2,int> const &dims,
                                               span<char const> data,bool eligible,
                                               bool native=false)
            {
                bool const alpha=texture_has_nonopaque_alpha(data);
                using format=gl::texture_internal_format;
                bool const allow_bc1=native?the_config->experimental_native_texture_compression_bc1:
                                            the_config->experimental_texture_compression_bc1;
                bool const allow_bc3=native?the_config->experimental_native_texture_compression_bc3:
                                            the_config->experimental_texture_compression_bc3;
                auto storage=format::srgb_a8;
                if(eligible && get<x>(dims)>=4 && get<y>(dims)>=4) {
                    if(!alpha && allow_bc1 && bc1_supported)storage=format::srgb_bc1;
                    if(alpha && allow_bc3 && bc3_supported)storage=format::srgb_bc3;
                }
                auto existing_buf=get_existing_free_srgb_texture(dims,storage);
                srgb_texture_id rv=existing_buf.has_value()
                    ?*existing_buf:srgb_texture_id(srgb_textures.size());
                bool allocate=!existing_buf.has_value();
                if(allocate) {
                    srgb_textures.emplace_back(dims);
                    // Keep the existing logical RGBA preload budget for A/B.
                    current_capacity+=dims_to_bytes(dims);
                }
                auto &em=at(srgb_textures,rv);
                em.nonopaque_alpha=alpha;
                em.native_albedo=native;
                em.refct=1;
                gl::bind_texture(gl::texture_bind_target::texture_2d,em.handle);

                if(storage!=format::srgb_a8) {
                    bool const bc1=storage==format::srgb_bc1;
                    auto const started=std::chrono::steady_clock::now();
                    auto bytes=upload_compressed_albedo(dims,data,storage);
                    auto const elapsed=static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now()-started).count());
                    (bc1?bc1_upload_us:bc3_upload_us)+=elapsed;
                    if(native)(bc1?native_bc1_upload_us:native_bc3_upload_us)+=elapsed;
                    if(bytes) {
                        em.storage=storage;
                        em.compressed_bytes=*bytes;
                        ++(bc1?bc1_uploads:bc3_uploads);
                        if(native)++(bc1?native_bc1_uploads:native_bc3_uploads);
                    }
                    else {
                        ++(bc1?bc1_fallbacks:bc3_fallbacks);
                        if(native)++(bc1?native_bc1_fallbacks:native_bc3_fallbacks);
                        LOG_WARNING("TEXTURE ",bc1?"BC1":"BC3"," upload failed dims=",get<x>(dims),
                                    "x",get<y>(dims),"; retrying RGBA8");
                        // A partial mip chain cannot be reused. A fresh GL
                        // object also restores default sampler state.
                        em.handle=gl::texture{};
                        gl::bind_texture(gl::texture_bind_target::texture_2d,em.handle);
                        em.storage=format::srgb_a8;
                        em.compressed_bytes=0;
                        allocate=true;
                    }
                }
                if(em.storage==format::srgb_a8) {
                    if(allocate)
                        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
                            gl::texture_internal_format::srgb_a8,dims,
                            gl::texture_pixel_format::rgba,gl::texture_pixel_type::uint8,data);
                    else
                        gl::tex_sub_image_2d(gl::texture_bind_target::texture_2d,0,
                            make_box(make_point(0,0),dims),gl::texture_pixel_format::rgba,
                            gl::texture_pixel_type::uint8,data);
                    gl::generate_mipmap(gl::texture_bind_target::texture_2d);
                }
                if(allocate) {
                    gl::set_texture_max_anisotropy(gl::texture_bind_target::texture_2d,
                        std::max(1.0f,the_config->max_anisotropy));
                    if(the_config->enable_texture_filtering) {
                        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                                   gl::mag_filter::linear);
                        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                                   gl::min_filter::linear_mipmap_linear);
                    }
                    else {
                        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                                   gl::mag_filter::nearest);
                        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                                   gl::min_filter::nearest_mipmap_linear);
                    }
                }
                return rv;
            }

            srgb_texture_id get_srgb_texture_from_filename(fs::path const &file) override
            {
                auto it = file_to_srgb_texture_map.find(file);
                if(it != file_to_srgb_texture_map.end()) {
                    // Image file already loaded
                    srgb_texture_id rv(it->second);
                    ++at(srgb_textures, rv).refct;
                    return rv;
                }

                auto fs = make_file_input_block(file);
                auto img = load_image(fs.get());

                auto rv = create_srgb_texture(img->dimensions,
                    make_span(img->data).as_const_bytes(),
                    compression_albedo_paths.count(file)!=0);

                auto &em = at(srgb_textures, rv);
                em.origin_filename = file;
                file_to_srgb_texture_map.emplace(file, rv.get());

                return rv;
            }

            void retain_srgb_texture(srgb_texture_id id) override
            {
                ++at(srgb_textures, id).refct;
            }

            void release_srgb_texture(srgb_texture_id id) override
            {
                --at(srgb_textures, id).refct;
            }

            std::optional<linear_texture_id>
                get_existing_free_linear_texture(size<2, int> const &dims)
            {
                for(size_t i = 0; i < linear_textures.size(); ++i) {
                    auto &em = linear_textures[i];
                    if(em.refct <= 0 && em.dims == dims) {
                        // This texture is a match. Clean it up before returning.
                        if(em.origin_filename.has_value()) {
                            file_to_linear_texture_map.erase(*em.origin_filename);
                        }

                        em.refct = 0;

                        return linear_texture_id(i);
                    }
                }

                return std::nullopt;
            }

            linear_texture_id create_linear_texture_from_buffer(size<2, int> const &dims,
                                                                span<char const> data)
            {
                auto existing_buf = get_existing_free_linear_texture(dims);
                if(existing_buf.has_value()) {
                    // Matching texture already exists. Refill it.
                    auto &em = at(linear_textures, *existing_buf);
                    gl::bind_texture(gl::texture_bind_target::texture_2d, em.handle);
                    gl::tex_sub_image_2d(gl::texture_bind_target::texture_2d,
                                         0,
                                         make_box(make_point(0, 0), dims),
                                         gl::texture_pixel_format::rgba,
                                         gl::texture_pixel_type::uint8,
                                         data);
                    gl::generate_mipmap(gl::texture_bind_target::texture_2d);

                    ++em.refct;
                    return *existing_buf;
                }

                // Create new texture
                linear_texture_id rv(linear_textures.size());
                linear_textures.emplace_back(dims);
                current_capacity += dims_to_bytes(dims);

                auto &em = linear_textures.back();
                em.refct = 1;

                gl::bind_texture(gl::texture_bind_target::texture_2d, em.handle);
                gl::tex_image_2d(gl::texture_bind_target::texture_2d,
                                 /*level*/ 0,
                                 gl::texture_internal_format::rgba,
                                 dims,
                                 gl::texture_pixel_format::rgba,
                                 gl::texture_pixel_type::uint8,
                                 data);
                gl::generate_mipmap(gl::texture_bind_target::texture_2d);
                gl::set_texture_max_anisotropy(gl::texture_bind_target::texture_2d,
                                               std::max(1.0f, the_config->max_anisotropy));
                if(the_config->enable_texture_filtering) {
                    gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                               gl::mag_filter::linear);
                    gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                               gl::min_filter::linear_mipmap_linear);
                }
                else {
                    gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                               gl::mag_filter::nearest);
                    gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                               gl::min_filter::nearest_mipmap_linear);
                }

                return rv;
            }

            linear_texture_id get_linear_texture_from_filename(fs::path const &file) override
            {
                auto it = file_to_linear_texture_map.find(file);
                if(it != file_to_linear_texture_map.end()) {
                    // Image file already loaded
                    linear_texture_id rv(it->second);
                    ++at(linear_textures, rv).refct;
                    return rv;
                }

                auto fs = make_file_input_block(file);
                auto img = load_image(fs.get());

                auto rv = create_linear_texture_from_buffer(img->dimensions,
                                                            make_span(img->data).as_const_bytes());

                auto &em = at(linear_textures, rv);
                em.origin_filename = file;
                file_to_linear_texture_map.emplace(file, rv.get());

                return rv;
            }

            void release_linear_texture(linear_texture_id id) override
            {
                --at(linear_textures, id).refct;
            }

            void log_texture_profile() const override
            {
                uint64_t srgb_bytes=0,linear_bytes=0,bc1_bytes=0,bc1_reference_bytes=0;
                uint64_t bc1_count=0,bc1_referenced=0,srgb_referenced=0,linear_referenced=0;
                uint64_t native_count=0,native_referenced=0,native_bc1_count=0,native_bc1_referenced=0;
                uint64_t native_bc1_bytes=0,native_bc1_reference_bytes=0,native_bytes=0;
                uint64_t bc3_count=0,bc3_referenced=0,bc3_bytes=0,bc3_reference_bytes=0;
                uint64_t native_bc3_count=0,native_bc3_referenced=0;
                uint64_t native_bc3_bytes=0,native_bc3_reference_bytes=0;
                for(auto const &em:srgb_textures) {
                    if(em.refct>0)++srgb_referenced;
                    uint64_t const reference=rgba_mip_bytes(em.dims);
                    bool const bc1=em.storage==gl::texture_internal_format::srgb_bc1;
                    bool const bc3=em.storage==gl::texture_internal_format::srgb_bc3;
                    if(em.native_albedo) {
                        ++native_count;
                        if(em.refct>0)++native_referenced;
                        native_bytes+=(bc1||bc3)?em.compressed_bytes:reference;
                        if(bc1) {
                            ++native_bc1_count;
                            if(em.refct>0)++native_bc1_referenced;
                            native_bc1_bytes+=em.compressed_bytes;
                            native_bc1_reference_bytes+=reference;
                        }
                        if(bc3) {
                            ++native_bc3_count;
                            if(em.refct>0)++native_bc3_referenced;
                            native_bc3_bytes+=em.compressed_bytes;
                            native_bc3_reference_bytes+=reference;
                        }
                    }
                    if(bc1) {
                        ++bc1_count;
                        if(em.refct>0)++bc1_referenced;
                        bc1_bytes+=em.compressed_bytes;
                        bc1_reference_bytes+=reference;
                        srgb_bytes+=em.compressed_bytes;
                    }
                    else if(bc3) {
                        ++bc3_count;
                        if(em.refct>0)++bc3_referenced;
                        bc3_bytes+=em.compressed_bytes;
                        bc3_reference_bytes+=reference;
                        srgb_bytes+=em.compressed_bytes;
                    }
                    else srgb_bytes+=reference;
                }
                for(auto const &em:linear_textures) {
                    if(em.refct>0)++linear_referenced;
                    linear_bytes+=rgba_mip_bytes(em.dims);
                }
                LOG_INFO("PERF texture_cache srgb_allocated=",srgb_textures.size(),
                    " srgb_referenced=",srgb_referenced,
                    " linear_allocated=",linear_textures.size(),
                    " linear_referenced=",linear_referenced,
                    " bc1_allocated=",bc1_count," bc1_referenced=",bc1_referenced,
                    " srgb_bytes=",srgb_bytes," linear_rgba8_est_bytes=",linear_bytes,
                    " bc1_bytes=",bc1_bytes," bc1_rgba8_equiv_bytes=",bc1_reference_bytes,
                    " bc1_uploads_total=",bc1_uploads," bc1_fallbacks_total=",bc1_fallbacks,
                    " bc1_upload_us_total=",bc1_upload_us,
                    " native_albedo_allocated=",native_count,
                    " native_albedo_referenced=",native_referenced,
                    " native_albedo_bytes=",native_bytes,
                    " native_bc1_allocated=",native_bc1_count,
                    " native_bc1_referenced=",native_bc1_referenced,
                    " native_bc1_bytes=",native_bc1_bytes,
                    " native_bc1_rgba8_equiv_bytes=",native_bc1_reference_bytes,
                    " native_bc1_uploads_total=",native_bc1_uploads,
                    " native_bc1_fallbacks_total=",native_bc1_fallbacks,
                    " native_bc1_upload_us_total=",native_bc1_upload_us,
                    " bc3_allocated=",bc3_count," bc3_referenced=",bc3_referenced,
                    " bc3_bytes=",bc3_bytes," bc3_rgba8_equiv_bytes=",bc3_reference_bytes,
                    " bc3_uploads_total=",bc3_uploads," bc3_fallbacks_total=",bc3_fallbacks,
                    " bc3_upload_us_total=",bc3_upload_us,
                    " native_bc3_allocated=",native_bc3_count,
                    " native_bc3_referenced=",native_bc3_referenced,
                    " native_bc3_bytes=",native_bc3_bytes,
                    " native_bc3_rgba8_equiv_bytes=",native_bc3_reference_bytes,
                    " native_bc3_uploads_total=",native_bc3_uploads,
                    " native_bc3_fallbacks_total=",native_bc3_fallbacks,
                    " native_bc3_upload_us_total=",native_bc3_upload_us,
                    " NOTE=pooled_and_referenced;mip_payload_not_driver_overhead;upload_wall_time");
            }

            gl::texture_view get_texture_handle(srgb_texture_id id) const override
            {
                return at(srgb_textures, id).handle;
            }
            bool has_nonopaque_alpha(srgb_texture_id id) const override
            {
                return at(srgb_textures, id).nonopaque_alpha;
            }

            gl::texture_view get_texture_handle(linear_texture_id id) const override
            {
                return at(linear_textures, id).handle;
            }
        };
    }
}

std::unique_ptr<jkgm::texture_cache> jkgm::create_texture_cache(config const *the_config)
{
    return std::make_unique<jkgm::texture_cache_impl>(the_config);
}