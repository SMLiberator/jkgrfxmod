#pragma once

#include "common/material.hpp"
#include "d3dtexture_impl.hpp"
#include "ddrawsurface_impl.hpp"
#include "renderer_fwd.hpp"
#include <d3d.h>
#include <ddraw.h>
#include <vector>

namespace jkgm {
    class vidmem_texture_surface;

    class vidmem_texture : public Direct3DTexture_impl {
    private:
        vidmem_texture_surface *surf;

    public:
        explicit vidmem_texture(vidmem_texture_surface *surf);

        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;

        HRESULT WINAPI GetHandle(LPDIRECT3DDEVICE a, LPD3DTEXTUREHANDLE b) override;
        HRESULT WINAPI Load(LPDIRECT3DTEXTURE a) override;
    };

    class vidmem_texture_surface : public DirectDrawSurface_impl {
    private:
        vidmem_texture d3dtexture;

    public:
        renderer *r;

        int refct = 0;
        DDSURFACEDESC desc;

        material_instance_id material_id;

        // Non-owning link to the compiled replacement definition and its pack.
        // Kept per Direct3D texture instance, NOT per shared signature.
        material const *replacement_material = nullptr;

        // Vidmem textures embody a material instance:
        std::optional<srgb_texture_id> albedo_map;
        color albedo_factor = color::fill(1.0f);

        std::optional<srgb_texture_id> emissive_map;
        color_rgb emissive_factor = color_rgb::fill(0.0f);

        point<2, float> albedo_scroll = point<2, float>::zero();
        point<2, float> emissive_scroll = point<2, float>::zero();
        std::optional<material_emissive_pulse> emissive_pulse;

        std::optional<linear_texture_id> normal_map;
        std::optional<linear_texture_id> normal_map2;
        float normal_strength = 1.0f;
        float normal_strength2 = 1.0f;
        point<2, float> normal_scroll = point<2, float>::zero();
        point<2, float> normal_scroll2 = point<2, float>::zero();

        std::optional<linear_texture_id> displacement_map;
        float displacement_factor = 0.0f;

        std::optional<linear_texture_id> surface_map;
        float reflection_strength = 0.0f;
        float roughness = 0.5f;
        float metalness = 0.0f;
        std::optional<srgb_texture_id> env_map;
        material_reflection_mode reflection_mode = material_reflection_mode::envmap;
        std::optional<bool> texture_filtering;

        material_alpha_mode alpha_mode = material_alpha_mode::blend;
        material_blend_mode blend_mode = material_blend_mode::normal;
        float fluid_thickness = 0.25f;
        bool depth_clip = false;
        float alpha_cutoff = 0.5f;

        vidmem_texture_surface(renderer *r, material_instance_id material_id);

        void clear();
        void set_surface_desc(DDSURFACEDESC const &desc);

        HRESULT WINAPI QueryInterface(REFIID riid, LPVOID *ppvObj) override;
        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;

        HRESULT WINAPI GetSurfaceDesc(LPDDSURFACEDESC a) override;
    };
}