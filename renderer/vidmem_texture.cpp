#include "vidmem_texture.hpp"
#include "base/file_block.hpp"
#include "base/log.hpp"
#include "base/md5.hpp"
#include "common/error_reporter.hpp"
#include "common/image.hpp"
#include "dxguids.hpp"
#include "math/color_conv.hpp"
#include "sysmem_texture.hpp"
#include "texture_cache.hpp"

jkgm::vidmem_texture::vidmem_texture(vidmem_texture_surface *surf)
    : Direct3DTexture_impl("vidmem")
    , surf(surf)
{
}

ULONG WINAPI jkgm::vidmem_texture::AddRef()
{
    return surf->AddRef();
}

ULONG WINAPI jkgm::vidmem_texture::Release()
{
    return surf->Release();
}

HRESULT WINAPI jkgm::vidmem_texture::GetHandle(LPDIRECT3DDEVICE a, LPD3DTEXTUREHANDLE b)
{
    *b = (D3DTEXTUREHANDLE)(surf->material_id.get());
    return D3D_OK;
}

HRESULT WINAPI jkgm::vidmem_texture::Load(LPDIRECT3DTEXTURE a)
{
    // Copy the input texture to the OpenGL surface
    auto *cast_tex = dynamic_cast<sysmem_texture *>(a);
    if(!cast_tex) {
        report_unimplemented_function("Direct3DTexture(vidmem)::Load with a non-sysmem texture");
    }

    // Texture replacement is synchronous. Invalidate the native MAT/GPU
    // readiness caches AFTER the new surface is populated, so a ready
    // decision made following Load() can never see an incomplete upload.
    ++surf->texture_generation;
    surf->clear();

    auto *src = cast_tex->surf;

    // Compute the replacement signature; no per-upload diagnostic needed.
    uint32_t bound_width = src->desc.dwWidth;
    uint32_t bound_height = src->desc.dwHeight;

    md5_hasher mh;
    mh.add(make_span(&bound_width, 1).as_const_bytes());
    mh.add(make_span(&bound_height, 1).as_const_bytes());
    mh.add(make_span(src->buffer).as_const_bytes());

    auto sig = mh.finish();

    auto repl_map = surf->r->texture_cache->get_replacement_material(sig);

    if(repl_map.has_value()) {
        surf->replacement_material = *repl_map;
        if((*repl_map)->albedo_map.has_value()) {
            surf->albedo_map =
                surf->r->texture_cache->get_srgb_texture_from_filename(*(*repl_map)->albedo_map);
        }

        if((*repl_map)->emissive_map.has_value()) {
            surf->emissive_map =
                surf->r->texture_cache->get_srgb_texture_from_filename(*(*repl_map)->emissive_map);
        }

        // Opt-in default direct specular can use a normal-only replacement,
        // but must NEVER cause an inherited pack EnvMap or unused surface map
        // to be loaded merely because the default dielectric is enabled.
        bool const authored_reflections =
            surf->r->is_reflections_enabled() &&
            (*repl_map)->reflection_strength>0.0f;
        bool const default_normals =
            surf->r->is_default_specular_enabled() &&
            !(*repl_map)->has_explicit_specular_settings;
        auto const &matcap_factor=(*repl_map)->matcap_factor;
        bool const active_matcap=(*repl_map)->matcap_map.has_value() &&
            (get<0>(matcap_factor)>0.0f ||
             get<1>(matcap_factor)>0.0f ||
             get<2>(matcap_factor)>0.0f);
        if(active_matcap) {
            surf->matcap_map=surf->r->texture_cache->
                get_srgb_texture_from_filename(*(*repl_map)->matcap_map);
            if((*repl_map)->matcap_surface_map)
                surf->matcap_surface_map=surf->r->texture_cache->
                    get_linear_texture_from_filename(
                        *(*repl_map)->matcap_surface_map);
        }
        bool const coat_authored=(*repl_map)->clearcoat_strength>0.0f ||
            (*repl_map)->clearcoat_surface_map.has_value() ||
            (*repl_map)->clearcoat_normal_map.has_value();
        if((*repl_map)->clearcoat_surface_map)
            surf->clearcoat_surface_map=surf->r->texture_cache->
                get_linear_texture_from_filename(
                    *(*repl_map)->clearcoat_surface_map);
        if((*repl_map)->clearcoat_normal_map)
            surf->clearcoat_normal_map=surf->r->texture_cache->
                get_linear_texture_from_filename(
                    *(*repl_map)->clearcoat_normal_map);
        if(authored_reflections || default_normals || active_matcap) {
            if((*repl_map)->normal_map.has_value()) {
                surf->normal_map = surf->r->texture_cache->get_linear_texture_from_filename(
                    *(*repl_map)->normal_map);
            }
            if((*repl_map)->normal_map2.has_value()) {
                surf->normal_map2 = surf->r->texture_cache->get_linear_texture_from_filename(
                    *(*repl_map)->normal_map2);
            }
            if(authored_reflections) {
                if((*repl_map)->surface_map.has_value()) {
                    surf->surface_map = surf->r->texture_cache->get_linear_texture_from_filename(
                        *(*repl_map)->surface_map);
                }
            }
        }
        // Coat uses the same source image as the base. Upload an authored
        // EnvMap if either layer can request it (even when base strength=0).
        if((authored_reflections || coat_authored) &&
           (*repl_map)->env_map)
            surf->env_map=surf->r->texture_cache->
                get_srgb_texture_from_filename(*(*repl_map)->env_map);

        // A disabled or zero-strength parallax effect does not need its
        // displacement map uploaded at all.
        if(surf->r->is_parallax_enabled() && (*repl_map)->displacement_factor != 0.0f &&
           (*repl_map)->displacement_map.has_value()) {
            surf->displacement_map = surf->r->texture_cache->get_linear_texture_from_filename(
                *(*repl_map)->displacement_map);
        }

        surf->matcap_factor=(*repl_map)->matcap_factor;
        surf->matcap_roughness=(*repl_map)->matcap_roughness;
        surf->matcap_lighting_influence=
            (*repl_map)->matcap_lighting_influence;
        surf->clearcoat_defaults={{
            (*repl_map)->clearcoat_strength,
            (*repl_map)->clearcoat_roughness,
            (*repl_map)->clearcoat_transmission}};
        surf->reflection_strength = (*repl_map)->reflection_strength;
        surf->roughness = (*repl_map)->roughness;
        surf->metalness = (*repl_map)->metalness;
        surf->reflection_mode = (*repl_map)->reflection_mode;
        surf->texture_filtering = (*repl_map)->texture_filtering;

        surf->albedo_factor = (*repl_map)->albedo_factor;
        surf->emissive_factor = (*repl_map)->emissive_factor;
        surf->albedo_scroll = (*repl_map)->albedo_scroll;
        surf->emissive_scroll = (*repl_map)->emissive_scroll;
        surf->emissive_pulse = (*repl_map)->emissive_pulse;
        surf->normal_strength = (*repl_map)->normal_strength;
        surf->normal_strength2 = (*repl_map)->normal_strength2;
        surf->normal_scroll = (*repl_map)->normal_scroll;
        surf->normal_scroll2 = (*repl_map)->normal_scroll2;

        if(surf->r->is_parallax_enabled()) {
            surf->displacement_factor = (*repl_map)->displacement_factor;
        }
        else {
            surf->displacement_factor = 0.0f;
        }

        surf->alpha_mode = (*repl_map)->alpha_mode;
        surf->blend_mode = (*repl_map)->blend_mode;
        surf->depth_bias = (*repl_map)->depth_bias;
        surf->fluid_thickness = (*repl_map)->fluid_thickness;
        surf->alpha_cutoff = (*repl_map)->alpha_cutoff;

        if(!(*repl_map)->use_original_albedo || (*repl_map)->albedo_map.has_value()) {
            surf->r->notify_texture_uploaded();
            return D3D_OK;
        }

        // This material keeps the game's own texture as its albedo: carry on below to convert it.
    }

    // No replacement albedo (or a material retaining the original).
    // Reuse the already computed content signature BEFORE converting pixels.
    // Texture IDs are shared, not D3D material handles or authored state.
    bool const rgba5551=src->desc.ddpfPixelFormat.dwRGBAlphaBitMask!=0;
    bool const normal_blend=surf->blend_mode==material_blend_mode::normal;
    if(rgba5551)surf->alpha_mode=material_alpha_mode::blend;
    surf->albedo_map=surf->r->texture_cache->find_native_albedo_texture(
        sig,rgba5551,normal_blend);
    if(!surf->albedo_map) {
        uint16_t const *in_em=(uint16_t const *)src->buffer.data();
        // The decode policy is uniform for this entire texture.
        if(rgba5551) {
            for(auto &out_em:src->conv_buffer)
                out_em=rgba5551_to_srgb_a8(*in_em++);
        }
        else {
            for(auto &out_em:src->conv_buffer)
                out_em=rgb565_to_srgb_a8(*in_em++);
        }

        surf->albedo_map=surf->r->texture_cache->create_native_albedo_texture(
            make_size((int)src->desc.dwWidth,(int)src->desc.dwHeight),
            make_span(src->conv_buffer).as_const_bytes(),
            sig,rgba5551,normal_blend);
    }

    if(repl_map.has_value()) {
        // The conversion above resets the alpha mode; put the material's own settings back
        surf->alpha_mode = (*repl_map)->alpha_mode;
        surf->alpha_cutoff = (*repl_map)->alpha_cutoff;
    }

    surf->r->notify_texture_uploaded();
    return D3D_OK;
}

jkgm::vidmem_texture_surface::vidmem_texture_surface(renderer *r, material_instance_id material_id)
    : DirectDrawSurface_impl("vidmem texture")
    , d3dtexture(this)
    , r(r)
    , material_id(material_id)
{
}

void jkgm::vidmem_texture_surface::clear()
{
    replacement_material = nullptr;
    if(albedo_map.has_value()) {
        r->texture_cache->release_srgb_texture(*albedo_map);
        albedo_map.reset();
    }

    if(emissive_map.has_value()) {
        r->texture_cache->release_srgb_texture(*emissive_map);
        emissive_map.reset();
    }
    // Invalidate when JK uploads another cel or reuses this D3D handle.
    for(auto const &entry:palette_fullbright_masks)
        if(entry.second)
            r->texture_cache->release_srgb_texture(*entry.second);
    palette_fullbright_masks.clear();

    if(normal_map.has_value()) {
        r->texture_cache->release_linear_texture(*normal_map);
        normal_map.reset();
    }
    if(normal_map2.has_value()) {
        r->texture_cache->release_linear_texture(*normal_map2);
        normal_map2.reset();
    }

    if(displacement_map.has_value()) {
        r->texture_cache->release_linear_texture(*displacement_map);
        displacement_map.reset();
    }

    if(surface_map.has_value()) {
        r->texture_cache->release_linear_texture(*surface_map);
        surface_map.reset();
    }
    if(matcap_map.has_value()) {
        r->texture_cache->release_srgb_texture(*matcap_map);
        matcap_map.reset();
    }
    if(matcap_surface_map.has_value()) {
        r->texture_cache->release_linear_texture(*matcap_surface_map);
        matcap_surface_map.reset();
    }
    matcap_factor=color_rgb::fill(1.0f);
    matcap_roughness=0.0f;
    matcap_lighting_influence=1.0f;
    if(clearcoat_surface_map) {
        r->texture_cache->release_linear_texture(*clearcoat_surface_map);
        clearcoat_surface_map.reset();
    }
    if(clearcoat_normal_map) {
        r->texture_cache->release_linear_texture(*clearcoat_normal_map);
        clearcoat_normal_map.reset();
    }
    clearcoat_defaults={{0.0f,0.1f,1.0f}};

    if(env_map.has_value()) {
        r->texture_cache->release_srgb_texture(*env_map);
        env_map.reset();
    }

    reflection_strength = 0.0f;
    roughness = 0.0f;
    metalness = 0.0f;
    reflection_mode = material_reflection_mode::envmap;
    texture_filtering.reset();

    albedo_factor = color::fill(1.0f);
    emissive_factor = color_rgb::fill(0.0f);
    albedo_scroll = point<2, float>::zero();
    emissive_scroll = point<2, float>::zero();
    emissive_pulse.reset();
    normal_strength = 1.0f;
    normal_strength2 = 1.0f;
    normal_scroll = point<2, float>::zero();
    normal_scroll2 = point<2, float>::zero();
    displacement_factor = 0.0f;
    alpha_mode = material_alpha_mode::blend;
    blend_mode = material_blend_mode::normal;
    depth_bias = 0.0f;
    fluid_thickness = 0.25f;
    alpha_cutoff = 0.5f;
}

void jkgm::vidmem_texture_surface::set_surface_desc(DDSURFACEDESC const &desc)
{
    // Basic sanity checking
    if(desc.lpSurface != nullptr) {
        report_unimplemented_function("DirectDraw::CreateSurface(vidmem texture) with contents");
    }

    this->desc = desc;
}

HRESULT WINAPI jkgm::vidmem_texture_surface::QueryInterface(REFIID riid, LPVOID *ppvObj)
{
    if(riid == IID_IDirect3DTexture) {
        d3dtexture.AddRef();
        *ppvObj = &d3dtexture;
        return S_OK;
    }

    return DirectDrawSurface_impl::QueryInterface(riid, ppvObj);
}

ULONG WINAPI jkgm::vidmem_texture_surface::AddRef()
{
    return ++refct;
}

ULONG WINAPI jkgm::vidmem_texture_surface::Release()
{
    return --refct;
}

HRESULT WINAPI jkgm::vidmem_texture_surface::GetSurfaceDesc(LPDDSURFACEDESC a)
{
    *a = desc;
    return DD_OK;
}