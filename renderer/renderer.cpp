#include "renderer.hpp"
#include "frame_profile.hpp"
#include "presentation_gpu_profile.hpp"
#include "native_3do_profile.hpp"
#include "gpu_3do_preview.hpp"
#include "planar_reflection_things.hpp"
#include "planar_reflection_workset.hpp"
#include "planar_reflection_offscreen_gpu.hpp"
#include "planar_reflection_render_math.hpp"
#include "planar_reflection_capture_target.hpp"
#include "planar_reflection_capture_shader.hpp"
#include "saber_light_rgb.hpp"
#include "thing_light_rgb.hpp"
#include "gpu_3do_uv.hpp"
#include "native_mat_warm_queue.hpp"
#include "native_sprite_profile.hpp"
#include "audio_module_probe.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_alpha.hpp"
#include "native_jk_palette_effects.hpp"
#include "native_screenshot.hpp"
#include "native_sector_palette_bridge.hpp"
#include "thing_sector_tint.hpp"
#include "sector_crossing_gpu.hpp"
#include "retro_palette.hpp"
#include "jk_colormap_layout.hpp"
#include "jk_cmp_gpu.hpp"
#include "jk_cmp_fullbright_mask.hpp"
#include "gpu_3do_frustum.hpp"
#include "fast_vertex_colors.hpp"
#include "runtime_material.hpp"
#include "spotlight.hpp"
#include "spotlight_shadow_gpu.hpp"
#include "sun_shadow_gpu.hpp"
#include "sun_pcf_intensity.hpp"
#include "sun_sky_aperture_gpu.hpp"
#include "sun_sky_surface.hpp"
#include "sun_sky_caster_mask.hpp"
#include "world_cache_gpu.hpp"
#include "world_cache_streaming.hpp"
#include "continuous_ambient_capture.hpp"
#include "continuous_ambient_gpu.hpp"
#include "world_cache_alpha.hpp"
#include "world_cache_sky.hpp"
#include "sun_dynamic_shadow_gpu.hpp"
#include "backbuffer_menu_surface.hpp"
#include "backbuffer_surface.hpp"
#include "base/file_block.hpp"
#include "base/file_stream.hpp"
#include <atomic>
#include "base/log.hpp"
#include "base/memory_block.hpp"
#include "base/win32.hpp"
#include "common/error_reporter.hpp"
#include "common/image.hpp"
#include "d3d_impl.hpp"
#include "d3ddevice_impl.hpp"
#include "d3dviewport_impl.hpp"
#include "ddraw2_impl.hpp"
#include "ddraw_impl.hpp"
#include "ddrawpalette_impl.hpp"
#include "engine_camera.hpp"
#include "engine_material_debug.hpp"
#include "execute_buffer.hpp"
#include "fog.hpp"
#include "glad/glad.h"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/gl.hpp"
#include "glutil/program.hpp"
#include "glutil/shader.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include "math/color_conv.hpp"
#include "math/colors.hpp"
#include "offscreen_menu_surface.hpp"
#include "offscreen_surface.hpp"
#include "opengl_state.hpp"
#include "postfx.hpp"
#include "spatialblur.hpp"
#include "primary_menu_surface.hpp"
#include "primary_surface.hpp"
#include "renderer_ao.hpp"
#include "renderer_compositor.hpp"
#include "renderer_screen.hpp"
#include "sysmem_texture.hpp"
#include "texture_cache.hpp"
#include "triangle_batch.hpp"
#include "vidmem_texture.hpp"
#include "zbuffer_surface.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace jkgm {
    static WNDPROC original_wkernel_wndproc = nullptr;
    static size<2, int> original_configured_screen_res = make_size(0, 0);
    static box<2, int> actual_menu_area = make_box(make_point(0, 0), make_size(0, 0));

    using wglCreateContextAttribsARB_type = HGLRC(WINAPI *)(HDC hDC,
                                                            HGLRC hShareContext,
                                                            int const *attribList);
    static wglCreateContextAttribsARB_type wglCreateContextAttribsARB = nullptr;

    using wglChoosePixelFormatARB_type = BOOL(WINAPI *)(HDC hDC,
                                                        const int *piAttribIList,
                                                        const FLOAT *pfAttribFList,
                                                        UINT nMaxFormats,
                                                        int *piFormats,
                                                        UINT *nNumFormats);
    static wglChoosePixelFormatARB_type wglChoosePixelFormatARB = nullptr;

    using wglSwapIntervalEXT_type = BOOL(WINAPI *)(int interval);
    static wglSwapIntervalEXT_type wglSwapIntervalEXT = nullptr;

    // See https://www.opengl.org/registry/specs/ARB/wgl_create_context.txt for all values
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126

#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// See https://www.opengl.org/registry/specs/ARB/wgl_pixel_format.txt for all values
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023

#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_TYPE_RGBA_ARB 0x202B

    void init_wgl_extensions(HINSTANCE hInstance)
    {
        WNDCLASS dummy_class;
        ZeroMemory(&dummy_class, sizeof(WNDCLASS));
        dummy_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        dummy_class.lpfnWndProc = DefWindowProc;
        dummy_class.lpszClassName = L"kernel_wgl_ext_loader";

        if(!RegisterClass(&dummy_class)) {
            report_fatal_message(
                str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: Failed to register "
                        "WGL extension loader window class (",
                        win32::win32_category().message(GetLastError()),
                        ")")));
        }

        HWND dummy_window = CreateWindowEx(0,
                                           L"kernel_wgl_ext_loader",
                                           L"KernelWglExtLoader",
                                           0,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           NULL,
                                           NULL,
                                           hInstance,
                                           NULL);

        if(!dummy_window) {
            report_fatal_message(str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                                         "Failed to create WGL extension loader window (",
                                         win32::win32_category().message(GetLastError()),
                                         ")")));
        }

        HDC hDC = GetDC(dummy_window);

        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        int pixel_format = ChoosePixelFormat(hDC, &pfd);
        SetPixelFormat(hDC, pixel_format, &pfd);

        HGLRC dummy_context = wglCreateContext(hDC);
        if(dummy_context == NULL) {
            report_fatal_message(str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                                         "Failed to create WGL extension loader context (",
                                         win32::win32_category().message(GetLastError()),
                                         ")")));
        }

        wglMakeCurrent(hDC, dummy_context);

        wglCreateContextAttribsARB =
            (wglCreateContextAttribsARB_type)wglGetProcAddress("wglCreateContextAttribsARB");
        if(wglCreateContextAttribsARB == NULL) {
            report_fatal_message(
                str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: Failed to acquire "
                        "wglCreateContextAttribsARB extension (",
                        win32::win32_category().message(GetLastError()),
                        ")")));
        }

        wglChoosePixelFormatARB =
            (wglChoosePixelFormatARB_type)wglGetProcAddress("wglChoosePixelFormatARB");
        if(wglChoosePixelFormatARB == NULL) {
            report_fatal_message(str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                                         "Failed to acquire wglChoosePixelFormatARB extension (",
                                         win32::win32_category().message(GetLastError()),
                                         ")")));
        }

        wglSwapIntervalEXT = (wglSwapIntervalEXT_type)wglGetProcAddress("wglSwapIntervalEXT");
        if(wglSwapIntervalEXT == NULL) {
            report_fatal_message(str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                                         "Failed to acquire wglSwapIntervalEXT extension (",
                                         win32::win32_category().message(GetLastError()),
                                         ")")));
        }

        wglMakeCurrent(hDC, 0);
        wglDeleteContext(dummy_context);
        ReleaseDC(dummy_window, hDC);
        DestroyWindow(dummy_window);
    }

    LRESULT CALLBACK renderer_wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch(uMsg) {
        case WM_GETMINMAXINFO: {
            RECT r{0,
                   0,
                   get<x>(original_configured_screen_res),
                   get<y>(original_configured_screen_res)};
            AdjustWindowRectEx(&r,
                               GetWindowLongPtr(hWnd, GWL_STYLE),
                               /*bMenu*/ FALSE,
                               GetWindowLongPtr(hWnd, GWL_EXSTYLE));

            LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
            mmi->ptMaxSize.x = r.right - r.left;
            mmi->ptMaxSize.y = r.bottom - r.top;
            return 0;
        }

        case WM_WINDOWPOSCHANGING: {
            RECT r{0,
                   0,
                   get<x>(original_configured_screen_res),
                   get<y>(original_configured_screen_res)};
            AdjustWindowRectEx(&r,
                               GetWindowLongPtr(hWnd, GWL_STYLE),
                               /*bMenu*/ FALSE,
                               GetWindowLongPtr(hWnd, GWL_EXSTYLE));

            LPWINDOWPOS wp = (LPWINDOWPOS)lParam;
            wp->cx = r.right - r.left;
            wp->cy = r.bottom - r.top;
            return 0;
        }

        case WM_MOUSEMOVE: {
            // Scale the mouse position so JK thinks it's over the menu
            auto xPos = (int16_t)lParam;
            auto yPos = (int16_t)(lParam >> 16);

            auto d = actual_menu_area.size();
            auto wscale = 640.0f / static_cast<float>(get<x>(d));
            auto hscale = 480.0f / static_cast<float>(get<y>(d));

            xPos = (int16_t)((float)(xPos - get<x>(actual_menu_area.start)) * wscale);
            yPos = (int16_t)((float)(yPos - get<y>(actual_menu_area.start)) * hscale);

            lParam = (((LPARAM)yPos << 16) | (LPARAM)xPos);

            // Pass this message back to the original wndproc
            break;
        }
        }

        return CallWindowProc(original_wkernel_wndproc, hWnd, uMsg, wParam, lParam);
    }

    static size<2, int> make_internal_scr_res(config const *the_config)
    {
        if(the_config->correct_game_aspect_ratio) {
            // The game will be rendered in a best-fit virtual screen with a 4:3 aspect ratio
            constexpr float aspect = 4.0f / 3.0f;

            int w_if_h_fit = (int)((float)std::get<1>(the_config->resolution) * aspect);
            if(w_if_h_fit > std::get<0>(the_config->resolution)) {
                // Virtual screen is too tall. Fit to width instead.
                int h_if_w_fit = (int)((float)std::get<0>(the_config->resolution) / aspect);
                return make_size(std::get<0>(the_config->resolution), h_if_w_fit);
            }

            return make_size(w_if_h_fit, std::get<1>(the_config->resolution));
        }

        return make_size(std::get<0>(the_config->resolution), std::get<1>(the_config->resolution));
    }

    static size<2, float> make_internal_scr_res_scale_f(size<2, int> conf_scr_res)
    {
        return make_size(2.0f / (float)get<x>(conf_scr_res), 2.0f / (float)get<y>(conf_scr_res));
    }

    static box<2, int> make_internal_scr_area(size<2, int> conf_scr_res,
                                              size<2, int> internal_scr_res)
    {
        // Adjust actual display area to fit the physical display area
        float aspect = (float)get<x>(internal_scr_res) / (float)get<y>(internal_scr_res);

        int width_if_height_fit = (int)(aspect * (float)get<y>(conf_scr_res));
        if(width_if_height_fit <= get<x>(conf_scr_res)) {
            // Virtual screen is taller or as tall as the physical screen
            int leftpad = (get<x>(conf_scr_res) - width_if_height_fit) / 2;
            return make_box(make_point(leftpad, 0),
                            make_size(width_if_height_fit, get<y>(conf_scr_res)));
        }

        // Virtual screen is wider or as wide as the physical screen
        int height_if_width_fit = (int)((float)get<x>(conf_scr_res) / aspect);
        int toppad = (get<y>(conf_scr_res) - height_if_width_fit) / 2;
        return make_box(make_point(0, toppad),
                        make_size(get<x>(conf_scr_res), height_if_width_fit));
    }

    static box<2, int> make_internal_menu_area(size<2, int> conf_scr_res,
                                               size<2, int> internal_scr_res,
                                               bool correct_aspect_ratio)
    {
        if(!correct_aspect_ratio) {
            return make_internal_scr_area(conf_scr_res, internal_scr_res);
        }

        int menu_w = 0;
        int menu_h = 0;

        constexpr float aspect = 4.0f / 3.0f;
        int w_if_h_fit = (int)((float)get<y>(conf_scr_res) * aspect);
        if(w_if_h_fit > get<x>(conf_scr_res)) {
            // Menu is too tall. Fit to width instead.
            int h_if_w_fit = (int)((float)get<x>(conf_scr_res) / aspect);
            menu_w = get<x>(conf_scr_res);
            menu_h = h_if_w_fit;
        }
        else {
            menu_w = w_if_h_fit;
            menu_h = get<y>(conf_scr_res);
        }

        // Center the menu virtual screen in the physical screen
        int menu_off_x = (get<x>(conf_scr_res) - menu_w) / 2;
        int menu_off_y = (get<y>(conf_scr_res) - menu_h) / 2;

        return make_box(make_point(menu_off_x, menu_off_y), make_size(menu_w, menu_h));
    }

    static direction<2, float> make_internal_scr_offset_f(size<2, int> conf_scr_res,
                                                          box<2, int> actual_display_area)
    {
        float xoff =
            ((float)get<x>(actual_display_area.start) / (float)get<x>(conf_scr_res)) * 2.0f;
        float yoff =
            ((float)get<y>(actual_display_area.start) / (float)get<y>(conf_scr_res)) * 2.0f;

        return make_direction(xoff, yoff);
    }


    // Persistent per-mesh buffers. Allocated/uploaded exactly once per cached
    // source geometry, never rebuilt when the camera moves.
    struct gpu_3do_gl_mesh {
        gl::vertex_array vao;
        gl::buffer vertices;
        gl::buffer indices;
        unsigned num_indices = 0;
        explicit gpu_3do_gl_mesh(gpu_3do_geometry const &source)
        {
            num_indices=static_cast<unsigned>(source.indices.size());
            gl::bind_vertex_array(vao);
            gl::bind_buffer(gl::buffer_bind_target::array,vertices);
            gl::buffer_data(gl::buffer_bind_target::array,
                            make_span(source.vertices).as_const_bytes(),
                            gl::buffer_usage::static_draw);
            gl::enable_vertex_attrib_array(0U);
            gl::vertex_attrib_pointer(0U,3U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,x));
            gl::enable_vertex_attrib_array(1U);
            gl::vertex_attrib_pointer(1U,2U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,u));
            gl::enable_vertex_attrib_array(2U);
            gl::vertex_attrib_pointer(2U,3U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,nx));
            gl::enable_vertex_attrib_array(3U);
            gl::vertex_attrib_pointer(3U,1U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,static_light));
            gl::enable_vertex_attrib_array(4U);
            gl::vertex_attrib_pointer(4U,3U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,lnx));
            gl::enable_vertex_attrib_array(5U);
            gl::vertex_attrib_pointer(5U,3U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,fax));
            gl::enable_vertex_attrib_array(6U);
            gl::vertex_attrib_pointer(6U,1U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,face_extra));
            gl::enable_vertex_attrib_array(7U);
            gl::vertex_attrib_pointer(7U,1U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,light_mode));
            gl::enable_vertex_attrib_array(8U);
            gl::vertex_attrib_pointer(8U,3U,gl::vertex_element_type::float32,
                                      false,sizeof(gpu_3do_vertex),
                                      offsetof(gpu_3do_vertex,cr));
            gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
            gl::buffer_data(gl::buffer_bind_target::element_array,
                            make_span(source.indices).as_const_bytes(),
                            gl::buffer_usage::static_draw);
        }
    };

        struct gpu_3do_texture {
            // Stable texture-cache ID, unlike a borrowed GL texture_view.
            std::optional<srgb_texture_id> albedo_id;
            // Late Camera-HUD rendering must own image IDs, not just GL
            // texture_view handles: D3D MAT reloads can recycle freed images
            // before the deferred HUD pass is presented.
            std::optional<srgb_texture_id> albedo2_id;
            std::optional<srgb_texture_id> emission_id;
            gl::texture_view texture = gl::default_texture;
            gl::texture_view texture2 = gl::default_texture;
            float albedo_transition=0.0f;
            int albedo_source2=2;
            gl::texture_view emission = gl::default_texture;
            gl::texture_view palette_fullbright_mask = gl::default_texture;
            gl::texture_view normal = gl::default_texture;
            gl::texture_view normal2 = gl::default_texture;
            gl::texture_view surface = gl::default_texture;
            gl::texture_view displacement = gl::default_texture;
            float displacement_factor=0.0f;
            gl::texture_view environment = gl::default_texture;
            gl::texture_view environment2 = gl::default_texture;
            bool has_environment2=false;
            float environment_transition=0.0f;
            float width=1.0f, height=1.0f;
            float reflection_strength=0.0f,roughness=0.0f,metalness=0.0f;
            // Preserve authored zero-strength specular overrides.
            bool has_authored_specular=false;
            float normal_strength=1.0f,normal_strength2=1.0f;
            bool has_normal=false,has_normal2=false,has_surface=false,has_environment=false;
            int reflection_mode=0;
            point<2,float> normal_scroll=make_point(0.0f,0.0f);
            point<2,float> normal_scroll2=make_point(0.0f,0.0f);
            float alpha_cutoff=0.5f;
            float opacity=1.0f;
            float fluid_thickness=0.25f;
            // 0=invalid/unresolved MAT (diagnostic magenta), 1=texture,
            // 2=intentionally no albedo map (neutral white).
            int albedo_source=0;
            bool has_image=false, alpha=false, masked=false, dithered=false, special=false;
            bool solid_fullbright=false; // raw CMP index unchanged by all 64 software light levels
            bool has_palette_fullbright=false; // exact source 8-bit MAT texels
            bool has_emission=false, emissive_only=false;
            bool filtered=true;
            material_blend_mode blend=material_blend_mode::normal;
            point<4,float> tint=make_point(1.0f,1.0f,1.0f,1.0f);
            point<4,float> emission_factor=make_point(0.0f,0.0f,0.0f,0.0f);
            point<2,float> albedo_scroll=make_point(0.0f,0.0f);
            point<2,float> emissive_scroll=make_point(0.0f,0.0f);
        };


    class renderer_impl : public renderer {
    private:
        config const *the_config;

        renderer_mode mode = renderer_mode::menu;
        // Actual GDI/8-bit menu presentation need not switch display mode.
        bool menu_frame_pending = false;
        std::atomic<unsigned> pending_png_screenshots{0};
        unsigned next_png_screenshot_number=0U;
        size<2, int> conf_scr_res;

        size<2, int> internal_scr_res;
        size<2, float> internal_scr_res_f;
        size<2, float> internal_scr_res_scale_f;
        box<2, int> actual_display_area;
        box<2, int> actual_menu_area;
        direction<2, float> internal_scr_offset_f;

        DirectDraw_impl ddraw1;
        DirectDraw2_impl ddraw2;
        Direct3D_impl d3d1;
        Direct3DDevice_impl d3ddevice1;
        Direct3DViewport_impl d3dviewport1;

        primary_menu_surface ddraw1_primary_menu_surface;
        backbuffer_menu_surface ddraw1_backbuffer_menu_surface;
        primary_surface ddraw1_primary_surface;
        backbuffer_surface ddraw1_backbuffer_surface;
        zbuffer_surface ddraw1_zbuffer_surface;
        offscreen_surface ddraw1_offscreen_surface;
        offscreen_menu_surface ddraw1_offscreen_menu_surface;
        offscreen_menu_surface ddraw1_offscreen_credit_surface;

        DirectDrawPalette_impl ddraw1_palette;

        std::vector<std::unique_ptr<sysmem_texture_surface>> sysmem_texture_surfaces;
        std::vector<std::unique_ptr<vidmem_texture_surface>> vidmem_texture_surfaces;
        std::vector<std::unique_ptr<execute_buffer>> execute_buffers;

        HINSTANCE dll_instance = NULL;
        HWND hWnd = NULL;
        HDC hDC = NULL;
        HGLRC hGLRC = NULL;

        std::unique_ptr<opengl_state> ogs;
        // One opt-in, level/world-space projector for initial decal parity tests.
        std::unordered_map<uint64_t,std::unique_ptr<gpu_3do_gl_mesh>> gpu_3do_gl_cache;
        std::unordered_map<uint64_t,uint64_t> gpu_3do_last_drawn;
        uint64_t gpu_3do_present_serial=0;
        // Resolved MAT surfaces are identical for all instances within one
        // frame; share costly native memory reads across both GPU passes.
        std::unordered_map<uint64_t,gpu_3do_texture> gpu_frame_material_cache;
        // A Camera HUD's geometry and camera already survive until the
        // next presentation. Retain its MAT resolution too: accessing
        // native rdMaterial pointers one frame later can observe changed
        // cels, freed/reused objects or mid-animation upload transitions.
        std::unordered_map<uint64_t,gpu_3do_texture> deferred_gpu_hud_material_cache;
        std::vector<gpu_3do_instance> frame_gpu_3do_instances;
        saber_light_rgb::snapshot frame_saber_light_colors{};
        // Level-static sector planes; opt-in only.
        sector_crossing_snapshot sector_crossing_world{};
        std::array<uint32_t,6> sector_crossing_world_signature{};
        std::vector<sector_crossing_plane_range> sector_crossing_ranges;
        GLuint sector_crossing_plane_texture=0;
        GLuint sector_crossing_plane_buffer=0;
        // The scene is rendered AFTER the prior scene is presented. Retain
        // the camera used to transform these 3DOs, not the live rdCamera from
        // the next frame when Camera Thing HUDs are composited.
        struct gpu_preview_frame_camera {
            engine_camera camera{};
            gpu_3do_matrix34 native_view{};
            gpu_3do_native_frustum frustum{};
            float far_plane=1000.0f;
            bool perspective=false;
            bool valid=false;
        };
        gpu_preview_frame_camera frame_gpu_3do_camera{};
        gpu_preview_frame_camera frame_world_cache_camera{};
        gpu_preview_frame_camera deferred_gpu_3do_camera{};
        // Retained across the one-frame render/present boundary.
        std::vector<gpu_3do_instance> deferred_gpu_3do_instances;
        bool frame_has_exempt_gpu=false;
        std::unique_ptr<renderer_compositor> frame_compositor;
        std::unique_ptr<renderer_ao> frame_ao;
        std::unique_ptr<renderer_screen> frame_screen;
        // Opt-in cached fluid SPR target: half-size RGBA16F plus an 8-bit
        // per-pixel stencil layer count. Never used for world fluids or cutouts.
        std::unique_ptr<post_buffer> smoke_half_buffer;
        GLuint smoke_half_stencil=0;
        bool smoke_half_failed=false;
        // Independent low-cost planar capture: allocated on first enabled
        // scene, never by the normal GPU renderer or menus.
        std::unique_ptr<planar_reflection_capture_target> planar_capture_target;
        std::unique_ptr<planar_reflection_capture_shader> planar_capture_program;
        bool planar_capture_ready=false;
        uintptr_t planar_plane_world=0;
        uint64_t planar_plane_world_revision=0;
        uint64_t planar_plane_scanned_frame=~uint64_t(0);
        std::optional<planar_reflection_plane> planar_capture_plane;
        bool planar_plane_from_material=false;


        HGDIOBJ indexed_bitmap_dc = NULL;
        char const *indexed_bitmap_source = nullptr;
        std::vector<color_rgba8> indexed_bitmap_colors;

        double menu_accumulator = 0.0;

        // Independent renderer clock, unaffected by JK's simulation/pause state.
        std::chrono::steady_clock::time_point animation_epoch =
            std::chrono::steady_clock::now();
        double animation_seconds = 0.0;

        std::chrono::steady_clock::time_point last_matlink_scan{};
        uint32_t last_matlink_world = 0;
        uint32_t last_matlink_materials = 0;
        uint32_t last_matlink_static_materials = 0;
        std::string last_matlink_level;
        bool matlink_first_scan = true;
        std::map<uint64_t, std::string> reported_mat_links;
        bool matlink_build_warning_logged = false;

        // Stage 3: sampled only when COG material overrides are present.
        runtime_material_snapshot runtime_controls;
        std::chrono::steady_clock::time_point last_runtime_scan{};
        uint32_t runtime_current_world = 0;
        uint64_t last_runtime_revision = 0;
        bool force_runtime_refresh = true;
        unsigned runtime_fast_retries = 0;
        struct native_handle_link {
            uint32_t mat_id;
            vidmem_texture_surface const *surface;
            uint64_t generation;
        };
        std::map<uint32_t, native_handle_link> runtime_mat_by_handle;
        std::map<uint32_t, fs::path> runtime_pack_by_mat;
        std::map<fs::path, srgb_texture_id> runtime_images;
        std::set<uint32_t> spotlight_unready_mats;
        std::map<uint32_t,srgb_texture_id> spotlight_retained_albedo;
        uint32_t spotlight_retained_world=0;
        struct translucent_spotlight_draw {
            spotlight_instance light;
            spotlight_screen_rect coverage;
        };
        std::vector<translucent_spotlight_draw> translucent_spotlights_for_frame;
        std::unique_ptr<spotlight_shadow_gpu> spotlight_shadow_target;
        std::unique_ptr<sun_shadow_gpu> sun_shadow_target;
        std::unique_ptr<sun_sky_aperture_gpu> sun_sky_aperture_target;
        // Fail-open physical-sky eligibility, cached per SithWorld. The
        // world surface table is audited occasionally rather than on every
        // sunlit frame; aperture projection updates remain independent.
        bool sun_level_has_sky=true;
        bool sun_level_sky_known=false;
        bool sun_level_sky_sun_active=false;
        uint32_t sun_level_sky_faces=0;
        uint64_t sun_level_sky_identity=0;
        uintptr_t sun_level_sky_world=0;
        uint64_t sun_level_sky_last_audit_frame=0;
        std::unique_ptr<world_cache_gpu> world_cache_target;
        // The CPU sparse voxel field is an internal prerequisite of GPU ambient.
        std::unique_ptr<continuous_ambient_field> continuous_ambient_cpu_cache;
        uintptr_t continuous_ambient_cpu_world=0;
        uint64_t continuous_ambient_cpu_revision=0;
        bool continuous_ambient_cpu_overbright=false;
        bool continuous_ambient_cpu_valid=false;
        std::unique_ptr<continuous_ambient_gpu_table> continuous_ambient_gpu_cache;
        GLuint continuous_ambient_key_buffer=0,continuous_ambient_key_texture=0;
        GLuint continuous_ambient_value_buffer=0,continuous_ambient_value_texture=0;
        std::unordered_map<uint32_t,uintptr_t> continuous_ambient_sector_addresses;
        std::unordered_map<uint32_t,float> continuous_ambient_live_floors;
        bool continuous_ambient_gpu_enabled_this_frame=false;
        std::unique_ptr<sun_dynamic_shadow_gpu> sun_dynamic_target;
        std::vector<sun_shadow_projection> sun_dynamic_views;
        std::vector<sun_dynamic_shadow_tile> sun_dynamic_tiles;
        // Track only owners with a successfully rasterized atlas tile.
        std::unordered_set<uint64_t> sun_dynamic_atlas_owners;
        std::array<sun_shadow_projection,2> sun_shadow_views{};
        // Reuse stationary group bounds and both opaque/cutout hash results.
        // Live MATs are still resolved at each full validation; cutout hash
        // reuse additionally requires an exact match for their five inputs.
        struct sun_cached_static_group {
            decal_volume_aabb bounds{};
            uint64_t opaque_signature=0,cutout_signature=0;
            std::array<uint32_t,5> cutout_inputs{};
            uint32_t offset=0,count=0;
            uintptr_t material=0;
            int32_t cel=-1;
            std::array<float,3> bounds_min{},bounds_max{};
            bool descriptor_valid=false,bounds_valid=false;
            bool opaque_signature_valid=false,cutout_signature_valid=false;
        };
        struct sun_cached_static_instance {
            uint64_t owner=0,id=0;
            gpu_3do_geometry const *geometry=nullptr;
            std::array<float,12> transform{};
            std::vector<sun_cached_static_group> groups;
        };
        // Index-aligned to the current frame's instances. Reordering causes
        // a safe miss; changes to owner, geometry or transform invalidate.
        std::vector<sun_cached_static_instance> sun_static_geometry_cache;
        uint64_t sun_profile_static_bounds_reused=0;
        uint64_t sun_profile_static_signature_reused=0;
        uint64_t sun_profile_static_bounds_calculated=0;
        uint64_t sun_profile_static_signature_calculated=0;
        uint64_t sun_profile_cutout_hash_reused=0;
        uint64_t sun_profile_cutout_hash_calculated=0;
        uint64_t sun_profile_cutout_material_invalidations=0;
        uint64_t sun_profile_static_group_invalidations=0;
        uint64_t sun_profile_static_instance_invalidations=0;
        // Last fully validated global sun projection. The dynamic atlas
        // continues rendering every frame while cascade checks are throttled.
        sun_shadow_projection sun_shadow_validated_world_view{};
        std::chrono::steady_clock::time_point sun_shadow_last_validation{};
        bool sun_shadow_validated_world_view_ready=false;
        uint64_t sun_profile_throttle_skipped=0;
        uint64_t sun_profile_throttle_checked=0;
        uint64_t sun_profile_throttle_forced_early=0;
        // Forced-check causes are sampled independently; near/far camera
        // coverage failures can overlap in a single frame.
        uint64_t sun_profile_throttle_due_timer=0;
        uint64_t sun_profile_forced_unready=0;
        uint64_t sun_profile_forced_world=0;
        uint64_t sun_profile_forced_sun=0;
        std::array<uint64_t,2> sun_profile_forced_coverage{};
        uint64_t sun_profile_checked_pass_us=0;
        uint64_t sun_profile_checked_pass_count=0;
        uint64_t sun_profile_skipped_pass_us=0;
        uint64_t sun_profile_checked_pass_peak_us=0;
        uint64_t sun_profile_skipped_pass_peak_us=0;
        bool sun_shadow_ready=false;
        // Diagnostics for transient shadow loss: compare successive complete
        // shadow passes rather than sampling only one frame in 120.
        bool sun_shadow_observed=false;
        bool sun_shadow_previous_ready=false;
        size_t sun_shadow_previous_excluded=0;
        char const *sun_shadow_frame_failure=nullptr;
        std::array<bool,2> sun_shadow_cascade_ready{{false,false}};
        std::array<bool,2> sun_shadow_cascade_cached{{false,false}};
        std::array<std::array<float,3>,2> sun_shadow_directions{};
        std::array<uintptr_t,2> sun_shadow_worlds{};
        std::array<uint64_t,2> sun_shadow_revisions{};
        std::array<uint64_t,2> sun_shadow_caster_signatures{};
        std::array<bool,2> sun_shadow_static_cached{{false,false}};
        std::array<uint64_t,2> sun_profile_full_hits{{0,0}};
        std::array<uint64_t,2> sun_profile_static_hits{{0,0}};
        std::array<uint64_t,2> sun_profile_static_redraws{{0,0}};
        std::array<uint64_t,2> sun_profile_working_redraws{{0,0}};
        // Reasons overlap by design: e.g. a camera turn can change the XY
        // projection AND the static caster signature in the same frame.
        // Report full-map and static-layer misses independently.
        struct sun_shadow_miss_profile {
            uint64_t cold=0,world=0,revision=0,direction=0;
            uint64_t projection_xy=0,projection_size=0,projection_depth=0;
            uint64_t projection_basis=0,static_casters=0,dynamic_casters=0;
            uint64_t unclassified=0;
        };
        std::array<sun_shadow_miss_profile,2> sun_profile_full_misses{};
        std::array<sun_shadow_miss_profile,2> sun_profile_static_misses{};
        std::array<uint64_t,2> sun_profile_prepare_us{{0,0}};
        std::array<uint64_t,2> sun_profile_static_submit_us{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_submit_us{{0,0}};
        std::array<uint64_t,2> sun_profile_static_submitted{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_submitted{{0,0}};
        std::array<uint64_t,2> sun_profile_static_gl_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_gl_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_static_multi_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_multi_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_static_instanced_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_instanced_calls{{0,0}};
        std::array<uint64_t,2> sun_profile_static_instanced_ranges{{0,0}};
        std::array<uint64_t,2> sun_profile_moving_instanced_ranges{{0,0}};
        uint64_t sun_profile_candidate_us=0,sun_profile_candidate_groups=0;
        uint64_t sun_profile_atlas_us=0;
        // Timing-only breakdown of the work performed before cache reuse.
        std::array<uint64_t,2> sun_profile_projection_us{};
        std::array<uint64_t,2> sun_profile_world_query_us{};
        std::array<uint64_t,2> sun_profile_world_filter_us{};
        std::array<uint64_t,2> sun_profile_model_scan_us{};
        std::array<uint64_t,2> sun_profile_cache_check_us{};
        // Nanosecond sampling, never a timestamp for every 3DO group.
        // Per-frame preparation is now shared by both cascades.
        std::array<uint64_t,2> sun_profile_prepared_reuses{};
        uint64_t sun_profile_candidate_material_sample_ns=0;
        uint64_t sun_profile_candidate_bounds_sample_ns=0;
        uint64_t sun_profile_candidate_signature_sample_ns=0;
        uint64_t sun_profile_candidate_material_samples=0;
        uint64_t sun_profile_candidate_bounds_samples=0;
        uint64_t sun_profile_candidate_signature_samples=0;
        // Same delayed, nonblocking timestamp ring as presentation profiling:
        // stage 0 = dynamic atlas, 1 = near cascade, 2 = far cascade.
        presentation_gpu_profile sun_gpu_queries;
        std::array<uint64_t,presentation_gpu_profile::num_stages> sun_gpu_ns{};
        uint64_t sun_gpu_samples=0,sun_gpu_skipped=0;
        uint64_t sun_profile_atlas_mat_hits=0,sun_profile_atlas_mat_misses=0;
        std::array<uint64_t,2> sun_shadow_static_signatures{};
        std::array<uint64_t,2> sun_shadow_static_revisions{};
        std::array<uintptr_t,2> sun_shadow_static_worlds{};
        std::array<sun_shadow_projection,2> sun_shadow_static_views{};
        std::array<std::array<float,3>,2> sun_shadow_static_directions{};
        spotlight_instance spotlight_shadow_light{};
        spotlight_shadow_projection spotlight_shadow_view{};
        bool spotlight_shadow_ready=false;
        bool spotlight_shadow_cached=false;
        uint64_t spotlight_shadow_3do_signature=0;
        uint32_t spotlight_shadow_world=0;
        uint64_t spotlight_shadow_revision=0;
        std::set<std::pair<uint32_t, int>> runtime_missing_images;
        std::set<uint32_t> runtime_missing_sources;
        bool warned_reflections_disabled = false;

        profile_sample profile_frame;
        profile_sample profile_window;
        uint64_t profile_frames = 0, peak_period = 0, peak_present = 0;
        uint64_t peak_sort_opaque = 0, peak_sort_trans = 0, peak_triangles = 0;
        profile_clock::time_point last_profile_frame{};
        profile_clock::time_point profile_window_start{};
        profile_clock::time_point last_present_end{};
        profile_clock::time_point last_execute_end{};
        bool frame_had_execute = false;

        // Timestamp queries are only issued with profile_renderer enabled.
        // Results are delayed by up to eight frames and never force a GPU
        // flush/wait. The CPU present_swap stage still includes SwapBuffers.
        presentation_gpu_profile present_gpu_queries;
        std::array<uint64_t,presentation_gpu_profile::num_stages> present_gpu_ns{};
        uint64_t present_gpu_samples=0, present_gpu_skipped=0;

        // Asynchronous GPU timings for the scene, separate from postFX and
        // shadow profiling. No GL readbacks or query waits on the hot path.
        // 0 = opaque gbuffer (no shadow capture), 1 = world/decal/flare alpha,
        // 2 = cached 3DO/SPR alpha. Gun/overlay is not included in 1 or 2.
        presentation_gpu_profile scene_gpu_queries;
        std::array<uint64_t,presentation_gpu_profile::num_stages> scene_gpu_ns{};
        uint64_t scene_gpu_samples=0, scene_gpu_skipped=0;
        bool scene_gpu_timing=false;

        static uint64_t elapsed_profile_us(profile_clock::time_point a,
                                           profile_clock::time_point b)
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
        }

        static uint64_t elapsed_profile_ns(profile_clock::time_point a,
                                           profile_clock::time_point b)
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count());
        }

        void record_profile(profile_clock::time_point started,
                            profile_clock::time_point finished)
        {
            if(!the_config->profile_renderer) return;
            profile_frame.present_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(finished-started).count());
            profile_window.add(profile_frame);
            ++profile_frames;
            peak_period = std::max(peak_period, profile_frame.period_us);
            peak_present = std::max(peak_present, profile_frame.present_us);
            peak_triangles = std::max(peak_triangles, profile_frame.tris);
            peak_sort_opaque = std::max(peak_sort_opaque,
                profile_frame.timings[profile_idx(profile_stage::sort_opaque)]);
            peak_sort_trans = std::max(peak_sort_trans,
                profile_frame.timings[profile_idx(profile_stage::sort_transparent)]);
            profile_frame = {};
            if(profile_window_start == profile_clock::time_point{}) {
                profile_window_start = finished;
                return;
            }
            if(finished-profile_window_start < std::chrono::seconds(2)) return;
            const uint64_t frames = std::max<uint64_t>(profile_frames,1);
            auto avg = [&](profile_stage stage)->uint64_t {
                return profile_window.timings[profile_idx(stage)] / frames;
            };
            LOG_INFO("PERF frames=", frames,
                     " period_us avg/peak=", profile_window.period_us / frames,
                     "/", peak_period, " submit_us avg/peak=",
                     profile_window.present_us / frames, "/", peak_present,
                     " execute_us=", avg(profile_stage::execute));
            LOG_INFO("PERF JK-side gaps_us before_first=", profile_window.before_first_execute_us / frames,
                     " between=", profile_window.between_executes_us / frames,
                     " after_last=", profile_window.after_last_execute_us / frames,
                     " frames_with_execute=", profile_window.frames_with_execute, "/", frames);
            LOG_INFO("PERF execute_buffers calls_avg=", profile_window.execute_calls / frames,
                     " vertices_avg=", profile_window.execute_vertices / frames,
                     " instruction_bytes_avg=", profile_window.execute_instruction_bytes / frames);
            if(native_3do_available()) {
                LOG_INFO("PERF native_3do inclusive_us model=",
                         (profile_window.native_model_ns / 1000) / frames,
                         " mesh=", (profile_window.native_mesh_ns / 1000) / frames,
                         " face=", (profile_window.native_face_ns / 1000) / frames,
                         " model_calls_avg=", profile_window.native_model_calls / frames,
                         " model_drawn_avg=", profile_window.native_model_drawn / frames,
                         " mesh_calls_avg=", profile_window.native_mesh_calls / frames,
                         " face_calls_avg=", profile_window.native_face_calls / frames,
                         " face_emitted_avg=", profile_window.native_face_emitted / frames);
                LOG_INFO("PERF native_3do breakdown_us model_hook=",
                         (profile_window.native_model_hook_ns/1000)/frames,
                         " model_engine_inclusive=",
                         (profile_window.native_model_engine_ns/1000)/frames,
                         " model_engine_excluding_mesh=",
                         (profile_window.native_model_engine_self_ns/1000)/frames,
                         " mesh_queue=",
                         (profile_window.native_mesh_queue_ns/1000)/frames,
                         " mesh_native_fallback=",
                         (profile_window.native_mesh_native_ns/1000)/frames,
                         " mesh_decal_capture=",
                         (profile_window.native_mesh_decal_ns/1000)/frames,
                         " cached_mesh_calls_avg=",
                         profile_window.native_mesh_gpu_calls/frames,
                         " fallback_mesh_calls_avg=",
                         profile_window.native_mesh_native_calls/frames,
                         " NOTE=model_includes_mesh; engine_excluding_mesh_is_estimate");
            }
            if(native_sprite_available()) {
                LOG_INFO("PERF native_sprite calls_avg type0/1/2/3/4=",
                         profile_window.sprite_type_calls[0]/frames,"/",
                         profile_window.sprite_type_calls[1]/frames,"/",
                         profile_window.sprite_type_calls[2]/frames,"/",
                         profile_window.sprite_type_calls[3]/frames,"/",
                         profile_window.sprite_type_calls[4]/frames,
                         " other=",profile_window.sprite_other_type_calls/frames,
                         " hidden=",profile_window.sprite_hidden_calls/frames);
                LOG_INFO("PERF native_sprite draw_us_avg=",
                         (profile_window.sprite_native_ns/1000)/frames,
                         " rdSprite_Draw_calls_avg=",
                         profile_window.sprite_native_calls/frames,
                         " returned_nonzero_avg=",
                         profile_window.sprite_native_success/frames,
                         " proc_faces_avg=",
                         profile_window.sprite_native_proc_faces/frames,
                         " projected_gpu_avg=",
                         profile_window.sprite_projected_gpu/frames,
                         " projected_native_warmup_avg=",
                         profile_window.sprite_projected_warmups/frames,
                         " NOTE=inclusive_native_only_not_gpu_or_decal_projection");
            }
            LOG_INFO("PERF sprite_gpu cached_avg=",
                     profile_window.sprite_gpu_cached_quads/frames,
                     " geometry_reuses_avg=",
                     profile_window.sprite_gpu_quad_reuses/frames,
                     " fallback_avg=",
                     profile_window.sprite_gpu_quad_fallbacks/frames,
                     " mat_warmups_avg=",
                     profile_window.sprite_gpu_quad_mat_wait/frames,
                     " NOTE=shared_quads_instancing_opt_in");
            LOG_INFO("PERF sprite_instancing enabled=",
                     the_config->enable_gpu_sprite_instancing?1:0,
                     " eligible_avg=",
                     profile_window.gpu_sprite_instancing_eligible/frames,
                     " batches_avg=",
                     profile_window.gpu_sprite_instanced_calls/frames,
                     " batched_sprites_avg=",
                     profile_window.gpu_sprite_instanced_quads/frames,
                     " saved_draw_calls_avg=",
                     (profile_window.gpu_sprite_instanced_quads-
                      profile_window.gpu_sprite_instanced_calls)/frames);
            auto const &rejects=profile_window.sprite_gpu_quad_rejects;
            LOG_INFO("PERF sprite_gpu_reject_avg source=",rejects[0]/frames,
                     " geomode=",rejects[1]/frames,
                     " lightmode=",rejects[2]/frames,
                     " texmode=",rejects[3]/frames,
                     " face_flags=",rejects[4]/frames,
                     " resource_uv=",rejects[5]/frames,
                     " camera_pose=",rejects[6]/frames,
                     " capacity=",rejects[7]/frames);
            LOG_INFO("PERF sorting_us opaque=", avg(profile_stage::sort_opaque),
                     " transparent=", avg(profile_stage::sort_transparent),
                     " peaks=", peak_sort_opaque, "/", peak_sort_trans,
                     " upload_us=", avg(profile_stage::upload),
                     " nativeMAT_us=", avg(profile_stage::mat_resolve));
            LOG_INFO("PERF transparent_sort_fallback groups=",
                     profile_window.sort_fallback_groups,
                     " triangles=",profile_window.sort_fallback_triangles,
                     " frames=",profile_window.sort_fallback_frames,
                     "/",frames,
                     " NOTE=approximate_eye_depth_only_on_budget_exhaustion");
            LOG_INFO("PERF passes_us opaque=", avg(profile_stage::opaque_pass),
                     " transparent=", avg(profile_stage::transparent_pass),
                     " post_swap=", avg(profile_stage::present_swap),
                     " HUDupload=", avg(profile_stage::hud_upload),
                     " HUDdraw=", avg(profile_stage::hud_draw));
            LOG_INFO("PERF present_cpu_us resolve=",avg(profile_stage::present_resolve),
                     " blur=",avg(profile_stage::present_blur),
                     " retro=",avg(profile_stage::present_retro),
                     " compositor=",avg(profile_stage::present_composite),
                     " swapbuffers=",avg(profile_stage::present_swapbuffers),
                     " beginframe=",avg(profile_stage::present_beginframe),
                     " NOTE=substages_of_post_swap");
            uint64_t const gpu_samples=present_gpu_samples;
            LOG_INFO("PERF present_gpu_us resolve=",
                     gpu_samples?(present_gpu_ns[0]/1000U)/gpu_samples:0U,
                     " blur=",gpu_samples?(present_gpu_ns[1]/1000U)/gpu_samples:0U,
                     " compositor=",gpu_samples?(present_gpu_ns[2]/1000U)/gpu_samples:0U,
                     " samples=",gpu_samples," ring_busy_skips=",present_gpu_skipped,
                     " supported=",present_gpu_queries.is_supported(),
                     " NOTE=delayed_nonblocking_GPU_timestamps_zero_if_no_samples");
            present_gpu_ns.fill(0U);
            present_gpu_samples=present_gpu_skipped=0U;
            uint64_t const scene_samples=scene_gpu_samples;
            LOG_INFO("PERF scene_gpu_us opaque_gbuffer=",
                     scene_samples?(scene_gpu_ns[0]/1000U)/scene_samples:0U,
                     " world_decal_flare_alpha=",
                     scene_samples?(scene_gpu_ns[1]/1000U)/scene_samples:0U,
                     " cached_3do_sprite_alpha=",
                     scene_samples?(scene_gpu_ns[2]/1000U)/scene_samples:0U,
                     " samples=",scene_samples,
                     " ring_busy_skips=",scene_gpu_skipped,
                     " supported=",scene_gpu_queries.is_supported(),
                     " NOTE=async_GPU_only;opaque_excludes_shadow_maps;world_includes_decals;gun_overlay_excluded");
            scene_gpu_ns.fill(0U);
            scene_gpu_samples=scene_gpu_skipped=0U;
            if(the_config->use_gpu_cache ||
               the_config->enable_decals) {
                LOG_INFO("PERF gpu_3do fallback_native_per_frame geometry=",
                         profile_window.gpu_geometry_native/frames,
                         " snapshot=",profile_window.gpu_snapshot_native/frames,
                         " material=",profile_window.gpu_material_native/frames,
                         " lighting_or_camera=",profile_window.gpu_lighting_native/frames,
                         " empty_mesh_nodes=",profile_window.gpu_empty_mesh_native/frames,
                         " NOTE=empty_nodes_do_not_generate_GPU_draws");
                LOG_INFO("PERF native_memory_checks avg_cache_hits=",
                         profile_window.gpu_native_span_cache_hits/frames,
                         " avg_VirtualQuery=",
                         profile_window.gpu_native_span_queries/frames,
                         " NOTE=positive_regions_only_per_frame");
                {
                    // Cumulative since start; subtract consecutive lines for a
                    // rate. Every guarded native read goes through this cache.
                    auto const &shared=
                        engine_camera_detail::shared_readable_cache();
                    LOG_INFO("PERF shared_readable_cache hits_total=",shared.hits,
                             " VirtualQuery_total=",shared.queries,
                             " NOTE=cumulative_render_thread");
                }
                auto const &current_fog=get_fog_settings();
                auto const fog_params=current_fog.info.value();
                // The lightweight logger only formats integral values and
                // strings. Convert the animated floating-point fog settings
                // explicitly instead of passing unsupported float types.
                LOG_INFO("PERF gpu_3do fog start=",std::to_string(fog_params[0]),
                         " end=",std::to_string(fog_params[1]),
                         " opacity=",std::to_string(
                             the_config->enable_fog?fog_params[2]:0.0f));
                LOG_INFO("PERF gpu_3do candidates_avg=",profile_window.gpu_candidates/frames,
                         " sphere_culled_avg=",profile_window.gpu_culled/frames,
                         " instances_avg=",profile_window.gpu_queued/frames,
                         " snapshots_total=",profile_window.gpu_snapshots,
                         " ready_checks_avg=",profile_window.gpu_ready_checks/frames);
                LOG_INFO("PERF gpu_3do readiness_us_avg=",
                         (profile_window.gpu_ready_ns/1000)/frames,
                         " mat_cel_refs_avg=",
                         profile_window.gpu_ready_material_refs/frames,
                         " shared_resident_hits_avg=",
                         profile_window.gpu_ready_cache_hits/frames);
                LOG_INFO("PERF gpu_3do source_check_us_avg=",
                         (profile_window.gpu_source_ns/1000)/frames,
                         " checks_avg=",profile_window.gpu_source_checks/frames,
                         " invalidations_total=",
                         profile_window.gpu_source_invalidations,
                         " failed_snapshot_reuses_total=",
                         profile_window.gpu_failed_snapshot_reuses);
                LOG_INFO("PERF gpu_3do CPU_us_avg=",profile_window.gpu_preview_cpu_us/frames,
                         " draws_avg=",profile_window.gpu_draw_calls/frames,
                         " triangles_avg=",profile_window.gpu_draw_triangles/frames,
                         " material_resolves_avg=",profile_window.gpu_material_resolves/frames,
                         " uploads_total=",profile_window.gpu_uploads,
                         " NOTE=CPU_submission_not_GPU_time");
                LOG_INFO("PERF gpu_3do visible_breakdown_us opaque_collect=",
                         profile_window.gpu_preview_collect_us[0]/frames,
                         " opaque_sort=",profile_window.gpu_preview_sort_us[0]/frames,
                         " opaque_submit=",profile_window.gpu_preview_submit_us[0]/frames,
                         " alpha_collect=",profile_window.gpu_preview_collect_us[1]/frames,
                         " alpha_sort=",profile_window.gpu_preview_sort_us[1]/frames,
                         " alpha_submit=",profile_window.gpu_preview_submit_us[1]/frames,
                         " calls_opaque_alpha=",
                         profile_window.gpu_preview_calls[0]/frames,"/",
                         profile_window.gpu_preview_calls[1]/frames,
                         " NOTE=opaque_collect_prepares_ordinary_alpha_setup_and_restoration_not_in_substages");
                LOG_INFO("PERF gpu_3do filter_state updates_avg=",
                         profile_window.gpu_filter_state_sets/frames,
                         " skipped_updates_avg=",profile_window.gpu_filter_state_skips/frames,
                         " avoided_glTexParameter_calls_avg=",
                         (profile_window.gpu_filter_state_skips*3U)/frames,
                         " NOTE=albedo_emission_and_transition_textures_only");
                LOG_INFO("PERF gpu_3do batch GL_calls_avg=",
                         profile_window.gpu_draw_calls/frames,
                         " source_ranges_avg=",
                         profile_window.gpu_draw_ranges/frames,
                         " multi_calls_avg=",
                         profile_window.gpu_multidraw_calls/frames,
                         " NOTE=opaque_material_sorted_alpha_consecutive_order_preserved");
                LOG_INFO("PERF overdraw_ranges_avg fluid_sprites=",
                         profile_window.gpu_alpha_fluid_sprite_ranges/frames,
                         " other_alpha_sprites=",
                         profile_window.gpu_alpha_other_sprite_ranges/frames,
                         " alpha_models=",
                         profile_window.gpu_alpha_model_ranges/frames,
                         " opaque_cutout_sprites=",
                         profile_window.gpu_opaque_cutout_sprite_ranges/frames,
                         " NOTE=GPU_3DO_draw_ranges_not_fragments_or_GL_calls;fluid_only_sprite");
                LOG_INFO("PERF gpu_3do passes opaque_GL_calls_avg=",
                         profile_window.gpu_opaque_draw_calls/frames,
                         " opaque_ranges_avg=",
                         profile_window.gpu_opaque_draw_ranges/frames,
                         " transparent_GL_calls_avg=",
                         profile_window.gpu_transparent_draw_calls/frames,
                         " transparent_ranges_avg=",
                         profile_window.gpu_transparent_draw_ranges/frames);
                LOG_INFO("PERF gpu_3do groups_checked_avg=",
                         profile_window.gpu_groups_considered/frames,
                         " frustum_culled_avg=",
                         profile_window.gpu_groups_frustum_culled/frames,
                         " backface_culled_avg=",
                         profile_window.gpu_groups_backface_culled/frames,
                         " NOTE=actual_group_walks_shared_ordinary_alpha_counted_once");
            }
            LOG_INFO("PERF geometry tris avg/max=", profile_window.tris / frames,
                     "/", peak_triangles,
                     " transparent_avg=", profile_window.trans_tris / frames,
                     " draw_batches_avg=", profile_window.draw_batches / frames,
                     " material_binds_avg=", profile_window.binds / frames);
            profile_window = {};
            profile_frames = peak_period = peak_present = 0;
            peak_sort_opaque = peak_sort_trans = peak_triangles = 0;
            profile_window_start = finished;
        }


        using timestamp_t = std::chrono::high_resolution_clock::time_point;
        timestamp_t menu_prev_ticks;
        timestamp_t menu_curr_ticks;

        triangle_batch world_batch;
        sorted_triangle_batch world_transparent_batch;
        // Reuse space across frames: only depth-writing foliage cutouts need
        // buffering when splitting them from truly blended native triangles.
        std::vector<triangle> native_cutout_scratch;
        sorted_triangle_batch world_flare_batch;
        unsigned flare_gpu_trace_count=0U;
        unsigned flare_present_trace_count=0U;
        triangle_batch gun_batch;
        sorted_triangle_batch gun_transparent_batch;
        triangle_batch overlay_batch;

        bool is_gun = false;
        bool is_transparent = false;
        triangle_batch *current_triangle_batch = &world_batch;

        material_instance_id current_material = material_instance_id(0U);
        float active_native_depth_bias = 0.0f;

        // Polygon offset is a depth-buffer preference, not a mesh transform.
        // A zero offset disables it entirely so unrelated draws retain JK
        // depth semantics. Avoid redundant GL state changes on material binds.
        void set_native_depth_bias(float bias)
        {
            if(bias == active_native_depth_bias) return;
            if(bias == 0.0f) {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            else {
                if(active_native_depth_bias == 0.0f)
                    glEnable(GL_POLYGON_OFFSET_FILL);
                // OpenGL standard depth increases away from the camera.
                // Positive material bias moves a face closer for depth tests;
                // factor handles grazing angles and units handle coplanarity.
                glPolygonOffset(-bias, -bias);
            }
            active_native_depth_bias = bias;
        }

    public:
        explicit renderer_impl(HINSTANCE dll_instance, config const *the_config)
            : the_config(the_config)
            , conf_scr_res(std::get<0>(the_config->resolution), std::get<1>(the_config->resolution))
            , internal_scr_res(make_internal_scr_res(the_config))
            , internal_scr_res_f(static_cast<size<2, float>>(internal_scr_res))
            , internal_scr_res_scale_f(make_internal_scr_res_scale_f(conf_scr_res))
            , actual_display_area(make_internal_scr_area(conf_scr_res, internal_scr_res))
            , actual_menu_area(make_internal_menu_area(conf_scr_res,
                                                       internal_scr_res,
                                                       the_config->correct_menu_aspect_ratio))
            , internal_scr_offset_f(make_internal_scr_offset_f(conf_scr_res, actual_display_area))
            , ddraw1(this)
            , ddraw2(this)
            , d3d1(this)
            , d3ddevice1(this)
            , d3dviewport1(this)
            , ddraw1_primary_menu_surface(this)
            , ddraw1_backbuffer_menu_surface(this, &ddraw1_primary_menu_surface)
            , ddraw1_primary_surface(this)
            , ddraw1_backbuffer_surface(this, internal_scr_res)
            , ddraw1_zbuffer_surface(this)
            , ddraw1_palette(this)
            , dll_instance(dll_instance)
        {
            indexed_bitmap_colors.resize(256, color_rgba8::zero());

            menu_prev_ticks = std::chrono::high_resolution_clock::now();
            menu_curr_ticks = menu_prev_ticks;
        }

        ~renderer_impl() override
        {
            clear_deferred_gpu_hud_materials();
            reset_sector_crossing();
            reset_continuous_ambient_gpu();
            release_spotlight_images();
            spotlight_shadow_target.reset();
            sun_shadow_target.reset();
            sun_sky_aperture_target.reset();
            sun_dynamic_target.reset();
            // Context-owned GL queries must be deleted before the context
            // disappears. If the context is already gone, its objects go
            // away with it; do not issue GL calls on an invalid context.
            if(hGLRC && wglGetCurrentContext()==hGLRC) {
                if(smoke_half_stencil) {
                    glDeleteRenderbuffers(1,&smoke_half_stencil);
                    smoke_half_stencil=0;
                }
                smoke_half_buffer.reset();
                planar_capture_program.reset();
                planar_capture_target.reset();
                present_gpu_queries.release();
                scene_gpu_queries.release();
                sun_gpu_queries.release();
            }
        }

        void set_renderer_mode(renderer_mode mode) override
        {
            if(this->mode != mode) {
                // The previous scene's Camera HUD may still be waiting for
                // present_game() when JK switches into its pause or end menu.
                // Invalidate it before clearing GPU/MAT resources: otherwise
                // a stale late-pass draw can briefly sample an unresolved MAT.
                deferred_gpu_3do_instances.clear();
                clear_deferred_gpu_hud_materials();
                deferred_gpu_3do_camera={};
                frame_has_exempt_gpu=false;
                planar_capture_ready=false;
                planar_capture_plane.reset();
                planar_plane_world=0;
                planar_plane_world_revision=0;
                planar_plane_scanned_frame=~uint64_t(0);
                reset_sector_crossing();
                reset_continuous_ambient_gpu();
                continuous_ambient_cpu_cache.reset();
                continuous_ambient_cpu_world=0;
                continuous_ambient_cpu_revision=0;
                continuous_ambient_cpu_overbright=false;
                continuous_ambient_cpu_valid=false;
                if(the_config->use_gpu_cache ||
                   the_config->enable_decals ||
                   (the_config->enable_spotlights && the_config->enable_spotlight_shadows) ||
                   the_config->enable_sun_shadows) {
                    // Levels recycle native mesh pointers; discard both copies.
                    reset_gpu_3do_instances();
                    gpu_3do_gl_cache.clear();
                    gpu_3do_last_drawn.clear();
                    gpu_3do_present_serial=0;
                    gpu_frame_material_cache.clear();
                    frame_gpu_3do_instances.clear();
                }
                if(the_config->profile_renderer) {
                    last_profile_frame = {};
                    profile_window_start = {};
                    profile_window = {};
                    profile_frame = {};
                    profile_frames = 0;
                    last_present_end = {};
                    last_execute_end = {};
                    frame_had_execute = false;
                }
                spotlight_shadow_ready=false;
                spotlight_shadow_cached=false;
                spotlight_shadow_3do_signature=0;
                sun_shadow_ready=false;
                sun_shadow_observed=false;
                sun_shadow_previous_ready=false;
                sun_shadow_previous_excluded=0;
                sun_shadow_cascade_ready={{false,false}};
                sun_shadow_cascade_cached={{false,false}};
                sun_shadow_static_cached={{false,false}};
                sun_shadow_validated_world_view_ready=false;
                sun_shadow_last_validation={};
                // Sky projection cache cannot survive a level/mode reset.
                sun_sky_aperture_target.reset();
                sun_level_has_sky=true;
                sun_level_sky_known=false;
                sun_level_sky_sun_active=false;
                sun_level_sky_faces=0;
                sun_level_sky_identity=0;
                sun_level_sky_world=0;
                sun_level_sky_last_audit_frame=0;
                sun_static_geometry_cache.clear();
                sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
                sun_dynamic_atlas_owners.clear();
                // Pausing/menu transitions may evict and reload D3D textures.
                // Refresh the native MAT resolver on the next game frame,
                // instead of letting the previous one-second timer cause a flash.
                force_runtime_refresh = true;
                runtime_fast_retries = 0;
                // A scene transition cannot reuse old frame history.
                // Do not do this in begin_game/end_game (called each frame).
                get_frame_blend_settings().reset();
                if(frame_screen)frame_screen->invalidate_retained_scene();
                get_distance_blur_settings() = distance_blur_settings();
                get_peripheral_blur_settings() = peripheral_blur_settings();
            }
            this->mode = mode;
        }

        size<2, int> get_internal_screen_resolution() override
        {
            return internal_scr_res;
        }

        bool is_parallax_enabled() override
        {
            return the_config->enable_parallax;
        }

        bool is_reflections_enabled() override
        {
            return the_config->enable_reflections;
        }

        bool is_default_specular_enabled() override
        {
            return the_config->enable_reflections &&
                   the_config->enable_default_specular &&
                   the_config->default_specular_strength>0.0f;
        }

        void notify_texture_uploaded() override
        {
            force_runtime_refresh = true;
            runtime_fast_retries = 0;
            // A D3D load can replace a native MAT handle mid-frame.
            // Drop previously confirmed positive residency on next lookup.
            notify_native_mat_texture_uploaded();
        }

        point<2, int> get_cursor_pos(point<2, int> real_pos) override
        {
            if(mode == renderer_mode::menu) {
                auto d = actual_menu_area.size();
                auto wscale = 640.0f / static_cast<float>(get<x>(d));
                auto hscale = 480.0f / static_cast<float>(get<y>(d));

                // Stretch the point into 640x480
                return make_point(
                    (int)((float)(get<x>(real_pos) - get<x>(actual_menu_area.start)) * wscale),
                    (int)((float)(get<y>(real_pos) - get<y>(actual_menu_area.start)) * hscale));
            }

            return real_pos;
        }

        void initialize(HINSTANCE hInstance, HWND parentWnd) override
        {
            init_wgl_extensions(hInstance);

            hWnd = parentWnd;

            original_wkernel_wndproc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
            SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG)&renderer_wndproc);

            original_configured_screen_res = conf_scr_res;
            jkgm::actual_menu_area = actual_menu_area;

            if(the_config->fullscreen) {
                DEVMODE dm;
                ZeroMemory(&dm, sizeof(dm));
                dm.dmSize = sizeof(dm);
                dm.dmPelsWidth = get<x>(conf_scr_res);
                dm.dmPelsHeight = get<y>(conf_scr_res);
                dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

                auto rv = ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
                if(rv != DISP_CHANGE_SUCCESSFUL) {
                    report_fatal_message(str(fmt(
                        "JkGfxMod could not change the display to the configured resolution. This "
                        "may have happened because your graphics device does not support the "
                        "configured resolution, or due to an implementation bug in "
                        "JkGfxMod.\n\nDetails: ChangeDisplaySettings returned ",
                        rv)));
                }

                MoveWindow(
                    hWnd, 0, 0, get<x>(conf_scr_res), get<y>(conf_scr_res), /*repaint*/ TRUE);
            }

            hDC = GetDC(hWnd);

            std::vector<int> pfd_attribs{WGL_DRAW_TO_WINDOW_ARB,
                                         GL_TRUE,
                                         WGL_SUPPORT_OPENGL_ARB,
                                         GL_TRUE,
                                         WGL_DOUBLE_BUFFER_ARB,
                                         GL_TRUE,
                                         WGL_ACCELERATION_ARB,
                                         WGL_FULL_ACCELERATION_ARB,
                                         WGL_PIXEL_TYPE_ARB,
                                         WGL_TYPE_RGBA_ARB,
                                         WGL_COLOR_BITS_ARB,
                                         32,
                                         WGL_DEPTH_BITS_ARB,
                                         24,
                                         WGL_STENCIL_BITS_ARB,
                                         8,
                                         0};

            int pfdid;
            UINT num_formats;
            bool cpf_res = wglChoosePixelFormatARB(
                hDC, pfd_attribs.data(), nullptr, /*max formats*/ 1, &pfdid, &num_formats);
            if(!cpf_res) {
                report_fatal_message(
                    str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                            "Call to wglChoosePixelFormatARB failed unexpectedly (returned ",
                            GetLastError(),
                            ")")));
            }

            if(num_formats == 0) {
                report_fatal_message("JkGfxMod could not initialize OpenGL.\n\nDetails: Failed to "
                                     "find suitable pixel format.");
            }

            PIXELFORMATDESCRIPTOR pfd;
            DescribePixelFormat(hDC, pfdid, sizeof(pfd), &pfd);
            SetPixelFormat(hDC, pfdid, &pfd);

            std::vector<int> gl_attribs{WGL_CONTEXT_MAJOR_VERSION_ARB,
                                        3,
                                        WGL_CONTEXT_MINOR_VERSION_ARB,
                                        3,
                                        WGL_CONTEXT_PROFILE_MASK_ARB,
                                        WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                                        0};
            hGLRC = wglCreateContextAttribsARB(hDC, NULL, gl_attribs.data());

            if(hGLRC == NULL) {
                report_fatal_message(str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: "
                                             "Failed to create OpenGL context (returned ",
                                             GetLastError(),
                                             ")")));
            }

            wglMakeCurrent(hDC, hGLRC);

            int present_interval = the_config->enable_vsync ? 1 : 0;
            auto vsync_res = wglSwapIntervalEXT(present_interval);
            if(!vsync_res) {
                report_fatal_message(
                    str(fmt("JkGfxMod could not initialize OpenGL.\n\nDetails: Call to "
                            "wglSwapIntervalEXT failed unexpectedly (returned ",
                            GetLastError(),
                            ")")));
            }

            if(!gladLoadGL()) {
                report_fatal_message("JkGfxMod could not initialize OpenGL.\n\nDetails: Failed to "
                                     "load extensions (gladLoadGL)");
            }

            ShowWindow(hWnd, SW_SHOW);

            gl::enable(gl::capability::framebuffer_srgb);

            gl::set_clear_color(solid(colors::black));
            gl::clear({gl::clear_flag::color, gl::clear_flag::depth});

            SwapBuffers(hDC);

            ogs = std::make_unique<opengl_state>(
                conf_scr_res, internal_scr_res, actual_display_area, actual_menu_area, the_config);
            texture_cache = create_texture_cache(the_config);
            frame_compositor = create_renderer_compositor(the_config);
            frame_ao = create_renderer_ao(the_config, conf_scr_res);
            frame_screen = create_renderer_screen(the_config, conf_scr_res);
            begin_frame();
        }

        audio_module_probe audio_modules;

        void poll_audio_modules()
        {
            if(!the_config->profile_audio)return;
            auto const report=audio_modules.poll();
            if(report)LOG_INFO("AUDIO modules: ",*report);
        }

        void begin_frame()
        {
            gl::set_active_texture_unit(0);
            frame_screen->begin_frame(
                the_config->retain_hom_pixels && mode==renderer_mode::ingame);
        }

        // Refresh immediately for a COG command or menu return; otherwise
        // perform the established safe native scan at most once a second.
        // During D3D reuploads, retry briefly without a one-second visual gap.
        void refresh_runtime_mat_links()
        {
            runtime_controls = get_runtime_material_controls().snapshot();
            if(runtime_controls.empty()) {
                runtime_mat_by_handle.clear();
                runtime_pack_by_mat.clear();
                last_runtime_revision = runtime_controls.revision;
                return;
            }

            bool const new_command = last_runtime_revision != runtime_controls.revision;
            if(new_command || force_runtime_refresh) {
                // The first few cache probes after an upload, new command or
                // unpause run at 100 ms intervals, not the normal one second.
                runtime_fast_retries = 0;
            }
            auto const now = std::chrono::steady_clock::now();
            auto const retry_interval = runtime_fast_retries < 10
                ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
            // Frequent animated HUD uploads must not force a native-table
            // scan every single frame. Keep a short minimum interval while
            // still reacting far sooner than the original one-second timer.
            auto const min_interval = force_runtime_refresh
                ? std::chrono::milliseconds(80) : retry_interval;
            if(!new_command && now - last_runtime_scan < min_interval) {
                return;
            }
            last_runtime_revision = runtime_controls.revision;
            last_runtime_scan = now;
            force_runtime_refresh = false;

            auto state = engine_material_debug::scan();
            if(!state.supported_build) {
                LOG_WARNING("Runtime MAT override unavailable: unknown JK executable build");
                return;
            }
            if(state.current_world && runtime_current_world &&
               state.current_world != runtime_current_world) {
                // The COG layer may already have accepted commands from the
                // *new* world's startup scripts; don't clear those commands.
                get_runtime_material_controls().prepare_world(state.current_world);
                runtime_controls = get_runtime_material_controls().snapshot();
                last_runtime_revision = runtime_controls.revision;
                LOG_DEBUG("JK material image cache cleared on level change");
                for(auto const &image : runtime_images) {
                    texture_cache->release_srgb_texture(image.second);
                }
                runtime_images.clear();
                runtime_missing_images.clear();
                runtime_missing_sources.clear();
                runtime_mat_by_handle.clear();
                runtime_pack_by_mat.clear();
                runtime_fast_retries = 0;
            }
            if(state.current_world) {
                runtime_current_world = state.current_world;
                get_runtime_material_controls().prepare_world(state.current_world);
                runtime_controls = get_runtime_material_controls().snapshot();
                last_runtime_revision = runtime_controls.revision;
            }
            if(runtime_controls.empty()) {
                return;
            }

            std::map<uint32_t, native_handle_link> new_handles;
            std::map<uint32_t, fs::path> new_packs;
            for(auto const &link : state.textures) {
                if(link.handle == 0 || link.handle > vidmem_texture_surfaces.size()) {
                    continue;
                }
                auto const &surface = vidmem_texture_surfaces[link.handle - 1];
                if(!surface) continue;
                new_handles.emplace(link.handle, native_handle_link{
                    link.mat_id, surface.get(), surface->texture_generation});
                if(surface->replacement_material) {
                    auto pack = surface->replacement_material->pack_root;
                    auto inserted = new_packs.emplace(link.mat_id, pack);
                    if(!inserted.second && inserted.first->second != pack) {
                        LOG_WARNING("JK MAT ", link.mat_id,
                                    " has multiple source packs; first pack is used for image IDs");
                    }
                }
            }

            if(!new_handles.empty()) {
                // Preserve known source-pack identities for MATs whose D3D
                // surfaces were temporarily evicted, within the same world.
                for(auto const &item : new_packs) {
                    runtime_pack_by_mat[item.first] = item.second;
                }
                runtime_mat_by_handle = std::move(new_handles);
            }
            else {
                // Do not discard existing identities on an incomplete pause/
                // resume cache scan. A reused handle is rejected by generation
                // and surface-pointer checks in bind_material().
                for(auto it = runtime_mat_by_handle.begin();
                    it != runtime_mat_by_handle.end();) {
                    uint32_t handle = it->first;
                    if(!handle || handle > vidmem_texture_surfaces.size() ||
                       !vidmem_texture_surfaces[handle - 1] ||
                       vidmem_texture_surfaces[handle - 1].get() != it->second.surface ||
                       vidmem_texture_surfaces[handle - 1]->texture_generation !=
                           it->second.generation) {
                        it = runtime_mat_by_handle.erase(it);
                    }
                    else ++it;
                }
            }

            bool unresolved = runtime_mat_by_handle.empty();
            for(auto const &mat : runtime_controls.per_mat) {
                bool found = false;
                for(auto const &link : runtime_mat_by_handle) {
                    if(link.second.mat_id == mat.first) {
                        found = true;
                        break;
                    }
                }
                if(!found) unresolved = true;
            }
            if(runtime_controls.global_env && runtime_controls.global_env->to &&
               runtime_pack_by_mat.count(runtime_controls.global_env->to->source_mat) == 0) {
                unresolved = true;
            }
            if(unresolved) {
                if(runtime_fast_retries < 10) ++runtime_fast_retries;
            }
            else runtime_fast_retries = 10;

            if(new_command || runtime_fast_retries < 2) {
                LOG_DEBUG("Runtime MAT resolver: revision=", runtime_controls.revision,
                          " cached handles=", runtime_mat_by_handle.size(),
                          " source packs=", runtime_pack_by_mat.size(),
                          " pending=", unresolved);
            }
            if(!the_config->enable_reflections && !warned_reflections_disabled) {
                bool env_requested = runtime_controls.global_env.has_value();
                for(auto const &mat : runtime_controls.per_mat) {
                    env_requested |= mat.second.env.has_value();
                }
                if(env_requested) {
                    LOG_WARNING("Runtime EnvMap requested but enable_reflections=false; "
                                "environment effects are disabled by the master switch");
                    warned_reflections_disabled = true;
                }
            }
        }

        std::optional<srgb_texture_id> resolve_runtime_image(runtime_image_key const &image)
        {
            auto pack = runtime_pack_by_mat.find(image.source_mat);
            if(pack == runtime_pack_by_mat.end()) {
                if(runtime_missing_sources.emplace(image.source_mat).second) {
                    LOG_WARNING("Runtime image ", image.image_id, ": native source MAT ",
                                image.source_mat, " has no replacement pack mapping yet");
                }
                return std::nullopt;
            }
            runtime_missing_sources.erase(image.source_mat);
            auto path = texture_cache->get_pack_image(pack->second, image.image_id);
            if(!path) {
                if(runtime_missing_images.emplace(image.source_mat, image.image_id).second) {
                    LOG_WARNING("Unregistered image ID ", image.image_id,
                                " in source MAT ", image.source_mat, " pack ",
                                pack->second.generic_string());
                }
                return std::nullopt;
            }
            auto found = runtime_images.find(*path);
            if(found != runtime_images.end()) {
                return found->second;
            }
            auto texture = texture_cache->get_srgb_texture_from_filename(*path);
            runtime_images.emplace(*path, texture); // Keep one owning reference per image.
            LOG_INFO("Runtime MAT image ready: source MAT=",image.source_mat,
                     " image=",image.image_id,
                     " pack=",pack->second.generic_string());
            return texture;
        }

        // "base" is the compiled-pack image (or the original JK albedo).
        // Pending source packs must not clear an already valid environment,
        // even if the previous image in an interrupted fade is unavailable.
        void resolve_runtime_transition(runtime_image_transition const &transition,
                                        std::optional<srgb_texture_id> base,
                                        std::optional<srgb_texture_id> &from,
                                        std::optional<srgb_texture_id> &to,
                                        float &weight)
        {
            resolve_runtime_transition_images(
                transition,base,
                [this](runtime_image_key const &key) {
                    return resolve_runtime_image(key);
                },
                from,to,weight);
        }

        // Diagnostic only: correlate JK material cels to live D3D texture
        // instances already mapped to replacement packs. Logs changed links,
        // and never writes to engine memory or creates an engine hook.
        void log_mat_links()
        {
            if(!the_config->log_path.has_value()) {
                return;
            }
            auto const now = std::chrono::steady_clock::now();
            if(now - last_matlink_scan < std::chrono::seconds(3)) {
                return;
            }
            last_matlink_scan = now;

            auto const snapshot = engine_material_debug::scan();
            if(the_config->profile_renderer && snapshot.supported_build)
                LOG_INFO("PERF native_MAT_table_scan region_cache_hits=",
                         snapshot.region_cache_hits,
                         " VirtualQuery_calls=",snapshot.region_queries,
                         " materials_valid=",snapshot.visited_materials,
                         " NOTE=scan_local_positive_regions_only");
            if(!snapshot.supported_build) {
                if(!matlink_build_warning_logged) {
                    LOG_WARNING("JK MAT link diagnostic disabled: unknown engine build");
                    matlink_build_warning_logged = true;
                }
                return;
            }
            bool const new_world = matlink_first_scan ||
                snapshot.current_world != last_matlink_world ||
                snapshot.current_materials != last_matlink_materials ||
                snapshot.static_materials != last_matlink_static_materials ||
                snapshot.level_name != last_matlink_level;
            if(new_world) {
                matlink_first_scan = false;
                reported_mat_links.clear();
                last_matlink_world = snapshot.current_world;
                last_matlink_materials = snapshot.current_materials;
                last_matlink_static_materials = snapshot.static_materials;
                last_matlink_level = snapshot.level_name;
                LOG_DEBUG("JK MAT diagnostic: level=", snapshot.level_name,
                          " world=", snapshot.current_world,
                          " materials=", snapshot.current_count,
                          " static materials=", snapshot.static_count,
                          " validated=", snapshot.visited_materials,
                          " rejected=", snapshot.rejected_materials);
            }

            size_t matched = 0;
            std::map<uint32_t, std::set<std::string>> packs_per_mat;
            for(auto const &link : snapshot.textures) {
                if(link.handle == 0 || link.handle > vidmem_texture_surfaces.size()) {
                    continue; // Not a live renderer texture handle.
                }
                auto const &surface = vidmem_texture_surfaces[link.handle - 1];
                if(!surface || !surface->replacement_material) {
                    continue;
                }
                auto const *replacement = surface->replacement_material;
                std::string const pack = replacement->pack_root.generic_string();
                packs_per_mat[link.mat_id].insert(pack);
                ++matched;

                uint64_t const key = (uint64_t(link.mat_id) << 16) |
                    (uint64_t(link.cel) << 8) |
                    (uint64_t(link.mip) << 1) | uint64_t(link.alpha);
                std::string const identity = std::to_string(link.handle) + ":" +
                                             pack + ":" + replacement->source_name;
                auto const it = reported_mat_links.find(key);
                if(it == reported_mat_links.end() || it->second != identity) {
                    reported_mat_links[key] = identity;
                    LOG_DEBUG("JK MAT link: id=", link.mat_id,
                              " name=", link.name, " cel=", link.cel,
                              " mip=", link.mip, " alpha=", link.alpha,
                              " D3D=", link.handle, " pack=", pack,
                              " material=", replacement->source_name);
                }
            }
            for(auto const &entry : packs_per_mat) {
                if(entry.second.size() > 1) {
                    // The image-ID source for this MAT would be ambiguous.
                    LOG_WARNING("JK MAT ", entry.first,
                                " has replacement cels in multiple packs");
                }
            }
            if(new_world) {
                LOG_DEBUG("JK MAT diagnostic: native cache links=", snapshot.textures.size(),
                          " live replacement links=", matched);
            }
        }

        // Keep each image referenced by the deferred Camera HUD alive
        // through the following presentation. GL texture_view stores only
        // an object name; without a texture-cache reference, a MAT reload
        // can release and recycle that object for a DIFFERENT image.
        // Each cached material owns one retain per texture field (including
        // a repeated ID); releases must mirror those retains.
        void retain_deferred_gpu_hud_material(gpu_3do_texture const &m)
        {
            if(!texture_cache)return;
            if(m.albedo_id)texture_cache->retain_srgb_texture(*m.albedo_id);
            if(m.albedo2_id)texture_cache->retain_srgb_texture(*m.albedo2_id);
            if(m.emission_id)texture_cache->retain_srgb_texture(*m.emission_id);
        }
        void clear_deferred_gpu_hud_materials()
        {
            if(texture_cache)
                for(auto const &entry:deferred_gpu_hud_material_cache) {
                    auto const &m=entry.second;
                    if(m.albedo_id)texture_cache->release_srgb_texture(*m.albedo_id);
                    if(m.albedo2_id)texture_cache->release_srgb_texture(*m.albedo2_id);
                    if(m.emission_id)texture_cache->release_srgb_texture(*m.emission_id);
                }
            // Actual map clear (not the wrapper, to avoid recursion).
            deferred_gpu_hud_material_cache.clear();
        }

        // Composite Camera-type HUD Things after scene post-processing, but
        // before DirectDraw HUD. In Retro Mode draw to the linear staging
        // buffer so final palette quantization includes these Camera Things.
        void draw_post_exempt()
        {
            if(deferred_gpu_3do_instances.empty()) {
                clear_deferred_gpu_hud_materials();
                return;
            }

            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                 ogs->retro_mode_active && ogs->retro_present_buffer
                     ? ogs->retro_present_buffer->fbo
                     : gl::default_framebuffer);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            gl::disable(gl::capability::depth_test);
            gl::set_depth_mask(false);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::blend);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);

            // Retain per-instance transforms and the originating frame camera.
            // Each Camera Thing chooses overlay or depth-tested world UI.
            draw_gpu_3do_preview(true,true);

            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::set_active_texture_unit(0);
            gl::disable(gl::capability::blend);
            gl::set_depth_mask(true);
            // Present consumed this frame's retained image references.
            clear_deferred_gpu_hud_materials();
        }

        // Called ONLY by the game's original Screenshot hotkey or COG
        // command detour. No GL operations occur on JK's input thread.
        bool queue_screenshot_png() override
        {
            if(!ogs || mode!=renderer_mode::ingame)return false;
            unsigned count=pending_png_screenshots.load(std::memory_order_relaxed);
            do {
                if(count>=8U)return false;
            } while(!pending_png_screenshots.compare_exchange_weak(
                count,count+1U,std::memory_order_release,
                std::memory_order_relaxed));
            return true;
        }

        void capture_pending_screenshot_png()
        {
            // Consume only one request per presented frame. Repeated hotkey
            // presses queue their own captures instead of overwriting files.
            unsigned const requests=pending_png_screenshots.exchange(
                0U,std::memory_order_acquire);
            if(requests==0U)return;
            if(requests>1U)
                pending_png_screenshots.fetch_add(
                    requests-1U,std::memory_order_release);

            int const w=get<x>(conf_scr_res),h=get<y>(conf_scr_res);
            // No screenshot when the OpenGL window is not fully initialized.
            if(!ogs || !hDC || w<=0 || h<=0 ||
               w>16384 || h>16384 ||
               uint64_t(w)*uint64_t(h)>64000000ULL) {
                LOG_WARNING("Screenshot PNG skipped: invalid presenter size");
                return;
            }
            // Preserve JKGM's JSHOTxxx naming convention, but never
            // replace an existing PNG (including from earlier sessions).
            fs::path filename;
            bool unused=false;
            for(;next_png_screenshot_number<1000U;
                ++next_png_screenshot_number) {
                auto const name=native_screenshot::png_filename(
                    next_png_screenshot_number);
                fs::path candidate(name);
                std::error_code ec;
                bool const exists=fs::exists(candidate,ec);
                if(ec) {
                    LOG_WARNING("Screenshot PNG: cannot check filename: ",
                                name," (",ec.message(),")");
                    return;
                }
                if(!exists) {
                    filename=std::move(candidate);
                    ++next_png_screenshot_number;
                    unused=true;
                    break;
                }
            }
            if(!unused) {
                LOG_WARNING("Screenshot PNG: all JSHOT000.PNG..JSHOT999.PNG exist");
                return;
            }

            try {
                image img(make_size(w,h));
                static_assert(sizeof(color_rgba8)==4U,
                              "OpenGL screenshot readback needs packed RGBA8");
                // After the postFX, Camera HUD, native HUD, and optional
                // Retro palette pass, the final sRGB image is in the
                // DEFAULT WINDOW BACK buffer. Never read the old DirectDraw
                // screenshot surface or an intermediate scene FBO.
                GLint old_read_fb=0,old_read_buffer=0,old_pack_alignment=4;
                GLint old_pack_row_length=0,old_pack_buffer=0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read_fb);
                glGetIntegerv(GL_READ_BUFFER,&old_read_buffer);
                glGetIntegerv(GL_PACK_ALIGNMENT,&old_pack_alignment);
                glGetIntegerv(GL_PACK_ROW_LENGTH,&old_pack_row_length);
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&old_pack_buffer);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
                glReadBuffer(GL_BACK);
                glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
                glPixelStorei(GL_PACK_ALIGNMENT,1);
                glPixelStorei(GL_PACK_ROW_LENGTH,0);
                // Don't attribute a stale error from earlier scene draws
                // to this rare screenshot readback. OpenGL errors are sticky.
                for(unsigned i=0;i<8U && glGetError()!=GL_NO_ERROR;++i) {}
                glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,
                             img.data.data());
                GLenum const read_error=glGetError();
                glPixelStorei(GL_PACK_ROW_LENGTH,old_pack_row_length);
                glPixelStorei(GL_PACK_ALIGNMENT,old_pack_alignment);
                glBindBuffer(GL_PIXEL_PACK_BUFFER,GLuint(old_pack_buffer));
                glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(old_read_fb));
                glReadBuffer(GLenum(old_read_buffer));
                if(read_error!=GL_NO_ERROR) {
                    LOG_WARNING("Screenshot PNG: glReadPixels failed, GL error=",
                                unsigned(read_error));
                    return;
                }
                auto *rgba=reinterpret_cast<uint8_t *>(img.data.data());
                native_screenshot::flip_rgba_rows(rgba,size_t(w),size_t(h));
                native_screenshot::force_opaque_alpha(rgba,size_t(w),size_t(h));
                auto output=make_file_output_stream(filename);
                store_image_png(output.get(),img);
                output.reset(); // flush/close before reporting success
                LOG_INFO("Screenshot PNG saved: ",filename.string(),
                         " (",w,"x",h,", final presented frame)");
            } catch(std::exception const &e) {
                LOG_WARNING("Screenshot PNG save failed: ",e.what());
            }
        }

        void end_frame()
        {
            bool const presenting_menu = menu_frame_pending ||
                                         mode==renderer_mode::menu;
            menu_frame_pending = false;
            bool const measure=the_config->profile_renderer &&
                               !presenting_menu;
            presentation_gpu_profile::completed_sample completed{};
            bool const gpu_timing=measure && present_gpu_queries.begin(completed);
            if(completed.ready) {
                for(size_t i=0;i<present_gpu_ns.size();++i)
                    present_gpu_ns[i]+=completed.nanoseconds[i];
                ++present_gpu_samples;
            }
            if(completed.skipped)++present_gpu_skipped;
            if(mode == renderer_mode::ingame && !the_config->profile_renderer) {
                log_mat_links(); // Don't add diagnostic scans to profiling overhead.
            }
            // Resolve the scene, e.g. the final MSAA color blit.
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_resolve)]);
                if(gpu_timing)present_gpu_queries.timestamp(0U,true);
                frame_screen->end_frame();
                if(gpu_timing)present_gpu_queries.timestamp(0U,false);
            }
            // Optional depth/peripheral blur occurs before bloom, color processing
            // and frame blending. Keep the source texture untouched for no-blur mode.
            gl::texture_view scene = frame_screen->get_resolved_final_texture();

            if(presenting_menu) {
                deferred_gpu_3do_instances.clear();
                clear_deferred_gpu_hud_materials();
                deferred_gpu_3do_camera={};
                // Menus are palette-sRGB imagery drawn into our linear screen
                // buffer. Present directly: GL_FRAMEBUFFER_SRGB performs the
                // usual linear->sRGB conversion, but there is no game bloom,
                // grading, vignette, grain, exposure, blur or frame trails.
                // Skipping the compositor also avoids the bloom GPU work.
                ogs->retro_mode_active=false;
                ogs->auto_exposure_scene=false;
                ogs->auto_exposure_history_valid=false;
                ogs->frame_blend_history_valid=false;
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                     gl::default_framebuffer);
                gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
                gl::disable(gl::capability::blend);
                gl::disable(gl::capability::depth_test);
                gl::disable(gl::capability::cull_face);
                gl::set_depth_mask(false);
                // The fullscreen quad has positions only; its shader generates UVs.
                ogs->menu_present_program.use_program();
                ogs->menu_present_program.set_menu_sampler(0);
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,scene);
                gl::bind_vertex_array(ogs->postmdl.vao);
                gl::draw_elements(gl::element_type::triangles,
                    ogs->postmdl.num_indices,gl::index_type::uint32);
                gl::set_depth_mask(true);
                capture_pending_screenshot_png();
                SwapBuffers(hDC);
                begin_frame();
                return;
            }
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_blur)]);
                if(gpu_timing)present_gpu_queries.timestamp(1U,true);
                if(mode == renderer_mode::ingame) {
                    scene = apply_spatial_blur(
                        ogs.get(), conf_scr_res, scene,
                        frame_screen->get_resolved_depth_texture());
                }
                if(gpu_timing)present_gpu_queries.timestamp(1U,false);
            }
            // Retro Mode consumes the exact in-memory RGB888 entries from JK's
            // identity CMP (+0x30, NOT the file's +0x40 palette offset).
            // Compare this pointer against SithWorld::colormaps[0] and dump
            // every RGB byte on first load/change; log rows can be compared to
            // the original CMP with tools/verify_retro_cmp.py.
            {
            profile_scope cpu(measure,profile_frame.timings[
                profile_idx(profile_stage::present_retro)]);
            ogs->retro_mode_active=false;
            if(mode==renderer_mode::ingame && the_config->retro_mode) {
                if(!engine_material_debug::matches_tested_executable()) {
                    if(!ogs->retro_read_failure_logged)
                        LOG_WARNING("Retro CMP verify: unsupported EXE; palette read disabled");
                    ogs->retro_read_failure_logged=true;
                }
                else {
                uint32_t identity_cmp=0;
                retro_palette_rgb palette{};
                if(engine_material_debug::read_object(0x0073A414U,identity_cmp) &&
                   identity_cmp>=0x10000U &&
                   engine_material_debug::read_bytes(
                       uintptr_t(identity_cmp)+jk_cmp_colors_offset,
                       palette.data(),palette.size())) {
                    ogs->retro_read_failure_logged=false;
                    uint32_t world_addr=0,world_cmp0=0;
                    engine_material_debug::world_header world{};
                    bool const world_readable=
                        engine_material_debug::read_object(
                            engine_material_debug::current_world_global,world_addr) &&
                        world_addr>=0x10000U &&
                        engine_material_debug::read_object(world_addr,world) &&
                        world.num_colormaps>0 && world.num_colormaps<=256 &&
                        world.colormaps>=0x10000U;
                    if(world_readable) world_cmp0=world.colormaps;
                    bool const palette_changed=!ogs->retro_lut_valid ||
                        ogs->retro_source_palette!=palette;
                    if(!ogs->retro_diagnostic_logged || palette_changed ||
                       ogs->retro_logged_identity_cmp!=identity_cmp ||
                       ogs->retro_logged_world_cmp!=world_cmp0 ||
                       ogs->retro_logged_world_address!=world_addr) {
                        LOG_INFO("Retro CMP verify: identity_addr=",identity_cmp,
                                 " current_world_addr=",world_addr,
                                 " world_cmp0_addr=",world_cmp0);
                        LOG_INFO("Retro CMP verify: identity_FNV1a32=",
                                 retro_palette_fnv1a(palette));
                        retro_palette_rgb world_palette{};
                        bool const world_palette_readable=world_readable &&
                            engine_material_debug::read_bytes(
                                uintptr_t(world_cmp0)+jk_cmp_colors_offset,
                                world_palette.data(),world_palette.size());
                        if(world_palette_readable) {
                            unsigned mismatches=0;
                            for(size_t k=0;k<palette.size();k++)
                                if(palette[k]!=world_palette[k]) ++mismatches;
                            LOG_INFO("Retro CMP verify: world=",
                                     engine_material_debug::printable_name(
                                         world.level_name,sizeof(world.level_name)),
                                     " colormaps=",world.num_colormaps,
                                     " same_pointer=",identity_cmp==world_cmp0,
                                     " different_RGB_bytes=",mismatches,
                                     " world_FNV1a32=",retro_palette_fnv1a(world_palette));
                        }
                        else {
                            LOG_WARNING("Retro CMP verify: current world's first CMP cannot be read");
                        }
                        // 16 x 16 RGB entries, 48 bytes per line, in original
                        // palette order. Emitted only on load/change, not per frame.
                        for(int first=0;first<256;first+=16)
                            LOG_INFO("Retro CMP #0 RGB[",first,"]:",
                                     retro_palette_hex_row(palette,first));
                        ogs->retro_diagnostic_logged=true;
                        ogs->retro_logged_identity_cmp=identity_cmp;
                        ogs->retro_logged_world_cmp=world_cmp0;
                        ogs->retro_logged_world_address=world_addr;
                    }
                    if(palette_changed) {
                        auto pixels=build_retro_palette_atlas(palette);
                        gl::set_active_texture_unit(2);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         ogs->retro_lut_texture);
                        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
                            gl::texture_internal_format::rgba,
                            make_size(retro_lut_width,retro_lut_height),
                            gl::texture_pixel_format::rgba,
                            gl::texture_pixel_type::uint8,
                            make_span(pixels).as_const_bytes());
                        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                                   gl::min_filter::nearest);
                        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                                   gl::mag_filter::nearest);
                        gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
                        ogs->retro_source_palette=palette;
                        ogs->retro_lut_valid=true;
                        LOG_INFO("Retro CMP #0 palette lookup refreshed");
                    }
                    ogs->retro_mode_active=ogs->retro_lut_valid;
                }
                else if(!ogs->retro_read_failure_logged) {
                    LOG_WARNING("Retro CMP verify: identity CMP pointer/palette unreadable; full-color fallback");
                    ogs->retro_read_failure_logged=true;
                }
                }
            }
            }
            // Only meter in-game scenes. Avoid menu exposure and preserve
            // exact Retro CMP color presentation.
            ogs->auto_exposure_scene=(mode==renderer_mode::ingame) &&
                                      !ogs->retro_mode_active;
            // Preserve the old fast direct-to-screen path unless Retro is
            // active. One RGBA16F staging texture is allocated lazily only
            // for Retro so the HUD stays exempt from all earlier effects.
            if(ogs->retro_mode_active && !ogs->retro_present_buffer)
                ogs->retro_present_buffer=std::make_unique<post_buffer>(
                    conf_scr_res);
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_composite)]);
                if(gpu_timing)present_gpu_queries.timestamp(2U,true);
                frame_compositor->end_frame(ogs.get(),conf_scr_res,scene);
                draw_post_exempt();
                // Keep DirectDraw HUD out of HDR, bloom, grading, blur and
                // trails. It overlays Camera Thing HUDs, but Retro is the
                // ONLY effect applied afterwards, to the complete image.
                if(mode==renderer_mode::ingame) {
                    profile_scope hud_cpu(measure,profile_frame.timings[
                        profile_idx(profile_stage::hud_draw)]);
                    draw_hud();
                }
                // Never quantize menus (their independent presentation path
                // returned earlier). Exactly one final LUT pass in gameplay.
                present_retro_after_hud();
                if(gpu_timing)present_gpu_queries.timestamp(2U,false);
            }
            if(the_config->planar_reflection_debug_view &&
               planar_capture_ready && planar_capture_target &&
               planar_capture_target->ready()) {
                // Raw FBO inspection, after HUD/post/Retro: never alters
                // the actual reflection texture or samples screen depth.
                GLint old_draw=0,old_read=0,old_read_buffer=0;
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
                glGetIntegerv(GL_READ_BUFFER,&old_read_buffer);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                  planar_capture_target->framebuffer());
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
                glBlitFramebuffer(0,0,planar_capture_target->width(),
                    planar_capture_target->height(),0,0,
                    get<x>(conf_scr_res),get<y>(conf_scr_res),
                    GL_COLOR_BUFFER_BIT,GL_LINEAR);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(old_read));
                glReadBuffer(GLenum(old_read_buffer));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(old_draw));
            }
            if(gpu_timing)present_gpu_queries.end();
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_swapbuffers)]);
                capture_pending_screenshot_png();
                SwapBuffers(hDC);
            }
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_beginframe)]);
                begin_frame();
            }
        }

        void show_directplay_dialog() override
        {
            ShowWindow(hWnd, SW_HIDE);
        }

        void restore_from_directplay_dialog() override
        {
            ShowWindow(hWnd, SW_SHOW);
        }

        HRESULT enumerate_devices(LPDDENUMCALLBACKA cb, LPVOID lpContext) override
        {
            // Emit only a single device, the default system device
            std::string fullname = "JkGfxMod OpenGL Device";
            std::string shortname = "device";
            cb(NULL, &fullname[0], &shortname[0], lpContext);

            return DD_OK;
        }

        void set_menu_source(HGDIOBJ ho, char const *indexed_bitmap) override
        {
            indexed_bitmap_dc = ho;
            indexed_bitmap_source = indexed_bitmap;
        }

        void maybe_clear_menu_source(HGDIOBJ ho) override
        {
            if(ho == indexed_bitmap_dc) {
                indexed_bitmap_dc = NULL;
                indexed_bitmap_source = nullptr;
            }
        }

        void set_menu_palette(UINT start, span<RGBQUAD const> entries) override
        {
            UINT curr = start;
            for(auto const &em : entries) {
                if(curr > 255U) {
                    break;
                }

                indexed_bitmap_colors[curr++] =
                    color_rgba8(em.rgbRed, em.rgbGreen, em.rgbBlue, uint8_t(255U));
            }
        }

        void present_menu_gdi() override
        {
            poll_audio_modules();
            menu_frame_pending=true;
            if(!indexed_bitmap_source) {
                end_frame();
                return;
            }

            // Copy new data from menu source
            if(indexed_bitmap_source) {
                for(size_t idx = 0U; idx < ogs->menu_texture_data.size(); ++idx) {
                    uint8_t index = indexed_bitmap_source[idx];
                    ogs->menu_texture_data[idx] = indexed_bitmap_colors[index];
                }
            }

            // Blit texture data into texture
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->menu_texture);
            gl::tex_sub_image_2d(gl::texture_bind_target::texture_2d,
                                 0,
                                 make_box(make_point(0, 0), make_point(640, 480)),
                                 gl::texture_pixel_format::rgba,
                                 gl::texture_pixel_type::uint8,
                                 make_span(ogs->menu_texture_data).as_const_bytes());

            // Render
            gl::enable(gl::capability::blend);
            gl::disable(gl::capability::depth_test);

            ogs->menu_program.use_program();
            ogs->menu_program.set_menu_sampler(0);

            gl::bind_vertex_array(ogs->menumdl.vao);
            gl::draw_elements(
                gl::element_type::triangles, ogs->menumdl.num_indices, gl::index_type::uint32);

            end_frame();
        }

        void present_menu_surface_body()
        {
            poll_audio_modules();
            menu_frame_pending=true;
            // Copy new data from menu source
            for(size_t idx = 0U; idx < ogs->menu_texture_data.size(); ++idx) {
                uint8_t index = ddraw1_primary_menu_surface.buffer[idx];
                auto &palent = ddraw1_palette.entries[index];
                ogs->menu_texture_data[idx] = ddraw1_palette.srgb_entries[index];
            }

            // Blit texture data into texture
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->menu_texture);
            gl::tex_sub_image_2d(gl::texture_bind_target::texture_2d,
                                 0,
                                 make_box(make_point(0, 0), make_point(640, 480)),
                                 gl::texture_pixel_format::rgba,
                                 gl::texture_pixel_type::uint8,
                                 make_span(ogs->menu_texture_data).as_const_bytes());

            // Render
            gl::enable(gl::capability::blend);
            gl::disable(gl::capability::depth_test);

            ogs->menu_program.use_program();
            ogs->menu_program.set_menu_sampler(0);

            gl::bind_vertex_array(ogs->menumdl.vao);
            gl::draw_elements(
                gl::element_type::triangles, ogs->menumdl.num_indices, gl::index_type::uint32);

            end_frame();
        }

        void present_menu_surface_immediate() override
        {
            menu_prev_ticks = std::chrono::high_resolution_clock::now();
            menu_curr_ticks = menu_prev_ticks;
            menu_accumulator = 0.0;

            present_menu_surface_body();
        }

        void present_menu_surface_delayed() override
        {
            // Present menu, emulating a 60 Hz monitor
            menu_prev_ticks = menu_curr_ticks;
            menu_curr_ticks = std::chrono::high_resolution_clock::now();
            auto elapsed_ticks = menu_curr_ticks - menu_prev_ticks;
            double elapsed = std::chrono::duration<double>(elapsed_ticks).count();

            menu_accumulator += elapsed;
            if(menu_accumulator >= (1.0 / 60.0)) {
                menu_accumulator = 0.0;
                present_menu_surface_body();
            }
        }

        static constexpr int hud_texture_num_slabs = 4;

        int curr_slab = 0;
        // Mirror the contents of each actual OpenGL HUD texture. Front/back
        // swap every four frames; their contents can differ. Comparing against
        // just the previous source bitmap would therefore be incorrect.
        std::array<std::vector<color_rgba8>,2> hud_uploaded_pixels;
        std::array<std::array<bool,hud_texture_num_slabs>,2> hud_slab_valid{};
        unsigned hud_back_index = 1U; // hud_texture_a=front, hud_texture_b=back
        void update_hud_texture()
        {
            if(curr_slab == 0) {
                // Convert the source bitmap once every four frames.
                // Every output pixel is overwritten, so no ZeroMemory needed.
                for(size_t i=0;i<ddraw1_backbuffer_surface.buffer.size();++i) {
                    auto const in_em=ddraw1_backbuffer_surface.buffer[i];
                    ogs->hud_texture_data[i]=rgb565_key_to_srgb_a8(
                        in_em,in_em==ddraw1_backbuffer_surface.color_key);
                }
            }

            int const width=get<x>(internal_scr_res);
            int const slab_rows=get<y>(internal_scr_res)/hud_texture_num_slabs;
            int const start_row=slab_rows*curr_slab;
            int const stop_row=curr_slab==hud_texture_num_slabs-1
                               ? get<y>(internal_scr_res)
                               : slab_rows*(curr_slab+1);
            int first_changed=start_row;
            int last_changed=stop_row;

            auto &mirror=hud_uploaded_pixels[hud_back_index];
            if(mirror.size()!=ogs->hud_texture_data.size()) {
                mirror.resize(ogs->hud_texture_data.size(),color_rgba8::zero());
                hud_slab_valid[hud_back_index].fill(false);
            }
            if(hud_slab_valid[hud_back_index][curr_slab]) {
                // Find the first and last changed rows; keep exactly one
                // contiguous upload. This avoids making many tiny GL calls
                // for scattered UI pixels while shrinking unchanged slabs.
                first_changed=stop_row;
                last_changed=start_row;
                size_t const row_bytes=size_t(width)*sizeof(color_rgba8);
                for(int row=start_row;row<stop_row;++row) {
                    size_t const offset=size_t(row)*size_t(width);
                    if(std::memcmp(ogs->hud_texture_data.data()+offset,
                                   mirror.data()+offset,row_bytes)!=0) {
                        first_changed=std::min(first_changed,row);
                        last_changed=row+1;
                    }
                }
            }

            if(first_changed<last_changed) {
                // Texture mirrors track the actual back texture, not the
                // previous source bitmap, so alternating front/back buffers
                // never cause stale pixels or skipped updates.
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 ogs->hud_texture_back);
                auto const region=make_box(make_point(0,first_changed),
                                           make_point(width,last_changed));
                size_t const offset=size_t(first_changed)*size_t(width);
                size_t const num_pixels=size_t(last_changed-first_changed)*size_t(width);
                gl::tex_sub_image_2d(
                    gl::texture_bind_target::texture_2d,
                    /*level*/ 0,region,gl::texture_pixel_format::rgba,
                    gl::texture_pixel_type::uint8,
                    make_span(ogs->hud_texture_data)
                        .subspan(offset,num_pixels).as_const_bytes());
                std::memcpy(mirror.data()+offset,
                            ogs->hud_texture_data.data()+offset,
                            num_pixels*sizeof(color_rgba8));
            }
            hud_slab_valid[hud_back_index][curr_slab]=true;

            ++curr_slab;
            if(curr_slab>=hud_texture_num_slabs) {
                // Preserve JKGM's original four-slab presentation latency and
                // backbuffer clear regardless of which rows needed upload.
                std::fill(ddraw1_backbuffer_surface.buffer.begin(),
                          ddraw1_backbuffer_surface.buffer.end(),
                          ddraw1_backbuffer_surface.color_key);
                std::swap(ogs->hud_texture_front,ogs->hud_texture_back);
                hud_back_index^=1U;
                curr_slab=0;
            }
        }

        void draw_hud()
        {
            // Keep the DirectDraw HUD out of scene tone mapping, bloom and
            // temporal history. In Retro Mode it *must* be included in the
            // final CMP palette quantization, after Camera-type HUD Things.
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                 ogs->retro_mode_active && ogs->retro_present_buffer
                     ? ogs->retro_present_buffer->fbo
                     : gl::default_framebuffer);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            gl::enable(gl::capability::blend);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::disable(gl::capability::depth_test);

            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->hud_texture_front);

            // Render
            ogs->menu_program.use_program();
            ogs->menu_program.set_menu_sampler(0);

            gl::bind_vertex_array(ogs->hudmdl.vao);
            gl::draw_elements(
                gl::element_type::triangles, ogs->hudmdl.num_indices, gl::index_type::uint32);
        }

        // Retro quantization is the VERY LAST gameplay draw, following
        // scene effects, trails, GPU Camera-type HUD and DirectDraw HUD.
        // All pixels are sampled in linear light, converted to sRGB for the
        // 64^3 LUT lookup, then written in the CMP's exact encoded RGB8.
        void present_retro_after_hud()
        {
            if(!ogs->retro_mode_active || !ogs->retro_present_buffer)return;
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            gl::disable(gl::capability::blend);
            gl::disable(gl::capability::depth_test);
            gl::disable(gl::capability::cull_face);
            gl::set_depth_mask(false);
            ogs->post_frame_blend.use_program();
            ogs->post_frame_blend.set_current_sampler(0);
            ogs->post_frame_blend.set_history_sampler(1);
            ogs->post_frame_blend.set_blend_strength(0.0f);
            ogs->post_frame_blend.set_retro_palette(2,true);
            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             ogs->retro_lut_texture);
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             ogs->retro_present_buffer->tex);
            gl::bind_vertex_array(ogs->postmdl.vao);
            // The LUT already stores sRGB8 encoded CMP colors.
            gl::disable(gl::capability::framebuffer_srgb);
            gl::draw_elements(gl::element_type::triangles,
                ogs->postmdl.num_indices,gl::index_type::uint32);
            gl::enable(gl::capability::framebuffer_srgb);
            gl::set_depth_mask(true);
        }

        void update_current_batch()
        {
            if(is_gun && is_transparent) {
                current_triangle_batch = &gun_transparent_batch;
            }
            else if(is_gun) {
                current_triangle_batch = &gun_batch;
            }
            else if(is_transparent) {
                current_triangle_batch = &world_transparent_batch;
            }
            else {
                current_triangle_batch = &world_batch;
            }
        }

        // Filter overrides are set at bind time, so materials sharing a cached image
        // can independently request a pixelated appearance.
        void bind_color_texture(gl::texture_view tex, int unit, bool filtered)
        {
            gl::set_active_texture_unit(unit);
            gl::bind_texture(gl::texture_bind_target::texture_2d, tex);
            if(tex.value() == 0U) {
                return;
            }
            gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                       filtered ? gl::mag_filter::linear : gl::mag_filter::nearest);
            gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                       filtered ? gl::min_filter::linear_mipmap_linear
                                                : gl::min_filter::nearest_mipmap_nearest);
            gl::set_texture_max_anisotropy(gl::texture_bind_target::texture_2d,
                                           filtered ? std::max(1.0f, the_config->max_anisotropy)
                                                    : 1.0f);
        }

        material_blend_mode get_material_blend_mode(material_instance_id id) const
        {
            if(id.get() == 0U) {
                return material_blend_mode::normal;
            }
            return vidmem_texture_surfaces.at(id.get() - 1)->blend_mode;
        }

        bool is_special_material(material_instance_id id) const
        {
            if(id.get() == 0U) {
                return false;
            }
            auto const &mat = vidmem_texture_surfaces.at(id.get() - 1);
            return mat->blend_mode != material_blend_mode::normal;
        }

        // Compute UV offsets in double precision before reducing them to
        // one texture repeat. This avoids large shader-time precision loss
        // during long game sessions (and supports negative scroll speeds).
        point<2, float> scroll_offset(point<2, float> speed) const
        {
            return make_point(
                static_cast<float>(std::fmod(static_cast<double>(get<0>(speed)) *
                                             animation_seconds, 1.0)),
                static_cast<float>(std::fmod(static_cast<double>(get<1>(speed)) *
                                             animation_seconds, 1.0)));
        }

        // Stateless hash: the same material and step produce the same random
        // value regardless of FPS, rendering order, or number of draw calls.
        static float pulse_random(size_t material_id, int64_t step)
        {
            uint64_t x = static_cast<uint64_t>(material_id) * 0x9E3779B97F4A7C15ULL +
                         static_cast<uint64_t>(step) * 0xD1B54A32D192ED03ULL;
            x ^= x >> 30;
            x *= 0xBF58476D1CE4E5B9ULL;
            x ^= x >> 27;
            x *= 0x94D049BB133111EBULL;
            x ^= x >> 31;
            return static_cast<float>(x >> 40) / 16777216.0f;
        }

        float pulse_multiplier(material_emissive_pulse const &pulse,
                               material_instance_id id) const
        {
            double const cycles = animation_seconds / pulse.rate + pulse.phase;
            double const step_d = std::floor(cycles);
            auto const step = static_cast<int64_t>(step_d);
            float const fraction = static_cast<float>(cycles - step_d);
            float level = 1.0f;
            switch(pulse.wave) {
            case material_pulse_wave::sine:
                // One full smooth brightness cycle every 'rate' seconds.
                level = 0.5f - 0.5f * std::cos(fraction * 6.283185307179586f);
                break;
            case material_pulse_wave::square:
                level = fraction < 0.5f ? 1.0f : 0.0f;
                break;
            case material_pulse_wave::flicker:
                level = pulse_random(id.get(), step) < 0.5f ? 0.0f : 1.0f;
                break;
            case material_pulse_wave::random: {
                float const a = pulse_random(id.get(), step);
                float const b = pulse_random(id.get(), step + 1);
                float const smooth = fraction * fraction * (3.0f - 2.0f * fraction);
                level = a + (b - a) * smooth;
                break;
            }
            }
            return pulse.min + (pulse.max - pulse.min) * level;
        }

        enum class batch_blend_filter { regular, all, special };

        // Opaque screen-door coverage is meaningful only for ordinary
        // over/under compositing. Special blend modes remain translucent.
        bool is_dithered_material(material_instance_id id) const
        {
            if(id.get()==0U) return false;
            auto const &mat=vidmem_texture_surfaces.at(id.get()-1);
            return mat && mat->alpha_mode==material_alpha_mode::dithered &&
                   mat->blend_mode==material_blend_mode::normal;
        }

        void bind_material(shaders::game_pass_shader *prog,
                           material_instance_id id,
                           bool force_opaque,
                           bool posterize_lighting)
        {
            if(the_config->profile_renderer) ++profile_frame.binds;
            // Only the transparent shader actually declares blend_mode; setting
            // it on the opaque shader is ignored (uniform location -1).
            // Native triangle batches only: custom GPU-resident decals, flares,
            // post passes and menus never inherit this raster state.
            set_native_depth_bias(id.get()!=0U
                ? vidmem_texture_surfaces.at(id.get()-1)->depth_bias : 0.0f);
            auto const blend_mode = get_material_blend_mode(id);
            prog->set_blend_mode(static_cast<int>(blend_mode));
            prog->set_fluid_thickness(
                id.get() != 0U ? vidmem_texture_surfaces.at(id.get() - 1)->fluid_thickness
                               : 0.25f);

            if(prog == &ogs->game_transparency_pass_program) {
                // Normal/fluid use premultiplied alpha (ONE, 1-SRC_ALPHA).
                // Additive/multiply preserve framebuffer alpha independently.
                switch(blend_mode) {
                case material_blend_mode::additive:
                    gl::set_blend_function_separate(
                        gl::blend_function::one, gl::blend_function::one,
                        gl::blend_function::zero, gl::blend_function::one);
                    break;
                case material_blend_mode::multiply:
                    gl::set_blend_function_separate(
                        gl::blend_function::destination_color, gl::blend_function::zero,
                        gl::blend_function::zero, gl::blend_function::one);
                    break;
                default:
                    gl::set_blend_function(
                        gl::blend_function::one,
                        gl::blend_function::one_minus_source_alpha);
                    break;
                }
            }

            if(id.get() == 0U) {
                // This is the default (untextured) material
                gl::set_active_texture_unit(4);
                gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);
                gl::set_active_texture_unit(3);
                gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);
                gl::set_active_texture_unit(2);
                gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);
                gl::set_active_texture_unit(1);
                gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);

                // Enable features
                prog->set_features(make_point(/*has albedo map*/ 0.0f,
                                              /*has emissive map*/ 0.0f,
                                              /*alpha mode*/ 0.0f,
                                              (posterize_lighting ? 1.0f : 0.0f)));

                // Albedo factor
                prog->set_albedo_factor(color::fill(1.0f));

                // Emissive factor; default material has no animation.
                prog->set_emissive_factor(color_rgb::zero());
                prog->set_uv_offsets(point<2, float>::zero(),
                                     point<2, float>::zero());

                // Displacement factor
                prog->set_displacement_factor(0.0f);

                // Alpha cutoff
                prog->set_alpha_cutoff(0.0f);

                // No normal maps or reflections.
                prog->set_normal_params(make_point(0.0f, 0.0f, 0.0f, 0.0f),
                                        point<2, float>::zero(),
                                        point<2, float>::zero());
                prog->set_reflection(make_point(0.0f, 0.0f, 0.0f, 0.0f),
                                     make_point(0.0f, 0.0f, 0.0f, 0.0f));
                prog->set_albedo_transition(8, make_point(0.0f, 0.0f, 0.0f, 0.0f));
                prog->set_env_transition(9, make_point(0.0f, 0.0f, 0.0f, 0.0f));
            }
            else {
                auto const &mat = vidmem_texture_surfaces.at(id.get() - 1);

                // Translate this D3D handle to the *native* JK MAT ID. One
                // material can span many cels/D3D surfaces.
                runtime_mat_override const *override_mat = nullptr;
                auto native_it = runtime_mat_by_handle.find(id.get());
                if(native_it != runtime_mat_by_handle.end() &&
                   native_it->second.surface == mat.get() &&
                   native_it->second.generation == mat->texture_generation) {
                    auto ctl_it = runtime_controls.per_mat.find(native_it->second.mat_id);
                    if(ctl_it != runtime_controls.per_mat.end()) {
                        override_mat = &ctl_it->second;
                    }
                }

                auto base_albedo = mat->albedo_map;
                auto from_albedo = base_albedo;
                auto to_albedo = base_albedo;
                float albedo_weight = 0.0f;
                if(override_mat && override_mat->albedo) {
                    resolve_runtime_transition(*override_mat->albedo, base_albedo,
                                               from_albedo, to_albedo, albedo_weight);
                }
                gl::texture_view albedo_map = from_albedo
                    ? texture_cache->get_texture_handle(*from_albedo)
                    : gl::default_texture;
                if(albedo_weight > 0.0f) {
                    gl::texture_view second = to_albedo
                        ? texture_cache->get_texture_handle(*to_albedo)
                        : gl::default_texture;
                    bind_color_texture(second, 8, mat->texture_filtering.value_or(
                        the_config->enable_texture_filtering));
                }
                prog->set_albedo_transition(
                    8, make_point(to_albedo ? 1.0f : 0.0f, albedo_weight,
                                  albedo_weight > 0.0f ? 1.0f : 0.0f, 0.0f));

                gl::texture_view emissive_map = gl::default_texture;
                if(mat->emissive_map.has_value()) {
                    emissive_map = texture_cache->get_texture_handle(*mat->emissive_map);
                }

                // A reflection/lighting master switch: normal maps currently
                // only affect environment and directional-sun specularity.
                // Do not fetch or bind these resources when they cannot contribute.
                bool const reflections_enabled =
                    the_config->enable_reflections && mat->reflection_strength > 0.0f;
                gl::texture_view normal_map = gl::default_texture;
                if(reflections_enabled && mat->normal_map.has_value()) {
                    normal_map = texture_cache->get_texture_handle(*mat->normal_map);
                }
                gl::texture_view normal_map2 = gl::default_texture;
                if(reflections_enabled && mat->normal_map2.has_value()) {
                    normal_map2 = texture_cache->get_texture_handle(*mat->normal_map2);
                }

                gl::texture_view displacement_map = gl::default_texture;
                if(mat->displacement_factor != 0.0f && mat->displacement_map.has_value()) {
                    displacement_map = texture_cache->get_texture_handle(*mat->displacement_map);
                }

                // Surface/specular maps are shared by envmap and sun GGX, and
                // have no purpose when the reflection master switch is off.
                gl::texture_view surface_map = gl::default_texture;
                if(reflections_enabled && mat->surface_map.has_value()) {
                    surface_map = texture_cache->get_texture_handle(*mat->surface_map);
                }

                // EnvMap precedence: explicit material map > runtime global
                // map > inherited pack default, with per-MAT COG override at
                // the top. Explicit maps are unaffected by a global command.
                auto env_from = mat->env_map;
                auto env_to = mat->env_map;
                float env_weight = 0.0f;
                bool env_command = false;
                bool const global_allowed = !mat->replacement_material ||
                    !mat->replacement_material->has_explicit_env_map;
                if(the_config->enable_reflections &&
                   global_allowed && runtime_controls.global_env) {
                    // Global EnvMap changes the reflected IMAGE, not each
                    // material's reflectivity. Otherwise matte materials with
                    // default 0 strength / 0 roughness become sharp mirrors.
                    resolve_runtime_transition(*runtime_controls.global_env,
                                               mat->env_map, env_from, env_to, env_weight);
                }
                if(the_config->enable_reflections &&
                   override_mat && override_mat->env) {
                    // A material reset returns to the ordinary/global target.
                    auto const global_target = env_weight >= 1.0f ? env_to :
                                               (env_weight > 0.0f ? env_to : env_from);
                    resolve_runtime_transition(*override_mat->env, global_target,
                                               env_from, env_to, env_weight);
                    env_command = override_mat->env->from.has_value() ||
                                  override_mat->env->to.has_value();
                }
                bool const has_env_map = env_from.has_value() || env_to.has_value();
                // Runtime reflection commands can activate previously matte
                // materials (neutral dielectric Fresnel, not metallic).
                float const reflection_strength =
                    mat->reflection_strength > 0.0f ? mat->reflection_strength :
                    (env_command && has_env_map ? 1.0f : 0.0f);
                bool const runtime_reflections = the_config->enable_reflections &&
                    reflection_strength > 0.0f;
                gl::texture_view env_map = gl::default_texture;
                if(runtime_reflections && env_from) {
                    env_map = texture_cache->get_texture_handle(*env_from);
                }
                if(runtime_reflections && env_weight > 0.0f && env_to) {
                    gl::set_active_texture_unit(9);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     texture_cache->get_texture_handle(*env_to));
                }
                prog->set_env_transition(
                    9, make_point(env_from ? 1.0f : 0.0f,
                                  env_to ? 1.0f : 0.0f, env_weight, 0.0f));

                // Units 6/7 are for linear normal textures; unit 5 is the
                // already established opaque scene-depth sampler for decals.
                bool filtered = mat->texture_filtering.value_or(the_config->enable_texture_filtering);
                // Inactive sampler units can retain old bindings: their feature
                // uniforms are zero, so GLSL never accesses those units.
                // Avoid OpenGL state changes for entirely unreflective materials.
                if(runtime_reflections) {
                    bind_color_texture(normal_map, 6, filtered);
                    bind_color_texture(normal_map2, 7, filtered);
                    gl::set_active_texture_unit(4);
                    gl::bind_texture(gl::texture_bind_target::texture_2d, env_map);
                    gl::set_active_texture_unit(3);
                    gl::bind_texture(gl::texture_bind_target::texture_2d, surface_map);
                }
                gl::set_active_texture_unit(2);
                gl::bind_texture(gl::texture_bind_target::texture_2d, displacement_map);
                // Filter selection is per material, not per cached image. Keep
                // surface/displacement/EnvMap sampling unchanged for reflection roughness.
                bind_color_texture(emissive_map, 1, filtered);
                bind_color_texture(albedo_map, 0, filtered);

                // Enable features
                prog->set_features(make_point(
                    from_albedo.has_value() ? 1.0f : 0.0f,
                    mat->emissive_map.has_value() ? 1.0f : 0.0f,
                    is_dithered_material(id) ? 2.0f
                    : ((mat->alpha_mode==material_alpha_mode::mask) && !force_opaque)
                        ? 1.0f : 0.0f,
                    (posterize_lighting ? 1.0f : 0.0f)));

                auto material_color = mat->albedo_factor;
                if(override_mat && override_mat->color) {
                    auto rgb = override_mat->color->value();
                    material_color = color(get<0>(material_color) * rgb[0],
                                           get<1>(material_color) * rgb[1],
                                           get<2>(material_color) * rgb[2],
                                           get<3>(material_color));
                }
                prog->set_albedo_factor(material_color);
                prog->set_uv_offsets(scroll_offset(mat->albedo_scroll),
                                     scroll_offset(mat->emissive_scroll));
                prog->set_normal_params(
                    make_point(mat->normal_strength, mat->normal_strength2,
                               (reflections_enabled && mat->normal_map.has_value()) ? 1.0f : 0.0f,
                               (reflections_enabled && mat->normal_map2.has_value()) ? 1.0f : 0.0f),
                    scroll_offset(mat->normal_scroll),
                    scroll_offset(mat->normal_scroll2));

                // Pulse affects emissive_factor whether or not there is an
                // emissive_map. Nonemissive terms are never animated here.
                float const emissive_intensity = mat->emissive_pulse.has_value()
                    ? pulse_multiplier(*mat->emissive_pulse, id) : 1.0f;
                auto const &ef = mat->emissive_factor;
                prog->set_emissive_factor(color_rgb(
                    get<0>(ef) * emissive_intensity,
                    get<1>(ef) * emissive_intensity,
                    get<2>(ef) * emissive_intensity));
                // A factor without a displacement image must never enter the 32–128-sample parallax loop.
                prog->set_displacement_factor(mat->displacement_map.has_value()
                                                  ? mat->displacement_factor : 0.0f);
                prog->set_alpha_cutoff(mat->alpha_cutoff);

                // Zero reflection strength also suppresses directional sunlight
                // GGX highlights (previously unaffected by enable_reflections).
                prog->set_reflection(make_point(runtime_reflections ? reflection_strength : 0.0f,
                                                mat->roughness,
                                                mat->metalness,
                                                (reflections_enabled && mat->surface_map.has_value()) ? 1.0f : 0.0f),
                                     make_point((runtime_reflections && has_env_map) ? 1.0f : 0.0f,
                                                static_cast<float>(static_cast<int>(mat->reflection_mode)),
                                                0.0f, 0.0f));
            }

            current_material = id;
        }

        void draw_batch(shaders::game_pass_shader *prog,
                        triangle_batch const &tb,
                        triangle_buffer_model *trimdl,
                        bool force_opaque,
                        bool posterize_lighting,
                        batch_blend_filter filter = batch_blend_filter::regular)
        {
            gl::bind_vertex_array(trimdl->vao);

            // Preserve the offsets of the original per-batch vertex buffer
            // even when a blend material is deliberately skipped.
            size_t curr_offset = 0U;
            size_t num_verts = 0U;
            bind_material(prog, material_instance_id(0U), force_opaque, posterize_lighting);

            for(auto const &tri : tb) {
                bool special = is_special_material(tri.material);
                bool selected = filter == batch_blend_filter::all ||
                                (filter == batch_blend_filter::special ? special : !special);
                // All native world/gun transparent batches are initially
                // classified by JK, not by replacement metadata. A dithered
                // replacement was already drawn depth-writing in the opaque
                // world pass, or during the gun's force-opaque pass after
                // its depth clear. Never draw it a second time as alpha blend.
                if(prog==&ogs->game_transparency_pass_program &&
                   !force_opaque && is_dithered_material(tri.material))
                    selected=false;
                if(!selected) {
                    if(num_verts > 0) {
                        if(the_config->profile_renderer) ++profile_frame.draw_batches;
                gl::draw_arrays(gl::element_type::triangles, curr_offset, num_verts);
                        curr_offset += num_verts;
                        num_verts = 0U;
                    }
                    curr_offset += 3U;
                    continue;
                }

                if(num_verts == 0U || current_material != tri.material) {
                    if(num_verts > 0) {
                        if(the_config->profile_renderer) ++profile_frame.draw_batches;
                gl::draw_arrays(gl::element_type::triangles, curr_offset, num_verts);
                        curr_offset += num_verts;
                        num_verts = 0U;
                    }
                    bind_material(prog, tri.material, force_opaque, posterize_lighting);
                }
                num_verts += 3U;
            }

            if(num_verts > 0) {
                if(the_config->profile_renderer) ++profile_frame.draw_batches;
                gl::draw_arrays(gl::element_type::triangles, curr_offset, num_verts);
            }
            // A native batch must not leak polygon offset into GPU sprite
            // decals, post-processing, HUD, or menu rendering.
            set_native_depth_bias(0.0f);
        }

        // Engine sideband normal is stored in view axes x=right, y=forward, z=up.
        // Convert a normal using the inverse-transpose of the projection scale:
        // our shader later reverses this same scale to recover view-space normals.
        static direction<3, float> unpack_model_normal(uint32_t packed,
                                                        float hscale, float vscale)
        {
            float nx = (float)( packed        & 0x3ffU) / 511.5f - 1.0f;
            float nf = (float)((packed >> 10) & 0x3ffU) / 511.5f - 1.0f;
            float nu = (float)((packed >> 20) & 0x3ffU) / 511.5f - 1.0f;
            return normalize(make_direction(nx * hscale, nu * vscale, nf));
        }

        void fill_buffer(triangle_batch const &tb, triangle_buffer_model *mdl)
        {
            // Opt-in: empty native batches need no GL map/unmap. Allocate
            // populated GPU buffers from used counts rather than the CPU
            // batch's 10,000-triangle reserve.
            if(the_config->experimental_native_triangle_uploads &&
               tb.size()==0U) {
                mdl->num_vertices=0;
                return;
            }
            mdl->maybe_grow_buffers(
                (the_config->experimental_native_triangle_uploads
                    ?tb.size():tb.capacity())*3);

            auto *vx = mdl->mmio.data();
            auto const cam = read_engine_camera();
            float hscale = 1.0f; // 90 degree horizontal-FOV fallback
            if(cam.valid) {
                float half_fov = cam.fov * 0.5f * 3.14159265358979f / 180.0f;
                hscale = std::tan(half_fov);
            }
            float aspect = get<x>(internal_scr_res_f) / get<y>(internal_scr_res_f);
            float vscale = hscale / aspect;
            for(auto const &tri : tb) {
                bool use_native_normals = (tri.v0.nativeNormal & 0x80000000u) &&
                                          (tri.v1.nativeNormal & 0x80000000u) &&
                                          (tri.v2.nativeNormal & 0x80000000u);
                vx->pos = tri.v0.pos;
                vx->texcoords = tri.v0.texcoords;
                vx->col = tri.v0.color;
                vx->normal = use_native_normals
                                 ? unpack_model_normal(tri.v0.nativeNormal, hscale, vscale)
                                 : tri.normal;

                ++vx;

                vx->pos = tri.v1.pos;
                vx->texcoords = tri.v1.texcoords;
                vx->col = tri.v1.color;
                vx->normal = use_native_normals
                                 ? unpack_model_normal(tri.v1.nativeNormal, hscale, vscale)
                                 : tri.normal;

                ++vx;

                vx->pos = tri.v2.pos;
                vx->texcoords = tri.v2.texcoords;
                vx->col = tri.v2.color;
                vx->normal = use_native_normals
                                 ? unpack_model_normal(tri.v2.nativeNormal, hscale, vscale)
                                 : tri.normal;

                ++vx;
            }

            mdl->num_vertices = tb.size() * 3;
            mdl->update_buffers();
        }



        gpu_3do_texture resolve_gpu_3do_solid_material(
            gpu_3do_group const &group,bool camera_hud)
        {
            gpu_3do_texture out{};
            out.albedo_source=2; // No texture; shader multiplies palette RGB.
            if(!group.material)return out;
            using namespace engine_material_debug;
            native_material mat{};
            if(!matches_tested_executable() ||
               !read_object(group.material,mat) ||
               mat.num_texinfo==0 || mat.num_texinfo>16) {
                out.albedo_source=0;
                return out;
            }
            int cel=group.cel<0?static_cast<int>(mat.current_cel):group.cel;
            cel=std::clamp(cel,0,static_cast<int>(mat.num_texinfo)-1);
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info)) {
                out.albedo_source=0;
                return out;
            }
            // rdTexinfoHeader::solidColor is word #1 (offset +4).
            uint32_t index=0;
            std::memcpy(&index,info.header+4,sizeof(index));
            uint32_t cmp=0;
            constexpr uintptr_t active_cmp=0x0073A410U;
            constexpr uintptr_t identity_cmp=0x0073A414U;
            if(index>=256U ||
               !read_object(camera_hud?identity_cmp:active_cmp,cmp) ||
               cmp<0x10000U) {
                out.albedo_source=0;
                return out;
            }
            uint8_t rgb[3]{};
            if(!read_bytes(uintptr_t(cmp)+jk_cmp_colors_offset+
                           uintptr_t(index)*3U,rgb,sizeof(rgb))) {
                out.albedo_source=0;
                return out;
            }
            // Native MAT solid colors retain their exact 8-bit palette
            // index. Detect unshaded entries from the owning CMP's full
            // 64-level software table; never guess fullbright from RGB.
            jk_cmp_lighttable levels{};
            std::array<uint8_t,256U*3U> palette{};
            if(jk_cmp_read_lighttable(read_bytes,cmp,levels) &&
               read_bytes(uintptr_t(cmp)+jk_cmp_colors_offset,
                          palette.data(),palette.size()))
                out.solid_fullbright=jk_cmp_is_fullbright(
                    levels,palette,static_cast<uint8_t>(index));
            out.tint=make_point(float(rgb[0])/255.0f,
                                float(rgb[1])/255.0f,
                                float(rgb[2])/255.0f,1.0f);
            return out;
        }

        // Diagnostics only: native MAT residency is required before the
        // world-cache resolver can access Fluid/normal/EnvMap replacement data.
        // Do NOT call JK's native MAT uploader from rendering: the uploader
        // has its own device/cache lifetime and an unsafe frame-time path.
        std::string world_cache_material_failure(
            world_cache_draw_candidate const &face)
        {
            using namespace engine_material_debug;
            std::ostringstream reason;
            reason<<"surface="<<face.surface_index
                  <<" mat_ptr="<<face.material<<" cel="<<face.cel
                  <<" geo="<<face.geometry_mode
                  <<" face_type="<<face.face_type;
            if(!matches_tested_executable()) {
                reason<<" stage=executable_signature_mismatch";
                return reason.str();
            }
            native_material mat{};
            if(!read_object(face.material,mat)) {
                reason<<" stage=unreadable_native_MAT";
                return reason.str();
            }
            reason<<" name="<<printable_name(mat.name,sizeof(mat.name))
                  <<" id="<<mat.id<<" tex_type="<<mat.tex_type
                  <<" num_texinfo="<<mat.num_texinfo;
            if((mat.tex_type&2U)==0U) {
                reason<<" stage=native_MAT_untextured";
                return reason.str();
            }
            if(mat.num_texinfo==0 || mat.num_texinfo>16) {
                reason<<" stage=invalid_texinfo_count";
                return reason.str();
            }
            int const cel=std::clamp(face.cel<0
                ?static_cast<int>(mat.current_cel):face.cel,
                0,static_cast<int>(mat.num_texinfo)-1);
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info) || !info.texture) {
                reason<<" stage=missing_texinfo_texture cel_selected="<<cel;
                return reason.str();
            }
            native_texture tex{};
            if(!read_object(info.texture,tex)) {
                reason<<" stage=unreadable_native_texture";
                return reason.str();
            }
            reason<<" mip_count="<<tex.header[6]
                  <<" has_alpha_flag="<<((tex.header[0]&1U)?1:0);
            unsigned const max_mips=std::min(tex.header[6],4U);
            bool resident=false,valid_handle=false;
            size_t selected_handle=0;
            for(unsigned i=0;i<max_mips;++i) {
                auto const &alpha=tex.alpha_mips[i];
                auto const &opaque=tex.opaque_mips[i];
                if(i==0) {
                    reason<<" alpha_loaded="<<alpha.texture_loaded
                          <<" alpha_handle="<<alpha.texture_handle
                          <<" opaque_loaded="<<opaque.texture_loaded
                          <<" opaque_handle="<<opaque.texture_handle;
                }
                auto const valid=[](native_surface const &sf) {
                    return sf.texture_loaded!=0 && sf.texture_handle>0 &&
                           sf.width>0 && sf.height>0 &&
                           sf.width<=8192 && sf.height<=8192;
                };
                native_surface const *chosen=nullptr;
                if((tex.header[0]&1U)!=0 && valid(alpha))chosen=&alpha;
                else if(valid(opaque))chosen=&opaque;
                else if(valid(alpha))chosen=&alpha;
                if(!chosen)continue;
                resident=true;
                auto const id=static_cast<size_t>(chosen->texture_handle);
                selected_handle=id;
                valid_handle=id>0 && id<=vidmem_texture_surfaces.size() &&
                             bool(vidmem_texture_surfaces[id-1]);
                reason<<" chosen_mip="<<i<<" chosen_handle="<<id
                      <<" gpu_handle_valid="<<(valid_handle?1:0);
                break;
            }
            if(max_mips==0)reason<<" stage=no_native_mips";
            else if(!resident)reason<<" stage=unloaded_native_texture";
            else if(!valid_handle)reason<<" stage=missing_GPU_texture_handle";
            else {
                // The native surface can be resident with no GPU albedo
                // because the replacement deliberately uses tint/reflection
                // and Fluid absorption without an albedo map. Report the
                // actual renderer material, not just the native D3D handle.
                auto const &gpu=vidmem_texture_surfaces[selected_handle-1];
                bool const has_albedo=gpu->albedo_map.has_value();
                bool const replacement=gpu->replacement_material!=nullptr;
                bool const intentionally_no_albedo=replacement &&
                    !gpu->replacement_material->use_original_albedo &&
                    !gpu->replacement_material->albedo_map.has_value();
                reason<<" gpu_albedo="<<(has_albedo?1:0)
                      <<" replacement="<<(replacement?1:0)
                      <<" replacement_no_albedo="<<(intentionally_no_albedo?1:0)
                      <<" blend_mode="<<static_cast<int>(gpu->blend_mode)
                      <<" fluid_thickness="<<gpu->fluid_thickness;
                reason<<" stage="<<(has_albedo?"resident_with_albedo_but_unresolved"
                    :(intentionally_no_albedo?"intentional_no_albedo":
                      "missing_GPU_albedo"));
            }
            return reason.str();
        }

        gpu_3do_texture resolve_gpu_3do_material(gpu_3do_group const &group)
        {
            gpu_3do_texture out;
            // Material-less 3DO faces are legitimate: they are untextured,
            // not a missing or failed texture lookup. Their exact flat/vertex
            // colors will be handled with the upcoming GPU lighting path.
            if(group.material==0) {
                out.albedo_source=2;
                return out;
            }
            if(!engine_material_debug::matches_tested_executable()) return out;

            using namespace engine_material_debug;
            native_material mat{};
            if(!read_object(group.material,mat)) return out;
            // The native MAT may explicitly select flat, untextured geometry.
            if((mat.tex_type&2U)==0) {
                out.albedo_source=2;
                return out;
            }
            if(mat.num_texinfo==0 || mat.num_texinfo>16) return out;

            int32_t cel = group.cel < 0
                ? static_cast<int32_t>(mat.current_cel) : group.cel;
            cel = std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info) || !info.texture) return out;
            native_texture tex{};
            if(!read_object(info.texture,tex)) return out;
            // Original MAT alpha bits do not tell us whether a replacement
            // PNG has translucent texels; sample uploaded image metadata.
            out.alpha=false;
            auto const valid_surface=[](native_surface const &sf) {
                return sf.texture_loaded!=0 && sf.texture_handle>0 &&
                       sf.width>0 && sf.height>0 &&
                       sf.width<=8192 && sf.height<=8192;
            };
            native_surface const *chosen=nullptr;
            unsigned mip=0;
            for(unsigned i=0;i<std::min(tex.header[6],4U);i++) {
                auto const &alpha=tex.alpha_mips[i];
                auto const &opaque=tex.opaque_mips[i];
                if((tex.header[0]&1U)!=0 && valid_surface(alpha)) chosen=&alpha;
                else if(valid_surface(opaque)) chosen=&opaque;
                else if(valid_surface(alpha)) chosen=&alpha;
                if(chosen) { mip=i; break; }
            }
            if(!chosen) {
                // Missing native MAT cels must not require a visible native
                // fallback draw. Ask the tick-bound uploader to warm them.
                if(the_config && the_config->enable_gpu_mat_warmup)
                    jkgm::get_native_mat_warm_queue().request(group.material,cel);
                return out;
            }
            auto const id=static_cast<size_t>(chosen->texture_handle);
            if(id==0 || id>vidmem_texture_surfaces.size()) return out;
            auto const &surface=vidmem_texture_surfaces[id-1];
            if(!surface) return out;
            // JK-assisted and GPU 3DO paths share the same stable native
            // MAT ID. Validate D3D handle generation before trusting links.
            uint32_t native_id=mat.id;
            auto handle_it=runtime_mat_by_handle.find(id);
            if(handle_it!=runtime_mat_by_handle.end() &&
               handle_it->second.surface==surface.get() &&
               handle_it->second.generation==surface->texture_generation)
                native_id=handle_it->second.mat_id;
            runtime_mat_override const *ctl=nullptr;
            auto ctl_it=runtime_controls.per_mat.find(native_id);
            if(ctl_it!=runtime_controls.per_mat.end()) ctl=&ctl_it->second;
            // A replacement can deliberately have no albedo map, e.g.
            // emissive-only brake lights with albedo_factor 0 0 0 0.
            // Do not discard the *entire* material and its emissive map.
            auto from_albedo=surface->albedo_map;
            auto to_albedo=from_albedo;
            float albedo_weight=0.0f;
            if(ctl && ctl->albedo)
                resolve_runtime_transition(*ctl->albedo,surface->albedo_map,
                                           from_albedo,to_albedo,albedo_weight);
            if(from_albedo) {
                out.albedo_id=from_albedo;
                out.texture=texture_cache->get_texture_handle(*from_albedo);
                out.alpha=texture_cache->has_nonopaque_alpha(*from_albedo);
                out.albedo_source=1;
                out.has_image=true;
            } else out.albedo_source=2; // intentionally untextured
            if(albedo_weight>0.0f) {
                out.albedo_transition=albedo_weight;
                if(to_albedo) {
                    out.albedo2_id=to_albedo;
                    out.texture2=texture_cache->get_texture_handle(*to_albedo);
                    out.albedo_source2=1;
                    out.alpha=out.alpha ||
                        texture_cache->has_nonopaque_alpha(*to_albedo);
                } else out.albedo_source2=2;
            }
            out.masked=surface->alpha_mode==material_alpha_mode::mask;
            out.dithered=surface->alpha_mode==material_alpha_mode::dithered;
            out.blend=surface->blend_mode;
            out.fluid_thickness=surface->fluid_thickness;
            if(the_config->enable_parallax && surface->displacement_map &&
               surface->displacement_factor!=0.0f) {
                out.displacement=texture_cache->get_texture_handle(
                    *surface->displacement_map);
                out.displacement_factor=surface->displacement_factor;
            }
            out.special=surface->blend_mode!=material_blend_mode::normal;
            out.filtered=surface->texture_filtering.value_or(
                the_config->enable_texture_filtering);
            out.alpha_cutoff=surface->alpha_cutoff;
            auto const &tint=surface->albedo_factor;
            out.tint=make_point(get<0>(tint),get<1>(tint),
                                get<2>(tint),get<3>(tint));
            if(ctl && ctl->color) {
                auto const rgb=ctl->color->value();
                out.tint=make_point(get<0>(tint)*rgb[0],get<1>(tint)*rgb[1],
                                    get<2>(tint)*rgb[2],get<3>(tint));
            }
            out.opacity=get<3>(tint);
            out.albedo_scroll=scroll_offset(surface->albedo_scroll);
            out.emissive_scroll=scroll_offset(surface->emissive_scroll);
            if(surface->emissive_map) {
                out.emission_id=surface->emissive_map;
                out.emission=texture_cache->get_texture_handle(*surface->emissive_map);
                out.has_emission=true;
            }
            float emission_strength=surface->emissive_pulse
                ? pulse_multiplier(*surface->emissive_pulse,surface->material_id)
                : 1.0f;
            auto const &em=surface->emissive_factor;
            out.emission_factor=make_point(get<0>(em)*emission_strength,
                                           get<1>(em)*emission_strength,
                                           get<2>(em)*emission_strength,0.0f);
            // An emissive-only face contributes light but *no opaque base*.
            // Route it through the blended pass with alpha=0 and independent
            // emissive RGB, so the background remains visible behind it.
            out.emissive_only=gpu_3do_emissive_only(
                out.opacity,get<0>(out.emission_factor),
                get<1>(out.emission_factor),get<2>(out.emission_factor));
            out.has_authored_specular=surface->replacement_material &&
                surface->replacement_material->has_explicit_specular_settings;
            // Opt-in GPU 3DO material specularity: compute only for
            // reflective replacements and active native Thing lights.
            auto env_from=surface->env_map;
            auto env_to=env_from;
            float env_weight=0.0f;
            bool env_command=false;
            if(the_config->enable_reflections &&
               (!surface->replacement_material ||
                !surface->replacement_material->has_explicit_env_map) &&
               runtime_controls.global_env) {
                // Only the texture is global. Reflection strength remains a
                // property of the actual material (or an explicit MAT verb).
                resolve_runtime_transition(*runtime_controls.global_env,
                                           surface->env_map,env_from,env_to,env_weight);
            }
            if(the_config->enable_reflections && ctl && ctl->env) {
                auto const global_target=env_weight>0.0f?env_to:env_from;
                resolve_runtime_transition(*ctl->env,global_target,
                                           env_from,env_to,env_weight);
                env_command=ctl->env->from.has_value() ||
                            ctl->env->to.has_value();
            }
            bool const has_env=env_from.has_value()||env_to.has_value();
            float const strength=surface->reflection_strength>0
                ? surface->reflection_strength :
                ((env_command && has_env)?1.0f:0.0f);
            // Default specularity can use authored *normal* maps even when
            // no reflection strength/map is specified. Do not bind an
            // inherited pack/global EnvMap just because this experiment
            // enables a weak direct-light GGX term.
            bool const default_normals=is_default_specular_enabled() &&
                !out.has_authored_specular;
            if(the_config->enable_reflections &&
               (strength>0.0f || default_normals)) {
                out.reflection_strength=strength;
                out.roughness=surface->roughness;
                out.metalness=surface->metalness;
                out.normal_strength=surface->normal_strength;
                out.normal_strength2=surface->normal_strength2;
                out.normal_scroll=scroll_offset(surface->normal_scroll);
                out.normal_scroll2=scroll_offset(surface->normal_scroll2);
                out.reflection_mode=static_cast<int>(surface->reflection_mode);
                if(strength>0.0f && env_from) {
                    out.environment=texture_cache->get_texture_handle(*env_from);
                    out.has_environment=true;
                }
                if(strength>0.0f && env_weight>0.0f) {
                    out.environment_transition=env_weight;
                    if(env_to) {
                        out.environment2=texture_cache->get_texture_handle(*env_to);
                        out.has_environment2=true;
                    }
                }
                if(surface->normal_map2) {
                    out.normal2=texture_cache->get_texture_handle(*surface->normal_map2);
                    out.has_normal2=true;
                }
                if(surface->surface_map) {
                    out.surface=texture_cache->get_texture_handle(*surface->surface_map);
                    out.has_surface=true;
                }
                if(surface->normal_map) {
                    out.normal=texture_cache->get_texture_handle(*surface->normal_map);
                    out.has_normal=true;
                }
            }
            // GPU upload has erased the original 8-bit MAT indices.
            // Use the read-only rdTexture source mip; do NOT infer from
            // RGB565/RGBA5551, which can alias multiple palette indices.
            bool const original_indexed_albedo=
                mat.format[0]==0U &&
                (!surface->replacement_material ||
                 (surface->replacement_material->use_original_albedo &&
                  !surface->replacement_material->albedo_map.has_value())) &&
                !(ctl && ctl->albedo) && from_albedo.has_value() &&
                albedo_weight<=0.0f;
            // Native JK hardware does not preserve CMP fullbright pixels
            // in textured MATs. Keep that behavior unless explicitly opted
            // in; GeoMode 3 solid colors remain fullbright independently.
            if(the_config->restore_cmp_lights &&
               original_indexed_albedo && tex.mipmap_buffers[mip]!=0U) {
                uint32_t cmp=0;
                constexpr uintptr_t active_cmp=0x0073A410U;
                if(read_object(active_cmp,cmp) &&
                   cmp>=0x10000U && cmp<0x7ff00000U) {
                    auto it=surface->palette_fullbright_masks.find(cmp);
                    if(it==surface->palette_fullbright_masks.end()) {
                        std::vector<char> mask;
                        std::optional<srgb_texture_id> image;
                        if(jk_cmp_build_fullbright_mask(
                               read_bytes,cmp,tex.mipmap_buffers[mip],
                               chosen->width,chosen->height,
                               (tex.header[0]&1U)!=0U,
                               static_cast<uint8_t>(tex.header[2]),mask))
                            image=texture_cache->create_srgb_texture_from_buffer(
                                make_size(static_cast<int>(chosen->width),
                                          static_cast<int>(chosen->height)),
                                make_span(mask).as_const_bytes());
                        it=surface->palette_fullbright_masks.emplace(
                            cmp,image).first;
                    }
                    if(it->second) {
                        out.palette_fullbright_mask=
                            texture_cache->get_texture_handle(*it->second);
                        out.has_palette_fullbright=true;
                    }
                }
            }
            // Preserve the native MAT's effective original mip-0 size.
            // Its UV reference size is clamped to 256 at draw submission,
            // rather than changing shared source geometry or decal UVs.
            out.width=static_cast<float>(chosen->width << mip);
            out.height=static_cast<float>(chosen->height << mip);
            return out;
        }

        // Direct-light default applies only to GPU caching and *only* to
        // materials without authored specular settings. It never enables
        // an EnvMap or alters an explicitly authored roughness/strength.
        std::pair<float,float> gpu_specular_parameters(
            gpu_3do_texture const &material,bool fallback_eligible) const
        {
            if(fallback_eligible && the_config->enable_reflections &&
               the_config->enable_default_specular &&
               the_config->default_specular_strength>0.0f &&
               material.reflection_strength<=0.0f &&
               !material.has_authored_specular) {
                return {the_config->default_specular_strength,
                        the_config->default_specular_roughness};
            }
            return {material.reflection_strength,material.roughness};
        }

        // Snapshot the rdCamera that rendered THIS scene. The exempt pass
        // executes at the next present_game() (after post processing), when
        // the engine may already have advanced the camera or switched modes.
        // Reusing that live camera would project retained 3DO GUI geometry
        // against the previous scene's depth with a different view matrix.
        gpu_preview_frame_camera capture_gpu_preview_camera()
        {
            gpu_preview_frame_camera frame{};
            frame.camera=read_active_render_camera();
            if(!frame.camera.valid ||
               !read_active_rd_camera_view(frame.native_view))
                return frame;

            constexpr uintptr_t current_camera=0x0073A418U;
            if(engine_camera_detail::readable(current_camera,sizeof(uintptr_t))) {
                auto const camera_ptr=*reinterpret_cast<uintptr_t const *>(current_camera);
                if(camera_ptr>=0x10000U &&
                   engine_camera_detail::readable(camera_ptr+0x48,sizeof(uintptr_t))) {
                    auto const frustum_ptr=*reinterpret_cast<uintptr_t const *>(camera_ptr+0x48);
                    if(frustum_ptr>=0x10000U &&
                       engine_camera_detail::readable(
                           frustum_ptr,sizeof(frame.frustum))) {
                        std::memcpy(&frame.frustum,
                                    reinterpret_cast<void const *>(frustum_ptr),
                                    sizeof(frame.frustum));
                        if(std::isfinite(frame.frustum.far_plane) &&
                           frame.frustum.far_plane>0.1f)
                            frame.far_plane=frame.frustum.far_plane;
                        if(engine_camera_detail::readable(camera_ptr,sizeof(int32_t))) {
                            int32_t project_type=0;
                            std::memcpy(&project_type,
                                        reinterpret_cast<void const *>(camera_ptr),
                                        sizeof(project_type));
                            frame.perspective=project_type==1;
                        }
                    }
                }
            }
            frame.valid=true;
            return frame;
        }

        // Allocate lazily: baseline builds never create the texture, stencil,
        // FBO, or composite shader. A missing stencil FBO fails open to the
        // original full-resolution draw, never to invisible smoke.
        bool ensure_smoke_half_target()
        {
            if(smoke_half_failed || !ogs || !ogs->post_smoke_composite)
                return false;
            if(smoke_half_buffer) return true;
            auto const dims=make_size(
                std::max(1,(get<x>(conf_scr_res)+1)/2),
                std::max(1,(get<y>(conf_scr_res)+1)/2));
            auto buffer=std::make_unique<post_buffer>(dims);
            glGenRenderbuffers(1,&smoke_half_stencil);
            glBindRenderbuffer(GL_RENDERBUFFER,smoke_half_stencil);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_STENCIL_INDEX8,
                                  get<x>(dims),get<y>(dims));
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,buffer->fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER,smoke_half_stencil);
            GLenum const status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindRenderbuffer(GL_RENDERBUFFER,0);
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
            if(status!=GL_FRAMEBUFFER_COMPLETE) {
                LOG_WARNING("SMOKE half-res unavailable: framebuffer status=",
                            static_cast<unsigned>(status),
                            " falling_back=full_resolution");
                glDeleteRenderbuffers(1,&smoke_half_stencil);
                smoke_half_stencil=0;
                smoke_half_failed=true;
                return false;
            }
            smoke_half_buffer=std::move(buffer);
            LOG_INFO("SMOKE half-res ready dimensions=",get<x>(dims),
                     "x",get<y>(dims),
                     " budget=",the_config->fluid_sprite_layer_budget,
                     " NOTE=fluid_SPR_only_back_to_front_no_world_changes");
            return true;
        }

        // Separate opaque and alpha passes. Native JK no longer processes
        // individual faces in either path: only GPU indexed ranges remain.
        void draw_gpu_3do_preview(bool transparent,bool post_exempt=false,
                                  bool decals_only=false)
        {
            auto const &instances=post_exempt ? deferred_gpu_3do_instances
                                              : frame_gpu_3do_instances;
            if(!ogs->gpu_3do_preview_program || instances.empty())
                return;
            if(decals_only &&
               std::none_of(instances.begin(),instances.end(),
                   [](gpu_3do_instance const &inst){return inst.is_projected_decal;}))
                return;
            bool const gpu_profiling=the_config->profile_renderer;
            profile_scope gpu_profiler(gpu_profiling,
                                       profile_frame.gpu_preview_cpu_us);
            size_t const pass_index=transparent?1U:0U;
            if(gpu_profiling)++profile_frame.gpu_preview_calls[pass_index];
            // Ordinary opaque/alpha passes share one camera snapshot.
            // Post-exempt ranges use exactly THAT frame's camera, although
            // they are presented after post-processing on the next tick.
            if(!post_exempt && !frame_gpu_3do_camera.valid)
                frame_gpu_3do_camera=capture_gpu_preview_camera();
            auto const &frame_camera=post_exempt
                ? deferred_gpu_3do_camera : frame_gpu_3do_camera;
            if(!frame_camera.valid) return;
            // An SPR is coplanar with its receiver after clipping. Small
            // numerical differences across 3DO triangle edges can cause
            // the ordinary depth test to expose the receiver as thin seams.
            // Prefer decal polygons by a couple of depth-buffer units only
            // in the decal-only pass; the shader still rejects unrelated
            // surfaces by comparing original receiver-plane depth.
            if(decals_only) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f,-2.0f);
            }
            auto const &cam=frame_camera.camera;
            auto const &native_view=frame_camera.native_view;
            float const far_plane=frame_camera.far_plane;
            auto const &native_frustum=frame_camera.frustum;
            bool const have_perspective_frustum=frame_camera.perspective;
            auto &prog=*ogs->gpu_3do_preview_program;
            prog.use_program();
            prog.set_specular_normal_mode(
                the_config->experimental_specular_normal_mode);
            prog.set_smoke_half_resolution(false);
            prog.set_light_brightness_limit(
                authored_light_limit(the_config->enable_baked_overbright));
            // Persistent GL program: reset instancing before every draw pass,
            // including when the opt-in flag has just been disabled.
            {
                std::array<std::array<float,12>,
                    shaders::gpu_3do_preview_shader::max_sprite_instances> none{};
                prog.set_sprite_instances(none,0);
            }
            // With the JSON option OFF, set both sampler type and zero
            // candidates once per pass, not once for every cached model.
            if(!the_config->enable_sector_crossing_lighting)
                prog.set_sector_crossing(nullptr,false,false,false);
            bool const crossing_buffer_ready=
                the_config->enable_sector_crossing_lighting &&
                !post_exempt && !decals_only &&
                sector_crossing_plane_texture!=0U;
            if(crossing_buffer_ready) {
                glActiveTexture(GL_TEXTURE0+
                    gpu_3do_sector_plane_texture_unit);
                glBindTexture(GL_TEXTURE_BUFFER,sector_crossing_plane_texture);
                glActiveTexture(GL_TEXTURE0);
            }
            // Both experiments reconstruct world XYZ from the
            // same camera-space vertex and these camera axes.
            bool const continuous_buffer_ready=
                continuous_ambient_gpu_enabled_this_frame &&
                !post_exempt && !decals_only &&
                continuous_ambient_key_texture!=0U &&
                continuous_ambient_value_texture!=0U;
            if(continuous_buffer_ready) {
                glActiveTexture(GL_TEXTURE0+
                    texture_units::forward_continuous_key_buffer);
                glBindTexture(GL_TEXTURE_BUFFER,continuous_ambient_key_texture);
                glActiveTexture(GL_TEXTURE0+
                    texture_units::forward_continuous_value_buffer);
                glBindTexture(GL_TEXTURE_BUFFER,continuous_ambient_value_texture);
                glActiveTexture(GL_TEXTURE0);
            }
            // Sparse ambient and sector crossing share the world-space transform.
            if(crossing_buffer_ready ||
               continuous_ambient_gpu_enabled_this_frame)
                prog.set_sector_crossing_camera_origin(cam.position);
            set_translucent_spotlights(prog,transparent && !post_exempt &&
                                      !decals_only);
            // Mask directional GGX in both opaque and translucent passes.
            set_translucent_sun(prog,!post_exempt && !decals_only);
            prog.set_scene_depth(8);
            prog.set_post_exempt(post_exempt);
            prog.set_decal_receiver_lighting(decals_only,
                texture_units::forward_decal_light,
                texture_units::forward_decal_occlusion);
            if(decals_only) {
                // Unit 10 is the spotlight's sampler2DShadow, even if it
                // is inactive. Reuse 2D displacement unit 9: decals disable
                // parallax and receive JK light from opaque emissive alpha.
                gl::set_active_texture_unit(
                    texture_units::forward_decal_light);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_emissive_texture());
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_ao->get_ssao_texture());
                gl::set_active_texture_unit(0);
            }
            prog.set_advanced_material(false,0.25f);
            prog.set_parallax(0.0f);
            if(transparent) {
                // The GPU alpha pass samples the already completed opaque
                // R16F depth image, never the framebuffer being written.
                gl::set_active_texture_unit(8);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
            }
            // No overhead from per-material GGX state for nonreflective maps.
            // Each pass resets shader state once; individual draws only
            // update the extra uniforms when actually reflective.
            prog.set_normal_mapped_diffuse(
                the_config->enable_normal_mapped_diffuse);
            prog.set_dynamic_light_specular(
                the_config->enable_reflections &&
                the_config->enable_dynamic_light_specular);
            prog.set_point_specular(
                make_point(0.0f,0.5f,0.0f,0.0f),
                make_point(0.0f,1.0f,0.0f,0.0f),
                make_point(0.0f,0.0f),make_point(0.0f,0.0f));
            prog.set_environment(false,false,0.0f,0);
            prog.set_albedo_transition(2,0.0f);
            prog.set_camera_axes(cam.right,cam.forward,cam.up);
            auto const &sun=get_sun_settings();
            auto const sun_direction=normalized_sun_direction(sun.direction.value());
            auto const sun_color=sun.color.value();
            auto const sun_project=[&](std::array<float,3> const &axis) {
                return sun_direction[0]*axis[0]+sun_direction[1]*axis[1]+
                       sun_direction[2]*axis[2];
            };
            // Match cached-world direct specular: disabling the COG sun
            // disables its GGX highlights too, independently of EnvMaps.
            float const sun_power=(the_config->enable_reflections &&
                                   the_config->enable_sun &&
                                   sun_level_has_sky)
                ? std::max(0.0f,sun.intensity.value()[0]) : 0.0f;
            bool const sun_active=sun_power>0.0f;
            prog.set_sun(make_point(sun_project(cam.right),
                                    sun_project(cam.up),
                                    sun_project(cam.forward),1.0f),
                         make_point(sun_color[0],sun_color[1],sun_color[2],
                                    sun_power));
            float const aspect=get<x>(internal_scr_res_f)/get<y>(internal_scr_res_f);
            float const tan_half_fov=std::tan(cam.fov*0.008726646259971648f);
            prog.set_projection(make_point(tan_half_fov,aspect,1.0f/far_plane,0.0f));
            // Native D3D has already applied this to per-vertex colors.
            // Apply only to the GPU-bypassed 3DO vertex shader.
            auto const effect=read_native_jk_hardware_color_effects();
            // Native rdSetColorEffects has already removed the validated
            // sector request from the shared hardware effect. Everything
            // else (filters, fade, RGB adds, other tints) remains global.
            // Only apply local tint when the native bridge confirms that
            // this exact request was active. When COG frees it, mode==1.
            int const sector_palette_mode=native_sector_palette_mode().load(
                std::memory_order_relaxed);
            bool const can_replace_sector_tint=sector_palette_mode==2;
            if(the_config->profile_renderer &&
               !transparent && !post_exempt && !decals_only &&
               gpu_3do_present_serial%120U==0U) {
                size_t valid=0,eligible=0;
                for(auto const &inst:instances) {
                    if(inst.shadow_only || inst.camera_hud ||
                       inst.is_projected_decal)continue;
                    ++eligible;
                    if(inst.sector_tint_valid)++valid;
                }
                LOG_INFO("THING SECTOR TINT bridge_mode=",
                         sector_palette_mode,
                         " replace=",can_replace_sector_tint?1:0,
                         " eligible=",eligible," local_valid=",valid,
                         " remaining_effect_active=",effect.active?1:0);
                LOG_INFO("THING SECTOR TINT remaining_effect_rgb=",
                         effect.tint[0],",",effect.tint[1],
                         ",",effect.tint[2]);
            }
            prog.set_jk_color_effects(
                make_point(float(effect.filter[0]),float(effect.filter[1]),
                           float(effect.filter[2]),effect.active?1.0f:0.0f),
                make_point(effect.tint[0],effect.tint[1],effect.tint[2],0.0f),
                make_point(effect.add[0]/255.0f,effect.add[1]/255.0f,
                           effect.add[2]/255.0f,effect.fade));
            // Same animated fog state used by JK's existing scene passes.
            {
                auto const &fog=get_fog_settings();
                auto const info=fog.info.value(),color=fog.color.value();
                prog.set_fog(make_point(info[0],info[1],
                                        the_config->enable_fog?info[2]:0.0f,0.0f),
                             make_point(color[0],color[1],color[2],0.0f));
            }
            struct pending_draw {
                gpu_3do_instance const *instance;
                gpu_3do_group const *group;
                // Borrow the material from the pass-local frame cache.
                // unordered_map element addresses survive rehash; both
                // the ordinary and retained HUD caches outlive this pass.
                gpu_3do_texture const *texture;
                gpu_3do_alpha_mode policy;
                gpu_3do_matrix34 const *modelview;
                float depth;
                size_t order;
                // Nonzero for opaque SPRs grouped at collection time.
                size_t sprite_batch_group=0;
            };
            // Opaque SPR draw state, excluding each Thing's transform.
            using sprite_batch_key=std::tuple<
                uint64_t,uintptr_t,int,float,float,float,float,uint32_t,
                bool,bool,bool,std::array<float,3>,std::array<float,3>,bool>;
            struct sprite_batch_bucket {
                std::vector<pending_draw> draws;
            };
            // Reuse pass-local CPU workspaces. Busy JkGR scenes may have
            // thousands of ranges in both opaque and alpha passes, so
            // rebuilding these vectors every frame only adds allocator work.
            // They are consumed entirely in this non-recursive draw call.
            static thread_local std::vector<pending_draw> draws;
            static thread_local std::vector<pending_draw> merged_draws;
            static thread_local std::vector<pending_draw> smoke_draws;
            // Opaque prepares the ordinary alpha list too. Keep its matrices
            // alive across the intervening projected-decal pass; deferred
            // Camera HUD and decals have independent scratch matrices.
            static thread_local std::vector<pending_draw> ordinary_alpha_draws;
            static thread_local std::vector<gpu_3do_matrix34> ordinary_modelviews;
            static thread_local std::vector<gpu_3do_matrix34> isolated_modelviews;
            static thread_local uint64_t ordinary_alpha_serial=0;
            static thread_local std::vector<sprite_batch_bucket> sprite_buckets;
            static thread_local std::map<sprite_batch_key,size_t> sprite_lookup;
            bool const ordinary_pass=!post_exempt && !decals_only;
            bool const prepare_both=ordinary_pass && !transparent;
            bool const reuse_alpha=ordinary_pass && transparent &&
                ordinary_alpha_serial!=0 &&
                ordinary_alpha_serial==gpu_3do_present_serial;
            if(prepare_both) {
                ordinary_alpha_serial=0;
                ordinary_alpha_draws.clear();
            }
            auto &modelviews=ordinary_pass
                ?ordinary_modelviews:isolated_modelviews;
            draws.clear();
            for(auto &bucket:sprite_buckets)bucket.draws.clear();
            sprite_lookup.clear();
            size_t used_sprite_buckets=0;
            size_t next_draw_order=0;
            bool const collect_sprite_batches=
                the_config->enable_gpu_sprite_instancing &&
                !transparent && !post_exempt && !decals_only;
            // Reusing alpha candidates must not clear their modelview
            // storage; otherwise a projected decal could invalidate it.
            if(!reuse_alpha) {
                modelviews.clear();
                modelviews.reserve(instances.size());
            }
            auto const collect_begin=gpu_profiling
                ?profile_clock::now():profile_clock::time_point{};
            if(!reuse_alpha) for(auto const &inst:instances) {
                if(!inst.geometry || inst.shadow_only ||
                   (!post_exempt && inst.is_projected_decal!=decals_only))
                    continue;
                modelviews.push_back(gpu_3do_native_modelview(native_view,inst.transform));
                std::array<float,3> local_eye{};
                bool const can_cpu_cull=gpu_3do_local_eye(modelviews.back(),local_eye);
                for(auto const &group:inst.geometry->groups) {
                    if(the_config->profile_renderer)
                        ++profile_frame.gpu_groups_considered;
                    // Native JK clips individual faces; test the bounds of
                    // whole cached draw ranges to avoid GL state work for
                    // entirely offscreen geometry. This is conservative:
                    // partially visible groups still reach OpenGL.
                    if(have_perspective_frustum &&
                       gpu_3do_batch_outside_frustum(
                           native_frustum,modelviews.back(),
                           group.bounds_min,group.bounds_max)) {
                        if(the_config->profile_renderer)
                            ++profile_frame.gpu_groups_frustum_culled;
                        continue;
                    }
                    // JK skips a face if dot(camera-local - anchor,normal)<=0,
                    // unless it is double-sided. Since opaque faces are batched
                    // by MAT, only skip a group when ALL its faces face away.
                    // This saves CPU-side GL draw calls, not just GPU fragments.
                    if(can_cpu_cull && !(group.face_type&0x1U) &&
                       group.plane_count>0 &&
                       group.plane_start+group.plane_count<=
                           inst.geometry->face_planes.size()) {
                        bool any_visible=false;
                        for(uint32_t p=group.plane_start;
                            p<group.plane_start+group.plane_count;p++) {
                            auto const &plane=inst.geometry->face_planes[p];
                            if(gpu_3do_face_facing_camera(
                                   local_eye,plane.anchor,plane.normal)) {
                                any_visible=true;
                                break;
                            }
                        }
                        if(!any_visible) {
                            if(the_config->profile_renderer)
                                ++profile_frame.gpu_groups_backface_culled;
                            continue;
                        }
                    }
                    // Cache MAT resolution per material/cel for the complete
                    // frame. Multiple cars using the same MAT no longer
                    // trigger thousands of redundant VirtualQuery lookups.
                    bool const solid=inst.effective_geometry_mode==3 ||
                        group.solid_color;
                    // Reserve two high bits of the per-frame material key
                    // for solid-mode and Camera HUD identity-CMP palette.
                    // The same MAT may be drawn textured and solid in one frame.
                    uint64_t const mat_key=
                        (uint64_t(group.material)<<32) |
                        uint32_t(group.cel) |
                        (solid?(uint64_t(1)<<63):0U) |
                        (solid && inst.camera_hud?(uint64_t(1)<<62):0U);
                    auto &mat_cache=post_exempt
                        ? deferred_gpu_hud_material_cache
                        : gpu_frame_material_cache;
                    auto found=mat_cache.find(mat_key);
                    if(found==mat_cache.end()) {
                        // The late Camera HUD pass must not dereference a
                        // native rdMaterial from the next engine tick.
                        // Missing entries were not visible in the originating
                        // scene pass, so skip instead of making a late lookup.
                        if(post_exempt) continue;
                        auto item=solid
                            ?resolve_gpu_3do_solid_material(group,inst.camera_hud)
                            :resolve_gpu_3do_material(group);
                        found=mat_cache.emplace(mat_key,std::move(item)).first;
                        if(the_config->profile_renderer)
                            ++profile_frame.gpu_material_resolves;
                    }
                    auto const &tex=found->second;
                    // Native MAT/D3D handles can be destroyed on the frame
                    // entering a 2D menu. Never show the GPU debug-magenta
                    // placeholder in a Camera HUD late pass: omit that
                    // unresolved draw until the material is resident again.
                    if(post_exempt && tex.albedo_source==0)
                        continue;
                    // Camera Things are an instance-level HUD layer. The
                    // same 3DO and MAT on an ordinary Thing remain in-scene.
                    if(!post_exempt && inst.camera_hud)
                        frame_has_exempt_gpu=true;
                    if(inst.camera_hud!=post_exempt) continue;
                    // Projected SPRs are always alpha-composited over
                    // the existing opaque architecture and depth-tested.
                    // The MAT may be fully opaque; its native SPR instance
                    // opacity (including SetSpriteInfo) is independent.
                    auto const material_policy=inst.is_projected_decal
                        ? gpu_3do_alpha_mode::blend_only
                        : choose_gpu_3do_alpha_mode(
                            (group.face_type&0x2U)!=0 ||
                                (inst.is_sprite_quad && inst.sprite_opacity<0.999f),
                            tex.alpha,tex.masked,
                            tex.special||tex.emissive_only,
                            tex.opacity,tex.dithered);
                    // A live saber SideMat can contain fully opaque core
                    // texels. The ordinary "mixed" route writes those to
                    // the opaque depth buffer, which then rejects the final
                    // joining triangle even if it is submitted last.
                    // Composite the COMPLETE active trail in the alpha pass
                    // with depth tests against world/other opaque surfaces,
                    // but no saber-to-saber depth writes. The 3D fan can now
                    // reliably cover both blades and the tip bridge.
                    auto const policy=gpu_3do_trail_alpha_mode(
                        material_policy,inst.saber_trail_active);
                    // Mixed-alpha faces belong to BOTH ordinary draw lists.
                    // Classify once without changing per-pass face ordering.
                    bool const opaque_eligible=prepare_both &&
                        gpu_3do_draws_in_pass(policy,false);
                    bool const alpha_eligible=prepare_both &&
                        gpu_3do_draws_in_pass(policy,true);
                    if(!post_exempt &&
                       !(prepare_both
                           ?(opaque_eligible || alpha_eligible)
                           :gpu_3do_draws_in_pass(policy,transparent)))
                        continue;
                    float depth=0.0f;
                    if(transparent || alpha_eligible) {
                        auto const &m=inst.transform;
                        auto const &c=group.centroid;
                        float const wx=m[9]+m[0]*c[0]+m[3]*c[1]+m[6]*c[2];
                        float const wy=m[10]+m[1]*c[0]+m[4]*c[1]+m[7]*c[2];
                        float const wz=m[11]+m[2]*c[0]+m[5]*c[1]+m[8]*c[2];
                        depth=(wx-cam.position[0])*cam.forward[0]+
                              (wy-cam.position[1])*cam.forward[1]+
                              (wz-cam.position[2])*cam.forward[2];
                    }
                    pending_draw candidate{&inst,&group,&tex,policy,
                                           &modelviews.back(),depth,
                                           next_draw_order++};
                    if(alpha_eligible)
                        ordinary_alpha_draws.push_back(candidate);
                    if(prepare_both && !opaque_eligible) continue;
                    // Ordinary opaque 3DOs and all transparent sprites stay
                    // on the original draw list; only compatible opaque
                    // sprite quads are collected in dedicated buckets.
                    bool const batchable_sprite=collect_sprite_batches &&
                        inst.is_sprite_quad && !inst.camera_hud &&
                        !inst.is_projected_decal && !inst.shadow_only &&
                        !inst.transient_procedural &&
                        inst.num_lights==0U && !inst.crossing_lighting &&
                        !tex.has_environment && !tex.has_environment2 &&
                        !tex.has_normal && !tex.has_normal2 &&
                        !tex.has_surface &&
                        tex.displacement_factor==0.0f &&
                        tex.blend!=material_blend_mode::fluid &&
                        gpu_specular_parameters(tex,false).first<=0.0f;
                    if(batchable_sprite) {
                        // One state-key insertion per sprite, instead of
                        // comparing all fields at every sort comparison.
                        // Include attenuation and skybox state too, since
                        // their uniforms must match within an instance draw.
                        sprite_batch_key key{
                            inst.id,reinterpret_cast<uintptr_t>(&group),
                            static_cast<int>(policy),inst.ambient,
                            inst.attenuation,inst.sprite_opacity,
                            inst.effective_light_mode,
                            inst.effective_geometry_mode,
                            inst.world_lighting_exempt,
                            inst.skybox_vertex_lighting,
                            inst.sector_tint_valid,inst.sector_tint,
                            inst.colormap_tint,
                            gpu_3do_reversed_winding(inst.transform)};
                        auto found=sprite_lookup.find(key);
                        size_t index;
                        if(found==sprite_lookup.end()) {
                            index=used_sprite_buckets++;
                            sprite_lookup.emplace(std::move(key),index);
                            if(sprite_buckets.size()<=index)
                                sprite_buckets.emplace_back();
                        } else index=found->second;
                        candidate.sprite_batch_group=index+1U;
                        sprite_buckets[index].draws.push_back(
                            std::move(candidate));
                    } else draws.push_back(std::move(candidate));
                }
            }
            // This is consumed once during the ordinary transparent pass.
            // Both the MAT element addresses and the ordinary modelviews
            // remain stable until then, including across decal rendering.
            if(prepare_both)
                ordinary_alpha_serial=gpu_3do_present_serial;
            if(reuse_alpha) {
                draws.swap(ordinary_alpha_draws);
                ordinary_alpha_serial=0;
            }
            if(gpu_profiling)
                profile_frame.gpu_preview_collect_us[pass_index]+=
                    elapsed_profile_us(collect_begin,profile_clock::now());
            auto const sort_begin=gpu_profiling
                ?profile_clock::now():profile_clock::time_point{};
            if(!transparent) {
                // Sprite groups were pre-keyed during collection. The
                // ordinary 3DO sorter no longer compares sprite state.
                // Opaque GPU 3DO draw ranges have no painter-order dependency.
                // Native JK batches faces by material before D3D submission;
                // our face-order grouping generated thousands of 1-2-triangle
                // GL calls on complex models. Reorder only the opaque pass
                // so nonadjacent ranges with identical material and face state
                // can be dispatched together in one glMultiDrawElements call.
                // Transparent/mixed-alpha fragments retain their existing
                // back-to-front path below.
                std::sort(draws.begin(),draws.end(),
                    [](pending_draw const &a,pending_draw const &b) {
                        if(a.instance!=b.instance)
                            return a.instance<b.instance;
                        if(a.group->material!=b.group->material)
                            return a.group->material<b.group->material;
                        if(a.group->cel!=b.group->cel)
                            return a.group->cel<b.group->cel;
                        if(a.group->face_type!=b.group->face_type)
                            return a.group->face_type<b.group->face_type;
                        if(a.group->nonplanar!=b.group->nonplanar)
                            return a.group->nonplanar<b.group->nonplanar;
                        if(a.group->solid_color!=b.group->solid_color)
                            return a.group->solid_color<b.group->solid_color;
                        return a.order<b.order;
                    });
                if(used_sprite_buckets>0U) {
                    // Opaque sprite quads were rendered before models in
                    // the previous sorter; retain that order. Assemble
                    // grouped quads in linear time, with no global resort.
                    merged_draws.clear();
                    size_t count=draws.size();
                    for(size_t i=0;i<used_sprite_buckets;++i)
                        count+=sprite_buckets[i].draws.size();
                    merged_draws.reserve(count);
                    for(size_t i=0;i<used_sprite_buckets;++i)
                        for(auto &item:sprite_buckets[i].draws)
                            merged_draws.push_back(std::move(item));
                    merged_draws.insert(merged_draws.end(),
                        std::make_move_iterator(draws.begin()),
                        std::make_move_iterator(draws.end()));
                    draws.swap(merged_draws);
                }
            }
            if(transparent && !(post_exempt &&
                                !the_config->camera_hud_depth_sort)) {
                // JK's own translucent polygons are depth-sorted. Our faces
                // need a GPU-path equivalent. Sorting index *ranges* does not
                // modify/re-upload any cached mesh vertices or indices.
                // In-place sort: MSVC x86 std::stable_sort uses an internal
                // aligned_storage buffer incompatible with our SIMD-aligned
                // point<> members. Preserve exact tie order explicitly.
                std::sort(draws.begin(),draws.end(),
                    [](pending_draw const &a,pending_draw const &b) {
                        if(a.depth==b.depth) return a.order<b.order;
                        return a.depth>b.depth; // back to front
                    });
            }
            // Composite special saber geometry in two explicit stages:
            // after the delayed/live blades and TipMat caps, draw the
            // SideMat bridge (virtual tip -> real tip), and LAST draw the
            // true 3D fan over everything else. Apply this only within
            // the same saber instance; ordinary model transparency still
            // uses the global back-to-front depth sort above.
            // Run separate passes so a previously moved fan cannot prevent
            // the bridge from being moved into its penultimate position.
            for(unsigned layer=0;layer<2U;++layer) {
                for(size_t i=0;i<draws.size();++i) {
                    auto const &candidate=draws[i];
                    if(!candidate.instance->saber_trail_active)continue;
                    bool const selected=layer==0U
                        ?candidate.group->saber_trail_tip_bridge
                        :candidate.group->saber_trail_join;
                    if(!selected)continue;
                    auto const *owner=candidate.instance;
                    size_t tail=i;
                    for(size_t j=i+1;j<draws.size();++j)
                        if(draws[j].instance==owner)tail=j;
                    if(tail>i)
                        std::rotate(draws.begin()+i,draws.begin()+i+1,
                                    draws.begin()+tail+1);
                }
            }
            if(gpu_profiling)
                profile_frame.gpu_preview_sort_us[pass_index]+=
                    elapsed_profile_us(sort_begin,profile_clock::now());
            // Preserve original back-to-front order within BOTH partitions.
            // Smoke is composed after the ordinary translucent 3DO pass.
            // The split never touches native world, gun, opaque, or cutouts.
            smoke_draws.clear();
            bool const split_smoke=transparent && ordinary_pass &&
                                   the_config->use_gpu_cache &&
                                   the_config->experimental_half_res_fluid_sprites &&
                                   ensure_smoke_half_target();
            // The lazy FBO constructor may bind its texture on the
            // previously active sampler unit (including scene-depth unit 8).
            // Restore opaque depth even when target creation failed and we
            // fall back to full-resolution smoke.
            if(transparent && ordinary_pass &&
               the_config->experimental_half_res_fluid_sprites) {
                gl::set_active_texture_unit(8);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
                gl::set_active_texture_unit(0);
            }
            if(split_smoke) {
                // FBO construction changes the binding on the first frame.
                frame_screen->begin_transparency_pass();
                size_t write=0;
                for(size_t read=0;read<draws.size();++read) {
                    auto &candidate=draws[read];
                    bool const fluid_sprite=candidate.instance->is_sprite_quad &&
                        !candidate.instance->camera_hud &&
                        !candidate.instance->is_projected_decal &&
                        candidate.texture->blend==material_blend_mode::fluid;
                    if(fluid_sprite)
                        smoke_draws.push_back(std::move(candidate));
                    else {
                        if(write!=read)draws[write]=std::move(candidate);
                        ++write;
                    }
                }
                draws.resize(write);
            }
            gpu_3do_instance const *active_instance=nullptr;
            bool prev_specular_active=false;
            bool prev_albedo_transition=false;
            bool prev_advanced_material=false;
            bool prev_parallax=false;
            // Filtering is texture-object state, not texture-unit state.
            // Repeated 3DO draw groups often use the exact same GL texture;
            // three glTexParameter calls for every bind become extremely
            // costly under Wine. Track only states WE applied within this
            // preview pass, since the other render passes may change GL
            // texture parameters between preview passes.
            //
            // If the same texture is used once filtered and once pixelated,
            // apply the change again: preserving per-MAT filtering is vital.
            static thread_local std::unordered_map<unsigned int,bool> filter_states;
            filter_states.clear();
            auto bind_preview_color=[&](gl::texture_view tex,int unit,bool filtered) {
                gl::set_active_texture_unit(unit);
                gl::bind_texture(gl::texture_bind_target::texture_2d,tex);
                if(tex.value()==0U)return;
                auto const found=filter_states.emplace(tex.value(),filtered);
                if(!found.second && found.first->second==filtered) {
                    if(gpu_profiling)++profile_frame.gpu_filter_state_skips;
                    return;
                }
                found.first->second=filtered;
                gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                    filtered?gl::mag_filter::linear:gl::mag_filter::nearest);
                gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                    filtered?gl::min_filter::linear_mipmap_linear
                            :gl::min_filter::nearest_mipmap_nearest);
                gl::set_texture_max_anisotropy(gl::texture_bind_target::texture_2d,
                    filtered?std::max(1.0f,the_config->max_anisotropy):1.0f);
                if(gpu_profiling)++profile_frame.gpu_filter_state_sets;
            };
            static thread_local std::vector<GLsizei> multidraw_counts;
            static thread_local std::vector<void const *> multidraw_offsets;
            auto const submit_begin=gpu_profiling
                ?profile_clock::now():profile_clock::time_point{};
            auto submit_draws=[&] {
            for(size_t draw_start=0;draw_start<draws.size();) {
                auto const &draw=draws[draw_start];
                auto const &material=*draw.texture;
                auto const &inst=*draw.instance;
                auto const &group=*draw.group;
                size_t draw_end=draw_start+1;
                // Opaque sprite compatibility was decided at collection.
                // Keep the old depth-ordered alpha batching unchanged.
                bool const sprite_batch_eligible=
                    transparent && the_config->enable_gpu_sprite_instancing &&
                    !post_exempt && !decals_only &&
                    inst.is_sprite_quad && !inst.camera_hud &&
                    !inst.is_projected_decal && !inst.shadow_only &&
                    !inst.transient_procedural &&
                    inst.num_lights==0U && !inst.crossing_lighting &&
                    !material.has_environment &&
                    !material.has_environment2 &&
                    !material.has_normal &&
                    !material.has_normal2 &&
                    !material.has_surface &&
                    material.displacement_factor==0.0f &&
                    material.blend!=material_blend_mode::fluid &&
                    gpu_specular_parameters(material,false).first<=0.0f;
                size_t sprite_batch_count=0;
                if(draw.sprite_batch_group!=0U) {
                    size_t end=draw_start+1;
                    while(end<draws.size() &&
                          end-draw_start<
                            shaders::gpu_3do_preview_shader::max_sprite_instances &&
                          draws[end].sprite_batch_group==draw.sprite_batch_group)
                        ++end;
                    if(end>draw_start+1) {
                        draw_end=end;
                        sprite_batch_count=end-draw_start;
                    }
                } else if(sprite_batch_eligible) {
                    size_t end=draw_start+1;
                    bool const reversed=gpu_3do_reversed_winding(inst.transform);
                    while(end<draws.size() &&
                          end-draw_start<
                            shaders::gpu_3do_preview_shader::max_sprite_instances) {
                        auto const &next=draws[end];
                        auto const &other=*next.instance;
                        if(!other.is_sprite_quad || other.camera_hud ||
                           other.num_lights!=0U || other.crossing_lighting ||
                           other.id!=inst.id || next.group!=draw.group ||
                           next.policy!=draw.policy ||
                           other.ambient!=inst.ambient ||
                           other.sprite_opacity!=inst.sprite_opacity ||
                           other.effective_light_mode!=inst.effective_light_mode ||
                           other.effective_geometry_mode!=inst.effective_geometry_mode ||
                           other.world_lighting_exempt!=inst.world_lighting_exempt ||
                           other.sector_tint_valid!=inst.sector_tint_valid ||
                           other.sector_tint!=inst.sector_tint ||
                           other.colormap_tint!=inst.colormap_tint ||
                           gpu_3do_reversed_winding(other.transform)!=reversed)
                            break;
                        ++end;
                    }
                    if(end>draw_start+1) {
                        draw_end=end;
                        sprite_batch_count=draw_end-draw_start;
                    }
                }
                // Transparent ranges have ALREADY been sorted back-to-front.
                // Merge only consecutive entries with identical draw state:
                // glMultiDrawElements preserves their index-range order,
                // so the painter order, alpha blending and depth tests are
                // unchanged. Never reorder alpha ranges by material as in
                // the opaque path.
                while(sprite_batch_count==0 && draw_end<draws.size()) {
                    auto const &other=draws[draw_end];
                    if(other.instance!=draw.instance ||
                       other.group->material!=group.material ||
                       other.group->cel!=group.cel ||
                       other.group->face_type!=group.face_type ||
                       other.group->nonplanar!=group.nonplanar ||
                       other.group->solid_color!=group.solid_color ||
                       other.group->saber_trail_join!=group.saber_trail_join ||
                       other.group->saber_trail_tip_bridge!=
                           group.saber_trail_tip_bridge ||
                       other.policy!=draw.policy)
                        break;
                    ++draw_end;
                }
                auto it=gpu_3do_gl_cache.find(inst.id);
                if(it==gpu_3do_gl_cache.end()) {
                    auto ptr=std::make_unique<gpu_3do_gl_mesh>(*inst.geometry);
                    if(the_config->profile_renderer) ++profile_frame.gpu_uploads;
                    it=gpu_3do_gl_cache.emplace(inst.id,std::move(ptr)).first;
                    // Count uploads through PERF; do not log every temporary
                    // mesh allocation in this frequently called draw path.
                }
                gpu_3do_last_drawn[inst.id]=gpu_3do_present_serial;
                if(active_instance!=draw.instance) {
                    // Multiple cars can instance the same cached mesh ID with
                    // different world matrices. Compare instances, not IDs.
                    // Match JK rdModel3_DrawMesh: combine the original
                    // rdCamera::orient and joint/world placement on CPU,
                    // then transform static local mesh vertices on the GPU.
                    auto const &m=*draw.modelview;
                    prog.set_view_matrix(make_point(m[0],m[1],m[2],0.0f),
                                         make_point(m[3],m[4],m[5],0.0f),
                                         make_point(m[6],m[7],m[8],0.0f),
                                         make_point(m[9],m[10],m[11],1.0f));
                    gl::bind_vertex_array(it->second->vao);
                    // Persistent GL uniforms must reset on each Thing.
                    prog.set_continuous_ambient(
                        continuous_buffer_ready && inst.continuous_ambient_fallback
                            ? &*inst.continuous_ambient_fallback : nullptr,
                        inst.owning_sector_id,
                        continuous_buffer_ready && continuous_ambient_gpu_cache
                            ? continuous_ambient_gpu_cache->mask() : 0U,
                        continuous_buffer_ready && continuous_ambient_gpu_cache
                            ? continuous_ambient_gpu_cache->spacing() : 1.0f);
                    // ThingFlag 0x20 intentionally uses the existing JK-style
                    // vertex dynamic-light loop; ordinary GPU 3DOs keep their
                    // per-fragment point lights. This is an instance choice,
                    // not a property of the shared mesh or material.
                    prog.set_lighting(inst,
                        the_config->gpu_pixel_lighting &&
                        !inst.skybox_vertex_lighting &&
                        !inst.camera_hud && !inst.is_projected_decal &&
                        inst.num_lights>0U,
                        frame_saber_light_colors.colors,
                        frame_saber_light_colors.valid);
                    // An authored skybox receives no per-pixel Thing-light
                    // GGX either, even when its MAT has an EnvMap. EnvMap
                    // reflections themselves remain as before.
                    prog.set_dynamic_light_specular(
                        !inst.skybox_vertex_lighting &&
                        the_config->enable_reflections &&
                        the_config->enable_dynamic_light_specular);
                    prog.set_camera_hud_thing(
                        inst.camera_hud,inst.camera_hud_world_depth);
                    // 0x20 suppresses JkGRfxMod light-reception effects on
                    // GPU-cached 3DOs, not the authored JK vertex lighting.
                    // Native D3D triangles do not carry per-Thing flags here.
                    prog.set_world_lighting_exempt(inst.world_lighting_exempt);
                    prog.set_sprite_receiver(inst.is_sprite_quad);
                    // 0x20 skyboxes keep JK vertex lighting, not the sector's
                    // CMP colors, tint, or opt-in crossing-based lighting.
                    bool const allow_sector_effects=!inst.skybox_vertex_lighting;
                    prog.set_colormap_tint(allow_sector_effects
                        ? inst.colormap_tint
                        : std::array<float,3>{{1.0f,1.0f,1.0f}});
                    // Zero sector count preserves the existing fast path.
                    if(the_config->enable_sector_crossing_lighting)
                        prog.set_sector_crossing(inst.crossing_lighting.get(),
                            crossing_buffer_ready && allow_sector_effects &&
                            !inst.camera_hud && !inst.is_projected_decal &&
                            !inst.is_sprite_quad,
                            can_replace_sector_tint,
                            the_config->experimental_sector_crossing_vertex);
                    // Shared 3DO geometry can have several instances in
                    // different SithSectors. Do this per Thing, not per
                    // material/mesh. Invalid native pointers preserve JK.
                    std::array<float,3> tint_delta{};
                    if(can_replace_sector_tint && allow_sector_effects &&
                       inst.sector_tint_valid && !inst.camera_hud)
                        tint_delta=inst.sector_tint;
                    prog.set_thing_sector_tint_delta(tint_delta);
                    std::array<float,3> local_eye{};
                    bool const has_eye=gpu_3do_local_eye(m,local_eye);
                    prog.set_local_eye(has_eye?local_eye:
                                       std::array<float,3>{{0,0,0}});
                    active_instance=draw.instance;
                }
                if((group.face_type&0x1U)!=0 || group.nonplanar) {
                    // Warped polygons may have triangles that face away even
                    // when JK's single stored face normal faces the camera.
                    // Use the original CPU face-normal test above instead.
                    gl::disable(gl::capability::cull_face);
                } else {
                    gl::enable(gl::capability::cull_face);
                    // A mirrored R/F/U basis flips triangle winding, but
                    // JK still treats the object's outside as the front.
                    // Cull the opposite side rather than showing its inside.
                    gl::set_face_cull_mode(
                        gpu_3do_reversed_winding(inst.transform)
                            ? gl::face_mode::front : gl::face_mode::back);
                }
                // The native translucent face flag forces alpha even when
                // its MAT texture is fully opaque (JK uses alpha=90/255).
                float const face_opacity=inst.is_projected_decal
                    ? inst.decal_opacity
                    : (inst.is_sprite_quad ? inst.sprite_opacity : 1.0f) *
                      ((group.face_type&0x2U)!=0 ? 90.0f/255.0f : 1.0f);
                if(transparent) {
                    switch(material.blend) {
                    case material_blend_mode::additive:
                        gl::set_blend_function_separate(
                            gl::blend_function::one,gl::blend_function::one,
                            gl::blend_function::zero,gl::blend_function::one);
                        break;
                    case material_blend_mode::multiply:
                        gl::set_blend_function_separate(
                            gl::blend_function::destination_color,
                            gl::blend_function::zero,
                            gl::blend_function::zero,gl::blend_function::one);
                        break;
                    default:
                        gl::set_blend_function(gl::blend_function::one,
                            gl::blend_function::one_minus_source_alpha);
                        break;
                    }
                }
                bind_preview_color(material.texture,0,material.filtered);
                bind_preview_color(material.emission,1,material.filtered);
                // Unit 11 is the AO receiver on the projected-decal pass.
                bool const indexed_fullbright=
                    the_config->restore_cmp_lights &&
                    material.has_palette_fullbright &&
                    !inst.is_projected_decal && !inst.camera_hud;
                if(indexed_fullbright)
                    bind_preview_color(material.palette_fullbright_mask,
                                       11,material.filtered);
                prog.set_palette_fullbright(indexed_fullbright);
                bool const has_albedo_transition=
                    material.albedo_transition>0.0f;
                if(has_albedo_transition) {
                    bind_preview_color(material.texture2,6,material.filtered);
                    prog.set_albedo_transition(material.albedo_source2,
                                               material.albedo_transition);
                } else if(prev_albedo_transition) {
                    prog.set_albedo_transition(2,0.0f);
                }
                prev_albedo_transition=has_albedo_transition;
                // Projected SPR decals always use receiver-plane depth
                // clipping; shared MAT material flags cannot disable it.
                bool const clip_decal=transparent && inst.is_projected_decal;
                bool const advanced=clip_decal ||
                    (transparent &&
                     material.blend==material_blend_mode::fluid);
                if(advanced)
                    prog.set_advanced_material(clip_decal,
                                               material.fluid_thickness);
                else if(prev_advanced_material)
                    prog.set_advanced_material(false,0.25f);
                prev_advanced_material=advanced;
                // Unit 9 is also used by receiver lighting for projected
                // decals. Never overwrite it with a displacement MAT on
                // the decal-only draw path.
                bool const parallax=!inst.camera_hud &&
                    !inst.is_projected_decal &&
                    material.displacement_factor!=0.0f;
                if(parallax) {
                    gl::set_active_texture_unit(9);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     material.displacement);
                    prog.set_parallax(material.displacement_factor);
                } else if(prev_parallax) {
                    prog.set_parallax(0.0f);
                }
                prev_parallax=parallax;
                // Only bind normal/surface maps when EnvMap, sun or the
                // opt-in dynamic Thing point-light GGX can use them.
                // Decals have no native point lights; ordinary per-vertex JK
                // diffuse lighting is entirely unaffected by this switch.
                bool const point_specular_active=
                    !inst.camera_hud && !inst.skybox_vertex_lighting &&
                    the_config->enable_reflections &&
                    the_config->enable_dynamic_light_specular &&
                    !inst.is_projected_decal && inst.num_lights>0U;
                // Only unconfigured *ordinary* materials inherit this
                // direct-light dielectric response. Keep VFX, sprites,
                // Camera HUD, skybox flags, fully-bright CMP and decals out.
                bool const default_eligible=
                    !inst.camera_hud && !inst.is_projected_decal &&
                    !inst.is_sprite_quad && !inst.world_lighting_exempt &&
                    !material.solid_fullbright &&
                    !material.emissive_only &&
                    material.albedo_source!=0 &&
                    material.blend!=material_blend_mode::additive &&
                    material.blend!=material_blend_mode::multiply;
                auto const specular=gpu_specular_parameters(
                    material,default_eligible);
                bool const spec_active=!inst.camera_hud &&
                    specular.first>0.0f &&
                    (material.has_environment ||
                     material.has_environment2 || sun_active ||
                     point_specular_active);
                bool const diffuse_mapping=
                    the_config->enable_normal_mapped_diffuse &&
                    !inst.camera_hud && !inst.is_projected_decal &&
                    !inst.is_sprite_quad && !inst.world_lighting_exempt &&
                    !group.solid_color && !material.solid_fullbright &&
                    (material.has_normal || material.has_normal2);
                bool const material_normals_active=
                    spec_active || diffuse_mapping;
                if(material_normals_active) {
                    if(spec_active) {
                        gl::set_active_texture_unit(2);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         material.surface);
                    }
                    if(material.has_normal) {
                        gl::set_active_texture_unit(3);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         material.normal);
                    }
                    if(material.has_normal2) {
                        gl::set_active_texture_unit(5);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         material.normal2);
                    }
                    if(spec_active && material.has_environment) {
                        gl::set_active_texture_unit(4);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         material.environment);
                    }
                    if(spec_active &&
                       material.environment_transition>0.0f &&
                       material.has_environment2) {
                        gl::set_active_texture_unit(7);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         material.environment2);
                    }
                    prog.set_environment(
                        spec_active && material.has_environment,
                        spec_active && material.has_environment2,
                        spec_active?material.environment_transition:0.0f,
                        material.reflection_mode);
                    prog.set_point_specular(
                        make_point(spec_active?specular.first:0.0f,
                                   specular.second,material.metalness,
                                   spec_active && material.has_surface?1.0f:0.0f),
                        make_point(material.has_normal?1.0f:0.0f,
                                   material.normal_strength,
                                   material.has_normal2?1.0f:0.0f,
                                   material.normal_strength2),
                        material.normal_scroll,
                        material.normal_scroll2);
                } else if(prev_specular_active) {
                    prog.set_point_specular(
                        make_point(0.0f,0.5f,0.0f,0.0f),
                        make_point(0.0f,1.0f,0.0f,0.0f),
                        make_point(0.0f,0.0f),make_point(0.0f,0.0f));
                    prog.set_environment(false,false,0.0f,0);
                }
                prev_specular_active=material_normals_active;
                // JK's high-resolution MAT patch uses a 256x256 maximum UV
                // reference area for *models*, independently per dimension.
                // For a 64px MAT, 0..256 repeats four times; for >=256px,
                // the same UV span covers one complete texture.
                // Type-3 SPR decal UVs come from rdSprite and must continue
                // to use their original native dimensions.
                float const uv_width=(inst.is_projected_decal || inst.is_sprite_quad)
                    ? material.width
                    : gpu_3do_model_uv_extent(material.width);
                float const uv_height=(inst.is_projected_decal || inst.is_sprite_quad)
                    ? material.height
                    : gpu_3do_model_uv_extent(material.height);
                prog.set_solid_fullbright(material.solid_fullbright);
                prog.set_surface(material.albedo_source,uv_width,
                                 uv_height,transparent,
                                 static_cast<int>(material.blend),
                                 static_cast<int>(draw.policy),
                                 material.alpha_cutoff,face_opacity,
                                 material.tint,material.albedo_scroll,
                                 material.emissive_only);
                prog.set_emissive(material.has_emission,
                                  material.emission_factor,
                                  material.emissive_scroll);
                if(sprite_batch_count>1) {
                    std::array<std::array<float,12>,
                        shaders::gpu_3do_preview_shader::max_sprite_instances>
                            sprite_models{};
                    for(size_t n=draw_start;n<draw_end;++n)
                        sprite_models[n-draw_start]=*draws[n].modelview;
                    prog.set_sprite_instances(sprite_models,sprite_batch_count);
                    glDrawElementsInstanced(GL_TRIANGLES,
                        static_cast<GLsizei>(group.count),GL_UNSIGNED_INT,
                        reinterpret_cast<void const *>(
                            uintptr_t(group.offset)*sizeof(uint32_t)),
                        static_cast<GLsizei>(sprite_batch_count));
                    prog.set_sprite_instances(sprite_models,0);
                    // Restore the normal per-Thing uniform path.
                    active_instance=nullptr;
                } else if(draw_end==draw_start+1) {
                    gl::draw_elements(gl::element_type::triangles,group.count,
                                      gl::index_type::uint32,
                                      static_cast<ptrdiff_t>(
                                          group.offset*sizeof(uint32_t)));
                } else {
                    // Indexed EBO offsets belong to the same cached VAO.
                    // MultiDraw keeps all per-face attributes and depth
                    // tests, without repeated uniforms, texture binding,
                    // state changes, or individual GL dispatches.
                    multidraw_counts.clear();
                    multidraw_offsets.clear();
                    multidraw_counts.reserve(draw_end-draw_start);
                    multidraw_offsets.reserve(draw_end-draw_start);
                    for(size_t n=draw_start;n<draw_end;n++) {
                        auto const &g=*draws[n].group;
                        multidraw_counts.push_back(static_cast<GLsizei>(g.count));
                        multidraw_offsets.push_back(
                            reinterpret_cast<void const *>(
                                static_cast<uintptr_t>(g.offset)*sizeof(uint32_t)));
                    }
                    glMultiDrawElements(GL_TRIANGLES,multidraw_counts.data(),
                                        GL_UNSIGNED_INT,multidraw_offsets.data(),
                                        static_cast<GLsizei>(multidraw_counts.size()));
                }
                // Creation of a procedural trail doesn't prove the extra
                // triangles survived to an actual OpenGL material draw.
                if(the_config->profile_renderer && inst.saber_trail_active &&
                   group.count>=9U && inst.geometry &&
                   inst.geometry->groups.size()>=3U &&
                   group.material==inst.geometry->groups[0].material) {
                    static unsigned trail_draw_logs=0;
                    if(trail_draw_logs++<16U)
                        LOG_INFO("GPU SABER TRAIL: GL draw",
                                 " pass=",transparent?"alpha":"opaque",
                                 " indices=",group.count,
                                 " policy=",static_cast<int>(draw.policy),
                                 " material=",group.material,
                                 " tex_px=",material.width,"x",material.height);
                }
                if(the_config->profile_renderer &&
                   transparent && draw.instance->is_projected_decal) {
                    static unsigned decal_gpu_draw_logs=0;
                    if(decal_gpu_draw_logs++<12U) {
                        std::ostringstream msg;
                        msg<<"SPR decal GPU stage=draw triangles="
                           <<(group.count/3U)
                           <<" alpha="<<draw.instance->decal_opacity;
                        LOG_INFO(msg.str());
                    }
                }
                if(the_config->profile_renderer) {
                    ++profile_frame.gpu_draw_calls;
                    profile_frame.gpu_draw_ranges+=draw_end-draw_start;
                    if(transparent) {
                        ++profile_frame.gpu_transparent_draw_calls;
                        profile_frame.gpu_transparent_draw_ranges+=draw_end-draw_start;
                    } else {
                        ++profile_frame.gpu_opaque_draw_calls;
                        profile_frame.gpu_opaque_draw_ranges+=draw_end-draw_start;
                    }
                    if(draw.sprite_batch_group!=0U || sprite_batch_eligible)
                        profile_frame.gpu_sprite_instancing_eligible+=
                            sprite_batch_count>1 ? sprite_batch_count : 1U;
                    if(sprite_batch_count>1) {
                        ++profile_frame.gpu_sprite_instanced_calls;
                        profile_frame.gpu_sprite_instanced_quads+=
                            sprite_batch_count;
                    } else if(draw_end>draw_start+1)
                        ++profile_frame.gpu_multidraw_calls;
                    for(size_t n=draw_start;n<draw_end;n++) {
                        auto const &item=draws[n];
                        profile_frame.gpu_draw_triangles+=item.group->count/3;
                        if(post_exempt || decals_only)continue;
                        if(transparent) {
                            if(item.instance->is_sprite_quad) {
                                if(item.texture->blend==material_blend_mode::fluid)
                                    ++profile_frame.gpu_alpha_fluid_sprite_ranges;
                                else ++profile_frame.gpu_alpha_other_sprite_ranges;
                            } else ++profile_frame.gpu_alpha_model_ranges;
                        } else if(item.instance->is_sprite_quad &&
                                  (item.policy==gpu_3do_alpha_mode::masked ||
                                   item.policy==gpu_3do_alpha_mode::dithered)) {
                            ++profile_frame.gpu_opaque_cutout_sprite_ranges;
                        }
                    }
                }
                draw_start=draw_end;
            }
            };
            submit_draws();
            if(split_smoke && !smoke_draws.empty()) {
                // No GL depth attachment: shader checks the original opaque
                // R16F depth in its full-resolution 2x2 pixel footprint.
                // Stencil counts only fragments surviving shader discard.
                draws.swap(smoke_draws);
                active_instance=nullptr;
                auto const dims=make_size(
                    std::max(1,(get<x>(conf_scr_res)+1)/2),
                    std::max(1,(get<y>(conf_scr_res)+1)/2));
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                     smoke_half_buffer->fbo);
                gl::set_viewport(make_box(make_point(0,0),dims));
                GLfloat const blank[4]={0.0f,0.0f,0.0f,0.0f};
                GLint const stencil_clear=0;
                glStencilMask(0xff);
                glClearBufferfv(GL_COLOR,0,blank);
                glClearBufferiv(GL_STENCIL,0,&stencil_clear);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                auto const budget=the_config->fluid_sprite_layer_budget;
                if(budget>0U) {
                    glEnable(GL_STENCIL_TEST);
                    // GL compares reference OP stencil: ref > count.
                    glStencilFunc(GL_GREATER,static_cast<GLint>(budget),0xff);
                    glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
                } else glDisable(GL_STENCIL_TEST);
                // The shader still samples the full-res opaque depth image.
                // Change screen-coordinate reconstruction to half-size.
                auto const axis=[](std::array<float,3> const &a,float w) {
                    return make_point(a[0],a[1],a[2],w);
                };
                float const h=std::tan(cam.fov*0.008726646259971648f);
                float const v=h*float(get<y>(conf_scr_res))/
                                float(get<x>(conf_scr_res));
                prog.set_translucent_spotlight_camera(
                    axis(cam.position,0.0f),axis(cam.right,h),
                    axis(cam.forward,0.0f),axis(cam.up,v),
                    make_point(float(get<x>(dims)),float(get<y>(dims))));
                prog.set_smoke_half_resolution(true);
                submit_draws();
                prog.set_smoke_half_resolution(false);
                glDisable(GL_STENCIL_TEST);
                draws.swap(smoke_draws);
                // Full-res framebuffer and viewport for all remaining passes.
                frame_screen->begin_transparency_pass();
                gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
                // The smoke texture contains premultiplied RGBA. Composite
                // it once, without sampling the scene's color FBO itself.
                glDisable(GL_DEPTH_TEST);
                gl::disable(gl::capability::cull_face);
                gl::enable(gl::capability::blend);
                gl::set_blend_function(gl::blend_function::one,
                    gl::blend_function::one_minus_source_alpha);
                ogs->post_smoke_composite->use_program();
                ogs->post_smoke_composite->set_samplers(0,1);
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 smoke_half_buffer->tex);
                gl::set_active_texture_unit(1);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
                gl::set_active_texture_unit(0);
                gl::bind_vertex_array(ogs->postmdl.vao);
                gl::draw_elements(gl::element_type::triangles,
                    ogs->postmdl.num_indices,gl::index_type::uint32);
                glEnable(GL_DEPTH_TEST);
                if(gpu_profiling && gpu_3do_present_serial%120U==0U)
                    LOG_INFO("SMOKE half-res drawn_ranges=",smoke_draws.size(),
                             " budget=",budget,
                             " stencil=",budget>0U?1:0,
                             " NOTE=stencil_early_shader_reject_not_guaranteed_GL33");
            }
            if(gpu_profiling)
                profile_frame.gpu_preview_submit_us[pass_index]+=
                    elapsed_profile_us(submit_begin,profile_clock::now());
            // Preserve native/ordinary 3DO depth semantics; polygon
            // offset is exclusively for projected decal triangles.
            if(decals_only)glDisable(GL_POLYGON_OFFSET_FILL);
            // The transparency pass's following JK batches assume normal
            // premultiplied alpha blending, unit 0 and disabled face culling.
            gl::disable(gl::capability::cull_face);
            if(transparent)
                gl::set_blend_function(gl::blend_function::one,
                    gl::blend_function::one_minus_source_alpha);
            gl::set_active_texture_unit(0);
        }

        // Deliberate world-cache isolation test. The Direct3D "world"
        // batches also contain native fallback Things and ordinary sprites.
        // Suppress those mixed batches ONLY when our opt-in cache has
        // produced usable visible-sector candidates. The independent gun,
        // GPU-cached 3DO, projected decal and flare passes are unaffected.
        bool isolate_native_world_batches() const
        {
            return the_config->use_gpu_cache &&
                   !the_config->enable_native_fallback &&
                   world_cache_target && world_cache_target->ready &&
                   frame_world_cache_camera.valid &&
                   (!world_cache_target->sector_candidates.empty() ||
                    !world_cache_target->sector_special_candidates.empty());
        }

        void draw_game_opaque_into_gbuffer(triangle_buffer_models *trimdl, bool posterize_lighting)
        {
            // Draw batches
            gl::disable(gl::capability::blend);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::front_and_back);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::set_depth_function(gl::comparison_function::less);

            ogs->game_opaque_pass_program.use_program();
            ogs->game_opaque_pass_program.set_specular_normal_mode(
                the_config->experimental_specular_normal_mode);
            ogs->game_opaque_pass_program.set_screen_resolution(
                static_cast<size<2, float>>(conf_scr_res));
            ogs->game_opaque_pass_program.set_albedo_map_sampler(0);
            ogs->game_opaque_pass_program.set_emissive_map_sampler(1);
            ogs->game_opaque_pass_program.set_displacement_map_sampler(2);
            ogs->game_opaque_pass_program.set_surface_map_sampler(3);
            ogs->game_opaque_pass_program.set_env_map_sampler(4);
            ogs->game_opaque_pass_program.set_normal_map_samplers(6, 7);
            apply_engine_camera(ogs->game_opaque_pass_program);
            // Resolve sun GGX visibility before it is mixed into emissive.
            set_translucent_sun(ogs->game_opaque_pass_program,true);

            {
                auto const &fog = get_fog_settings();
                auto const current_color = fog.color.value();
                auto const current_info = fog.info.value();
                ogs->game_opaque_pass_program.set_fog(
                    make_point(current_info[0],
                               current_info[1],
                               the_config->enable_fog ? current_info[2] : 0.0f,
                               0.0f),
                    color_rgb(current_color[0], current_color[1], current_color[2]));
            }

            // The native world has no separate batch from native fallback
            // Things/sprites. For this experimental parity pass, skip BOTH
            // mixed batches so any unsupported world faces become obvious.
            // Restore native rendering automatically if no cache is ready.
            if(!isolate_native_world_batches()) {
                draw_batch(&ogs->game_opaque_pass_program,
                           world_batch,&trimdl->world_trimdl,
                           /*force opaque*/ true,posterize_lighting);
                draw_batch(&ogs->game_opaque_pass_program,
                           world_transparent_batch,
                           &trimdl->world_transparent_trimdl,
                           /*force opaque*/ true,posterize_lighting);
            } else if(the_config->profile_renderer &&
                      (gpu_3do_present_serial%120U)==0U) {
                LOG_INFO("WORLD CACHE native_isolation=1 skipped_opaque_tris=",
                         world_batch.size(),
                         " skipped_alpha_test_tris=",
                         world_transparent_batch.size(),
                         " mixed_native_batches_may_contain_Things=1");
            }

            draw_world_cache_preview();
            ogs->game_opaque_pass_program.use_program();

            // First-person gun rendering uses a separate depth transform:
            // never sample world shadow maps for its artificial placement.
            set_translucent_sun(ogs->game_opaque_pass_program,false);
            // Draw fourth pass (opaque gun geometry)
            draw_batch(&ogs->game_opaque_pass_program,
                       gun_batch,
                       &trimdl->gun_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);

            // Draw fifth pass (transparent gun geometry with alpha testing)
            draw_batch(&ogs->game_opaque_pass_program,
                       gun_transparent_batch,
                       &trimdl->gun_transparent_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);

            // GPU preview changes the active GL shader. Render it LAST within
            // the opaque pass, after all batches that expect game_opaque_pass.
            draw_gpu_3do_preview(false);
        }

        void release_spotlight_images()
        {
            if(texture_cache)
                for(auto const &item:spotlight_retained_albedo)
                    texture_cache->release_srgb_texture(item.second);
            spotlight_retained_albedo.clear();
            spotlight_unready_mats.clear();
        }

        // This is called on the renderer thread. Pin only active cookie MATs,
        // and invalidate immediately when JK loads a different world.
        void maintain_spotlight_images(std::vector<spotlight_instance> const &spots)
        {
            uint32_t world=0;
            engine_material_debug::read_object(
                engine_material_debug::current_world_global,world);
            if(world!=spotlight_retained_world) {
                release_spotlight_images();
                spotlight_retained_world=world;
            }
            std::set<uint32_t> active;
            for(auto const &spot:spots)active.insert(spot.material);
            for(auto it=spotlight_retained_albedo.begin();
                it!=spotlight_retained_albedo.end();) {
                if(active.count(it->first)==0) {
                    texture_cache->release_srgb_texture(it->second);
                    it=spotlight_retained_albedo.erase(it);
                }
                else ++it;
            }
        }

        // Keep the image in JKGM's GL texture cache independently of native
        // D3D texture eviction. Never call the native uploader from here.
        std::optional<gl::texture_view> spotlight_mat_texture(uint32_t mat_id)
        {
            if(!engine_material_debug::matches_tested_executable())return std::nullopt;
            auto held=spotlight_retained_albedo.find(mat_id);
            auto const retained=[&]()->std::optional<gl::texture_view> {
                if(held==spotlight_retained_albedo.end())return std::nullopt;
                return texture_cache->get_texture_handle(held->second);
            };
            bool const shared=(mat_id&0x8000U)!=0U;
            uint32_t const index=mat_id&0x7fffU;
            uint32_t address=0;
            engine_material_debug::world_header world{};
            if(!engine_material_debug::load_world(
                  shared?engine_material_debug::static_world_global:
                         engine_material_debug::current_world_global,
                  address,world) || index>=uint32_t(world.num_materials))
                return retained();
            uintptr_t const mat_ptr=uintptr_t(world.materials)+
                index*sizeof(engine_material_debug::native_material);
            gpu_3do_group group{};
            group.material=mat_ptr;
            group.cel=-1;
            auto const material=resolve_gpu_3do_material(group);
            if(material.has_image && material.albedo_source==1 &&
               material.albedo_id) {
                auto const id=*material.albedo_id;
                // Pin the new image before releasing a previous cel/handle.
                if(held==spotlight_retained_albedo.end()) {
                    texture_cache->retain_srgb_texture(id);
                    spotlight_retained_albedo.emplace(mat_id,id);
                }
                else if(held->second.get()!=id.get()) {
                    texture_cache->retain_srgb_texture(id);
                    texture_cache->release_srgb_texture(held->second);
                    held->second=id;
                }
                spotlight_unready_mats.erase(mat_id);
                return texture_cache->get_texture_handle(id);
            }
            if(auto image=retained())return image; // native cache was evicted
            if(spotlight_unready_mats.insert(mat_id).second)
                LOG_WARNING("Spotlight MAT ",mat_id,
                            " has no resident albedo; render/load its MAT first");
            return std::nullopt;
        }


        // Reuse the existing COG projector transforms/cookies. The translucent
        // stage shades only visible blended fragments, so it needs no new FBO
        // or fullscreen pass. Mask/dither coverage stays in the opaque pass.
        void prepare_translucent_spotlights()
        {
            translucent_spotlights_for_frame.clear();
            if(!the_config->enable_spotlights)return;
            auto const cam=read_active_render_camera();
            if(!cam.valid)return;
            float const h=std::tan(cam.fov*0.008726646259971648f);
            float const v=h*float(get<y>(conf_scr_res))/
                            float(get<x>(conf_scr_res));
            spotlight_projection_camera projection{};
            projection.position=cam.position;
            projection.right=cam.right;
            projection.up=cam.up;
            projection.forward=cam.forward;
            projection.tan_half_horizontal=h;
            projection.tan_half_vertical=v;
            projection.width=get<x>(conf_scr_res);
            projection.height=get<y>(conf_scr_res);
            for(auto const &spot:collect_spotlights_for_frame()) {
                auto const coverage=spotlight_screen_coverage(spot,projection);
                if(!coverage.visible)continue;
                auto image=spotlight_mat_texture(spot.material);
                if(!image)continue;
                int const slot=int(translucent_spotlights_for_frame.size());
                if(slot>=int(spotlight_max_active))break;
                // The cached 3DO shader already occupies units 0-11.
                // Four spare GLSL 3.3 units (12-15) accommodate MAT cookies
                // without rebinding any existing material textures.
                gl::set_active_texture_unit(12+slot);
                gl::bind_texture(gl::texture_bind_target::texture_2d,*image);
                translucent_spotlights_for_frame.push_back({spot,coverage});
            }
            gl::set_active_texture_unit(0);
        }

        template<typename Shader>
        void set_translucent_spotlights(Shader &prog,bool active)
        {
            int const count=active
                ? int(translucent_spotlights_for_frame.size()) : 0;
            prog.set_translucent_spotlight_count(count);
            // The same single shadow map is shared with the opaque
            // compositor; all other active projectors remain unshadowed.
            int shadow_slot=-1;
            if(count && spotlight_shadow_ready && spotlight_shadow_target) {
                for(int i=0;i<count;++i) {
                    if(shadow_matches_light(
                           translucent_spotlights_for_frame[i].light)) {
                        shadow_slot=i;break;
                    }
                }
            }
            prog.set_translucent_shadow(shadow_slot,
                shadow_slot>=0 ? spotlight_shadow_view.near_distance : 0.0f,
                shadow_slot>=0 ? spotlight_shadow_view.range : 0.0f,
                the_config->spotlight_shadow_angle_bias_scale);
            if(shadow_slot>=0) {
                gl::set_active_texture_unit(10);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 spotlight_shadow_target->depth);
                gl::set_active_texture_unit(0);
            }
            if(!count)return;
            auto const cam=read_active_render_camera();
            if(!cam.valid) {
                prog.set_translucent_spotlight_count(0);
                return;
            }
            float const h=std::tan(cam.fov*0.008726646259971648f);
            float const v=h*float(get<y>(conf_scr_res))/
                            float(get<x>(conf_scr_res));
            auto const axis=[](std::array<float,3> const &a,float w) {
                return make_point(a[0],a[1],a[2],w);
            };
            prog.set_translucent_spotlight_camera(
                axis(cam.position,0.0f),axis(cam.right,h),
                axis(cam.forward,0.0f),axis(cam.up,v),
                make_point(float(get<x>(conf_scr_res)),
                           float(get<y>(conf_scr_res))));
            for(int i=0;i<count;++i) {
                auto const &item=translucent_spotlights_for_frame[i];
                auto const &spot=item.light;
                auto const &rect=item.coverage;
                prog.set_translucent_spotlight(
                    i,axis(spot.origin,0.0f),
                    axis(spot.right,spot.range/spot.width),
                    axis(spot.up,spot.range/spot.height),
                    axis(spot.forward,1.0f/spot.range),
                    make_point(spot.light_intensity,spot.add_intensity),
                    make_point(rect.min_x,rect.min_y,rect.max_x,rect.max_y));
            }
        }



        // A level without any physical sky aperture cannot receive
        // COG sunlight, regardless of SetSunInfo or shadow settings.
        // Preserve the COG state so switching worlds later still works.
        // The mostly immutable SithSurface table only needs an eligibility
        // audit on world changes, sun reactivation or periodically for rare
        // COG sky-flag/adjoin changes. GeoMode 0 remains a valid aperture.
        // Any malformed native pointer/read fails open (sun stays allowed).
        void refresh_sun_level_sky(bool force_audit)
        {
            uint32_t world=0;
            bool const world_valid=engine_material_debug::read_object(
                engine_material_debug::current_world_global,world) &&
                decal_native_address_ok(world);
            constexpr uint64_t audit_interval_frames=120U;
            if(!force_audit && world_valid && sun_level_sky_known &&
               uintptr_t(world)==sun_level_sky_world &&
               gpu_3do_present_serial>=sun_level_sky_last_audit_frame &&
               gpu_3do_present_serial-sun_level_sky_last_audit_frame<
                   audit_interval_frames)
                return;

            bool present=true,known=false;
            uint32_t eligible=0;
            uint64_t identity=14695981039346656037ULL;
            if(world_valid) {
                uint32_t count=0,table=0;
                if(engine_material_debug::read_object(
                       uintptr_t(world)+0xa8U,count) &&
                   engine_material_debug::read_object(
                       uintptr_t(world)+0xacU,table) &&
                   count<=250000U &&
                   (count==0U ||
                    (decal_native_address_ok(table) &&
                     engine_camera_detail::readable(
                         uintptr_t(table),size_t(count)*0x5cU)))) {
                    known=true;
                    present=false;
                    for(uint32_t i=0;i<count;++i) {
                        uintptr_t const base=uintptr_t(table)+
                            uintptr_t(i)*0x5cU;
                        uint32_t adjoin=0,flags=0;
                        std::memcpy(&adjoin,
                            reinterpret_cast<void const *>(base+0x0cU),4U);
                        std::memcpy(&flags,
                            reinterpret_cast<void const *>(base+0x10U),4U);
                        if(sun_sky_surface_eligible(flags,adjoin)) {
                            present=true;
                            ++eligible;
                            identity^=uint64_t(i);
                            identity*=1099511628211ULL;
                        }
                    }
                }
            }
            // A newly eligible sky surface must be captured in the
            // aperture geometry snapshot, not merely in its live flags.
            if(known && (world!=sun_level_sky_world ||
                         !sun_level_sky_known ||
                         eligible!=sun_level_sky_faces ||
                         identity!=sun_level_sky_identity)) {
                sun_sky_aperture_target.reset();
                sun_shadow_cascade_cached={{false,false}};
                sun_shadow_validated_world_view_ready=false;
            }
            sun_level_has_sky=present;
            sun_level_sky_known=known;
            sun_level_sky_faces=eligible;
            sun_level_sky_identity=identity;
            sun_level_sky_world=world;
            if(known)
                sun_level_sky_last_audit_frame=gpu_3do_present_serial;
        }

        bool sun_sky_aperture_hardware_supported() const
        {
            // This is an immutable per-context capability. Query once:
            // glGetIntegerv every frame is expensive through Wine.
            static GLint fragment_units=0;
            if(fragment_units==0)
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
            return fragment_units>=
                texture_units::forward_next_extended_required_units;
        }

        bool sun_sky_aperture_ready() const
        {
            if(!the_config->experimental_sun_sky_aperture ||
               !sun_shadow_ready || !sun_sky_aperture_target ||
               !sun_sky_aperture_target->complete ||
               !sun_sky_aperture_target->valid[0] ||
               !sun_sky_aperture_target->valid[1])return false;
            // Unit 19 is the forward sector samplerBuffer, not an array
            // sampler. Unit 20 requires at least 21 fragment image units.
            return sun_sky_aperture_hardware_supported();
        }

        point<4,float> sun_sky_aperture_depth_transform() const
        {
            auto const near_depth_params=
                sun_sky_aperture_target->depth_transform(
                    0,sun_shadow_views[0]);
            auto const far_depth_params=
                sun_sky_aperture_target->depth_transform(
                    1,sun_shadow_views[1]);
            return make_point(near_depth_params[0],near_depth_params[1],
                              far_depth_params[0],far_depth_params[1]);
        }

        adaptive_sun_pcf_radii effective_sun_pcf_radii() const
        {
            return sun_intensity_pcf_radii(
                the_config->sun_shadow_near_pcf_radius,
                the_config->sun_shadow_far_pcf_radius,
                get_sun_settings().intensity.value()[0],
                the_config->experimental_sun_pcf_intensity_softness,
                the_config->experimental_sun_pcf_intensity_reference);
        }

        float effective_sun_dynamic_pcf_radius() const
        {
            return sun_intensity_dynamic_pcf_radius(
                the_config->sun_dynamic_shadow_pcf_radius,
                get_sun_settings().intensity.value()[0],
                the_config->experimental_sun_pcf_intensity_softness,
                the_config->experimental_sun_pcf_intensity_reference);
        }

        template<typename Shader>
        void set_translucent_sun(Shader &prog,bool active)
        {
            auto const off=make_point(0.0f,0.0f,1.0f);
            auto const dark=make_point(0.0f,0.0f,0.0f,0.0f);
            auto &uniforms=prog.translucent_sun;
            uniforms.set_state(false,false,0,off,dark,
                               the_config->sun_shadow_near_distance,1.0f,0.0f);
            uniforms.set_sky_aperture(false,
                texture_units::forward_next_extended_unit,
                make_point(1.0f,0.0f,1.0f,0.0f));
            if(!active || !the_config->enable_sun ||
               !sun_level_has_sky)return;
            auto const cam=read_active_render_camera();
            if(!cam.valid)return;
            auto const sun=get_sun_settings();
            float const intensity=sun.intensity.value()[0];
            if(!std::isfinite(intensity) || intensity<=0.0f)return;
            auto const dir=normalized_sun_direction(sun.direction.value());
            auto const rgb=sun.color.value();
            // The first 16 GL fragment sampler units remain reserved for
            // MATs, decals, spotlight cookie images and their shadow map.
            // Keep native and GPU-cached 3DOs functional on GL 3.3 devices
            // exposing only 16 fragment units: diffuse sun still works.
            static GLint fragment_texture_units=0;
            if(fragment_texture_units==0)
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_texture_units);
            bool const have_texture_slots=fragment_texture_units>=19;
            bool const shadows=have_texture_slots && sun_shadow_ready &&
                               bool(sun_shadow_target);
            // Unit 19 belongs to sector crossing's samplerBuffer. The
            // 2D-array sampler is bound at unit 20 only if supported.
            bool const sky_aperture=shadows &&
                fragment_texture_units>=
                    texture_units::forward_next_extended_required_units &&
                sun_sky_aperture_ready();
            int const dynamic=have_texture_slots &&
                the_config->enable_sun_dynamic_shadows &&
                sun_dynamic_target && !sun_dynamic_views.empty()
                    ? int(sun_dynamic_views.size()) : 0;
            auto const point_axis=[](std::array<float,3> const &v,float w) {
                return make_point(v[0],v[1],v[2],w);
            };
            float const h=std::tan(cam.fov*0.008726646259971648f);
            float const v=h*float(get<y>(conf_scr_res))/
                            float(get<x>(conf_scr_res));
            prog.set_translucent_spotlight_camera(
                point_axis(cam.position,0.0f),point_axis(cam.right,h),
                point_axis(cam.forward,0.0f),point_axis(cam.up,v),
                make_point(float(get<x>(conf_scr_res)),
                           float(get<y>(conf_scr_res))));
            auto const pcf=effective_sun_pcf_radii();
            uniforms.set_pcf_radius(pcf.near_radius,pcf.far_radius);
            uniforms.set_dynamic_pcf_radius(
                effective_sun_dynamic_pcf_radius());
            uniforms.set_sky_aperture(sky_aperture,
                texture_units::forward_next_extended_unit,
                sky_aperture?sun_sky_aperture_depth_transform()
                            :make_point(1.0f,0.0f,1.0f,0.0f));
            uniforms.set_state(true,shadows,dynamic,
                make_point(dir[0],dir[1],dir[2]),
                make_point(rgb[0],rgb[1],rgb[2],intensity),
                the_config->sun_shadow_near_distance,
                the_config->sun_shadow_receiver_bias_scale,
                the_config->sun_max_distance);
            auto set_projection=[&](int slot,sun_shadow_projection const &p,
                                    point<4,float> rect) {
                auto row=[&](decal_vec3 const &axis,float size) {
                    float const inv=1.0f/std::max(size,0.00001f);
                    float const origin_projection=
                        -(p.origin[0]*axis[0]+p.origin[1]*axis[1]+
                          p.origin[2]*axis[2])*inv;
                    return make_point(axis[0]*inv,axis[1]*inv,
                                      axis[2]*inv,origin_projection);
                };
                uniforms.set_map(slot,row(p.right,p.width),
                    row(p.up,p.height),row(p.forward,p.range),rect);
            };
            auto const null_rect=make_point(0.0f,0.0f,0.0f,0.0f);
            if(shadows) {
                set_projection(0,sun_shadow_views[0],null_rect);
                set_projection(1,sun_shadow_views[1],null_rect);
                gl::set_active_texture_unit(16);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    sun_shadow_target->depth);
                gl::set_active_texture_unit(17);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    sun_shadow_target->far_depth);
            }
            if(sky_aperture) {
                gl::set_active_texture_unit(
                    texture_units::forward_next_extended_unit);
                glBindTexture(GL_TEXTURE_2D_ARRAY,
                              sun_sky_aperture_target->depth.value());
            }
            if(dynamic>0) {
                for(int i=0;i<dynamic;++i) {
                    auto const &tile=sun_dynamic_tiles[i];
                    set_projection(i+2,sun_dynamic_views[i],
                        make_point(float(tile.x),float(tile.y),
                                   float(tile.size),float(tile.size)));
                }
                gl::set_active_texture_unit(18);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 sun_dynamic_target->depth);
            }
            gl::set_active_texture_unit(0);
            if(the_config->profile_renderer && !have_texture_slots &&
               gpu_3do_present_serial%240U==0U)
                LOG_INFO("SUN transparent receives diffuse only: fragment sampler units=",
                         fragment_texture_units," need >=19 for shadow maps");
        }

        // One intentionally low-resolution depth map. Static SithWorld
        // casters remain independent of camera visibility; cached 3DOs share
        // their existing vertex/index buffers, including dynamic transforms.
        // Alpha-tested holes are still excluded until the depth shader can
        // sample their material textures.
        void draw_spotlight_shadow_map()
        {
            spotlight_shadow_ready=false;
            if(!the_config->enable_spotlights ||
               !the_config->enable_spotlight_shadows ||
               !ogs->spotlight_shadow_depth_program ||
               !engine_material_debug::matches_tested_executable())
                return;
            auto const camera=read_active_render_camera();
            if(!camera.valid)return;
            spotlight_projection_camera coverage_camera{};
            coverage_camera.position=camera.position;
            coverage_camera.right=camera.right;
            coverage_camera.forward=camera.forward;
            coverage_camera.up=camera.up;
            coverage_camera.tan_half_horizontal=
                std::tan(camera.fov*0.008726646259971648f);
            coverage_camera.tan_half_vertical=
                coverage_camera.tan_half_horizontal*
                float(get<y>(conf_scr_res))/float(get<x>(conf_scr_res));
            coverage_camera.width=get<x>(conf_scr_res);
            coverage_camera.height=get<y>(conf_scr_res);
            spotlight_instance chosen{};
            spotlight_shadow_projection view{};
            bool selected=false;
            for(auto const &spot:collect_spotlights_for_frame()) {
                if(spot.light_intensity<=0.0f &&
                   spot.add_intensity<=0.0f)continue;
                if(!spotlight_screen_coverage(spot,coverage_camera).visible)
                    continue;
                if(!make_spotlight_shadow_projection(spot,view))continue;
                if(!spotlight_mat_texture(spot.material))continue;
                chosen=spot;
                selected=true;
                break; // strict one-shadowed-spotlight budget
            }
            if(!selected)return;

            uint32_t world=0;
            if(!engine_material_debug::read_object(
                   engine_material_debug::current_world_global,world) ||
               !decal_native_address_ok(world))return;
            decal_native_read const safe_read=[](uintptr_t ptr,void *dst,size_t n) {
                if(n==0 || !engine_camera_detail::readable(ptr,n))return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),n);
                return true;
            };
            if(!spotlight_shadow_target)
                spotlight_shadow_target=std::make_unique<spotlight_shadow_gpu>(
                    the_config->spotlight_shadow_resolution);
            auto &map=*spotlight_shadow_target;
            if(!map.update_world(safe_read,world))return;

            // Collect GPU-cached 3DO caster ranges intersecting the light
            // volume. Their existing VAO/VBO/EBO is reused by the depth-only
            // shadow pass; no texture binds or new mesh uploads per frame.
            // This does NOT yet include native-only 3DOs or any geometry
            // never submitted by JK's rdModel3 traversal.
            struct shadow_3do_range {
                gpu_3do_instance const *instance;
                gpu_3do_group const *group;
                gpu_3do_texture texture;
                bool cutout=false;
            };
            std::vector<shadow_3do_range> model_casters;
            size_t excluded_source_ranges=0;
            size_t excluded_source_exact=0;
            size_t excluded_source_family=0;
            size_t neighboring_other_ranges=0;
            uint64_t nearest_other_owner=0;
            float nearest_other_distance2=std::numeric_limits<float>::max();
            auto const light_bounds=spotlight_shadow_volume_bounds(view);
            {
                for(auto const &instance:frame_gpu_3do_instances) {
                    // SithThing 0x20 skybox/UI meshes are visible but must
                    // never cost a spotlight depth pass or shadow the scene.
                    // Shadow-only sprite quads are oriented toward the sun,
                    // not toward arbitrary projected spotlight sources.
                    if(!instance.geometry || instance.is_projected_decal ||
                       instance.is_sprite_quad ||
                       !instance.casts_sun_shadows)
                        continue;
                    // Exclude the projector's owning vehicle/Thing from its
                    // own spotlight depth map. The previous origin-inside-AABB
                    // check alone failed for cars with many small mesh groups.
                    // This also handles a separate light marker attached to
                    // a parent car's model node (validated parent-chain key).
                    if(chosen.shadow_owner!=0 &&
                       (instance.shadow_owner==chosen.shadow_owner ||
                        instance.spotlight_shadow_family==chosen.shadow_owner)) {
                        size_t const ranges=instance.geometry->groups.size();
                        excluded_source_ranges+=ranges;
                        if(instance.shadow_owner==chosen.shadow_owner)
                            excluded_source_exact+=ranges;
                        else excluded_source_family+=ranges;
                        continue;
                    }
                    auto const &m=instance.transform;
                    for(auto const &group:instance.geometry->groups) {
                        if(group.count<3U)continue;
                        decal_vec3 low{{std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max()}};
                        decal_vec3 high{{-std::numeric_limits<float>::max(),
                                         -std::numeric_limits<float>::max(),
                                         -std::numeric_limits<float>::max()}};
                        for(int c=0;c<8;++c) {
                            float const x=(c&1)?group.bounds_max[0]:group.bounds_min[0];
                            float const y=(c&2)?group.bounds_max[1]:group.bounds_min[1];
                            float const z=(c&4)?group.bounds_max[2]:group.bounds_min[2];
                            decal_vec3 worldpos{{
                                m[0]*x+m[3]*y+m[6]*z+m[9],
                                m[1]*x+m[4]*y+m[7]*z+m[10],
                                m[2]*x+m[5]*y+m[8]*z+m[11]}};
                            for(int a=0;a<3;++a) {
                                low[a]=std::min(low[a],worldpos[a]);
                                high[a]=std::max(high[a],worldpos[a]);
                            }
                        }
                        if(!decal_aabbs_overlap(low,high,
                                                light_bounds.low,light_bounds.high))
                            continue;
                        // Diagnostic only: nearby non-family casters may
                        // represent separate COG-controlled car parts. Never
                        // suppress them speculatively; they could be walls.
                        float near2=0.0f;
                        for(int axis=0;axis<3;++axis) {
                            float const d=std::max(std::max(
                                low[axis]-chosen.origin[axis],
                                chosen.origin[axis]-high[axis]),0.0f);
                            near2+=d*d;
                        }
                        float const radius=std::min(
                            1.0f,std::max(0.1f,chosen.range*0.01f));
                        if(near2<radius*radius) {
                            ++neighboring_other_ranges;
                            if(near2<nearest_other_distance2) {
                                nearest_other_distance2=near2;
                                nearest_other_owner=instance.shadow_owner;
                            }
                        }
                        // A spotlight mounted within its source 3DO must not
                        // have that mesh's inside/backside block its own beam.
                        if(spotlight_shadow_origin_inside(chosen.origin,low,high))
                            continue;
                        // A blended effect never writes opaque shadow
                        // depth. Masked/mixed MATs cast cutout silhouettes
                        // using the same UVs/textures as visible geometry.
                        bool const solid=instance.effective_geometry_mode==3 ||
                            group.solid_color;
                        auto const tex=solid
                            ?resolve_gpu_3do_solid_material(group,false)
                            :resolve_gpu_3do_material(group);
                        bool const cutout=tex.masked||tex.alpha||tex.dithered;
                        if(tex.albedo_source==0 || tex.special ||
                           tex.emissive_only ||
                           tex.opacity<0.999f || (group.face_type&0x2U) ||
                           (cutout && tex.albedo_source!=1))
                            continue;
                        model_casters.push_back({&instance,&group,tex,cutout});
                    }
                }
            }

            // Resolve world MAT coverage once per light update. Most
            // architecture remains a single untextured MultiDraw; only
            // alpha-cutout surfaces need bound textures and individual draws.
            struct shadow_world_cutout {
                spotlight_shadow_surface_range range;
                gpu_3do_texture texture;
                int32_t current_cel=-1;
            };
            std::vector<GLsizei> opaque_world_counts;
            std::vector<void const *> opaque_world_offsets;
            std::vector<shadow_world_cutout> masked_world;
            // This signature includes live geomodes, even invisible
            // candidates, so stationary projectors update on 0<->4 changes.
            uint64_t world_visibility_signature=14695981039346656037ULL;
            auto hash_world_visibility=[&](uint32_t value) {
                for(int b=0;b<4;++b) {
                    world_visibility_signature^=uint64_t((value>>(b*8))&255U);
                    world_visibility_signature*=1099511628211ULL;
                }
            };
            query_spotlight_shadow_static_mesh(map.world,map.mesh,view,map.selected);
            opaque_world_counts.reserve(map.selected.size());
            opaque_world_offsets.reserve(map.selected.size());
            for(auto const &range:map.selected) {
                if(range.source_index>=map.world.receivers.size())continue;
                auto const &surface=map.world.receivers[range.source_index];
                // Do NOT use cached geomode: COG may change it at any
                // moment without changing the world/surface allocation.
                uint32_t current_geomode=0;
                uintptr_t const surface_address=map.world.surfaces_addr+
                    uintptr_t(surface.surface_index)*0x5cU;
                engine_material_debug::read_object(
                    surface_address+0x1cU,current_geomode);
                hash_world_visibility(surface.surface_index);
                hash_world_visibility(current_geomode);
                if(!spotlight_shadow_world_face_visible(
                        current_geomode,surface.normal,view.forward))
                    continue;
                gpu_3do_group material_group{};
                material_group.material=surface.material;
                material_group.cel=surface.cel;
                // SetSurfaceCel can change world surface cel without
                // reallocating the static polygon snapshot.
                int32_t active_cel=0;
                auto const cel_address=map.world.surfaces_addr+
                    uintptr_t(surface.surface_index)*0x5cU+0x38U;
                if(engine_material_debug::read_object(cel_address,active_cel))
                    material_group.cel=active_cel;
                // Reuse the frame-scoped material lookup shared by 3DOs.
                uint64_t const mat_key=
                    (uint64_t(surface.material)<<32)|uint32_t(material_group.cel);
                auto found=gpu_frame_material_cache.find(mat_key);
                if(found==gpu_frame_material_cache.end())
                    found=gpu_frame_material_cache.emplace(
                        mat_key,resolve_gpu_3do_material(material_group)).first;
                auto const &tex=found->second;
                bool const cutout=tex.masked||tex.alpha||tex.dithered;
                if(tex.albedo_source==0 || tex.special ||
                   tex.emissive_only ||
                   tex.opacity<0.999f ||
                   (cutout && (tex.albedo_source!=1 ||
                               !surface.has_shadow_uv)) ||
                   range.count>uint32_t(std::numeric_limits<GLsizei>::max()))
                    continue;
                // A shadow never treats unresolved transparent adjoins as
                // opaque. Resolved non-alpha adjoins cast solid shadows.
                if(cutout)
                    masked_world.push_back({range,tex,material_group.cel});
                else {
                    opaque_world_counts.push_back(static_cast<GLsizei>(range.count));
                    opaque_world_offsets.push_back(reinterpret_cast<void const *>(
                        uintptr_t(range.first_index)*sizeof(uint32_t)));
                }
            }

            // Hash the actual opaque 3DO caster ranges and their node
            // transforms. A parked car under a fixed light is effectively
            // static: it should NOT force a depth pass every frame.
            uint64_t caster_signature=14695981039346656037ULL;
            auto hash_u32=[&](uint32_t v) {
                for(int b=0;b<4;++b) {
                    caster_signature^=uint64_t((v>>(b*8))&255U);
                    caster_signature*=1099511628211ULL;
                }
            };
            // Include current world surface visibility, not just MAT alpha.
            hash_u32(uint32_t(world_visibility_signature));
            hash_u32(uint32_t(world_visibility_signature>>32));
            auto hash_float=[&](float v) {
                uint32_t bits=0;
                std::memcpy(&bits,&v,sizeof(bits));
                hash_u32(bits);
            };
            // Textured casters may animate their material cel/scroll even
            // with fixed geometry. Include their coverage state in the
            // shadow-cache key so a stationary grate updates correctly.
            auto hash_material=[&](uintptr_t material,int32_t cel,
                                   gpu_3do_texture const &tex) {
                hash_u32(uint32_t(material));
                hash_u32(uint32_t(cel));
                if(cel<0 && material) {
                    engine_material_debug::native_material mat{};
                    if(engine_material_debug::read_object(material,mat))
                        hash_u32(mat.current_cel);
                }
                hash_float(tex.alpha_cutoff);
                hash_float(tex.opacity);
                hash_float(get<x>(tex.albedo_scroll));
                hash_float(get<y>(tex.albedo_scroll));
                hash_float(tex.albedo_transition);
            };
            for(auto const &draw:masked_world) {
                auto const &surface=map.world.receivers[draw.range.source_index];
                hash_u32(surface.surface_index);
                hash_material(surface.material,draw.current_cel,draw.texture);
            }
            for(auto const &caster:model_casters) {
                if(caster.cutout)
                    hash_material(caster.group->material,
                                  caster.group->cel,caster.texture);
                hash_u32(uint32_t(caster.instance->id));
                hash_u32(uint32_t(caster.instance->id>>32));
                hash_u32(caster.group->offset);
                hash_u32(caster.group->count);
                for(float value:caster.instance->transform) {
                    uint32_t bits=0;
                    std::memcpy(&bits,&value,sizeof(bits));
                    hash_u32(bits);
                }
            }
            if(the_config->profile_renderer &&
               (gpu_3do_present_serial%120U)==0U) {
                LOG_INFO("SHADOW spotlight: world_surfaces=",map.mesh.surfaces.size(),
                         " model_ranges=",model_casters.size(),
                         " excluded_source_ranges=",excluded_source_ranges,
                         " excluded_exact=",excluded_source_exact,
                         " excluded_attached=",excluded_source_family,
                         " nearby_other_ranges=",neighboring_other_ranges,
                         " nearest_other_owner=",nearest_other_owner,
                         " source_owner=",chosen.shadow_owner,
                         " model_cache=",the_config->use_gpu_cache?1:0,
                         " reused=",spotlight_shadow_cached &&
                                    caster_signature==spotlight_shadow_3do_signature?1:0);
            }
            // No CPU/GPU depth readback or per-frame geometry draw is
            // necessary when both light and caster transforms are unchanged.
            auto same=[](std::array<float,3> const &a,
                         std::array<float,3> const &b) {
                for(int i=0;i<3;++i)
                    if(std::fabs(a[i]-b[i])>0.00001f)return false;
                return true;
            };
            bool const unchanged=spotlight_shadow_cached &&
                spotlight_shadow_world==world &&
                spotlight_shadow_revision==map.revision &&
                chosen.material==spotlight_shadow_light.material &&
                chosen.shadow_owner==spotlight_shadow_light.shadow_owner &&
                same(chosen.origin,spotlight_shadow_light.origin) &&
                same(chosen.right,spotlight_shadow_light.right) &&
                same(chosen.up,spotlight_shadow_light.up) &&
                same(chosen.forward,spotlight_shadow_light.forward) &&
                std::fabs(chosen.width-spotlight_shadow_light.width)<0.00001f &&
                std::fabs(chosen.height-spotlight_shadow_light.height)<0.00001f &&
                std::fabs(chosen.range-spotlight_shadow_light.range)<0.00001f;
            if(unchanged &&
               caster_signature==spotlight_shadow_3do_signature) {
                spotlight_shadow_ready=true;
                return;
            }
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,map.fbo);
            gl::set_viewport(make_box(make_point(0,0),
                make_size(int(map.resolution),int(map.resolution))));
            gl::disable(gl::capability::blend);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::set_depth_function(gl::comparison_function::less);
            gl::clear_buffer_depth(1.0f);
            auto &prog=*ogs->spotlight_shadow_depth_program;
            prog.use_program();
            auto vec=[](std::array<float,3> const &a) {
                return make_point(a[0],a[1],a[2]);
            };
            prog.set_light(vec(view.origin),vec(view.right),
                           vec(view.up),vec(view.forward),
                           make_point(view.width,view.height,view.range,
                                      view.near_distance));
            auto set_shadow_cutout=[&](gpu_3do_texture const &tex,
                                       bool world_uv) {
                float const width=world_uv ? tex.width
                    : gpu_3do_model_uv_extent(tex.width);
                float const height=world_uv ? tex.height
                    : gpu_3do_model_uv_extent(tex.height);
                bool const second=tex.albedo_transition>0.0f &&
                                  tex.albedo_source2==1;
                prog.set_shadow_alpha(true,
                    make_point(1.0f/std::max(width,1.0f),
                               1.0f/std::max(height,1.0f)),
                    tex.albedo_scroll,
                    tex.masked ? tex.alpha_cutoff :
                    (tex.dithered ? 0.5f : 0.998f),
                    tex.opacity,second,tex.albedo_transition);
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 tex.texture);
                if(second) {
                    gl::set_active_texture_unit(12);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     tex.texture2);
                }
            };
            prog.set_model(make_point(1.0f,0.0f,0.0f),
                           make_point(0.0f,1.0f,0.0f),
                           make_point(0.0f,0.0f,1.0f),
                           make_point(0.0f,0.0f,0.0f));
            prog.set_shadow_alpha(false,make_point(1.0f,1.0f),
                make_point(0.0f,0.0f),0.5f,1.0f,false,0.0f);
            gl::bind_vertex_array(map.vao);
            if(!opaque_world_counts.empty())
                glMultiDrawElements(GL_TRIANGLES,opaque_world_counts.data(),
                    GL_UNSIGNED_INT,opaque_world_offsets.data(),
                    static_cast<GLsizei>(opaque_world_counts.size()));
            for(auto const &draw:masked_world) {
                set_shadow_cutout(draw.texture,true);
                gl::draw_elements(gl::element_type::triangles,
                    draw.range.count,gl::index_type::uint32,
                    static_cast<ptrdiff_t>(
                        draw.range.first_index*sizeof(uint32_t)));
            }
            prog.set_shadow_alpha(false,make_point(1.0f,1.0f),
                make_point(0.0f,0.0f),0.5f,1.0f,false,0.0f);
            for(auto const &caster:model_casters) {
                auto const &instance=*caster.instance;
                auto const &group=*caster.group;
                auto it=gpu_3do_gl_cache.find(instance.id);
                if(it==gpu_3do_gl_cache.end()) {
                    it=gpu_3do_gl_cache.emplace(instance.id,
                        std::make_unique<gpu_3do_gl_mesh>(*instance.geometry)).first;
                    if(the_config->profile_renderer)
                        ++profile_frame.gpu_uploads;
                }
                gpu_3do_last_drawn[instance.id]=gpu_3do_present_serial;
                auto const &m=instance.transform;
                prog.set_model(make_point(m[0],m[1],m[2]),
                               make_point(m[3],m[4],m[5]),
                               make_point(m[6],m[7],m[8]),
                               make_point(m[9],m[10],m[11]));
                // Match visible 3DO rendering. Two-sided/warped faces
                // are intentionally exempt; mirrored models invert winding.
                if((group.face_type&0x1U)!=0 || group.nonplanar) {
                    gl::disable(gl::capability::cull_face);
                } else {
                    gl::enable(gl::capability::cull_face);
                    gl::set_face_cull_mode(
                        gpu_3do_reversed_winding(instance.transform)
                            ? gl::face_mode::front : gl::face_mode::back);
                }
                gl::bind_vertex_array(it->second->vao);
                if(caster.cutout)
                    set_shadow_cutout(caster.texture,false);
                else
                    prog.set_shadow_alpha(false,make_point(1.0f,1.0f),
                        make_point(0.0f,0.0f),0.5f,1.0f,false,0.0f);
                gl::draw_elements(gl::element_type::triangles,group.count,
                                  gl::index_type::uint32,
                                  static_cast<ptrdiff_t>(
                                      group.offset*sizeof(uint32_t)));
            }
            // World draw state above stays double-sided for historical
            // sector winding; 3DOs explicitly honor their backfaces.
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::back);
            // The alpha caster pass can bind units 11/12. Never leak
            // its active texture state into the ordinary game renderer.
            gl::set_active_texture_unit(0);
            // The regular opaque pass binds its own FBO next, but it does
            // not restore the viewport; undo the small shadow viewport here.
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            spotlight_shadow_light=chosen;
            spotlight_shadow_view=view;
            spotlight_shadow_world=world;
            spotlight_shadow_revision=map.revision;
            spotlight_shadow_cached=true;
            spotlight_shadow_3do_signature=caster_signature;
            spotlight_shadow_ready=true;
        }

        // Fit one directional map to the nearby camera frustum, retaining
        // the loaded level's full light-axis depth for offscreen casters.
        void draw_sun_dynamic_shadow_atlas(
            sun_shadow_projection const &world_view,
            bool cached_world_view=false)
        {
            sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
            sun_dynamic_atlas_owners.clear();
            if(!the_config->enable_sun_shadows ||
               !the_config->enable_sun_dynamic_shadows ||
               !ogs->spotlight_shadow_depth_program)return;
            auto camera=read_active_render_camera();
            if(!camera.valid)return;
            struct candidate {
                uint64_t key=0;
                std::vector<gpu_3do_instance const *> parts;
                decal_volume_aabb bounds{};
                float distance2=0.0f;
                bool has_bounds=false;
            };
            std::map<uint64_t,candidate> keyed;
            for(auto const &inst:frame_gpu_3do_instances) {
                if(!inst.geometry || !inst.casts_sun_shadows ||
                   !inst.dynamic_sun_caster || !inst.shadow_owner ||
                   inst.is_projected_decal)continue;
                auto &c=keyed[inst.shadow_owner];
                c.key=inst.shadow_owner;c.parts.push_back(&inst);
                auto const &m=inst.transform;
                for(auto const &g:inst.geometry->groups) {
                    if(g.count<3U || (g.face_type&2U))continue;
                    for(int j=0;j<8;++j) {
                        float x=(j&1)?g.bounds_max[0]:g.bounds_min[0];
                        float y=(j&2)?g.bounds_max[1]:g.bounds_min[1];
                        float z=(j&4)?g.bounds_max[2]:g.bounds_min[2];
                        decal_vec3 p{{m[0]*x+m[3]*y+m[6]*z+m[9],
                                      m[1]*x+m[4]*y+m[7]*z+m[10],
                                      m[2]*x+m[5]*y+m[8]*z+m[11]}};
                        if(!decal_finite(p))continue;
                        // Valid geometry can be positioned at world origin.
                        // Track initialization explicitly, not via zero AABBs.
                        if(!c.has_bounds) {
                            c.bounds.low=p;c.bounds.high=p;
                            c.has_bounds=true;
                        }
                        else for(int k=0;k<3;++k) {
                            c.bounds.low[k]=std::min(c.bounds.low[k],p[k]);
                            c.bounds.high[k]=std::max(c.bounds.high[k],p[k]);
                        }
                    }
                }
            }
            std::vector<candidate *> sorted;
            // Match the cascades' camera-forward near distance. Project
            // the closest AABB corner along the camera axis, not its center,
            // so long cars and objects near a wide-FOV edge survive.
            float const near_depth=std::max(0.0f,
                the_config->sun_shadow_near_distance);
            size_t atlas_distance_culled=0;
            for(auto &entry:keyed) {
                auto &c=entry.second;
                if(!c.has_bounds)continue;
                auto const &lo=c.bounds.low,&hi=c.bounds.high;
                float closest_depth=0.0f;
                for(int axis=0;axis<3;++axis) {
                    float const edge=camera.forward[axis]>=0.0f
                        ? lo[axis] : hi[axis];
                    closest_depth+=camera.forward[axis]*
                        (edge-camera.position[axis]);
                }
                if(near_depth<=0.0f || closest_depth>near_depth) {
                    ++atlas_distance_culled;
                    continue;
                }
                c.distance2=0.0f;
                for(int i=0;i<3;++i) {
                    float d=(lo[i]+hi[i])*0.5f-camera.position[i];
                    c.distance2+=d*d;
                }
                sorted.push_back(&c);
            }
            std::sort(sorted.begin(),sorted.end(),
                [](candidate const *a,candidate const *b) {
                    return a->distance2<b->distance2;
                });
            if(sorted.empty())return;
            if(!sun_dynamic_target)
                sun_dynamic_target=std::make_unique<sun_dynamic_shadow_gpu>(
                    the_config->sun_dynamic_shadow_resolution);
            if(!sun_dynamic_target->complete)return;
            int const tile_size=int(sun_dynamic_target->tile_resolution);
            sun_dynamic_shadow_layout tile_layout(
                sun_dynamic_shadow_columns*tile_size,
                sun_dynamic_shadow_rows*tile_size);
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 sun_dynamic_target->fbo);
            gl::set_viewport(make_box(make_point(0,0),
                make_size(sun_dynamic_shadow_columns*tile_size,
                          sun_dynamic_shadow_rows*tile_size)));
            gl::disable(gl::capability::blend);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::set_depth_function(gl::comparison_function::less);
            gl::clear_buffer_depth(1.0f);
            // Slope-scale offset keeps grazing-angle caster depth off
            // the matching receiver plane, without blurring silhouettes.
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.25f*the_config->sun_shadow_caster_bias_scale,
                            1.5f*the_config->sun_shadow_caster_bias_scale);
            auto &prog=*ogs->spotlight_shadow_depth_program;
            prog.use_program();
            auto vec=[](decal_vec3 const &p) {
                return make_point(p[0],p[1],p[2]);
            };
            auto shadow_material=[&](gpu_3do_texture const &tex,
                                     bool cutout,bool sprite_cutout) {
                if(!cutout) {
                    prog.set_shadow_alpha(false,make_point(1.0f,1.0f),
                        make_point(0.0f,0.0f),0.5f,1.0f,false,0.0f);
                    return;
                }
                float w=gpu_3do_model_uv_extent(tex.width);
                float h=gpu_3do_model_uv_extent(tex.height);
                bool second=tex.albedo_transition>0.0f && tex.albedo_source2==1;
                prog.set_shadow_alpha(true,
                    make_point(1.0f/std::max(w,1.0f),
                               1.0f/std::max(h,1.0f)),
                    tex.albedo_scroll,
                    tex.masked?tex.alpha_cutoff:((tex.dithered||sprite_cutout)?0.5f:0.998f),
                    tex.opacity,second,tex.albedo_transition);
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 tex.texture);
                if(second) {
                    gl::set_active_texture_unit(12);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     tex.texture2);
                }
            };
            for(auto *object:sorted) {
                if(sun_dynamic_views.size()>=sun_dynamic_shadow_slots)break;
                sun_shadow_projection view{};
                if(!make_sun_dynamic_shadow_projection(
                    world_view,object->bounds,view))continue;
                if(cached_world_view) {
                    // The atlas still renders CURRENT moving-object bounds.
                    // Expand only its Z range if an object moved beyond the
                    // last validated static-world depth interval.
                    float z_min=decal_dot(view.origin,view.forward);
                    float z_max=z_min+view.range;
                    float caster_min=std::numeric_limits<float>::max();
                    float caster_max=-caster_min;
                    for(int corner=0;corner<8;++corner) {
                        decal_vec3 point{};
                        for(int axis=0;axis<3;++axis)
                            point[axis]=(corner&(1<<axis))
                                ?object->bounds.high[axis]
                                :object->bounds.low[axis];
                        float const z=decal_dot(point,view.forward);
                        caster_min=std::min(caster_min,z);
                        caster_max=std::max(caster_max,z);
                    }
                    float const margin=std::max(0.25f,view.range*0.02f);
                    z_min=std::min(z_min,caster_min-margin);
                    z_max=std::max(z_max,caster_max+margin);
                    float const change=z_min-
                        decal_dot(view.origin,view.forward);
                    for(int axis=0;axis<3;++axis)
                        view.origin[axis]+=view.forward[axis]*change;
                    view.range=z_max-z_min;
                }
                sun_dynamic_shadow_tile tile{};
                {
                    // Automatically size each dynamic shadow tile based on
                    // its fitted light-space footprint and camera distance.
                    // Target shadow texels per screen pixel in LIGHT space,
                    // not from the 3DO AABB diagonal. The fitted shadow
                    // rectangle also contains the caster's projected reach;
                    // a large car thus deserves more pixels than a small
                    // actor even when their apparent mesh sizes look similar.
                    // Radial distance is conservative for offscreen casters.
                    float const tan_half=std::tan(
                        camera.fov*0.008726646259971648f);
                    float const world_units_per_pixel=
                        2.0f*std::max(std::sqrt(object->distance2),0.1f)*
                        std::max(tan_half,0.05f)/
                        float(get<x>(conf_scr_res));
                    float const pixel_span=
                        std::max(view.width,view.height)/
                        std::max(world_units_per_pixel,0.0001f);
                    auto const desired=choose_sun_dynamic_shadow_tile(
                        pixel_span,sun_dynamic_target->tile_resolution);
                    // Leave minimum space for remaining eligible Things.
                    // A single large car must not evict all smaller casters.
                    std::size_t used_pixels=0;
                    for(auto const &used:sun_dynamic_tiles)
                        used_pixels+=std::size_t(used.size)*used.size;
                    auto const eligible=std::min(sorted.size(),
                        std::size_t(sun_dynamic_shadow_slots));
                    auto const remaining=eligible>sun_dynamic_views.size()+1
                        ? eligible-sun_dynamic_views.size()-1 : 0U;
                    auto const budget=std::size_t(
                        sun_dynamic_shadow_columns*tile_size)*
                        std::size_t(sun_dynamic_shadow_rows*tile_size);
                    auto const requested=sun_dynamic_shadow_budget_tile(
                        desired,used_pixels,budget,remaining);
                    if(!tile_layout.allocate(int(requested),tile))continue;
                }
                int const inner=tile.size-2*sun_dynamic_shadow_inset;
                gl::set_viewport(make_box(make_point(
                    tile.x+sun_dynamic_shadow_inset,
                    tile.y+sun_dynamic_shadow_inset),make_size(inner,inner)));
                prog.set_orthographic_light(vec(view.origin),vec(view.right),
                    vec(view.up),vec(view.forward),
                    make_point(view.width,view.height,view.range,0.0f));
                bool any=false;
                for(auto const *inst:object->parts) {
                    auto const &m=inst->transform;
                    prog.set_model(make_point(m[0],m[1],m[2]),
                        make_point(m[3],m[4],m[5]),make_point(m[6],m[7],m[8]),
                        make_point(m[9],m[10],m[11]));
                    auto it=gpu_3do_gl_cache.find(inst->id);
                    if(it==gpu_3do_gl_cache.end())
                        it=gpu_3do_gl_cache.emplace(inst->id,
                            std::make_unique<gpu_3do_gl_mesh>(
                                *inst->geometry)).first;
                    for(auto const &g:inst->geometry->groups) {
                        if(g.count<3U || (g.face_type&2U))continue;
                        // Share the same MAT/cel snapshot with both sun
                        // cascades. This avoids repeating native MAT lookups
                        // and VirtualQuery work per dynamic Thing/group.
                        bool const solid=inst->effective_geometry_mode==3 ||
                            g.solid_color;
                        uint64_t const key=(uint64_t(g.material)<<32)|
                            uint32_t(g.cel)|(solid?(uint64_t(1)<<63):0U);
                        auto found=gpu_frame_material_cache.find(key);
                        if(found==gpu_frame_material_cache.end()) {
                            auto mat=solid
                                ?resolve_gpu_3do_solid_material(g,false)
                                :resolve_gpu_3do_material(g);
                            found=gpu_frame_material_cache.emplace(
                                key,std::move(mat)).first;
                            if(the_config->profile_renderer)
                                ++sun_profile_atlas_mat_misses;
                        } else if(the_config->profile_renderer)
                            ++sun_profile_atlas_mat_hits;
                        auto const &tex=found->second;
                        bool const cutout=tex.masked||tex.alpha||
                            tex.dithered||
                            (inst->is_sprite_quad && tex.albedo_source==1);
                        if(tex.albedo_source==0 || tex.special ||
                           tex.emissive_only ||
                           tex.opacity<0.999f ||
                           (cutout && tex.albedo_source!=1))continue;
                        if((!the_config->sun_shadow_cull_back_faces &&
                             !the_config->sun_shadow_cull_front_faces) ||
                           (g.face_type&1U) || g.nonplanar)
                            gl::disable(gl::capability::cull_face);
                        else {
                            gl::enable(gl::capability::cull_face);
                            bool const cull_front=the_config->sun_shadow_cull_front_faces;
                            bool const mirrored=gpu_3do_reversed_winding(m);
                            gl::set_face_cull_mode(cull_front!=mirrored
                                ?gl::face_mode::front:gl::face_mode::back);
                        }
                        shadow_material(tex,cutout,inst->is_sprite_quad);
                        gl::bind_vertex_array(it->second->vao);
                        gl::draw_elements(gl::element_type::triangles,g.count,
                            gl::index_type::uint32,static_cast<ptrdiff_t>(
                                g.offset*sizeof(uint32_t)));
                        any=true;
                    }
                }
                if(any) {
                    sun_dynamic_views.push_back(view);
                    sun_dynamic_tiles.push_back(tile);
                    sun_dynamic_atlas_owners.insert(object->key);
                    if(the_config->profile_renderer_verbose &&
                       gpu_3do_present_serial%120U==0U) {
                        auto const &lo=object->bounds.low;
                        auto const &hi=object->bounds.high;
                        LOG_INFO("SUN atlas caster owner=",object->key,
                                 " parts=",object->parts.size(),
                                 " tile=",tile.size,
                                 " center=",
                                 std::to_string((lo[0]+hi[0])*0.5f),",",
                                 std::to_string((lo[1]+hi[1])*0.5f),",",
                                 std::to_string((lo[2]+hi[2])*0.5f));
                    }
                } else {
                    tile_layout.undo_last();
                }
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::set_active_texture_unit(0);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
            if(the_config->profile_renderer_verbose && gpu_3do_present_serial%120U==0U) {
                size_t shaded_pixels=0;
                std::ostringstream tile_sizes;
                for(size_t i=0;i<sun_dynamic_tiles.size();++i) {
                    auto const &tile=sun_dynamic_tiles[i];
                    if(i!=0)tile_sizes<<",";
                    tile_sizes<<tile.size;
                    auto const inner=tile.size-2*sun_dynamic_shadow_inset;
                    shaded_pixels+=size_t(inner)*size_t(inner);
                }
                // LOG_INFO/formatter_numeric accepts integers and strings,
                // but has no formatter<float> specialization.
                std::string const near_distance_label=std::to_string(near_depth);
                LOG_INFO("SUN dynamic shadow atlas Things=",
                    sun_dynamic_views.size()," candidates=",sorted.size(),
                    " outside_near_distance=",atlas_distance_culled,
                    " near_distance=",near_distance_label,
                    " base_tile=",tile_size,
                    " adaptive=1",
                    " tile_sizes=[",tile_sizes.str(),"]",
                    " depth_viewport_pixels=",shaded_pixels);
            }
        }

        // Rasterize SKY apertures from their actual SithWorld polygons
        // into a two-layer DEPTH texture matching the near/far sunlight
        // XY projections. The PCF depth maps remain independent of
        // this gate. Hidden 3DO Things are untouched.
        void draw_sun_sky_aperture(int cascade,
                                   sun_shadow_projection const &shadow_view,
                                   decal_native_read const &read)
        {
            if(!sun_sky_aperture_target ||
               !sun_sky_aperture_target->complete ||
               !ogs->spotlight_shadow_depth_program)return;
            auto &gate=*sun_sky_aperture_target;
            if(gate.loaded_world==0)return;
            auto const view=gate.expanded_sky_view(shadow_view);
            if(!(view.range>0.0f) || !std::isfinite(view.range))return;
            auto const signature=gate.live_signature(read);
            if(gate.valid[cascade] && gate.signatures[cascade]==signature &&
               sun_shadow_same_projection(view,gate.views[cascade]))
                return;

            GLint saved_draw_fbo=0,saved_read_fbo=0,saved_vao=0;
            GLint saved_program=0,saved_depth_func=0,saved_viewport[4]{};
            GLfloat saved_polygon_factor=0.0f,saved_polygon_units=0.0f;
            GLboolean saved_depth_mask=GL_TRUE;
            GLboolean const scissor=glIsEnabled(GL_SCISSOR_TEST);
            GLboolean const blend=glIsEnabled(GL_BLEND);
            GLboolean const cull=glIsEnabled(GL_CULL_FACE);
            GLboolean const depth_test=glIsEnabled(GL_DEPTH_TEST);
            GLboolean const polygon_offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&saved_draw_fbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&saved_read_fbo);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&saved_vao);
            glGetIntegerv(GL_CURRENT_PROGRAM,&saved_program);
            glGetIntegerv(GL_DEPTH_FUNC,&saved_depth_func);
            glGetIntegerv(GL_VIEWPORT,saved_viewport);
            glGetBooleanv(GL_DEPTH_WRITEMASK,&saved_depth_mask);
            glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&saved_polygon_factor);
            glGetFloatv(GL_POLYGON_OFFSET_UNITS,&saved_polygon_units);
            if(scissor)glDisable(GL_SCISSOR_TEST);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE); // sky openings are two-sided
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gate.fbo[cascade]);
            glViewport(0,0,GLsizei(gate.resolution),
                       GLsizei(gate.resolution));
            gl::clear_buffer_depth(1.0f);
            auto &shader=*ogs->spotlight_shadow_depth_program;
            shader.use_program();
            auto vec=[](decal_vec3 const &p) {
                return make_point(p[0],p[1],p[2]);
            };
            shader.set_model(make_point(1.0f,0.0f,0.0f),
                             make_point(0.0f,1.0f,0.0f),
                             make_point(0.0f,0.0f,1.0f),
                             make_point(0.0f,0.0f,0.0f));
            shader.set_shadow_alpha(false,make_point(1.0f,1.0f),
                make_point(0.0f,0.0f),0.5f,1.0f,false,0.0f);
            // Gather live SKY faces once, then submit the same mesh at
            // five tiny light-space offsets. This expands the sky silhouette
            // by less than one ordinary sun-shadow texel, placing its HARD
            // cutoff inside the ordinary depth-map wall shadows. The ordinary
            // sun-shadow map is unchanged. This work only happens when
            // the cached aperture projection or sky flags change.
            std::vector<GLsizei> sky_counts;
            std::vector<void const *> sky_offsets;
            sky_counts.reserve(gate.world.receivers.size());
            sky_offsets.reserve(gate.world.receivers.size());
            for(size_t i=0;i<gate.world.receivers.size();++i) {
                auto const &face=gate.world.receivers[i];
                uintptr_t const base=gate.world.surfaces_addr+
                    uintptr_t(face.surface_index)*0x5cU;
                uint32_t flags=0,adjoin=0;
                if(!read(base+0x10U,&flags,4U) ||
                   !read(base+0x0cU,&adjoin,4U) ||
                   !sun_sky_surface_eligible(flags,adjoin))
                    continue;
                auto const &range=gate.mesh.surfaces[i];
                if(range.count==0)continue;
                sky_counts.push_back(static_cast<GLsizei>(range.count));
                sky_offsets.push_back(reinterpret_cast<void const *>(
                    uintptr_t(range.first_index)*sizeof(uint32_t)));
            }
            if(!sky_counts.empty()) {
                gl::bind_vertex_array(gate.vao);
                // Use each cascade's actual shadow texel size, not the
                // aperture atlas's (potentially larger) resolution.
                unsigned const shadow_pixels=std::max(1U,cascade==0
                    ?sun_shadow_target->resolution
                    :sun_shadow_target->far_resolution);
                float const step_x=view.width/float(shadow_pixels);
                float const step_y=view.height/float(shadow_pixels);
                constexpr float sky_expansion_texels=0.75f;
                auto draw_sky_offset=[&](float dx,float dy) {
                    auto origin=view.origin;
                    for(int axis=0;axis<3;++axis)
                        origin[axis]-=sky_expansion_texels*(
                            dx*step_x*view.right[axis]+
                            dy*step_y*view.up[axis]);
                    shader.set_orthographic_light(
                        vec(origin),vec(view.right),vec(view.up),
                        vec(view.forward),
                        make_point(view.width,view.height,view.range,0.0f));
                    glMultiDrawElements(GL_TRIANGLES,sky_counts.data(),
                        GL_UNSIGNED_INT,sky_offsets.data(),
                        static_cast<GLsizei>(sky_counts.size()));
                };
                draw_sky_offset(0.0f,0.0f); // Original aperture coverage.
                for(float dx:{-1.0f,1.0f})
                    for(float dy:{-1.0f,1.0f})
                        draw_sky_offset(dx,dy);
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(saved_draw_fbo));
            glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(saved_read_fbo));
            glViewport(saved_viewport[0],saved_viewport[1],
                       saved_viewport[2],saved_viewport[3]);
            gl::bind_vertex_array(gl::vertex_array_view(GLuint(saved_vao)));
            gl::use_program(gl::program_view(GLuint(saved_program)));
            glDepthMask(saved_depth_mask);
            glDepthFunc(GLenum(saved_depth_func));
            glPolygonOffset(saved_polygon_factor,saved_polygon_units);
            if(scissor)glEnable(GL_SCISSOR_TEST);
            if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
            if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
            if(depth_test)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            if(polygon_offset)glEnable(GL_POLYGON_OFFSET_FILL);
            else glDisable(GL_POLYGON_OFFSET_FILL);
            gate.views[cascade]=view;
            gate.signatures[cascade]=signature;
            gate.valid[cascade]=true;
        }

        void draw_sun_shadow_map()
        {
            sun_shadow_ready=false;
            sun_shadow_frame_failure=nullptr;
            sun_shadow_cascade_ready={{false,false}};
            sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
            sun_dynamic_atlas_owners.clear();
            // Sun eligibility is constant for an ordinary level. A cheap
            // world-pointer check replaces the full per-frame surface scan;
            // a positive-sun transition forces a fresh audit immediately.
            bool const sun_active=
                (the_config->enable_sun || the_config->enable_sun_shadows) &&
                get_sun_settings().intensity.value()[0]>0.0f;
            if(sun_active) {
                refresh_sun_level_sky(!sun_level_sky_sun_active);
                sun_level_sky_sun_active=true;
            }
            else sun_level_sky_sun_active=false;
            if(!the_config->enable_sun_shadows || !sun_level_has_sky)return;
            auto const sun_pass_started=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            auto sun_failure=[&](char const *reason) {
                sun_shadow_frame_failure=reason;
                if(the_config->profile_renderer_verbose &&
                   gpu_3do_present_serial%120U==0U)
                    LOG_INFO("SUN shadow unavailable: ",reason);
            };
            if(!ogs->spotlight_shadow_depth_program ||
               !engine_material_debug::matches_tested_executable()) {
                sun_failure("shader missing or executable signature mismatch");
                return;
            }
            auto const sun=get_sun_settings();
            auto const direction=normalized_sun_direction(sun.direction.value());
            auto const rgb=sun.color.value();
            float const intensity=sun.intensity.value()[0];
            if(!(intensity>0.0f) || !std::isfinite(intensity)) {
                sun_failure("sunlight intensity not positive");
                return;
            }

            uint32_t world=0;
            if(!engine_material_debug::read_object(
                   engine_material_debug::current_world_global,world) ||
               !decal_native_address_ok(world)) {
                sun_failure("SithWorld pointer not readable");
                return;
            }
            decal_native_read const safe_read=[](uintptr_t ptr,void *dst,size_t n) {
                if(n==0 || !engine_camera_detail::readable(ptr,n))return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),n);
                return true;
            };
            if(!sun_shadow_target)
                sun_shadow_target=std::make_unique<sun_shadow_gpu>(
                    the_config->sun_shadow_resolution,
                    the_config->sun_shadow_far_resolution);
            auto &map=*sun_shadow_target;
            if(!map.update_world(safe_read,world)) {
                sun_failure("SithWorld shadow snapshot failed");
                return;
            }
            if(the_config->experimental_sun_sky_aperture &&
               sun_sky_aperture_hardware_supported()) {
                if(!sun_sky_aperture_target)
                    sun_sky_aperture_target=
                        std::make_unique<sun_sky_aperture_gpu>(
                            std::max(map.resolution,map.far_resolution));
                if(!sun_sky_aperture_target->update_world(safe_read,world) &&
                   the_config->profile_renderer_verbose &&
                   gpu_3do_present_serial%120U==0U)
                    LOG_WARNING("SUN sky aperture geometry unavailable");
            }
            auto const camera=read_active_render_camera();
            if(!camera.valid) {
                sun_failure("render camera unavailable");
                return;
            }
            float const horizontal=std::tan(camera.fov*
                (3.14159265358979323846f/360.0f));
            float const vertical=horizontal*float(get<y>(conf_scr_res))/
                float(get<x>(conf_scr_res));
            auto const validation_now=std::chrono::steady_clock::now();
            double const interval=the_config->sun_shadow_update_interval;
            bool cached_usable=sun_shadow_validated_world_view_ready;
            for(int cascade=0;cascade<2 && cached_usable;++cascade) {
                cached_usable=sun_shadow_cascade_cached[cascade] &&
                    sun_shadow_worlds[cascade]==world &&
                    sun_shadow_revisions[cascade]==map.revision;
                for(int axis=0;axis<3 && cached_usable;++axis)
                    cached_usable=std::fabs(direction[axis]-
                        sun_shadow_directions[cascade][axis])<1.0e-5f;
                if(!cached_usable)break;
                float const distance=cascade==0
                    ?the_config->sun_shadow_near_distance
                    :sun_shadow_effective_far_distance(
                         the_config->sun_shadow_far_distance,
                         the_config->sun_max_distance);
                cached_usable=sun_shadow_projection_covers_camera(
                    sun_shadow_views[cascade],camera.position,camera.right,
                    camera.up,camera.forward,horizontal,vertical,distance);
            }
            double const since_validation=
                sun_shadow_last_validation==
                    std::chrono::steady_clock::time_point{}
                    ?std::numeric_limits<double>::infinity()
                    :std::chrono::duration<double>(
                        validation_now-sun_shadow_last_validation).count();
            bool const skip_cascade_validation=
                !sun_shadow_cascade_validation_due(
                    since_validation,interval,cached_usable);
            if(the_config->profile_renderer) {
                if(skip_cascade_validation)++sun_profile_throttle_skipped;
                else if(interval>0.0) {
                    ++sun_profile_throttle_checked;
                    if(since_validation>=interval)
                        ++sun_profile_throttle_due_timer;
                    else if(since_validation>=0.0) {
                        ++sun_profile_throttle_forced_early;
                        if(!sun_shadow_validated_world_view_ready)
                            ++sun_profile_forced_unready;
                        else for(int c=0;c<2;++c) {
                            if(!sun_shadow_cascade_cached[c] ||
                               sun_shadow_worlds[c]!=world ||
                               sun_shadow_revisions[c]!=map.revision) {
                                ++sun_profile_forced_world;
                                continue;
                            }
                            bool direction_changed=false;
                            for(int axis=0;axis<3;++axis)
                                direction_changed=direction_changed ||
                                    std::fabs(direction[axis]-
                                        sun_shadow_directions[c][axis])>=1.0e-5f;
                            if(direction_changed) {
                                ++sun_profile_forced_sun;
                                continue;
                            }
                            float const distance=c==0
                                ?the_config->sun_shadow_near_distance
                                :sun_shadow_effective_far_distance(
                                     the_config->sun_shadow_far_distance,
                                     the_config->sun_max_distance);
                            if(!sun_shadow_projection_covers_camera(
                                    sun_shadow_views[c],camera.position,
                                    camera.right,camera.up,camera.forward,
                                    horizontal,vertical,distance))
                                ++sun_profile_forced_coverage[c];
                        }
                    }
                }
            }
            sun_shadow_projection world_view=sun_shadow_validated_world_view;
            // Avoid three full sets of transformed group bounds (projection,
            // near cascade, far cascade), especially in crowded 3DO levels.
            // A level may have only a tiny native-world shell while its
            // playable architecture is made from 3DOs. Include submitted
            // model groups before fitting the sun map; otherwise both the
            // receiver lighting and model shadows fall outside the map.
            struct sun_model_candidate {
                gpu_3do_instance const *instance;
                gpu_3do_group const *group;
                decal_volume_aabb bounds;
                // The frame material cache owns this object until the next
                // game frame. unordered_map rehash preserves element addresses.
                gpu_3do_texture const *texture;
                bool cutout;
                uint64_t signature;
            };
            std::vector<sun_model_candidate> model_candidates;
            size_t skipped_skybox_instances=0;
            if(!skip_cascade_validation) {
            std::vector<decal_volume_aabb> model_bounds;
            auto const candidate_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            // Only ACTUAL depth casters are allowed to contribute to the
            // light-space bounds. Previously an emissive-only / blended VFX
            // Thing could enlarge the shared sun depth range while its faces
            // were never submitted to the depth map. This was particularly
            // destructive with non-unit Thing R/L/U axes (legacy VFX scale).
            // Reuse the same per-frame MAT cache and policy in both phases:
            // never resolve a material twice just to build the projection.
            auto sun_caster_material=[&](
                gpu_3do_group const &group,
                gpu_3do_instance const &instance)->gpu_3do_texture const & {
                bool const solid=instance.effective_geometry_mode==3 ||
                    group.solid_color;
                uint64_t const key=(uint64_t(group.material)<<32)|
                    uint32_t(group.cel)|(solid?(uint64_t(1)<<63):0U);
                auto found=gpu_frame_material_cache.find(key);
                if(found==gpu_frame_material_cache.end()) {
                    auto mat=solid
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group);
                    found=gpu_frame_material_cache.emplace(
                        key,std::move(mat)).first;
                }
                return found->second;
            };
            // The group hash is independent of cascade projection and
            // membership. Compute it once per frame instead of again for
            // near and far; keep the exact original hash inputs and mixing.
            auto mix_model_signature=[](uint64_t h) {
                h^=h>>30;h*=0xbf58476d1ce4e5b9ULL;
                h^=h>>27;h*=0x94d049bb133111ebULL;
                return h^(h>>31);
            };
            size_t noncaster_bounds_skipped=0;
            uint64_t candidate_sample_index=0;
            // Verify exact source geometry and pose for each slot. Do not
            // assume frame submission order is stable between frames.
            sun_static_geometry_cache.resize(frame_gpu_3do_instances.size());
            for(size_t instance_idx=0;
                instance_idx<frame_gpu_3do_instances.size();++instance_idx) {
                auto const &instance=frame_gpu_3do_instances[instance_idx];
                if(!instance.casts_sun_shadows) {
                    ++skipped_skybox_instances;
                    continue;
                }
                if(!instance.geometry || instance.is_projected_decal)continue;
                auto const &m=instance.transform;
                auto *cached=static_cast<sun_cached_static_instance *>(nullptr);
                // Dynamic/unknown objects and camera-dependent sprite poses
                // keep the original recomputation behavior.
                if(instance.shadow_owner!=0 && !instance.dynamic_sun_caster &&
                   !instance.is_sprite_quad && !instance.transient_procedural) {
                    auto &slot=sun_static_geometry_cache[instance_idx];
                    if(!sun_shadow_static_geometry_reusable(
                           instance.shadow_owner,instance.id,
                           reinterpret_cast<uintptr_t>(instance.geometry.get()),
                           m,instance.geometry->groups.size(),
                           slot.owner,slot.id,
                           reinterpret_cast<uintptr_t>(slot.geometry),
                           slot.transform,slot.groups.size(),false)) {
                        if(the_config->profile_renderer && slot.owner!=0)
                            ++sun_profile_static_instance_invalidations;
                        slot.owner=instance.shadow_owner;
                        slot.id=instance.id;
                        slot.geometry=instance.geometry.get();
                        slot.transform=m;
                        slot.groups.assign(instance.geometry->groups.size(),{});
                    }
                    cached=&slot;
                }
                auto const &groups=instance.geometry->groups;
                for(size_t group_idx=0;group_idx<groups.size();++group_idx) {
                    auto const &group=groups[group_idx];
                    if(group.count<3U || (group.face_type&0x2U))continue;
                    // One of every 64 eligible-for-testing groups is timed.
                    // Avoid thousands of expensive clock calls per frame.
                    bool const sample=the_config->profile_renderer &&
                        ((candidate_sample_index++ & 63U)==0U);
                    auto const mat_begin=sample
                        ?profile_clock::now():profile_clock::time_point{};
                    auto const &tex=sun_caster_material(group,instance);
                    bool const cutout=tex.masked||tex.alpha||tex.dithered||
                        (instance.is_sprite_quad && tex.albedo_source==1);
                    bool const eligible=sun_shadow_material_casts_depth(
                        tex.albedo_source,tex.special,tex.emissive_only,
                        tex.opacity,cutout);
                    if(sample) {
                        sun_profile_candidate_material_sample_ns+=elapsed_profile_ns(
                            mat_begin,profile_clock::now());
                        ++sun_profile_candidate_material_samples;
                    }
                    if(!eligible) {
                        ++noncaster_bounds_skipped;
                        continue;
                    }
                    auto const bounds_begin=sample
                        ?profile_clock::now():profile_clock::time_point{};
                    auto *group_cache=cached?&cached->groups[group_idx]:nullptr;
                    if(group_cache) {
                        // Fail closed if a nominally immutable GPU group is
                        // edited in place without changing its mesh pointer.
                        bool const same_group=group_cache->descriptor_valid &&
                            group_cache->offset==group.offset &&
                            group_cache->count==group.count &&
                            group_cache->material==group.material &&
                            group_cache->cel==group.cel &&
                            group_cache->bounds_min==group.bounds_min &&
                            group_cache->bounds_max==group.bounds_max;
                        if(!same_group) {
                            if(the_config->profile_renderer &&
                               group_cache->descriptor_valid)
                                ++sun_profile_static_group_invalidations;
                            group_cache->offset=group.offset;
                            group_cache->count=group.count;
                            group_cache->material=group.material;
                            group_cache->cel=group.cel;
                            group_cache->bounds_min=group.bounds_min;
                            group_cache->bounds_max=group.bounds_max;
                            group_cache->descriptor_valid=true;
                            group_cache->bounds_valid=false;
                            group_cache->opaque_signature_valid=false;
                            group_cache->cutout_signature_valid=false;
                        }
                    }
                    bool const reuse_bounds=group_cache &&
                        group_cache->bounds_valid;
                    decal_volume_aabb bounds=reuse_bounds
                        ?group_cache->bounds
                        :sun_shadow_transformed_aabb(
                             group.bounds_min,group.bounds_max,m);
                    if(!reuse_bounds && group_cache &&
                       decal_finite(bounds.low) && decal_finite(bounds.high)) {
                        group_cache->bounds=bounds;
                        group_cache->bounds_valid=true;
                    }
                    if(the_config->profile_renderer) {
                        if(reuse_bounds)++sun_profile_static_bounds_reused;
                        else ++sun_profile_static_bounds_calculated;
                    }
                    if(sample) {
                        sun_profile_candidate_bounds_sample_ns+=elapsed_profile_ns(
                            bounds_begin,profile_clock::now());
                        ++sun_profile_candidate_bounds_samples;
                    }
                    if(decal_finite(bounds.low) && decal_finite(bounds.high)) {
                        auto const signature_begin=sample
                            ?profile_clock::now():profile_clock::time_point{};
                        // Check the exact five live MAT inputs that the
                        // original group hash incorporated for cutout faces.
                        std::array<uint32_t,5> const cutout_inputs=
                            cutout && group_cache
                            ?sun_shadow_cutout_signature_inputs(
                                tex.alpha_cutoff,tex.opacity,
                                get<x>(tex.albedo_scroll),
                                get<y>(tex.albedo_scroll),
                                tex.albedo_transition)
                            :std::array<uint32_t,5>{};
                        bool const reuse_opaque=!cutout && group_cache &&
                            group_cache->opaque_signature_valid;
                        bool const reuse_cutout=cutout && group_cache &&
                            group_cache->cutout_signature_valid &&
                            group_cache->cutout_inputs==cutout_inputs;
                        bool const reuse_signature=reuse_opaque||reuse_cutout;
                        if(the_config->profile_renderer && cutout &&
                           group_cache && group_cache->cutout_signature_valid &&
                           !reuse_cutout)
                            ++sun_profile_cutout_material_invalidations;
                        uint64_t member=reuse_opaque
                            ?group_cache->opaque_signature
                            :reuse_cutout?group_cache->cutout_signature
                            :14695981039346656037ULL;
                        if(!reuse_signature) {
                        auto member_u32=[&](uint32_t v) {
                            for(int b=0;b<4;++b) {
                                member^=uint64_t((v>>(b*8))&255U);
                                member*=1099511628211ULL;
                            }
                        };
                        auto member_float=[&](float v) {
                            uint32_t bits=0;
                            std::memcpy(&bits,&v,sizeof(bits));
                            member_u32(bits);
                        };
                        member_u32(uint32_t(instance.id));
                        member_u32(uint32_t(instance.id>>32));
                        member_u32(group.offset);
                        member_u32(group.count);
                        member_u32(uint32_t(instance.shadow_owner));
                        member_u32(uint32_t(instance.shadow_owner>>32));
                        member_u32(uint32_t(group.material));
                        member_u32(uint32_t(group.cel));
                        if(cutout) {
                            member_float(tex.alpha_cutoff);
                            member_float(tex.opacity);
                            member_float(get<x>(tex.albedo_scroll));
                            member_float(get<y>(tex.albedo_scroll));
                            member_float(tex.albedo_transition);
                        }
                        for(float v:instance.transform)member_float(v);
                        member=mix_model_signature(member);
                        if(group_cache) {
                            if(cutout) {
                                group_cache->cutout_signature=member;
                                group_cache->cutout_inputs=cutout_inputs;
                                group_cache->cutout_signature_valid=true;
                            } else {
                                group_cache->opaque_signature=member;
                                group_cache->opaque_signature_valid=true;
                            }
                        }
                        } // full hash on changed pose, geometry or material
                        if(the_config->profile_renderer) {
                            if(reuse_opaque)++sun_profile_static_signature_reused;
                            else if(reuse_cutout)++sun_profile_cutout_hash_reused;
                            else ++sun_profile_static_signature_calculated;
                            if(cutout && !reuse_cutout)
                                ++sun_profile_cutout_hash_calculated;
                        }
                        if(sample) {
                            sun_profile_candidate_signature_sample_ns+=
                                elapsed_profile_ns(
                                    signature_begin,profile_clock::now());
                            ++sun_profile_candidate_signature_samples;
                        }
                        model_bounds.push_back(bounds);
                        model_candidates.push_back(
                            {&instance,&group,bounds,&tex,cutout,member});
                    }
                }
            }
            if(the_config->profile_renderer) {
                sun_profile_candidate_us+=elapsed_profile_us(
                    candidate_begin,profile_clock::now());
                sun_profile_candidate_groups+=model_candidates.size();
            }
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN caster bounds depth_eligible_groups=",
                         model_candidates.size(),
                         " noncaster_material_groups_skipped=",
                         noncaster_bounds_skipped);
            world_view={};
            if(!make_sun_shadow_projection(map.world,direction,world_view,model_bounds)) {
                sun_failure("no valid sun projection (world + casting 3DO bounds)");
                return;
            }
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U) {
                std::ostringstream diag;
                diag<<"SUN fitted global depth range="<<world_view.range
                    <<" depth_origin="
                    <<decal_dot(world_view.origin,world_view.forward);
                LOG_INFO(diag.str());
            }
            } // expensive global caster bounds/projection: checked frames only
            presentation_gpu_profile::completed_sample sun_completed{};
            bool const sun_gpu_timing=the_config->profile_renderer &&
                sun_gpu_queries.begin(sun_completed);
            if(sun_completed.ready) {
                for(size_t i=0;i<sun_gpu_ns.size();++i)
                    sun_gpu_ns[i]+=sun_completed.nanoseconds[i];
                ++sun_gpu_samples;
            }
            if(sun_completed.skipped)++sun_gpu_skipped;
            // Atlas first: exclude only successfully drawn Things from CSM.
            // Excess candidates retain the normal cascade fallback.
            auto const atlas_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            if(sun_gpu_timing)sun_gpu_queries.timestamp(0U,true);
            draw_sun_dynamic_shadow_atlas(world_view,skip_cascade_validation);
            if(sun_gpu_timing)sun_gpu_queries.timestamp(0U,false);
            if(the_config->profile_renderer)
                sun_profile_atlas_us+=elapsed_profile_us(
                    atlas_begin,profile_clock::now());
            // Diagnose non-unit or mirrored SetModelNodeRLUVecs placements.
            // These are valid Sith transforms; do not normalize their axes.
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U) {
                size_t transformed=0,moving=0,in_atlas=0,excluded=0;
                size_t logged=0;
                for(auto const &inst:frame_gpu_3do_instances) {
                    if(!inst.geometry || inst.is_projected_decal)continue;
                    auto const &m=inst.transform;
                    float lengths[3]{};
                    for(int i=0;i<3;++i) {
                        int const j=3*i;
                        lengths[i]=std::sqrt(m[j]*m[j]+m[j+1]*m[j+1]+
                                             m[j+2]*m[j+2]);
                    }
                    float const determinant=gpu_3do_transform_determinant(m);
                    if(std::fabs(lengths[0]-1.0f)<0.005f &&
                       std::fabs(lengths[1]-1.0f)<0.005f &&
                       std::fabs(lengths[2]-1.0f)<0.005f &&
                       determinant>=0.0f)continue;
                    ++transformed;
                    if(!inst.casts_sun_shadows)++excluded;
                    if(inst.dynamic_sun_caster)++moving;
                    bool const atlas=inst.shadow_owner!=0 &&
                        sun_dynamic_atlas_owners.count(inst.shadow_owner)!=0;
                    if(atlas)++in_atlas;
                    if(logged++<8U) {
                        LOG_INFO("SUN RLU owner=",inst.shadow_owner,
                                 " mesh=",inst.id,
                                 " moving=",inst.dynamic_sun_caster?1:0,
                                 " casts=",inst.casts_sun_shadows?1:0,
                                 " atlas=",atlas?1:0,
                                 " axis_lengths=",std::to_string(lengths[0]),
                                 ",",std::to_string(lengths[1]),
                                 ",",std::to_string(lengths[2]),
                                 " determinant=",std::to_string(determinant));
                    }
                }
                LOG_INFO("SUN RLU total=",transformed,
                         " moving=",moving," excluded=",excluded,
                         " atlas=",in_atlas,
                         " atlas_owners=",sun_dynamic_atlas_owners.size());
                // Some moving Things never reach the atlas. Report their
                // source owner, not just the number of successful tiles.
                std::unordered_set<uint64_t> moving_owners;
                size_t moving_parts=0,unknown_owner_parts=0,listed=0;
                for(auto const &inst:frame_gpu_3do_instances) {
                    if(!inst.geometry || !inst.casts_sun_shadows ||
                       !inst.dynamic_sun_caster || inst.is_projected_decal)
                        continue;
                    ++moving_parts;
                    if(!inst.shadow_owner)++unknown_owner_parts;
                    if(!moving_owners.insert(inst.shadow_owner).second ||
                       listed>=8U)continue;
                    ++listed;
                    auto const &m=inst.transform;
                    LOG_INFO("SUN moving owner=",inst.shadow_owner,
                             " mesh=",inst.id,
                             " atlas=",sun_dynamic_atlas_owners.count(
                                 inst.shadow_owner)?1:0,
                             " origin=",std::to_string(m[9]),",",
                             std::to_string(m[10]),",",std::to_string(m[11]));
                }
                LOG_INFO("SUN moving coverage parts=",moving_parts,
                         " owners=",moving_owners.size(),
                         " unknown_owner_parts=",unknown_owner_parts,
                         " atlas_owners=",sun_dynamic_atlas_owners.size());
            }
            if(!skip_cascade_validation) {
            for(int cascade=0;cascade<2;++cascade) {
            auto const cascade_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            sun_shadow_projection view=world_view;
            float const distance=cascade==0
                ? the_config->sun_shadow_near_distance
                : sun_shadow_effective_far_distance(
                      the_config->sun_shadow_far_distance,
                      the_config->sun_max_distance);
            if(!fit_sun_shadow_to_camera(view,camera.position,camera.right,
                    camera.up,camera.forward,horizontal,vertical,distance)) {
                sun_failure("camera-local cascade projection invalid");
                if(sun_gpu_timing) {
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,true);
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,false);
                }
                continue;
            }
            // Reuse an already rendered projection whenever the ACTUAL
            // camera frustum still fits within its coverage. Rotation and
            // translation no longer force redundant 4096px shadow redraws.
            // Keep the same sunlight depth-axis origin/range too: an updated
            // world AABB must never silently clip new casters in Z.
            bool locked_view=false;
            if(sun_shadow_cascade_cached[cascade] &&
               sun_shadow_worlds[cascade]==world &&
               sun_shadow_revisions[cascade]==map.revision) {
                auto const &previous=sun_shadow_views[cascade];
                bool same_axes=true;
                for(int axis=0;axis<3;++axis)
                    same_axes=same_axes &&
                        std::fabs(previous.forward[axis]-world_view.forward[axis])<1.e-5f &&
                        std::fabs(previous.right[axis]-world_view.right[axis])<1.e-5f &&
                        std::fabs(previous.up[axis]-world_view.up[axis])<1.e-5f;
                same_axes=same_axes &&
                    std::fabs(previous.range-world_view.range)<1.e-4f &&
                    std::fabs(decal_dot(previous.origin,previous.forward)-
                              decal_dot(world_view.origin,world_view.forward))<1.e-4f;
                if(same_axes && sun_shadow_projection_covers_camera(
                       previous,camera.position,camera.right,camera.up,
                       camera.forward,horizontal,vertical,distance)) {
                    view=previous;
                    locked_view=true;
                }
            }
            unsigned const cascade_resolution=cascade==0
                ?map.resolution:map.far_resolution;
            if(!locked_view &&
               !stabilize_sun_shadow_projection(view,cascade_resolution)) {
                sun_failure("camera-local cascade stabilization failed");
                if(sun_gpu_timing) {
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,true);
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,false);
                }
                continue;
            }

            if(the_config->experimental_sun_sky_aperture &&
               sun_sky_aperture_hardware_supported())
                draw_sun_sky_aperture(cascade,view,safe_read);
            // Phase 2 experimental fast sky-first WORLD depth selector.
            // The depth filter and dynamic/static 3DOs are unchanged.
            sun_sky_caster_fast_mask sky_prune_mask{};
            bool sky_prune_ready=false;
            uint64_t sky_prune_build_us=0;
            size_t sky_pruned_world=0,sky_tested_world=0;
            if(the_config->experimental_sun_sky_caster_pruning &&
               the_config->experimental_sun_sky_aperture &&
               sun_level_sky_known &&
               sun_sky_aperture_target &&
               sun_sky_aperture_target->loaded_world==world &&
               sun_sky_aperture_target->valid[0] &&
               sun_sky_aperture_target->valid[1] &&
               cascade_resolution>=256U) {
                auto const build_begin=profile_clock::now();
                sky_prune_mask.reset(view);
                unsigned eligible_in_snapshot=0;
                uint64_t snapshot_identity=14695981039346656037ULL;
                auto const &gate=*sun_sky_aperture_target;
                for(auto const &face:gate.world.receivers) {
                    uintptr_t const addr=gate.world.surfaces_addr+
                        uintptr_t(face.surface_index)*0x5cU;
                    uint32_t flags=0,adjoin=0;
                    if(!safe_read(addr+0x10U,&flags,4U) ||
                       !safe_read(addr+0x0cU,&adjoin,4U)) {
                        sky_prune_mask.mark_all();
                        break;
                    }
                    if(!sun_sky_surface_eligible(flags,adjoin))continue;
                    ++eligible_in_snapshot;
                    snapshot_identity^=uint64_t(face.surface_index);
                    snapshot_identity*=1099511628211ULL;
                    sky_prune_mask.include_polygon(face.polygon);
                }
                // Snapshot/read mismatch is never grounds for culling.
                if(eligible_in_snapshot!=sun_level_sky_faces ||
                   snapshot_identity!=sun_level_sky_identity)
                    sky_prune_mask.mark_all();
                sky_prune_ready=sky_prune_mask.valid &&
                    !sky_prune_mask.fail_open;
                sky_prune_build_us=elapsed_profile_us(
                    build_begin,profile_clock::now());
            }
            if(the_config->profile_renderer)
                sun_profile_projection_us[cascade]+=elapsed_profile_us(
                    cascade_begin,profile_clock::now());
            auto const world_query_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            // Query the complete orthographic box. The spotlight helper's
            // perspective pyramid bounds do not cover a directional map.
            map.selected.clear();
            auto const world_bounds=sun_shadow_volume_bounds(view);
            std::vector<uint32_t> selected_surfaces;
            map.world.acceleration.query(map.world.receivers,
                world_bounds.low,world_bounds.high,selected_surfaces);
            map.selected.reserve(selected_surfaces.size());
            for(uint32_t id:selected_surfaces)
                if(id<map.mesh.surfaces.size() && map.mesh.surfaces[id].count>0U)
                    map.selected.push_back(map.mesh.surfaces[id]);
            if(the_config->profile_renderer)
                sun_profile_world_query_us[cascade]+=elapsed_profile_us(
                    world_query_begin,profile_clock::now());
            auto const world_filter_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            std::vector<GLsizei> counts;
            std::vector<void const *> offsets;
            struct sun_world_cutout {
                spotlight_shadow_surface_range range;
                gpu_3do_texture texture;
            };
            std::vector<sun_world_cutout> world_cutouts;
            size_t world_hidden=0,world_backfacing=0,world_material_skipped=0;
            uint64_t caster_signature=14695981039346656037ULL;
            auto hash_u32=[&](uint32_t v) {
                for(int b=0;b<4;++b) {
                    caster_signature^=uint64_t((v>>(b*8))&255U);
                    caster_signature*=1099511628211ULL;
                }
            };
            auto hash_float=[&](float v) {
                uint32_t bits=0;std::memcpy(&bits,&v,sizeof(bits));hash_u32(bits);
            };
            for(float v:direction)hash_float(v);
            // Cache signatures MUST change with the active sky mask.
            hash_u32(sky_prune_ready?1U:0U);
            if(sky_prune_ready) {
                uint64_t const key=sky_prune_mask.signature();
                hash_u32(uint32_t(key));
                hash_u32(uint32_t(key>>32));
            }
            // Read live geomodes and MAT coverage each frame. COG changes to
            // a surface therefore invalidate the cached depth image.
            //
            // engine_material_debug::read_object() performs a VirtualQuery()
            // system call for every read. In an open level thousands of
            // surfaces are selected, and this loop runs once per cascade per
            // frame with two reads per surface. Validate the native surface
            // table as ONE block per frame, then read the two fields directly.
            // If the table cannot be validated as a single readable block the
            // original per-read checked path is used unchanged.
            bool const surface_table_ok=map.world.surfaces_addr!=0U &&
                map.world.surface_count>0U &&
                engine_camera_detail::readable(map.world.surfaces_addr,
                    size_t(map.world.surface_count)*0x5cU);
            auto read_surface_field=[&](uint32_t surface_index,
                                        uintptr_t field_offset,auto &out) {
                uintptr_t const address=map.world.surfaces_addr+
                    uintptr_t(surface_index)*0x5cU+field_offset;
                if(surface_table_ok && surface_index<map.world.surface_count) {
                    std::memcpy(&out,reinterpret_cast<void const *>(address),
                                sizeof(out));
                    return true;
                }
                return engine_material_debug::read_object(address,out);
            };
            for(auto const &range:map.selected) {
                if(range.source_index>=map.world.receivers.size())continue;
                auto const &surface=map.world.receivers[range.source_index];
                uint32_t geomode=0;
                read_surface_field(surface.surface_index,0x1cU,geomode);
                hash_u32(surface.surface_index);hash_u32(geomode);
                if(geomode<2U || !decal_finite(surface.normal)) {
                    ++world_hidden;
                    continue;
                }
                if(sky_prune_ready) {
                    ++sky_tested_world;
                    if(!sky_prune_mask.may_overlap(surface.polygon)) {
                        ++sky_pruned_world;
                        continue;
                    }
                }
                gpu_3do_group group{};group.material=surface.material;
                group.cel=surface.cel;
                int32_t cel=group.cel;
                read_surface_field(surface.surface_index,0x38U,cel);
                group.cel=cel;
                uint64_t const key=(uint64_t(surface.material)<<32)|uint32_t(cel);
                auto found=gpu_frame_material_cache.find(key);
                if(found==gpu_frame_material_cache.end())
                    found=gpu_frame_material_cache.emplace(
                        key,resolve_gpu_3do_material(group)).first;
                auto const &tex=found->second;
                bool const cutout=tex.masked||tex.alpha||tex.dithered;
                // Keep cutout MAT silhouettes using their shadow UVs.
                if(tex.albedo_source==0 || tex.special || tex.emissive_only || tex.opacity<0.999f ||
                   (cutout && (tex.albedo_source!=1 || !surface.has_shadow_uv)) ||
                   range.count>uint32_t(std::numeric_limits<GLsizei>::max())) {
                    ++world_material_skipped;
                    continue;
                }
                if(cutout)world_cutouts.push_back({range,tex});
                else {
                    counts.push_back(static_cast<GLsizei>(range.count));
                    offsets.push_back(reinterpret_cast<void const *>(
                        uintptr_t(range.first_index)*sizeof(uint32_t)));
                }
                hash_u32(surface.surface_index);hash_u32(uint32_t(cel));
            }
            for(auto const &draw:world_cutouts) {
                auto const &surface=map.world.receivers[draw.range.source_index];
                hash_u32(surface.surface_index);
                hash_float(draw.texture.alpha_cutoff);
                hash_float(draw.texture.opacity);
                hash_float(get<x>(draw.texture.albedo_scroll));
                hash_float(get<y>(draw.texture.albedo_scroll));
                hash_float(draw.texture.albedo_transition);
            }
            if(the_config->experimental_sun_sky_caster_pruning &&
               gpu_3do_present_serial%120U==0U) {
                LOG_INFO("SUN sky-prune cascade=",cascade,
                    " active=",sky_prune_ready?1:0,
                    " tested=",sky_tested_world,
                    " pruned=",sky_pruned_world,
                    " coverage_tiles=",sky_prune_mask.occupied_tiles(),
                    "/16384 build_cpu_us=",sky_prune_build_us,
                    " fail_open=",sky_prune_mask.fail_open?1:0,
                    " culling_applied=",sky_prune_ready?1:0);
            }
            if(the_config->profile_renderer)
                sun_profile_world_filter_us[cascade]+=elapsed_profile_us(
                    world_filter_begin,profile_clock::now());
            // Keep the stable world state and add stationary 3DOs, but
            // NEVER include moving/unknown Thing geometry in this key.
            uint64_t static_signature=caster_signature;
            // Shadow-map depth is independent of the order of model draws.
            // Native JK/COG can resubmit the same Things in a different
            // order across frames. Never invalidate a depth cache merely
            // because enumeration order changed: hash the *multiset* of
            // groups using two independently aggregated 64-bit quantities.
            // Keep group count so identical duplicate groups are preserved.
            uint64_t all_sum=0,all_sum_sq=0,static_sum=0,static_sum_sq=0;
            uint64_t all_count=0,static_count=0;
            auto append_model_set=[&](uint64_t &signature,uint64_t count,
                                      uint64_t sum,uint64_t squared) {
                auto append=[&](uint32_t v) {
                    for(int b=0;b<4;++b) {
                        signature^=uint64_t((v>>(b*8))&255U);
                        signature*=1099511628211ULL;
                    }
                };
                for(uint64_t v:{count,sum,squared}) {
                    append(uint32_t(v));append(uint32_t(v>>32));
                }
            };

            // Include submitted 3DO transforms. The native hook is enabled
            // when this feature is on; model groups share their normal GPU
            // buffers with the visible renderer.
            auto const bounds=world_bounds;
            struct sun_model_draw {gpu_3do_instance const *instance;
                                   gpu_3do_group const *group;
                                   gpu_3do_texture texture;bool cutout;
                                   decal_volume_aabb bounds;};
            std::vector<sun_model_draw> models,static_models;
            size_t dynamic_far_skipped=0,dynamic_near_distance_skipped=0;
            float const dynamic_near_depth=std::max(0.0f,
                the_config->sun_shadow_near_distance);
            auto const model_scan_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            for(auto const &prepared:model_candidates) {
                auto const &instance=*prepared.instance;
                auto const &group=*prepared.group;
                // Far cascades contain only stationary architecture. A
                // moving Thing outside the dynamic atlas may still use the
                // NEAR cascade as fallback, but never the distant one.
                if(instance.dynamic_sun_caster) {
                    if(cascade!=0) {
                        ++dynamic_far_skipped;
                        continue;
                    }
                    float closest_depth=0.0f;
                    for(int axis=0;axis<3;++axis) {
                        float const edge=camera.forward[axis]>=0.0f
                            ? prepared.bounds.low[axis]
                            : prepared.bounds.high[axis];
                        closest_depth+=camera.forward[axis]*
                            (edge-camera.position[axis]);
                    }
                    if(dynamic_near_depth<=0.0f ||
                       closest_depth>dynamic_near_depth) {
                        ++dynamic_near_distance_skipped;
                        continue;
                    }
                }
                if(!sun_shadow_caster_in_static_cascades(
                       instance.shadow_owner,sun_dynamic_atlas_owners))continue;
                bool const in_cascade=decal_aabbs_overlap(
                    prepared.bounds.low,prepared.bounds.high,
                    bounds.low,bounds.high);
                if(!in_cascade)continue;
                // Eligibility and material values were evaluated once
                // before the shared shadow projection was fitted.
                auto const &tex=*prepared.texture;
                bool const cutout=prepared.cutout;
                if(the_config->profile_renderer)
                    ++sun_profile_prepared_reuses[cascade];
                // Verified currently stationary SithThings may join the
                // cached static layer. Unknown owners remain dynamic.
                // Movement type alone is not decisive: a parked Thing can
                // become static until motion or animation is observed again.
                bool const stable=sun_shadow_cache_stationary_model(
                    instance.shadow_owner,instance.dynamic_sun_caster,
                    true,map.static_complete);
                (stable?static_models:models).push_back(
                    {&instance,&group,tex,cutout,prepared.bounds});

                uint64_t const member=prepared.signature;
                all_sum+=member;
                all_sum_sq+=member*member;
                ++all_count;
                if(stable) {
                    static_sum+=member;
                    static_sum_sq+=member*member;
                    ++static_count;
                }
            }
            append_model_set(caster_signature,all_count,all_sum,all_sum_sq);
            append_model_set(static_signature,static_count,static_sum,
                             static_sum_sq);
            if(the_config->profile_renderer)
                sun_profile_model_scan_us[cascade]+=elapsed_profile_us(
                    model_scan_begin,profile_clock::now());
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN shadow caster batches world=",
                         counts.size()+world_cutouts.size(),
                         " 3do=",models.size()+static_models.size(),
                         " static_3do=",static_models.size(),
                         " dynamic_far_skipped=",dynamic_far_skipped,
                         " dynamic_near_distance_skipped=",
                             dynamic_near_distance_skipped,
                         " skybox_skipped=",skipped_skybox_instances,
                         " cascade=",cascade,
                         " map=",cascade==0?map.resolution:map.far_resolution,
                         " selected_world=",map.selected.size(),
                         " hidden_world=",world_hidden,
                         " backfacing_world=",world_backfacing,
                         " material_skipped_world=",world_material_skipped,
                         " two_sided_world=1");
            // Explain WHY a cache miss occurred, not just whether a draw
            // happened. This is diagnostic-only and cannot change reuse.
            auto note_projection_miss=[&](sun_shadow_miss_profile &p,
                                           sun_shadow_projection const &previous) {
                if(sun_shadow_same_projection(view,previous))return;
                constexpr float tol=1.0e-5f;
                bool basis_changed=false;
                for(int axis=0;axis<3;++axis)
                    basis_changed=basis_changed ||
                        std::fabs(view.right[axis]-previous.right[axis])>tol ||
                        std::fabs(view.up[axis]-previous.up[axis])>tol ||
                        std::fabs(view.forward[axis]-previous.forward[axis])>tol;
                if(basis_changed)++p.projection_basis;
                if(std::fabs(view.width-previous.width)>tol ||
                   std::fabs(view.height-previous.height)>tol)
                    ++p.projection_size;
                auto const delta=decal_sub(view.origin,previous.origin);
                float const x=decal_dot(delta,view.right);
                float const y=decal_dot(delta,view.up);
                float const z=decal_dot(delta,view.forward);
                if(std::fabs(x)>tol || std::fabs(y)>tol)++p.projection_xy;
                if(std::fabs(z)>tol ||
                   std::fabs(view.range-previous.range)>tol)
                    ++p.projection_depth;
                // When changes are at the component tolerance boundary,
                // don't silently misattribute a genuine projection miss.
                if(!basis_changed &&
                   std::fabs(view.width-previous.width)<=tol &&
                   std::fabs(view.height-previous.height)<=tol &&
                   std::fabs(x)<=tol && std::fabs(y)<=tol &&
                   std::fabs(z)<=tol &&
                   std::fabs(view.range-previous.range)<=tol)
                    ++p.unclassified;
            };
            auto note_common_miss=[&](sun_shadow_miss_profile &p,
                                      bool cached,uintptr_t previous_world,
                                      uint64_t previous_revision,
                                      sun_shadow_projection const &previous_view,
                                      std::array<float,3> const &previous_direction) {
                if(!cached)++p.cold;
                if(previous_world!=world)++p.world;
                if(previous_revision!=map.revision)++p.revision;
                for(int axis=0;axis<3;++axis)
                    if(std::fabs(direction[axis]-previous_direction[axis])>=
                       1.0e-5f) {
                        ++p.direction;break;
                    }
                note_projection_miss(p,previous_view);
            };
            auto const cache_check_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            bool unchanged=sun_shadow_cascade_cached[cascade] &&
                sun_shadow_worlds[cascade]==world &&
                sun_shadow_revisions[cascade]==map.revision &&
                caster_signature==sun_shadow_caster_signatures[cascade];
            // Reuse only when the complete stabilized orthographic map,
            // including its light-space basis, is identical.
            unchanged=unchanged &&
                sun_shadow_same_projection(view,sun_shadow_views[cascade]);
            for(int i=0;i<3 && unchanged;++i)
                unchanged=std::fabs(direction[i]-sun_shadow_directions[cascade][i])<1.0e-5f;
            if(the_config->profile_renderer)
                sun_profile_cache_check_us[cascade]+=elapsed_profile_us(
                    cache_check_begin,profile_clock::now());
            if(unchanged) {
                if(the_config->profile_renderer) {
                    ++sun_profile_full_hits[cascade];
                    sun_profile_prepare_us[cascade]+=elapsed_profile_us(
                        cascade_begin,profile_clock::now());
                }
                sun_shadow_cascade_ready[cascade]=true;
                if(the_config->profile_renderer_verbose &&
                   gpu_3do_present_serial%120U==0U)
                    LOG_INFO("SUN cascade=",cascade," full_cache_hit=1");
                if(sun_gpu_timing) {
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,true);
                    sun_gpu_queries.timestamp(size_t(cascade)+1U,false);
                }
                continue;
            }
            if(the_config->profile_renderer) {
                auto &miss=sun_profile_full_misses[cascade];
                note_common_miss(miss,sun_shadow_cascade_cached[cascade],
                    sun_shadow_worlds[cascade],sun_shadow_revisions[cascade],
                    sun_shadow_views[cascade],sun_shadow_directions[cascade]);
                if(static_signature!=sun_shadow_static_signatures[cascade])
                    ++miss.static_casters;
                if(caster_signature!=sun_shadow_caster_signatures[cascade] &&
                   static_signature==sun_shadow_static_signatures[cascade])
                    ++miss.dynamic_casters;
            }
            // Reusing a depth texture requires the exact same stabilized
            // light projection, not merely the same sun and static casters.
            auto const static_check_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            bool static_same=map.static_complete &&
                sun_shadow_static_cached[cascade] &&
                sun_shadow_static_worlds[cascade]==world &&
                sun_shadow_static_revisions[cascade]==map.revision &&
                sun_shadow_static_signatures[cascade]==static_signature &&
                sun_shadow_same_projection(
                    view,sun_shadow_static_views[cascade]);
            for(int i=0;i<3 && static_same;++i)
                static_same=std::fabs(direction[i]-
                    sun_shadow_static_directions[cascade][i])<1.0e-5f;
            if(the_config->profile_renderer) {
                sun_profile_cache_check_us[cascade]+=elapsed_profile_us(
                    static_check_begin,profile_clock::now());
                ++sun_profile_working_redraws[cascade];
                sun_profile_prepare_us[cascade]+=elapsed_profile_us(
                    cascade_begin,profile_clock::now());
                if(static_same) ++sun_profile_static_hits[cascade];
                else {
                    ++sun_profile_static_redraws[cascade];
                    auto &miss=sun_profile_static_misses[cascade];
                    note_common_miss(miss,sun_shadow_static_cached[cascade],
                        sun_shadow_static_worlds[cascade],
                        sun_shadow_static_revisions[cascade],
                        sun_shadow_static_views[cascade],
                        sun_shadow_static_directions[cascade]);
                    if(static_signature!=sun_shadow_static_signatures[cascade])
                        ++miss.static_casters;
                    if(!map.static_complete &&
                       sun_shadow_static_cached[cascade])++miss.cold;
                }
            }
            if(sun_gpu_timing)
                sun_gpu_queries.timestamp(size_t(cascade)+1U,true);
            auto const &cascade_target=cascade==0 ? map.fbo : map.far_fbo;
            auto const &static_target=cascade==0 ? map.static_fbo : map.static_far_fbo;
            auto const dimensions=make_size(
                int(cascade_resolution),int(cascade_resolution));
            gl::set_viewport(make_box(make_point(0,0),dimensions));
            gl::disable(gl::capability::blend);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::set_depth_function(gl::comparison_function::less);
            if(!static_same) {
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                    map.static_complete ? static_target : cascade_target);
                gl::clear_buffer_depth(1.0f);
            }
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.25f*the_config->sun_shadow_caster_bias_scale,
                            1.5f*the_config->sun_shadow_caster_bias_scale);
            auto &prog=*ogs->spotlight_shadow_depth_program;
            prog.use_program();
            auto vec=[](decal_vec3 const &a){return make_point(a[0],a[1],a[2]);};
            prog.set_orthographic_light(vec(view.origin),vec(view.right),
                vec(view.up),vec(view.forward),
                make_point(view.width,view.height,view.range,0.0f));
            prog.set_model(make_point(1.0f,0.0f,0.0f),make_point(0.0f,1.0f,0.0f),
                           make_point(0.0f,0.0f,1.0f),make_point(0.0f,0.0f,0.0f));
            prog.set_shadow_alpha(false,make_point(1.0f,1.0f),make_point(0.0f,0.0f),
                                  0.5f,1.0f,false,0.0f);
            auto set_cutout=[&](gpu_3do_texture const &tex,
                               bool world_uv,bool sprite_cutout=false) {
                float const width=world_uv?tex.width:gpu_3do_model_uv_extent(tex.width);
                float const height=world_uv?tex.height:gpu_3do_model_uv_extent(tex.height);
                bool const second=tex.albedo_transition>0.0f && tex.albedo_source2==1;
                prog.set_shadow_alpha(true,
                    make_point(1.0f/std::max(width,1.0f),
                               1.0f/std::max(height,1.0f)),
                    tex.albedo_scroll,
                    tex.masked?tex.alpha_cutoff:((tex.dithered||sprite_cutout)?0.5f:0.998f),
                    tex.opacity,second,tex.albedo_transition);
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,tex.texture);
                if(second) {
                    gl::set_active_texture_unit(12);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,tex.texture2);
                }
            };
            if(!static_same) {
                gl::bind_vertex_array(map.vao);
                if(!counts.empty())glMultiDrawElements(GL_TRIANGLES,counts.data(),
                    GL_UNSIGNED_INT,offsets.data(),static_cast<GLsizei>(counts.size()));
                for(auto const &draw:world_cutouts) {
                    set_cutout(draw.texture,true);
                    gl::bind_vertex_array(map.vao);
                    gl::draw_elements(gl::element_type::triangles,draw.range.count,
                        gl::index_type::uint32,static_cast<ptrdiff_t>(
                            draw.range.first_index*sizeof(uint32_t)));
                }
            }
            // Draw the static non-world 3DOs into the SAME cached depth
            // target as SithWorld surfaces. Their signatures include pose,
            // material/cel, mesh instance and validated SithThing owner.
            auto draw_models=[&](std::vector<sun_model_draw> const &draws) {
                size_t gl_calls=0,multi_calls=0;
                std::vector<sun_model_draw const *> scheduled;
                scheduled.reserve(draws.size());
                for(auto const &draw:draws)scheduled.push_back(&draw);
                // Opaque and alpha-tested shadow casters only write DEPTH.
                // Within each model instance their order is irrelevant, so
                // cluster like depth states instead of breaking a batch every
                // time another MAT/culling group interrupts it. Preserve the
                // order of *instances* and never mix their model matrices.
                // In particular, we cannot use this reordering in the
                // alpha-BLENDED visible 3DO pass.
                auto shadow_no_cull=[&](sun_model_draw const &d) {
                    return (!the_config->sun_shadow_cull_back_faces &&
                            !the_config->sun_shadow_cull_front_faces) ||
                           (d.group->face_type&0x1U)!=0 ||
                           d.group->nonplanar;
                };
                for(size_t i=0;i<scheduled.size();) {
                    size_t end=i+1;
                    while(end<scheduled.size() &&
                          scheduled[end]->instance==scheduled[i]->instance)
                        ++end;
                    if(end>i+1)
                        std::stable_sort(scheduled.begin()+i,
                            scheduled.begin()+end,
                            [&](sun_model_draw const *a,sun_model_draw const *b) {
                                if(a->cutout!=b->cutout)
                                    return a->cutout<b->cutout;
                                bool const ca=shadow_no_cull(*a);
                                bool const cb=shadow_no_cull(*b);
                                if(ca!=cb)return ca<cb;
                                if(!a->cutout)return false;
                                if(a->group->material!=b->group->material)
                                    return a->group->material<b->group->material;
                                return a->group->cel<b->group->cel;
                            });
                    i=end;
                }
                // Batch repeated identical 3DO shadow ranges using GL 3.3
                // instancing. Singletons retain the normal draw path.
                // Render states and index ranges match exactly,
                // while each Thing supplies its own model transform.
                size_t instanced_calls=0,instanced_ranges=0;
                std::vector<uint8_t> batched(scheduled.size(),0);
                {
                    using key=std::tuple<uint64_t,uintptr_t,uint32_t,uint32_t,
                                         bool,bool,bool,uintptr_t,int32_t>;
                    std::map<key,std::vector<size_t>> occurrences;
                    for(size_t i=0;i<scheduled.size();++i) {
                        auto const &d=*scheduled[i];
                        bool const flipped=gpu_3do_reversed_winding(
                            d.instance->transform);
                        occurrences[std::make_tuple(
                            d.instance->id,
                            reinterpret_cast<uintptr_t>(d.instance->geometry.get()),
                            d.group->offset,d.group->count,d.cutout,
                            shadow_no_cull(d),flipped,
                            d.cutout?d.group->material:uintptr_t(0),
                            d.cutout?d.group->cel:int32_t(0))].push_back(i);
                    }
                    for(auto const &entry:occurrences) {
                        auto const &members=entry.second;
                        if(members.size()<2)continue;
                        for(size_t start=0;start+1<members.size();) {
                            size_t const amount=std::min(
                                size_t(shaders::spotlight_shadow_depth_shader::
                                       max_shadow_instances),
                                members.size()-start);
                            if(amount<2)break;
                            auto const &d=*scheduled[members[start]];
                            auto const &g=*d.group;
                            auto const &inst=*d.instance;
                            auto mesh=gpu_3do_gl_cache.find(inst.id);
                            if(mesh==gpu_3do_gl_cache.end())
                                mesh=gpu_3do_gl_cache.emplace(inst.id,
                                    std::make_unique<gpu_3do_gl_mesh>(
                                        *inst.geometry)).first;
                            if(shadow_no_cull(d))
                                gl::disable(gl::capability::cull_face);
                            else {
                                gl::enable(gl::capability::cull_face);
                                bool const front=the_config->sun_shadow_cull_front_faces;
                                bool const flipped=gpu_3do_reversed_winding(
                                    inst.transform);
                                gl::set_face_cull_mode(front!=flipped
                                    ?gl::face_mode::front:gl::face_mode::back);
                            }
                            gl::bind_vertex_array(mesh->second->vao);
                            if(d.cutout)set_cutout(
                                d.texture,false,d.instance->is_sprite_quad);
                            else prog.set_shadow_alpha(false,
                                make_point(1.0f,1.0f),make_point(0.0f,0.0f),
                                0.5f,1.0f,false,0.0f);
                            std::array<std::array<float,12>,
                                shaders::spotlight_shadow_depth_shader::
                                    max_shadow_instances> transforms{};
                            for(size_t k=0;k<amount;++k) {
                                size_t const index=members[start+k];
                                transforms[k]=scheduled[index]->instance->transform;
                                batched[index]=1;
                            }
                            prog.set_instanced_models(transforms,amount);
                            glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(g.count),GL_UNSIGNED_INT,
                                reinterpret_cast<void const *>(
                                    uintptr_t(g.offset)*sizeof(uint32_t)),
                                static_cast<GLsizei>(amount));
                            ++gl_calls;
                            ++instanced_calls;
                            instanced_ranges+=amount;
                            start+=amount;
                        }
                    }
                }
                std::vector<GLsizei> batch_counts;
                std::vector<void const *> batch_offsets;
                batch_counts.reserve(32);
                batch_offsets.reserve(32);
                // All ranges in each batch have the same VAO/model matrix,
                // depth-culling state and (for alpha cutouts) MAT/cel.
                for(size_t first=0;first<scheduled.size();) {
                    if(batched[first]) { ++first;continue; }
                    auto const &draw=*scheduled[first];
                    auto const &instance=*draw.instance;
                    auto const &group=*draw.group;
                    bool const uncullable=shadow_no_cull(draw);
                    size_t last=first+1;
                    while(last<scheduled.size()) {
                        if(batched[last])break;
                        auto const &other=*scheduled[last];
                        if(other.instance!=draw.instance ||
                           other.cutout!=draw.cutout ||
                           shadow_no_cull(other)!=uncullable)break;
                        if(draw.cutout &&
                           (other.group->material!=group.material ||
                            other.group->cel!=group.cel))break;
                        ++last;
                    }
                    auto it=gpu_3do_gl_cache.find(instance.id);
                    if(it==gpu_3do_gl_cache.end())
                        it=gpu_3do_gl_cache.emplace(instance.id,
                            std::make_unique<gpu_3do_gl_mesh>(*instance.geometry)).first;
                    auto const &m=instance.transform;
                    prog.set_model(make_point(m[0],m[1],m[2]),
                        make_point(m[3],m[4],m[5]),make_point(m[6],m[7],m[8]),
                        make_point(m[9],m[10],m[11]));
                    if(uncullable)
                        gl::disable(gl::capability::cull_face);
                    else {
                        gl::enable(gl::capability::cull_face);
                        bool const cull_front=the_config->sun_shadow_cull_front_faces;
                        bool const mirrored=gpu_3do_reversed_winding(m);
                        gl::set_face_cull_mode(cull_front!=mirrored
                            ?gl::face_mode::front:gl::face_mode::back);
                    }
                    gl::bind_vertex_array(it->second->vao);
                    if(draw.cutout)set_cutout(
                        draw.texture,false,instance.is_sprite_quad);
                    else prog.set_shadow_alpha(false,
                        make_point(1.0f,1.0f),make_point(0.0f,0.0f),
                        0.5f,1.0f,false,0.0f);
                    if(last==first+1) {
                        gl::draw_elements(gl::element_type::triangles,
                            group.count,gl::index_type::uint32,
                            static_cast<ptrdiff_t>(
                                group.offset*sizeof(uint32_t)));
                    } else {
                        batch_counts.clear();
                        batch_offsets.clear();
                        for(size_t k=first;k<last;++k) {
                            auto const &g=*scheduled[k]->group;
                            batch_counts.push_back(static_cast<GLsizei>(g.count));
                            batch_offsets.push_back(reinterpret_cast<void const *>(
                                uintptr_t(g.offset)*sizeof(uint32_t)));
                        }
                        glMultiDrawElements(GL_TRIANGLES,batch_counts.data(),
                            GL_UNSIGNED_INT,batch_offsets.data(),
                            static_cast<GLsizei>(batch_counts.size()));
                        ++multi_calls;
                    }
                    ++gl_calls;
                    first=last;
                }
                // The depth program is shared by spotlight and sun passes.
                // Reset its transform mode after all instanced draws.
                if(instanced_calls)
                    prog.set_model(make_point(1.0f,0.0f,0.0f),
                                   make_point(0.0f,1.0f,0.0f),
                                   make_point(0.0f,0.0f,1.0f),
                                   make_point(0.0f,0.0f,0.0f));
                return std::make_tuple(gl_calls,multi_calls,
                                       instanced_calls,instanced_ranges);
            };
            if(!static_same) {
                // Render static casters into the dedicated cached depth
                // target when available, otherwise into the working cascade.
                // The target must be explicit because earlier shadow passes
                // may have changed the currently bound framebuffer.
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                    map.static_complete ? static_target : cascade_target);
                auto const static_begin=the_config->profile_renderer
                    ?profile_clock::now():profile_clock::time_point{};
                auto const submitted=draw_models(static_models);
                if(the_config->profile_renderer) {
                    sun_profile_static_submit_us[cascade]+=elapsed_profile_us(
                        static_begin,profile_clock::now());
                    sun_profile_static_submitted[cascade]+=static_models.size();
                    sun_profile_static_gl_calls[cascade]+=std::get<0>(submitted);
                    sun_profile_static_multi_calls[cascade]+=std::get<1>(submitted);
                    sun_profile_static_instanced_calls[cascade]+=std::get<2>(submitted);
                    sun_profile_static_instanced_ranges[cascade]+=std::get<3>(submitted);
                }
                if(map.static_complete) {
                    sun_shadow_static_cached[cascade]=true;
                    sun_shadow_static_worlds[cascade]=world;
                    sun_shadow_static_revisions[cascade]=map.revision;
                    sun_shadow_static_signatures[cascade]=static_signature;
                    sun_shadow_static_views[cascade]=view;
                    sun_shadow_static_directions[cascade]=direction;
                }
            }
            if(map.static_complete) {
                // Copy the static depth on GPU (no readback) and depth-test
                // current 3DOs against it. Even dynamic Thing updates no
                // longer trigger a draw of the world architecture.
                gl::bind_framebuffer(gl::framebuffer_bind_target::read,static_target);
                gl::bind_framebuffer(gl::framebuffer_bind_target::draw,cascade_target);
                gl::blit_framebuffer(
                    make_box(make_point(0,0),dimensions),
                    make_box(make_point(0,0),dimensions),
                    {gl::framebuffer_blit_buffer::depth},
                    gl::framebuffer_blit_filter::nearest);
            }
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,cascade_target);
            auto const moving_begin=the_config->profile_renderer
                ?profile_clock::now():profile_clock::time_point{};
            auto const submitted=draw_models(models);
            if(the_config->profile_renderer) {
                sun_profile_moving_submit_us[cascade]+=elapsed_profile_us(
                    moving_begin,profile_clock::now());
                sun_profile_moving_submitted[cascade]+=models.size();
                sun_profile_moving_gl_calls[cascade]+=std::get<0>(submitted);
                sun_profile_moving_multi_calls[cascade]+=std::get<1>(submitted);
                sun_profile_moving_instanced_calls[cascade]+=std::get<2>(submitted);
                sun_profile_moving_instanced_ranges[cascade]+=std::get<3>(submitted);
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::set_active_texture_unit(0);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            if(sun_gpu_timing)
                sun_gpu_queries.timestamp(size_t(cascade)+1U,false);
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN cascade=",cascade,
                         " static_cache_hit=",static_same?1:0,
                         " world_batches_redrawn=",static_same?0:
                             counts.size()+world_cutouts.size(),
                         " moving_model_batches=",models.size(),
                         " static_model_batches_redrawn=",
                            static_same?0:static_models.size());
            sun_shadow_views[cascade]=view;
            sun_shadow_directions[cascade]=direction;
            sun_shadow_worlds[cascade]=world;
            sun_shadow_revisions[cascade]=map.revision;
            sun_shadow_cascade_cached[cascade]=true;
            sun_shadow_caster_signatures[cascade]=caster_signature;
            sun_shadow_cascade_ready[cascade]=true;
            } // cascade
            } else {
                // Depth textures and their matching projection matrices are
                // retained. Do not skip the dynamic atlas above.
                sun_shadow_cascade_ready={{true,true}};
                if(sun_gpu_timing) {
                    for(size_t cascade=1;cascade<=2;++cascade) {
                        sun_gpu_queries.timestamp(cascade,true);
                        sun_gpu_queries.timestamp(cascade,false);
                    }
                }
            }
            if(sun_gpu_timing)sun_gpu_queries.end();
            sun_shadow_ready=sun_shadow_cascade_ready[0] &&
                             sun_shadow_cascade_ready[1];
            if(!sun_shadow_ready && !sun_shadow_frame_failure)
                sun_shadow_frame_failure="one or both cascades incomplete";
            if(!skip_cascade_validation) {
                // Do not establish a new throttle window after a failed or
                // incomplete validation; retry on the next frame instead.
                sun_shadow_validated_world_view_ready=sun_shadow_ready;
                if(sun_shadow_ready) {
                    sun_shadow_validated_world_view=world_view;
                    sun_shadow_last_validation=validation_now;
                }
            }
            if(the_config->profile_renderer) {
                auto const pass_us=elapsed_profile_us(
                    sun_pass_started,profile_clock::now());
                if(skip_cascade_validation) {
                    sun_profile_skipped_pass_us+=pass_us;
                    sun_profile_skipped_pass_peak_us=std::max(
                        sun_profile_skipped_pass_peak_us,pass_us);
                } else {
                    ++sun_profile_checked_pass_count;
                    sun_profile_checked_pass_us+=pass_us;
                    sun_profile_checked_pass_peak_us=std::max(
                        sun_profile_checked_pass_peak_us,pass_us);
                }
            }
            if(the_config->profile_renderer_verbose &&
               gpu_3do_present_serial%120U==0U) {
                LOG_INFO("SUN cache 120-frame-window near(full/static_hit/static_redraw/composite_redraw)=",
                    sun_profile_full_hits[0],"/",
                    sun_profile_static_hits[0],"/",
                    sun_profile_static_redraws[0],"/",
                    sun_profile_working_redraws[0],
                    " far=",
                    sun_profile_full_hits[1],"/",
                    sun_profile_static_hits[1],"/",
                    sun_profile_static_redraws[1],"/",
                    sun_profile_working_redraws[1],
                    " NOTE=full_hits_skip_all_shadow_draws; static_hits_still_blit_static_and_render_movers");
                LOG_INFO("SUN cascade validation throttle interval_s=",
                    std::to_string(interval),
                    " skipped_frames=",sun_profile_throttle_skipped,
                    " checked_frames=",sun_profile_throttle_checked,
                    " forced_early_checks=",sun_profile_throttle_forced_early,
                    " NOTE=dynamic_atlas_updated_each_frame");
                LOG_INFO("SUN validation early reasons timer_due=",
                    sun_profile_throttle_due_timer,
                    " no_validated_map=",sun_profile_forced_unready,
                    " world_or_cache_invalid=",sun_profile_forced_world,
                    " sun_direction=",sun_profile_forced_sun,
                    " near_camera_coverage=",sun_profile_forced_coverage[0],
                    " far_camera_coverage=",sun_profile_forced_coverage[1],
                    " NOTE=both_cascades_may_fail_in_one_frame");
                LOG_INFO("SUN validation frame CPU checked_us_avg/peak=",
                    sun_profile_checked_pass_us/
                        std::max<uint64_t>(1,sun_profile_checked_pass_count),
                    "/",sun_profile_checked_pass_peak_us,
                    " skipped_us_avg/peak=",
                    sun_profile_skipped_pass_us/
                        std::max<uint64_t>(1,sun_profile_throttle_skipped),
                    "/",sun_profile_skipped_pass_peak_us,
                    " NOTE=includes_dynamic_atlas_CPU_not_GPU");
                // Orthogonal miss reasons: totals can exceed the miss count.
                // No implicit GPU synchronization; these timings are CPU
                // planning/GL submission only, not GPU rasterization costs.
                for(int c=0;c<2;++c) {
                    auto log_misses=[&](char const *layer,
                                        sun_shadow_miss_profile const &p) {
                        LOG_INFO("SUN cache miss reasons cascade=",c,
                            " layer=",layer,
                            " cold=",p.cold," world=",p.world,
                            " revision=",p.revision,
                            " sun_direction=",p.direction,
                            " projection_xy=",p.projection_xy,
                            " projection_size=",p.projection_size,
                            " projection_depth=",p.projection_depth,
                            " projection_basis=",p.projection_basis,
                            " static_signature=",p.static_casters,
                            " dynamic_signature_only=",p.dynamic_casters,
                            " other_projection=",p.unclassified,
                            " NOTE=overlapping_causes");
                    };
                    log_misses("full",sun_profile_full_misses[c]);
                    log_misses("static",sun_profile_static_misses[c]);
                    auto const checks=sun_profile_full_hits[c]+
                        sun_profile_working_redraws[c];
                    auto const frames=checks+sun_profile_throttle_skipped;
                    LOG_INFO("SUN cache CPU cascade=",c,
                        " preparation_us_total=",sun_profile_prepare_us[c],
                        " preparation_us_per_frame=",
                            sun_profile_prepare_us[c]/std::max<uint64_t>(1,frames),
                        " preparation_us_per_check=",
                            sun_profile_prepare_us[c]/std::max<uint64_t>(1,checks),
                        " static_submit_us_total=",sun_profile_static_submit_us[c],
                        " static_groups_submitted=",sun_profile_static_submitted[c],
                        " static_GL_calls=",sun_profile_static_gl_calls[c],
                        " static_multi_calls=",sun_profile_static_multi_calls[c],
                        " static_instanced_calls=",sun_profile_static_instanced_calls[c],
                        " static_instanced_ranges=",sun_profile_static_instanced_ranges[c],
                        " moving_submit_us_total=",sun_profile_moving_submit_us[c],
                        " moving_groups_submitted=",sun_profile_moving_submitted[c],
                        " moving_GL_calls=",sun_profile_moving_gl_calls[c],
                        " moving_multi_calls=",sun_profile_moving_multi_calls[c],
                        " moving_instanced_calls=",sun_profile_moving_instanced_calls[c],
                        " moving_instanced_ranges=",sun_profile_moving_instanced_ranges[c],
                        " NOTE=CPU_only_no_GPU_wait");
                    LOG_INFO("SUN prepare CPU cascade=",c,
                        " projection_us_total=",sun_profile_projection_us[c],
                        " world_query_us_total=",sun_profile_world_query_us[c],
                        " world_filter_us_total=",sun_profile_world_filter_us[c],
                        " model_scan_us_total=",sun_profile_model_scan_us[c],
                        " cache_check_us_total=",sun_profile_cache_check_us[c],
                        " prepared_reuses=",sun_profile_prepared_reuses[c],
                        " NOTE=prepared_candidate_material_and_hash_reused");
                    sun_profile_full_misses[c]={};
                    sun_profile_static_misses[c]={};
                }
                LOG_INFO("SUN static group geometry cache bounds_reused=",
                    sun_profile_static_bounds_reused,
                    " bounds_calculated=",sun_profile_static_bounds_calculated,
                    " opaque_hash_reused=",sun_profile_static_signature_reused,
                    " cutout_hash_reused=",sun_profile_cutout_hash_reused,
                    " cutout_hash_calculated=",sun_profile_cutout_hash_calculated,
                    " cutout_material_invalidations=",
                        sun_profile_cutout_material_invalidations,
                    " group_invalidations=",sun_profile_static_group_invalidations,
                    " instance_invalidations=",sun_profile_static_instance_invalidations,
                    " hashes_calculated=",sun_profile_static_signature_calculated,
                    " NOTE=live_materials_still_resolved_each_check");
                LOG_INFO("SUN cache global CPU candidate_bounds_us_total=",
                    sun_profile_candidate_us,
                    " candidate_groups_total=",sun_profile_candidate_groups,
                    " dynamic_atlas_submit_us_total=",sun_profile_atlas_us,
                    " dynamic_atlas_material_hits=",
                        sun_profile_atlas_mat_hits,
                    " dynamic_atlas_material_misses=",
                        sun_profile_atlas_mat_misses,
                    " NOTE=CPU_only_no_GPU_wait");
                LOG_INFO("SUN prepare CPU candidate samples material_ns=",
                    sun_profile_candidate_material_sample_ns,
                    " material_samples=",sun_profile_candidate_material_samples,
                    " bounds_ns=",sun_profile_candidate_bounds_sample_ns,
                    " bounds_samples=",sun_profile_candidate_bounds_samples,
                    " signature_ns=",sun_profile_candidate_signature_sample_ns,
                    " signature_samples=",sun_profile_candidate_signature_samples,
                    " NOTE=each_candidate_prepared_once_samples_one_in_64_not_total");
                LOG_INFO("SUN GPU avg_us atlas=",
                    sun_gpu_samples?(sun_gpu_ns[0]/1000U)/sun_gpu_samples:0U,
                    " near=",sun_gpu_samples?(sun_gpu_ns[1]/1000U)/sun_gpu_samples:0U,
                    " far=",sun_gpu_samples?(sun_gpu_ns[2]/1000U)/sun_gpu_samples:0U,
                    " samples=",sun_gpu_samples,
                    " ring_busy_skips=",sun_gpu_skipped,
                    " supported=",sun_gpu_queries.is_supported(),
                    " NOTE=delayed_nonblocking_timestamps_cascade_zero_if_cached");
                sun_profile_throttle_skipped=0;
                sun_profile_throttle_checked=0;
                sun_profile_throttle_forced_early=0;
                sun_profile_throttle_due_timer=0;
                sun_profile_forced_unready=0;
                sun_profile_forced_world=0;
                sun_profile_forced_sun=0;
                sun_profile_forced_coverage.fill(0);
                sun_profile_checked_pass_us=0;
                sun_profile_checked_pass_count=0;
                sun_profile_skipped_pass_us=0;
                sun_profile_checked_pass_peak_us=0;
                sun_profile_skipped_pass_peak_us=0;
                sun_profile_prepare_us.fill(0);
                sun_profile_projection_us.fill(0);
                sun_profile_world_query_us.fill(0);
                sun_profile_world_filter_us.fill(0);
                sun_profile_model_scan_us.fill(0);
                sun_profile_cache_check_us.fill(0);
                sun_profile_prepared_reuses.fill(0);
                sun_profile_candidate_material_sample_ns=0;
                sun_profile_candidate_bounds_sample_ns=0;
                sun_profile_candidate_signature_sample_ns=0;
                sun_profile_candidate_material_samples=0;
                sun_profile_candidate_bounds_samples=0;
                sun_profile_candidate_signature_samples=0;
                sun_profile_static_bounds_reused=0;
                sun_profile_static_signature_reused=0;
                sun_profile_static_bounds_calculated=0;
                sun_profile_static_signature_calculated=0;
                sun_profile_cutout_hash_reused=0;
                sun_profile_cutout_hash_calculated=0;
                sun_profile_cutout_material_invalidations=0;
                sun_profile_static_group_invalidations=0;
                sun_profile_static_instance_invalidations=0;
                sun_gpu_ns.fill(0);
                sun_gpu_samples=sun_gpu_skipped=0;
                sun_profile_static_submit_us.fill(0);
                sun_profile_moving_submit_us.fill(0);
                sun_profile_static_submitted.fill(0);
                sun_profile_moving_submitted.fill(0);
                sun_profile_static_gl_calls.fill(0);
                sun_profile_moving_gl_calls.fill(0);
                sun_profile_static_multi_calls.fill(0);
                sun_profile_moving_multi_calls.fill(0);
                sun_profile_static_instanced_calls.fill(0);
                sun_profile_moving_instanced_calls.fill(0);
                sun_profile_static_instanced_ranges.fill(0);
                sun_profile_moving_instanced_ranges.fill(0);
                sun_profile_candidate_us=0;
                sun_profile_candidate_groups=0;
                sun_profile_atlas_us=0;
                sun_profile_atlas_mat_hits=0;
                sun_profile_atlas_mat_misses=0;
                sun_profile_full_hits.fill(0);
                sun_profile_static_hits.fill(0);
                sun_profile_static_redraws.fill(0);
                sun_profile_working_redraws.fill(0);
            }
        }

        bool shadow_matches_light(spotlight_instance const &spot) const
        {
            if(!spotlight_shadow_ready ||
               spot.material!=spotlight_shadow_light.material ||
               spot.shadow_owner!=spotlight_shadow_light.shadow_owner)
                return false;
            auto same=[](std::array<float,3> const &a,
                         std::array<float,3> const &b) {
                for(int i=0;i<3;++i)
                    if(std::fabs(a[i]-b[i])>0.00001f)return false;
                return true;
            };
            return same(spot.origin,spotlight_shadow_light.origin) &&
                   same(spot.right,spotlight_shadow_light.right) &&
                   same(spot.up,spotlight_shadow_light.up) &&
                   same(spot.forward,spotlight_shadow_light.forward) &&
                   std::fabs(spot.range-spotlight_shadow_light.range)<0.00001f;
        }

        void draw_game_opaque_composite()
        {
            gl::disable(gl::capability::depth_test);

            ogs->game_post_opaque_composite_program.use_program();
            ogs->game_post_opaque_composite_program.set_color_sampler(0);
            ogs->game_post_opaque_composite_program.set_emissive_sampler(1);
            ogs->game_post_opaque_composite_program.set_occlusion_sampler(2);
            ogs->game_post_opaque_composite_program.set_depth_sampler(3);
            // Required even when no sun/spotlight camera is configured.
            ogs->game_post_opaque_composite_program.set_coverage_normal_sampler(5);
            auto &prog=ogs->game_post_opaque_composite_program;
            prog.set_spotlight_count(0);
            prog.set_sun_dynamic_count(0);
            prog.set_debug_void_pixels(the_config->debug_void_pixels);
            prog.set_retain_hom_pixels(the_config->retain_hom_pixels);
            prog.set_sun_shadow_receiver_bias_scale(
                the_config->sun_shadow_receiver_bias_scale);
            auto const sun_pcf=effective_sun_pcf_radii();
            prog.set_sun_shadow_pcf_radius(sun_pcf.near_radius,
                                           sun_pcf.far_radius);
            float const dynamic_sun_pcf=effective_sun_dynamic_pcf_radius();
            prog.set_sun_dynamic_pcf_radius(dynamic_sun_pcf);
            prog.set_sun_sky_aperture(false,
                texture_units::opaque_sky_aperture,
                make_point(1.0f,0.0f,1.0f,0.0f));
            prog.set_sun_max_distance(the_config->sun_max_distance);
            prog.set_spotlight_shadow(-1,0.0f,0.0f,
                the_config->spotlight_shadow_angle_bias_scale);
            prog.set_sun_shadow(false,false,make_point(0.0f,0.0f,1.0f),
                make_point(0.0f,0.0f,0.0f,0.0f),
                make_point(0.0f,0.0f,0.0f),make_point(1.0f,0.0f,0.0f),
                make_point(0.0f,1.0f,0.0f),make_point(0.0f,0.0f,1.0f),
                make_point(1.0f,1.0f,1.0f));
            // Direct sunlight must survive a failed/unavailable shadow map.
            auto const sun=get_sun_settings();
            float const sun_power=sun.intensity.value()[0];
            bool const sun_lit=the_config->enable_sun &&
                sun_level_has_sky &&
                std::isfinite(sun_power) && sun_power>0.0f;
            auto const cam=read_active_render_camera();
            if((the_config->enable_sun || the_config->enable_sun_shadows) &&
               the_config->profile_renderer_verbose && gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN composite diffuse=",sun_lit && cam.valid?1:0,
                         " shadow=",sun_shadow_ready?1:0,
                         " intensity=",std::to_string(sun_power));
            bool const half_sun_active=sun_lit && cam.valid &&
                sun_shadow_ready && sun_shadow_target &&
                the_config->experimental_half_res_sun_visibility &&
                ogs->game_post_opaque_half_sun_program &&
                ogs->sun_half_visibility &&
                glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE &&
                glIsEnabled(GL_BLEND)==GL_FALSE;
            auto *half_sun_shader=half_sun_active
                ?ogs->game_post_opaque_half_sun_program.get():nullptr;
            if(cam.valid && (the_config->enable_spotlights || sun_lit)) {
                float const h=std::tan(cam.fov*(3.14159265358979323846f/360.0f));
                float const v=h*float(get<y>(conf_scr_res))/
                                  float(get<x>(conf_scr_res));
                auto point_axis=[](std::array<float,3> const &a,float w) {
                    return make_point(a[0],a[1],a[2],w);
                };
                prog.set_projector_camera(
                    point_axis(cam.position,0.0f),
                    point_axis(cam.right,h),
                    point_axis(cam.forward,0.0f),
                    point_axis(cam.up,v));
                int active=0;
                auto spots=(the_config->enable_spotlights)
                    ? collect_spotlights_for_frame()
                    : std::vector<spotlight_instance>{};
                if(the_config->enable_spotlights)maintain_spotlight_images(spots);
                if(the_config->enable_spotlights) {
                    spotlight_projection_camera projection{};
                    projection.position=cam.position;
                    projection.right=cam.right;
                    projection.up=cam.up;
                    projection.forward=cam.forward;
                    projection.tan_half_horizontal=h;
                    projection.tan_half_vertical=v;
                    projection.width=get<x>(conf_scr_res);
                    projection.height=get<y>(conf_scr_res);
                    for(auto const &spot:spots) {
                        // Conservative cone screen bounds: skip fully offscreen
                        // lights and gate GLSL projector/normal work per pixel.
                        // The shared fullscreen fog/SSAO composite is untouched.
                        auto const rect=spotlight_screen_coverage(spot,projection);
                        if(!rect.visible)continue;
                        auto image=spotlight_mat_texture(spot.material);
                        if(!image)continue;
                        if(shadow_matches_light(spot) && spotlight_shadow_target) {
                            prog.set_spotlight_shadow(
                                active,spotlight_shadow_view.near_distance,
                                spotlight_shadow_view.range,
                                the_config->spotlight_shadow_angle_bias_scale);
                            gl::set_active_texture_unit(10);
                            gl::bind_texture(gl::texture_bind_target::texture_2d,
                                             spotlight_shadow_target->depth);
                        }
                        prog.set_spotlight(
                            active,
                            point_axis(spot.origin,0.0f),
                            point_axis(spot.right,spot.range/spot.width),
                            point_axis(spot.up,spot.range/spot.height),
                            point_axis(spot.forward,1.0f/spot.range),
                            make_point(spot.light_intensity,spot.add_intensity),
                            make_point(rect.min_x,rect.min_y,
                                       rect.max_x,rect.max_y));
                        gl::set_active_texture_unit(6+active);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         *image);
                        if(++active>=int(spotlight_max_active))break;
                    }
                }
                prog.set_spotlight_count(active);
                if(sun_lit) {
                    // Set the sun uniforms once per shader, instead of
                    // bouncing between GL programs for every caster tile.
                    // Keep the original fullscreen compositor's state intact.
                    auto configure_sun=[&](auto &target) {
                    bool const shadows=sun_shadow_ready && bool(sun_shadow_target);
                    auto const direction=normalized_sun_direction(sun.direction.value());
                    auto const rgb=sun.color.value();
                    target.set_sun_shadow(true,shadows,
                        make_point(direction[0],direction[1],direction[2]),
                        make_point(rgb[0],rgb[1],rgb[2],sun_power),
                        make_point(sun_shadow_views[0].origin[0],sun_shadow_views[0].origin[1],sun_shadow_views[0].origin[2]),
                        make_point(sun_shadow_views[0].right[0],sun_shadow_views[0].right[1],sun_shadow_views[0].right[2]),
                        make_point(sun_shadow_views[0].up[0],sun_shadow_views[0].up[1],sun_shadow_views[0].up[2]),
                        make_point(sun_shadow_views[0].forward[0],sun_shadow_views[0].forward[1],sun_shadow_views[0].forward[2]),
                        make_point(sun_shadow_views[0].width,sun_shadow_views[0].height,sun_shadow_views[0].range));
                    bool const sky_aperture=shadows &&
                        sun_sky_aperture_ready();
                    if(sky_aperture) {
                        target.set_sun_sky_aperture(
                            true,texture_units::opaque_sky_aperture,
                            sun_sky_aperture_depth_transform());
                        gl::set_active_texture_unit(
                            texture_units::opaque_sky_aperture);
                        glBindTexture(GL_TEXTURE_2D_ARRAY,
                                      sun_sky_aperture_target->depth.value());
                    }
                    if(shadows) {
                        auto const &far_view=sun_shadow_views[1];
                        target.set_sun_shadow_far(
                            make_point(far_view.origin[0],far_view.origin[1],far_view.origin[2]),
                            make_point(far_view.right[0],far_view.right[1],far_view.right[2]),
                            make_point(far_view.up[0],far_view.up[1],far_view.up[2]),
                            make_point(far_view.forward[0],far_view.forward[1],far_view.forward[2]),
                            make_point(far_view.width,far_view.height,far_view.range),
                            the_config->sun_shadow_near_distance);
                        gl::set_active_texture_unit(13);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                            sun_shadow_target->depth);
                        gl::set_active_texture_unit(14);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                            sun_shadow_target->far_depth);
                    }
                    if(the_config->enable_sun_dynamic_shadows &&
                       sun_dynamic_target && !sun_dynamic_views.empty()) {
                        gl::set_active_texture_unit(15);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         sun_dynamic_target->depth);
                        for(size_t i=0;i<sun_dynamic_views.size();++i) {
                            auto const &v=sun_dynamic_views[i];
                            auto const &tile=sun_dynamic_tiles[i];
                            target.set_sun_dynamic_atlas_rect(int(i),
                                make_point(float(tile.x),float(tile.y),
                                           float(tile.size),float(tile.size)));
                            target.set_sun_dynamic(int(i),
                                make_point(v.origin[0],v.origin[1],v.origin[2]),
                                make_point(v.right[0],v.right[1],v.right[2]),
                                make_point(v.up[0],v.up[1],v.up[2]),
                                make_point(v.forward[0],v.forward[1],v.forward[2]),
                                make_point(v.width,v.height,v.range));
                        }
                        target.set_sun_dynamic_count(int(sun_dynamic_views.size()));
                    }
                    };
                    configure_sun(prog);
                    if(half_sun_shader) {
                        half_sun_shader->use_program();
                        half_sun_shader->set_depth_sampler(3);
                        half_sun_shader->set_coverage_normal_sampler(5);
                        half_sun_shader->set_projector_camera(
                            point_axis(cam.position,0.0f),
                            point_axis(cam.right,h),
                            point_axis(cam.forward,0.0f),
                            point_axis(cam.up,v));
                        half_sun_shader->set_sun_shadow_receiver_bias_scale(
                            the_config->sun_shadow_receiver_bias_scale);
                        half_sun_shader->set_sun_shadow_pcf_radius(
                            sun_pcf.near_radius,sun_pcf.far_radius);
                        half_sun_shader->set_sun_dynamic_pcf_radius(
                            dynamic_sun_pcf);
                        half_sun_shader->set_sun_dynamic_count(0);
                        configure_sun(*half_sun_shader);
                        prog.use_program();
                    }
                }
            }
            {
                auto const &fog=get_fog_settings();
                auto const info=fog.info.value(), rgb=fog.color.value();
                ogs->game_post_opaque_composite_program.set_fog(
                    make_point(info[0],info[1],
                               the_config->enable_fog?info[2]:0.0f,0.0f),
                    color_rgb(rgb[0],rgb[1],rgb[2]));
            }

            // Evaluate ONLY opaque sun-caster visibility at quarter pixels.
            // Restore the opaque composition FBO without clearing it; the
            // existing full-resolution compositor applies the sky gate,
            // incidence, fog and spotlight exactly as before.
            if(half_sun_shader) {
                auto &buffer=*ogs->sun_half_visibility;
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,buffer.fbo);
                gl::set_viewport(buffer.viewport);
                half_sun_shader->use_program();
                gl::set_active_texture_unit(5);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    frame_screen->get_resolved_normal_texture());
                gl::set_active_texture_unit(3);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    frame_screen->get_resolved_depth_texture());
                gl::bind_vertex_array(ogs->postmdl.vao);
                gl::draw_elements(gl::element_type::triangles,
                    ogs->postmdl.num_indices,gl::index_type::uint32);
                frame_screen->begin_compose_opaque_pass(true);
                gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
                prog.use_program();
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,buffer.tex);
            }
            prog.set_sun_half_visibility(half_sun_active,11);
            // HOM coverage reads normal.A on EVERY frame, including rooms
            // without sunlight or active spotlights. Always bind this
            // G-buffer attachment, not only when light shaders need RGB.
            gl::set_active_texture_unit(5);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_normal_texture());

            gl::set_active_texture_unit(3);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_depth_texture());

            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d, frame_ao->get_ssao_texture());

            gl::set_active_texture_unit(1);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_emissive_texture());

            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_color_texture());

            gl::bind_vertex_array(ogs->postmdl.vao);
            gl::draw_elements(
                gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);
        }

        // Sector-first stage of live GPU-ready SithWorld faces. Native JK
        // still supplies unsupported faces, portals, and transparency.
        void stage_world_cache()
        {
            frame_world_cache_camera.valid=false;
            if(world_cache_target)
                world_cache_target->frame_render_candidates.clear();
            if(!the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame)return;
            uint32_t address=0;
            if(!engine_material_debug::read_object(
                   engine_material_debug::current_world_global,address) ||
               !decal_native_address_ok(address))return;
            auto safe_read=[](uintptr_t ptr,void *dst,size_t len) {
                if(!len || !engine_camera_detail::readable(ptr,len))
                    return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),len);
                return true;
            };
            if(!world_cache_target)
                world_cache_target=std::make_unique<world_cache_gpu>();
            if(!world_cache_target->update_world(safe_read,address)) {
                if(gpu_3do_present_serial%120U==0U)
                    LOG_WARNING("WORLD CACHE phase0 native world snapshot unavailable");
                return;
            }
            auto &cache=*world_cache_target;
            engine_material_debug::world_header warm_world{};
            if(engine_material_debug::read_object(address,warm_world))
                jkgm::get_native_mat_warm_queue().prepare_world(
                    address,warm_world.materials);
            // Maintain a sector-first candidate list each frame. The
            // expensive whole-world parity scan remains diagnostic-only.
            bool const sampled=the_config->profile_renderer &&
                gpu_3do_present_serial%120U==0U;
            // Read the SAME native camera/rdClipFrustum as the GPU 3DO
            // path. Frustum rejection is conservative; it cannot tell
            // us which sectors JK traversed through active adjoins.
            auto const camera=capture_gpu_preview_camera();
            frame_world_cache_camera=camera;
            bool const can_cull=camera.valid && camera.perspective &&
                camera.frustum.far_plane>0.1f;
            world_cache_frame_stats state{};
            uint64_t collect_us=0;
            if(sampled) {
                auto const started=profile_clock::now();
                state=collect_world_cache_draw_candidates(
                    safe_read,cache.world.surfaces_addr,cache.mesh,
                    cache.frame_candidates,
                    can_cull?&camera.frustum:nullptr,
                    can_cull?&camera.native_view:nullptr);
                collect_us=elapsed_profile_us(
                    started,profile_clock::now());
            } else cache.frame_candidates.clear();
            // Exact producer signature verified against jkgm(5).exe:
            // 0x4c6875: mov eax,[0x831938]; cmp eax,0x1b58
            // 0x4c6885: mov [eax*4+0x6bba88],ecx
            constexpr std::array<uint8_t,23> producer_bytes{{
                0xA1,0x38,0x19,0x83,0x00,
                0x3D,0x58,0x1B,0x00,0x00,
                0x0F,0x83,0xAA,0x05,0x00,0x00,
                0x89,0x0C,0x85,0x88,0xBA,0x6B,0x00
            }};
            std::array<uint8_t,23> producer_live{};
            bool const producer_matches=
                safe_read(0x004C6875U,producer_live.data(),
                          producer_live.size()) &&
                producer_live==producer_bytes;
            auto const portal_started=profile_clock::now();
            world_cache_portal_stats portal{};
            if(producer_matches) {
                portal=inspect_world_cache_portal_membership(
                    safe_read,address,cache.world,cache.frame_candidates,
                    world_cache_native_visible_count,
                    world_cache_native_visible_list,
                    cache.portal_surface_membership,
                    &cache.visible_sector_spans,
                    &cache.native_visible_sector_scratch);
            }
            else {
                cache.portal_surface_membership.clear();
                cache.visible_sector_spans.clear();
                portal.reason="jk_visibility_producer_signature_mismatch";
            }
            auto const portal_us=elapsed_profile_us(
                portal_started,profile_clock::now());
            // Sector-first staging runs every frame. Compare it to an
            // independent whole-world scan only on diagnostic samples;
            // JK native rendering is never suppressed.
            world_cache_sector_stage_stats sector_stage{};
            world_cache_stage_parity parity{};
            uint64_t sector_stage_us=0;
            if(portal.valid) {
                auto const sector_started=profile_clock::now();
                sector_stage=collect_world_cache_sector_candidates(
                    safe_read,cache.world.surfaces_addr,cache.mesh,
                    cache.cached_surface_lookup,cache.visible_sector_spans,
                    cache.sector_seen_epochs,cache.sector_seen_epoch,
                    cache.sector_candidates,
                    can_cull?&camera.frustum:nullptr,
                    can_cull?&camera.native_view:nullptr,
                    &cache.sector_special_candidates,
                    &cache.sector_sky_candidates);
                sector_stage_us=elapsed_profile_us(
                    sector_started,profile_clock::now());
                if(sector_stage.valid && sampled)
                    parity=compare_world_cache_candidate_sets(
                        cache.frame_candidates,cache.portal_surface_membership,
                        cache.sector_candidates);
            } else {
                cache.sector_candidates.clear();
                cache.sector_special_candidates.clear();
                cache.sector_sky_candidates.clear();
            }
            if(sampled) {
                // These ranges have live material/cel and conservative
                // frustum filtering, NOT native sector/portal visibility.
                // The legacy world D3D batch also contains fallback Things.
                LOG_INFO("WORLD CACHE phase1 staged_surfaces=",
                    state.candidates,
                    " staged_tris=",state.triangles,
                    " frustum_rejected=",state.frustum_rejected,
                    " frustum_available=",state.frustum_available?1:0,
                    " hidden=",state.hidden,
                    " not_textured=",state.not_textured,
                    " solid_color=",state.solid_color,
                    " solid_adjoin=",state.solid_adjoin,
                    " passable_adjoin=",state.passable_adjoin,
                    " translucent_face=",state.translucent_face,
                     " special_candidates=",state.special_candidates,
                    " missing_uv=",state.missing_uv,
                    " missing_material=",state.missing_material,
                    " sky=",state.sky,
                    " sky_horizon=",state.sky_horizon,
                    " sky_ceiling=",state.sky_ceiling,
                    " changed_cel=",state.changed_cel,
                    " changed_material=",state.changed_material,
                    " unreadable=",state.unreadable,
                    " collect_us=",collect_us,
                    " all_cached_surfaces=",cache.mesh.surfaces.size(),
                    " legacy_world_batch_tris=",world_batch.size()+
                        world_transparent_batch.size(),
                    " portal_lookup_us=",portal_us,
                     " portal_producer_verified=",producer_matches?1:0,
                     " portal_probe_valid=",portal.valid?1:0,
                    " portal_probe_reason=",portal.reason,
                    " portal_visible_sectors=",portal.visible_sector_count,
                    " portal_member_surfaces=",portal.member_surfaces,
                    " portal_matched_candidates=",portal.matched_candidates,
                    " portal_rejected_candidates=",portal.rejected_candidates,
                    " sector_stage_valid=",sector_stage.valid?1:0,
                    " sector_stage_candidates=",sector_stage.faces.candidates,
                    " sector_stage_tris=",sector_stage.faces.triangles,
                    " sector_stage_special_faces=",
                        cache.sector_special_candidates.size(),
                    " sector_stage_sky_faces=",
                        cache.sector_sky_candidates.size(),
                    " sector_stage_translucent_face=",
                        sector_stage.faces.translucent_face,
                    " sector_stage_solid_adjoin=",
                        sector_stage.faces.solid_adjoin,
                    " sector_stage_passable_adjoin=",
                        sector_stage.faces.passable_adjoin,
 
                    " sector_stage_us=",sector_stage_us,
                    " sector_stage_native_surfaces=",sector_stage.unique_surfaces,
                    " sector_stage_cached_surfaces=",sector_stage.cached_surfaces,
                    " sector_stage_uncached_surfaces=",sector_stage.uncached_surfaces,
                    " sector_stage_duplicate_visits=",sector_stage.repeated_surfaces,
                    " sector_stage_frustum_rejected=",
                        sector_stage.faces.frustum_rejected,
                    " sector_stage_unreadable=",sector_stage.faces.unreadable,
                    " sector_parity_exact=",parity.exact?1:0,
                    " sector_parity_matched=",parity.matched,
                    " sector_parity_missing=",parity.only_full_scan,
                    " sector_parity_extra=",parity.only_sector_stage,
                    " sector_parity_metadata_diff=",parity.changed_metadata,
                    " visibility=verified_jkgm_portal_list_read_only",
                    " staging=sector_first_every_frame",
                    " status=gpu_first_depth_native_fallback");
            }
        }

        void draw_game_gbuffer_pass(triangle_buffer_models *trimdl, bool posterize_lighting)
        {
            frame_screen->begin_opaque_pass();
            draw_game_opaque_into_gbuffer(trimdl, posterize_lighting);
            frame_screen->end_opaque_pass();

            frame_ao->update_ssao_texture(ogs.get(),
                                          frame_screen->get_resolved_normal_texture(),
                                          frame_screen->get_resolved_depth_texture());

            frame_screen->begin_compose_opaque_pass(
                the_config->retain_hom_pixels && mode==renderer_mode::ingame);
            draw_game_opaque_composite();
            frame_screen->end_compose_opaque_pass();
        }

        // One MAT binder shared by opaque and transparent world geometry.
        // Matches the GPU 3DO sampler units and factors. Geometry-specific
        // vertex transforms/lighting and sky apertures remain independent.
        void bind_world_cache_material(
            shaders::world_cache_preview_shader &shader,
            gpu_3do_texture const &material,uint32_t geometry_mode)
        {
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             material.texture);
            shader.set_solid_fullbright(material.solid_fullbright);
            bool const indexed_fullbright=geometry_mode==4U &&
                the_config->restore_cmp_lights &&
                material.has_palette_fullbright;
            if(indexed_fullbright) {
                gl::set_active_texture_unit(11);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 material.palette_fullbright_mask);
            }
            shader.set_palette_fullbright(indexed_fullbright);
            if(geometry_mode==3U)shader.set_solid_color(material.tint);
            else shader.set_texture(material.width,material.height,
                                    material.tint,material.albedo_scroll,
                                    material.albedo_source);
            gl::set_active_texture_unit(1);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             material.emission);
            shader.set_emissive(material.has_emission,
                                material.emission_factor,
                                material.emissive_scroll);
            shader.set_albedo_transition(material.albedo_source2,
                                          material.albedo_transition);
            if(material.albedo_transition>0.0f) {
                gl::set_active_texture_unit(6);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 material.texture2);
            }
            // Like GPU 3DOs, direct sunlight and opt-in dynamic Thing
            // lights produce GGX without requiring an EnvMap. Preserve the
            // material's own strength, roughness, metalness and normal maps.
            auto const sun=get_sun_settings();
            bool const direct_sun=the_config->enable_sun &&
                sun_level_has_sky && sun.intensity.value()[0]>0.0f;
            bool const direct_points=the_config->enable_dynamic_light_specular;
            bool const default_eligible=(geometry_mode==3U ||
                                          geometry_mode==4U) &&
                material.albedo_source!=0 && !material.solid_fullbright &&
                !material.emissive_only &&
                material.blend!=material_blend_mode::additive &&
                material.blend!=material_blend_mode::multiply;
            auto const specular=gpu_specular_parameters(
                material,default_eligible);
            bool const reflective=the_config->enable_reflections &&
                specular.first>0.0f &&
                (material.has_environment || material.has_environment2 ||
                 direct_sun || direct_points);
            bool const diffuse_mapping=
                the_config->enable_normal_mapped_diffuse &&
                geometry_mode==4U && !material.solid_fullbright &&
                (material.has_normal || material.has_normal2);
            if(reflective) {
                if(material.has_surface) {
                    gl::set_active_texture_unit(2);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     material.surface);
                }
                if(material.has_environment) {
                    gl::set_active_texture_unit(4);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     material.environment);
                }
                if(material.has_environment2 &&
                   material.environment_transition>0.0f) {
                    gl::set_active_texture_unit(7);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     material.environment2);
                }
            }
            if((reflective || diffuse_mapping) && material.has_normal) {
                gl::set_active_texture_unit(3);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 material.normal);
            }
            if((reflective || diffuse_mapping) && material.has_normal2) {
                gl::set_active_texture_unit(5);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 material.normal2);
            }
            shader.set_reflection(
                make_point(reflective?specular.first:0.0f,
                           specular.second,material.metalness,
                           reflective && material.has_surface?1.0f:0.0f),
                make_point((reflective || diffuse_mapping) &&
                           material.has_normal?1.0f:0.0f,
                           material.normal_strength,
                           (reflective || diffuse_mapping) &&
                           material.has_normal2?1.0f:0.0f,
                           material.normal_strength2),
                material.normal_scroll,material.normal_scroll2,
                reflective && material.has_environment,
                reflective && material.has_environment2,
                reflective?material.environment_transition:0.0f,
                material.reflection_mode);
            gl::set_active_texture_unit(0);
        }

        void draw_world_cache_preview()
        {
            if(!the_config->use_gpu_cache ||
               !ogs->world_cache_preview_program || !world_cache_target ||
               !world_cache_target->ready ||
               !frame_world_cache_camera.valid ||
               !frame_world_cache_camera.perspective)return;
            auto &cache=*world_cache_target;
            if(cache.sector_candidates.empty() &&
               cache.sector_special_candidates.empty() &&
               cache.sector_sky_candidates.empty())return;
            auto const &cam=frame_world_cache_camera;
            float const aspect=get<x>(internal_scr_res_f)/
                               get<y>(internal_scr_res_f);
            float const half_fov=std::tan(
                cam.camera.fov*0.008726646259971648f);
            if(!(std::isfinite(half_fov) && half_fov>0.0f &&
                 std::isfinite(aspect) && aspect>0.0f &&
                 std::isfinite(cam.far_plane) && cam.far_plane>0.1f))
                return;

            // Group GPU cache draws by native material and current cel.
            // GPU G-buffer depth runs before the native LESS fallback.
            static thread_local std::vector<world_cache_draw_candidate const*> ordered;
            static thread_local std::vector<GLsizei> counts;
            static thread_local std::vector<void const*> offsets;
            ordered.clear();
            counts.clear();
            offsets.clear();
            cache.frame_render_candidates.clear();
            ordered.reserve(cache.sector_candidates.size()+
                            cache.sector_special_candidates.size());
            auto safe_read=[](uintptr_t ptr,void *dst,size_t len) {
                if(!len || !engine_camera_detail::readable(ptr,len))
                    return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),len);
                return true;
            };
            // GPU world geometry follows the SAME gpu_pixel_lighting
            // switch as GPU-cached 3DOs. When disabled, preserve JK's
            // native per-world-vertex Gouraud dynamic-light contributions.
            // The independent per-pixel path uses no native light scratch.
            float camera_ambient=0.0f,light_attenuation=0.0f;
            // sithRender global Extra Light is a separate live world
            // floor adjustment. Camera Ambient remains Thing-only.
            float global_extra=0.0f;
            if(!safe_read(0x008D4098U,&global_extra,sizeof(global_extra)) ||
               !std::isfinite(global_extra) ||
               std::fabs(global_extra)>8.0f)
                global_extra=0.0f;
            std::vector<world_cache_point_light> point_lights;
            bool const camera_ready=read_world_cache_camera_lights(
                safe_read,0x0073A418U,point_lights,camera_ambient,
                light_attenuation,the_config->enable_baked_overbright);
            // With pixel lighting enabled, an empty valid light list
            // still means zero pixel lights (never use stale native data).
            // When disabled, use JK's native world-vertex Gouraud light
            // buffer, except on verified zero-light frames: native JK
            // may leave scratch values from the previous lit frame.
            bool const pixel_lighting=world_cache_use_pixel_lights(
                the_config->gpu_pixel_lighting,camera_ready);
            bool native_dynamic_ready=false;
            if(pixel_lighting) {
                cache.native_dynamic_light.clear();
            } else if(camera_ready && point_lights.empty() &&
                      cache.world.vertex_count>0U &&
                      cache.world.vertex_count<=1000000U) {
                cache.native_dynamic_light.assign(
                    cache.world.vertex_count,0.0f);
                native_dynamic_ready=true;
            } else {
                native_dynamic_ready=read_world_cache_dynamic_lights(
                    safe_read,cache.loaded_world,cache.world.vertex_count,
                    cache.native_dynamic_light);
            }
            // update_world() initializes all live arrays and matching GPU
            // storage. Preserve hidden surfaces' last values: every visible
            // surface is sampled again before it can be submitted.
            size_t const live_vertices=cache.mesh.vertices.size();
            if(cache.frame_light_rgb.size()!=live_vertices ||
               cache.frame_light_state.size()!=live_vertices ||
               cache.frame_sector_tint.size()!=live_vertices ||
               cache.frame_sector_cmp_tint.size()!=live_vertices ||
               cache.frame_uv_offset.size()!=live_vertices)return;
            // In JKGM rdColormap_pIdentityMap lives at 0x73A414.
            // Native rdCache ignores this CMP's stored tint (even if zero).
            uint32_t identity_cmp=0;
            if(!safe_read(0x0073A414U,&identity_cmp,sizeof(identity_cmp)) ||
               !decal_native_address_ok(identity_cmp))
                identity_cmp=0;
            size_t light_fallback=0,gouraud_faces=0,mode4_faces=0;
            size_t light_read_rejected=0,uv_prepare_rejected=0;
            size_t stage_rejections_logged=0;
            std::map<std::string,size_t> sampled_stage_reasons;
            // Inspect a bounded number of exact failures on the regular
            // profiling cadence, not every native surface each frame.
            auto log_stage_rejection=[&](
                world_cache_draw_candidate const &candidate,
                char const *stage,char const *reason,
                world_cache_light_sample const &info) {
                if(!the_config->profile_renderer ||
                   gpu_3do_present_serial%120U!=0U)return;
                // Count every reason in a sampled frame, even when the
                // per-surface detail limit has already been reached.
                ++sampled_stage_reasons[std::string(stage)+"/"+
                    (reason?reason:"unspecified")];
                if(stage_rejections_logged>=12U)return;
                ++stage_rejections_logged;
                uint32_t sector=0,mode=0,indices=0,static_lights=0;
                uintptr_t const base=cache.world.surfaces_addr+
                    uintptr_t(candidate.surface_index)*0x5cU;
                safe_read(base+0x08U,&sector,sizeof(sector));
                safe_read(base+0x20U,&mode,sizeof(mode));
                safe_read(base+0x2cU,&indices,sizeof(indices));
                safe_read(base+0x54U,&static_lights,sizeof(static_lights));
                // Raw samples aid MotS-style mono/RGB investigation without
                // assuming the native JKGM loader stores interleaved RGB.
                std::array<float,4> static_sample{};
                bool const static_sample_readable=static_lights!=0U &&
                    safe_read(uintptr_t(static_lights),
                              static_sample.data(),sizeof(static_sample));
                LOG_INFO("WORLD CACHE staging reject world=",cache.loaded_world,
                         " surface=",candidate.surface_index,
                         " stage=",stage,
                         " reason=",reason?reason:"unspecified",
                         " geo=",candidate.geometry_mode,
                         " mode=",mode," sampled_mode=",info.mode,
                         " sector=",sector,
                         " vertex_indices_ptr=",indices,
                         " static_light_ptr=",static_lights,
                         " static_sample_readable=",static_sample_readable?1:0,
                         " static_sample=",static_sample[0],",",
                                           static_sample[1],",",
                                           static_sample[2],",",
                                           static_sample[3],
                         " native_vertex_count=",cache.world.vertex_count,
                         " dynamic_lights=",cache.native_dynamic_light.size(),
                         " pixel_lighting=",pixel_lighting?1:0,
                         " first_index=",candidate.first_index,
                         " indices=",candidate.count,
                         " mat_ptr=",candidate.material,
                         " cel=",candidate.cel);
            };
            bool const suppress_sector_tint=
                native_sector_palette_mode().load(
                    std::memory_order_relaxed)==1;
            auto stage_candidate=[&](world_cache_draw_candidate const &candidate) {
                world_cache_light_sample info{};
                char const *failure=nullptr;
                if(!sample_world_cache_surface_light(
                       safe_read,cache.world.surfaces_addr,cache.mesh,
                       candidate,cache.world.vertex_count,
                       cache.native_dynamic_light,cache.frame_light_rgb,
                       &info,&cache.frame_light_state,
                       pixel_lighting,global_extra,&cache.frame_sector_tint,
                       &cache.frame_sector_cmp_tint,identity_cmp,&failure,
                       the_config->enable_baked_overbright)) {
                    ++light_fallback;
                    ++light_read_rejected;
                    log_stage_rejection(candidate,"lighting",failure,info);
                    return;
                }
                // Native JK has already advanced SlideSurface UV offsets.
                // The GPU mesh retains its immutable authored coordinates.
                if(!write_world_cache_uv_animation(
                       cache.mesh,cache.cached_surface_lookup,
                       candidate,cache.frame_uv_offset,&failure)) {
                    ++light_fallback;
                    ++uv_prepare_rejected;
                    log_stage_rejection(candidate,"uv_animation",failure,info);
                    return;
                }
                // FreeColorEffect must suppress only surfaces actually
                // submitted. Do not clear every world vertex each frame.
                if(suppress_sector_tint) {
                    size_t const first=cache.mesh.indices[candidate.first_index];
                    size_t const count=size_t(candidate.count)/3U+2U;
                    std::fill_n(cache.frame_sector_tint.begin()+first,count,
                        std::array<float,3>{{0.0f,0.0f,0.0f}});
                }
                if(info.mode==3U)++gouraud_faces;
                if(info.authored_mode==4U)++mode4_faces;
                ordered.push_back(&candidate);
                cache.frame_render_candidates.push_back(candidate);
            };
            for(auto const &candidate:cache.sector_candidates)
                stage_candidate(candidate);
            // Even exclusively translucent native faces need their live
            // lighting, sector tint and animated UVs uploaded here.
            // They are submitted only in the later forward-alpha pass.
            for(auto const &candidate:cache.sector_special_candidates)
                stage_candidate(candidate);
            if(ordered.empty() && cache.sector_sky_candidates.empty()) {
                // Still report diagnostic totals if every candidate failed
                // before the GPU pass had anything to draw.
                if(the_config->profile_renderer &&
                   gpu_3do_present_serial%120U==0U && light_fallback>0) {
                    LOG_INFO("WORLD CACHE all staged draws rejected world=",
                             cache.loaded_world,
                             " lighting_read_rejected=",light_read_rejected,
                             " uv_prepare_rejected=",uv_prepare_rejected);
                    for(auto const &entry:sampled_stage_reasons)
                        LOG_INFO("WORLD CACHE staging reject summary world=",
                                 cache.loaded_world," reason=",entry.first,
                                 " surfaces=",entry.second);
                }
                return;
            }
            // Native portal visibility has already selected the only faces
            // whose live vertex attributes need refreshing this frame.
            // The full-upload fallback avoids dozens of small GL calls or
            // stalling in glBufferSubData on heavily visible/fragmented maps.
            auto const stream=plan_world_cache_vertex_stream(
                cache.mesh,cache.frame_render_candidates);
            auto upload_live=[&](auto const &buffer,auto const &values) {
                gl::bind_buffer(gl::buffer_bind_target::array,buffer);
                if(stream.partial) {
                    for(auto const &range:stream.ranges)
                        gl::buffer_sub_data(gl::buffer_bind_target::array,
                            range.first_vertex*sizeof(values[0]),
                            make_span(values.data()+range.first_vertex,
                                      range.count).as_const_bytes());
                } else {
                    gl::buffer_data(gl::buffer_bind_target::array,
                        make_span(values).as_const_bytes(),
                        gl::buffer_usage::stream_draw);
                }
            };
            if(!cache.frame_render_candidates.empty()) {
                upload_live(cache.lighting,cache.frame_light_rgb);
                upload_live(cache.light_state,cache.frame_light_state);
                upload_live(cache.sector_tint,cache.frame_sector_tint);
                upload_live(cache.sector_cmp_tint,cache.frame_sector_cmp_tint);
                upload_live(cache.uv_animation,cache.frame_uv_offset);
            }
            if(the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U) {
                size_t const transferred=cache.frame_render_candidates.empty()
                    ?0U:(stream.partial?stream.touched_vertices:live_vertices);
                LOG_INFO("WORLD CACHE vertex streaming mode=",
                    cache.frame_render_candidates.empty()?"none":
                        stream.partial?"partial":"full",
                    " visible_vertices=",stream.touched_vertices,
                    " uploaded_vertices=",transferred,
                    " total_vertices=",live_vertices,
                    " merged_ranges=",stream.ranges.size(),
                    " uploaded_bytes=",transferred*
                        (4U*sizeof(std::array<float,3>)+
                         sizeof(std::array<float,2>)));
            }
            std::sort(ordered.begin(),ordered.end(),
                [](world_cache_draw_candidate const *a,
                   world_cache_draw_candidate const *b) {
                    if(a->material!=b->material)
                        return a->material<b->material;
                    if(a->cel!=b->cel)return a->cel<b->cel;
                    if(a->geometry_mode!=b->geometry_mode)
                        return a->geometry_mode<b->geometry_mode;
                    if(a->solid_adjoin!=b->solid_adjoin)
                        return a->solid_adjoin<b->solid_adjoin;
                    if(a->passable_adjoin!=b->passable_adjoin)
                        return a->passable_adjoin<b->passable_adjoin;
                    if((a->face_type&0x2U)!=(b->face_type&0x2U))
                        return (a->face_type&0x2U)<(b->face_type&0x2U);
                    return a->surface_index<b->surface_index;
                });
            auto &shader=*ogs->world_cache_preview_program;
            shader.use_program();
            shader.set_specular_normal_mode(
                the_config->experimental_specular_normal_mode);
            shader.set_light_brightness_limit(
                authored_light_limit(the_config->enable_baked_overbright));
            shader.set_normal_mapped_diffuse(
                the_config->enable_normal_mapped_diffuse);
            auto const &m=cam.native_view;
            shader.set_view_matrix(
                make_point(m[0],m[1],m[2],0.0f),
                make_point(m[3],m[4],m[5],0.0f),
                make_point(m[6],m[7],m[8],0.0f),
                make_point(m[9],m[10],m[11],1.0f));
            shader.set_projection(make_point(
                half_fov,aspect,1.0f/cam.far_plane,0.0f));
            shader.set_reflection_camera(cam.camera.position,cam.camera.right,
                                         cam.camera.forward,cam.camera.up);
            // Reset MAT state for sky aperture rendering; custom material
            // emission/transition is bound only on ordinary world faces.
            shader.set_alpha_policy(false,0,0.5f,1.0f);
            shader.set_albedo_transition(2,0.0f);
            shader.set_emissive(false,make_point(0.0f,0.0f,0.0f,0.0f),
                                make_point(0.0f,0.0f));
            shader.set_reflection(make_point(0.0f,0.0f,0.0f,0.0f),
                                  make_point(0.0f,1.0f,0.0f,1.0f),
                                  make_point(0.0f,0.0f),make_point(0.0f,0.0f),
                                  false,false,0.0f,0);
            // If the native request-pool layout has not yet been
            // validated, do NOT copy the unseparated camera-sector tint
            // onto already locally tinted world faces. Preserve the
            // previously working world-cache behavior until bridge ready.
            jk_palette_effect remaining_effect{};
            if(native_sector_palette_mode().load(
                   std::memory_order_relaxed)!=0)
                remaining_effect=read_native_jk_hardware_color_effects();
            shader.set_jk_color_effects(
                make_point(float(remaining_effect.filter[0]),
                           float(remaining_effect.filter[1]),
                           float(remaining_effect.filter[2]),
                           remaining_effect.active?1.0f:0.0f),
                make_point(remaining_effect.tint[0],
                           remaining_effect.tint[1],
                           remaining_effect.tint[2],0.0f),
                make_point(remaining_effect.add[0]/255.0f,
                           remaining_effect.add[1]/255.0f,
                           remaining_effect.add[2]/255.0f,
                           remaining_effect.fade));
            bool const point_specular=the_config->enable_reflections &&
                the_config->enable_dynamic_light_specular && camera_ready;
            if(!pixel_lighting && !point_specular)point_lights.clear();
            shader.set_specular_lighting(pixel_lighting,point_specular);
            set_translucent_sun(shader,the_config->enable_reflections);
            shader.set_lights(point_lights,light_attenuation,
                frame_saber_light_colors.colors,
                frame_saber_light_colors.valid);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_function(gl::comparison_function::less);
            gl::set_depth_mask(true);
            gl::disable(gl::capability::blend);
            // World surfaces are unconditionally single-sided, including
            // those marked 0x1 in native JK. Fan winding is normalized to
            // each SithSurface's authored normal at cache build time.
            glFrontFace(GL_CCW);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::enable(gl::capability::cull_face);
            // The native opaque world batch is disabled for this isolation
            // test, so no polygon offset is required or desirable.
            if(isolate_native_world_batches()) {
                glDisable(GL_POLYGON_OFFSET_FILL);
            } else {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-2.0f,-2.0f);
            }
            gl::bind_vertex_array(cache.vao);
            size_t sky_drawn=0,sky_horizon_drawn=0,sky_ceiling_drawn=0;
            world_cache_sky_info sky_info{};
            std::array<float,3> sky_angles{};
            bool const sky_params_ready=read_world_cache_sky_info(
                safe_read,cache.loaded_world,sky_info) &&
                world_cache_sky_angles(cam.camera,sky_angles);
            if(sky_params_ready) {
                // Sky shader uses the same VBO but a camera-space UV and
                // far-depth G-buffer policy. No shadow or light receiver.
                shader.set_lights({},0.0f,
                    frame_saber_light_colors.colors,
                    frame_saber_light_colors.valid);
                for(auto const &face:cache.sector_sky_candidates) {
                    auto const sky_type=world_cache_sky_type(face.sky_flags);
                    if(sky_type==world_cache_sky_kind::none ||
                       (face.face_type&0x2U)!=0U ||
                       (sky_type==world_cache_sky_kind::horizon &&
                        !sky_info.horizon_valid) ||
                       (sky_type==world_cache_sky_kind::ceiling &&
                        !sky_info.ceiling_valid))continue;
                    gpu_3do_group group{};
                    group.material=face.material;
                    group.cel=face.cel;
                    bool const solid=face.geometry_mode==3U;
                    auto const mat=solid
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group);
                    // Mixed-alpha/special sky MATs need a later sky-alpha
                    // pass. Do not silently turn their pixels opaque.
                    if(mat.albedo_source!=(solid?2:1) ||
                       (!solid && !mat.has_image) ||
                       mat.alpha || mat.masked || mat.dithered ||
                       mat.special || mat.emissive_only ||
                       !std::isfinite(mat.opacity) || mat.opacity<0.999f ||
                       mat.albedo_transition!=0.0f ||
                       (!solid && (!(mat.width>0.0f) ||
                                    !(mat.height>0.0f))))
                        continue;
                    auto const &sky_offset=
                        sky_type==world_cache_sky_kind::horizon
                            ?sky_info.horizon_offset:sky_info.ceiling_offset;
                    shader.set_sky(static_cast<int>(sky_type),
                        make_point(sky_info.horizon_distance*half_fov,
                                   sky_info.pixels_per_revolution,half_fov,0.0f),
                        make_point(sky_angles[0],sky_angles[1],sky_angles[2]),
                        make_point(cam.camera.position[0],
                                   cam.camera.position[1],cam.camera.position[2]),
                        make_point(cam.camera.right[0],
                                   cam.camera.right[1],cam.camera.right[2]),
                        make_point(cam.camera.forward[0],
                                   cam.camera.forward[1],cam.camera.forward[2]),
                        make_point(cam.camera.up[0],cam.camera.up[1],
                                   cam.camera.up[2]),
                        sky_info.ceiling_height,
                        make_point(sky_offset[0],sky_offset[1]),
                        make_point(face.uv_offset[0],face.uv_offset[1]),
                        make_point(float(get<x>(internal_scr_res_f)),
                                   float(get<y>(internal_scr_res_f))));
                    if(solid)shader.set_solid_color(mat.tint);
                    else {
                        gl::set_active_texture_unit(0);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         mat.texture);
                        shader.set_texture(mat.width,mat.height,mat.tint,
                                           mat.albedo_scroll);
                    }
                    gl::draw_elements(gl::element_type::triangles,
                        static_cast<GLsizei>(face.count),gl::index_type::uint32,
                        static_cast<ptrdiff_t>(
                            face.first_index*sizeof(uint32_t)));
                    ++sky_drawn;
                    if(sky_type==world_cache_sky_kind::horizon)
                        ++sky_horizon_drawn;
                    else ++sky_ceiling_drawn;
                }
            }
            shader.set_sky(0,make_point(0.0f,0.0f,0.0f,0.0f),
                make_point(0.0f,0.0f,0.0f),make_point(0.0f,0.0f,0.0f),
                make_point(0.0f,0.0f,0.0f),make_point(0.0f,0.0f,0.0f),
                make_point(0.0f,0.0f,0.0f),0.0f,
                make_point(0.0f,0.0f),make_point(0.0f,0.0f),
                make_point(1.0f,1.0f));
            shader.set_lights(point_lights,light_attenuation,
                frame_saber_light_colors.colors,
                frame_saber_light_colors.valid);
            size_t drawn=0,solid_drawn=0,masked_drawn=0,adjoin_drawn=0;
            size_t passable_drawn=0,unresolved=0,groups=0,skipped_alpha=0;
            size_t mixed_drawn=0,dithered_drawn=0;
            // Report visible world MAT state: sun/Thing GGX now works
            // for reflective materials without a separate EnvMap.
            size_t mat_probe_faces=0,mat_probe_reflective=0;
            size_t mat_probe_normals=0,mat_probe_surface_maps=0;
            size_t mat_probe_env=0,mat_probe_missing_env=0;
            size_t mat_probe_no_strength=0;
            bool const mat_probe=the_config->profile_renderer &&
                                  gpu_3do_present_serial%120U==0U;
            std::set<std::pair<uintptr_t,int32_t>> mat_failures_logged;
            for(size_t i=0;i<ordered.size();) {
                size_t end=i+1;
                auto const *first=ordered[i];
                while(end<ordered.size() &&
                      ordered[end]->material==first->material &&
                      ordered[end]->cel==first->cel &&
                      ordered[end]->geometry_mode==first->geometry_mode &&
                      ordered[end]->solid_adjoin==first->solid_adjoin &&
                      ordered[end]->passable_adjoin==first->passable_adjoin &&
                      ((ordered[end]->face_type^first->face_type)&0x2U)==0U)++end;
                gpu_3do_group group{};
                group.material=first->material;
                group.cel=first->cel;
                bool const solid=first->geometry_mode==3U;
                auto const material=solid
                    ?resolve_gpu_3do_solid_material(group,false)
                    :resolve_gpu_3do_material(group);
                world_cache_material_info info{};
                info.albedo_source=material.albedo_source;
                info.has_image=material.has_image;
                info.alpha=material.alpha;
                info.masked=material.masked;
                info.dithered=material.dithered;
                info.special=material.special;
                info.blend_mode=static_cast<int>(material.blend);
                info.emissive_only=material.emissive_only;
                info.opacity=material.opacity;
                info.transition=material.albedo_transition;
                info.width=material.width;
                info.height=material.height;
                info.alpha_cutoff=material.alpha_cutoff;
                auto const policy=choose_world_cache_draw_policy(*first,info);
                if(mat_probe && first->geometry_mode==4U &&
                   !world_cache_albedo_ready(material.albedo_source,
                                              material.has_image) &&
                   mat_failures_logged.size()<8U &&
                   mat_failures_logged.insert({first->material,first->cel}).second)
                    LOG_INFO("WORLD CACHE unresolved MAT ",
                             world_cache_material_failure(*first));
                if(mat_probe && world_cache_draws_in_pass(
                    policy,world_cache_pass::opaque)) {
                    size_t const faces=end-i;
                    mat_probe_faces+=faces;
                    if(material.has_normal || material.has_normal2)
                        mat_probe_normals+=faces;
                    if(material.has_surface)mat_probe_surface_maps+=faces;
                    if(material.has_environment || material.has_environment2)
                        mat_probe_env+=faces;
                    if(material.reflection_strength>0.0f) {
                        if(material.has_environment || material.has_environment2)
                            mat_probe_reflective+=faces;
                        else mat_probe_missing_env+=faces;
                    } else mat_probe_no_strength+=faces;
                }
                if(world_cache_draws_in_pass(policy,world_cache_pass::opaque)) {
                    bind_world_cache_material(shader,material,
                                              first->geometry_mode);
                    shader.set_alpha_policy(false,world_cache_alpha_mode(policy),
                                            material.alpha_cutoff,1.0f);
                    counts.clear();
                    offsets.clear();
                    for(size_t j=i;j<end;++j) {
                        auto const *c=ordered[j];
                        counts.push_back(static_cast<GLsizei>(c->count));
                        offsets.push_back(reinterpret_cast<void const *>(
                            uintptr_t(c->first_index)*sizeof(uint32_t)));
                    }
                    glMultiDrawElements(GL_TRIANGLES,counts.data(),
                        GL_UNSIGNED_INT,offsets.data(),
                        static_cast<GLsizei>(counts.size()));
                    drawn+=counts.size();
                    if(solid)solid_drawn+=counts.size();
                    if(policy==world_cache_draw_policy::masked)
                        masked_drawn+=counts.size();
                    if(policy==world_cache_draw_policy::mixed)
                        mixed_drawn+=counts.size();
                    if(policy==world_cache_draw_policy::dithered)
                        dithered_drawn+=counts.size();
                    if(first->solid_adjoin)adjoin_drawn+=counts.size();
                    if(first->passable_adjoin)passable_drawn+=counts.size();
                    ++groups;
                } else if((solid && material.albedo_source!=2) ||
                          (!solid && !world_cache_albedo_ready(
                              material.albedo_source,material.has_image) &&
                           !material.emissive_only))unresolved+=end-i;
                else skipped_alpha+=end-i;
                i=end;
            }
            gl::bind_vertex_array(gl::vertex_array_view(0U));
            gl::disable(gl::capability::cull_face);
            glDisable(GL_POLYGON_OFFSET_FILL);
            gl::set_depth_function(gl::comparison_function::less);
            gl::set_active_texture_unit(0);
            if(the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U) {
                LOG_INFO("WORLD CACHE gpu_sky ranges=",sky_drawn,
                         " horizon=",sky_horizon_drawn,
                         " ceiling=",sky_ceiling_drawn,
                         " sky_stage=",cache.sector_sky_candidates.size(),
                         " sky_params_ready=",sky_params_ready?1:0,
                         " sky_ceiling_z=",sky_info.ceiling_height,
                         " sky_horizon_distance=",sky_info.horizon_distance,
                         " sky_pixels_per_rev=",sky_info.pixels_per_revolution,
                         " horizon_offset=",sky_info.horizon_offset[0],",",
                                              sky_info.horizon_offset[1],
                         " ceiling_offset=",sky_info.ceiling_offset[0],",",
                                              sky_info.ceiling_offset[1],
                         " world_sky_fields_offset=0xdc");
                LOG_INFO("WORLD CACHE gpu_first_opaque ranges=",drawn,
                         " solid_ranges=",solid_drawn,
                         " adjoin_ranges=",adjoin_drawn,
                         " passable_adjoin_ranges=",passable_drawn,
                         " masked_ranges=",masked_drawn,
                         " mixed_opaque_ranges=",mixed_drawn,
                         " dithered_ranges=",dithered_drawn,
                         " multi_draw_calls=",groups,
                         " unresolved_material=",unresolved,
                         " deferred_special=",skipped_alpha,
                         " light_fallback=",light_fallback,
                         " lighting_read_rejected=",light_read_rejected,
                         " uv_prepare_rejected=",uv_prepare_rejected,
                         " gouraud_faces=",gouraud_faces,
                         " mode4_gouraud_faces=",mode4_faces);
                for(auto const &entry:sampled_stage_reasons)
                    LOG_INFO("WORLD CACHE staging reject summary world=",
                             cache.loaded_world," reason=",entry.first,
                             " surfaces=",entry.second);
                LOG_INFO("WORLD CACHE material_effects_opaque visible=",mat_probe_faces,
                         " reflective=",mat_probe_reflective,
                         " env_maps=",mat_probe_env,
                         " normal_maps=",mat_probe_normals,
                         " surface_maps=",mat_probe_surface_maps,
                         " strength_without_env=",mat_probe_missing_env,
                         " zero_strength=",mat_probe_no_strength,
                         " enable_reflections=",the_config->enable_reflections?1:0);
                LOG_INFO("WORLD CACHE light_state pixel_path_valid=",
                         pixel_lighting?1:0,
                         " native_dynamic_fallback_ready=",
                         native_dynamic_ready?1:0,
                         " native_isolation=",isolate_native_world_batches()?1:0,
                         " global_extra=",global_extra,
                         " pixel_lights=",point_lights.size());
                // Read-only material readiness survey for the special
                // (translucent-flag/solid-adjoin) candidates. Read
                // once per unique MAT/cel, sampled just every 120 frames.
                // Zero GPU submissions and no changes to native alpha draws.
                std::map<std::pair<uintptr_t,int32_t>,gpu_3do_texture> materials;
                size_t alpha_texels=0,masked=0,translucent=0,special_blend=0;
                size_t opaque=0,unresolved_special=0,transition=0;
                size_t native_translucent=0,solid_adjoins=0;
                for(auto const &c:cache.sector_special_candidates) {
                    if(c.face_type&0x2U)++native_translucent;
                     if(c.solid_adjoin)++solid_adjoins;
                    auto const key=std::make_pair(c.material,c.cel);
                    auto it=materials.find(key);
                    if(it==materials.end()) {
                        gpu_3do_group group{};
                        group.material=c.material;
                        group.cel=c.cel;
                        it=materials.emplace(key,
                            resolve_gpu_3do_material(group)).first;
                    }
                    auto const &mat=it->second;
                    if(!world_cache_albedo_ready(mat.albedo_source,
                                                 mat.has_image)) {
                        ++unresolved_special;
                        if(mat_failures_logged.size()<8U &&
                           mat_failures_logged.insert(key).second)
                            LOG_INFO("WORLD CACHE unresolved MAT ",
                                     world_cache_material_failure(c));
                        continue;
                    }
                    if(mat.alpha)++alpha_texels;
                    if(mat.masked || mat.dithered)++masked;
                    if(mat.special)++special_blend;
                    if(mat.albedo_transition>0.0f)++transition;
                    if(mat.alpha && !mat.masked && !mat.dithered)
                        ++translucent;
                    if(!mat.alpha && !mat.masked &&
                       !mat.dithered && !mat.special)++opaque;
                }
                LOG_INFO("WORLD CACHE alpha_survey special_visible=",
                         cache.sector_special_candidates.size(),
                         " unique_material_cels=",materials.size(),
                         " native_face_translucent=",native_translucent,
                         " solid_adjoin=",solid_adjoins,
                          " alpha_texels=",alpha_texels,
                         " masked=",masked,
                         " translucent_texture=",translucent,
                         " special_blend=",special_blend,
                         " opaque_material=",opaque,
                         " animated_transition=",transition,
                         " unresolved_material=",unresolved_special,
                         " draw_calls=0 native_alpha_unchanged=1");
            }
        }

        // GPU cached world faces with true alpha enter the same
        // premultiplied scene transparency target as GPU 3DOs.
        // This uses the *opaque pass's* vertex lighting and UV upload:
        // never sample mutable JK native pointers after stage_world_cache().
        void draw_world_cache_transparency()
        {
            if(!the_config->use_gpu_cache ||
               !ogs->world_cache_preview_program || !world_cache_target ||
               !world_cache_target->ready || !frame_world_cache_camera.valid)
                return;
            auto &cache=*world_cache_target;
            if(cache.frame_render_candidates.empty())return;
            auto const &cam=frame_world_cache_camera.camera;
            if(!cam.valid)return;
            std::vector<world_cache_draw_candidate const *> draws;
            draws.reserve(cache.frame_render_candidates.size());
            // Cache material resolutions across the two passes; the same
            // native MAT and cel can cover thousands of world surfaces.
            std::map<std::tuple<uintptr_t,int32_t,uint32_t>,
                     gpu_3do_texture> materials;
            auto resolve=[&](world_cache_draw_candidate const &face)
                -> gpu_3do_texture const & {
                auto const key=std::make_tuple(face.material,face.cel,
                                               face.geometry_mode);
                auto it=materials.find(key);
                if(it==materials.end()) {
                    gpu_3do_group group{};
                    group.material=face.material;group.cel=face.cel;
                    it=materials.emplace(key,face.geometry_mode==3U
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group)).first;
                }
                return it->second;
            };
            auto policy_for=[&](world_cache_draw_candidate const &face) {
                auto const &mat=resolve(face);
                world_cache_material_info info{};
                info.albedo_source=mat.albedo_source;
                info.has_image=mat.has_image;info.alpha=mat.alpha;
                info.masked=mat.masked;info.dithered=mat.dithered;
                info.special=mat.special;info.emissive_only=mat.emissive_only;
                info.blend_mode=static_cast<int>(mat.blend);
                info.opacity=mat.opacity;info.transition=mat.albedo_transition;
                info.width=mat.width;info.height=mat.height;
                info.alpha_cutoff=mat.alpha_cutoff;
                return choose_world_cache_draw_policy(face,info);
            };
            for(auto const &face:cache.frame_render_candidates)
                if(world_cache_draws_in_pass(
                    policy_for(face),world_cache_pass::transparent))
                    draws.push_back(&face);
            if(draws.empty())return;
            // Sort actual cached surface ranges back-to-front; no VBO
            // reordering or CPU vertex transformation is required.
            auto depth=[&](world_cache_draw_candidate const *face) {
                auto const idx=cache.cached_surface_lookup[face->surface_index];
                auto const &range=cache.mesh.surfaces[idx];
                float d=0.0f;
                for(int axis=0;axis<3;++axis) {
                    float const center=(range.bounds_min[axis]+
                                        range.bounds_max[axis])*0.5f;
                    d+=(center-cam.position[axis])*cam.forward[axis];
                }
                return d;
            };
            std::sort(draws.begin(),draws.end(),
                [&](auto const *a,auto const *b) {
                    float const da=depth(a),db=depth(b);
                    if(da==db)return a->surface_index<b->surface_index;
                    return da>db;
                });
            auto &shader=*ogs->world_cache_preview_program;
            shader.use_program();
            shader.set_specular_normal_mode(
                the_config->experimental_specular_normal_mode);
            shader.set_light_brightness_limit(
                authored_light_limit(the_config->enable_baked_overbright));
            shader.set_normal_mapped_diffuse(
                the_config->enable_normal_mapped_diffuse);
            shader.set_reflection_camera(cam.position,cam.right,
                                         cam.forward,cam.up);
            // Alpha-only worlds must initialize these independently of
            // any opaque GPU world draws earlier in the frame.
            float camera_ambient=0.0f,light_attenuation=0.0f;
            std::vector<world_cache_point_light> point_lights;
            auto const read_native=[](uintptr_t ptr,void *out,size_t size) {
                if(!size || !engine_camera_detail::readable(ptr,size))
                    return false;
                std::memcpy(out,reinterpret_cast<void const *>(ptr),size);
                return true;
            };
            bool const camera_lights_ready=read_world_cache_camera_lights(
                read_native,0x0073A418U,point_lights,camera_ambient,
                light_attenuation,the_config->enable_baked_overbright);
            bool const pixel_lighting=world_cache_use_pixel_lights(
                the_config->gpu_pixel_lighting,camera_lights_ready);
            bool const point_specular=the_config->enable_reflections &&
                the_config->enable_dynamic_light_specular &&
                camera_lights_ready;
            if(!pixel_lighting && !point_specular)point_lights.clear();
            shader.set_specular_lighting(pixel_lighting,point_specular);
            shader.set_lights(point_lights,light_attenuation,
                frame_saber_light_colors.colors,
                frame_saber_light_colors.valid);
            set_translucent_sun(shader,the_config->enable_reflections);
            auto const &fog=get_fog_settings();
            auto const info=fog.info.value(),color=fog.color.value();
            shader.set_fog(make_point(info[0],info[1],
                               the_config->enable_fog?info[2]:0.0f,0.0f),
                           make_point(color[0],color[1],color[2]));
            // Diagnostic modes affect only GPU world Fluid in the forward
            // pass. Default 0 retains the normal rendering path.
            shader.set_fluid_debug_mode(the_config->world_cache_fluid_debug_mode);
            // Fluid samples the completed opaque depth texture, never the
            // framebuffer receiving translucent world fragments. The 3DO
            // shader already uses texture unit 8 for this same image.
            shader.set_scene_depth(8);
            bool const has_fluid=std::any_of(draws.begin(),draws.end(),
                [&](world_cache_draw_candidate const *face) {
                    return resolve(*face).blend==material_blend_mode::fluid;
                });
            if(has_fluid) {
                gl::set_active_texture_unit(8);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
                gl::set_active_texture_unit(0);
            }
            gl::enable(gl::capability::blend);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_function(gl::comparison_function::less);
            gl::set_depth_mask(false);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            glFrontFace(GL_CCW);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::enable(gl::capability::cull_face);
            gl::bind_vertex_array(cache.vao);
            size_t mixed=0,forced=0,fluid=0,albedo_free=0;
            size_t normal_maps=0,surface_maps=0,env_maps=0;
            size_t active_reflections=0,no_env=0,no_strength=0;
            float fluid_thickness_min=std::numeric_limits<float>::max();
            float fluid_thickness_max=0.0f;
            bool const material_probe=the_config->profile_renderer &&
                                      gpu_3do_present_serial%120U==0U;
            for(auto const *face:draws) {
                auto const &material=resolve(*face);
                auto const policy=policy_for(*face);
                shader.set_alpha_policy(true,world_cache_alpha_mode(policy),
                        material.alpha_cutoff,
                        (face->face_type&0x2U)?90.0f/255.0f:1.0f,
                        static_cast<int>(material.blend),
                        material.emissive_only);
                shader.set_fluid_thickness(material.blend==material_blend_mode::fluid
                    ?material.fluid_thickness:0.0f);
                switch(material.blend) {
                case material_blend_mode::additive:
                    gl::set_blend_function_separate(
                        gl::blend_function::one,gl::blend_function::one,
                        gl::blend_function::zero,gl::blend_function::one);
                    break;
                case material_blend_mode::multiply:
                    gl::set_blend_function_separate(
                        gl::blend_function::destination_color,
                        gl::blend_function::zero,
                        gl::blend_function::zero,gl::blend_function::one);
                    break;
                default:
                    gl::set_blend_function(gl::blend_function::one,
                        gl::blend_function::one_minus_source_alpha);
                    break;
                }
                bind_world_cache_material(shader,material,face->geometry_mode);
                gl::draw_elements(gl::element_type::triangles,
                    face->count,gl::index_type::uint32,
                    static_cast<ptrdiff_t>(
                        face->first_index*sizeof(uint32_t)));
                if(policy==world_cache_draw_policy::mixed)++mixed;
                else ++forced;
                if(material.blend==material_blend_mode::fluid) {
                    ++fluid;
                    if(material_probe) {
                        fluid_thickness_min=std::min(
                            fluid_thickness_min,material.fluid_thickness);
                        fluid_thickness_max=std::max(
                            fluid_thickness_max,material.fluid_thickness);
                    }
                }
                if(material_probe) {
                    if(material.albedo_source==2)++albedo_free;
                    if(material.has_normal || material.has_normal2)++normal_maps;
                    if(material.has_surface)++surface_maps;
                    bool const has_env=material.has_environment ||
                                       material.has_environment2;
                    if(has_env)++env_maps;
                    if(material.reflection_strength>0.0f) {
                        if(has_env)++active_reflections;
                        else ++no_env;
                    } else ++no_strength;
                }
            }
            gl::bind_vertex_array(gl::vertex_array_view(0U));
            shader.set_alpha_policy(false,0,0.5f,1.0f);
            gl::disable(gl::capability::cull_face);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::set_active_texture_unit(0);
            if(material_probe) {
                LOG_INFO("WORLD CACHE gpu_alpha ranges=",draws.size(),
                         " mixed=",mixed," forced_blend=",forced,
                         " fluid=",fluid," albedo_free=",albedo_free,
                         " sorted=back_to_front depth_write=0");
                LOG_INFO("WORLD CACHE material_effects_alpha visible=",draws.size(),
                         " reflective=",active_reflections,
                         " env_maps=",env_maps,
                         " normal_maps=",normal_maps,
                         " surface_maps=",surface_maps,
                         " strength_without_env=",no_env,
                         " zero_strength=",no_strength,
                         " fluid_ranges=",fluid,
                         " fluid_thickness_min=",fluid?
                             fluid_thickness_min:0.0f,
                         " fluid_thickness_max=",fluid_thickness_max,
                         " enable_reflections=",the_config->enable_reflections?1:0);
            }
        }

        void draw_game_transparency_pass(triangle_buffer_models *trimdl, bool posterize_lighting)
        {
            if(scene_gpu_timing)scene_gpu_queries.timestamp(1U,true);
            frame_screen->begin_transparency_pass();
            prepare_translucent_spotlights();

            // Draw batches
            gl::disable(gl::capability::blend);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::front_and_back);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::set_depth_function(gl::comparison_function::less);

            ogs->game_transparency_pass_program.use_program();
            ogs->game_transparency_pass_program.set_screen_resolution(
                static_cast<size<2, float>>(conf_scr_res));
            ogs->game_transparency_pass_program.set_albedo_map_sampler(0);
            ogs->game_transparency_pass_program.set_emissive_map_sampler(1);
            ogs->game_transparency_pass_program.set_displacement_map_sampler(2);
            ogs->game_transparency_pass_program.set_surface_map_sampler(3);
            ogs->game_transparency_pass_program.set_env_map_sampler(4);
            ogs->game_transparency_pass_program.set_normal_map_samplers(6, 7);
            ogs->game_transparency_pass_program.set_scene_depth_sampler(5);
            ogs->game_transparency_pass_program.set_flare_mode(false);
            set_translucent_spotlights(ogs->game_transparency_pass_program,true);
            set_translucent_sun(ogs->game_transparency_pass_program,true);
            // Native Fluid samples opaque scene depth from texture unit 5.
            // GPU-cached world/3DO rendering uses unit 5 for normal_map2,
            // so every transition BACK to native transparent batches must
            // restore this binding (otherwise water samples the normal map
            // as depth and its pigment can disappear).
            auto restore_native_fluid_depth=[&] {
                gl::set_active_texture_unit(5);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
                gl::set_active_texture_unit(0);
            };
            restore_native_fluid_depth();
            apply_engine_camera(ogs->game_transparency_pass_program);

            {
                auto const &fog = get_fog_settings();
                auto const current_color = fog.color.value();
                auto const current_info = fog.info.value();
                ogs->game_transparency_pass_program.set_fog(
                    make_point(current_info[0],
                               current_info[1],
                               the_config->enable_fog ? current_info[2] : 0.0f,
                               0.0f),
                    color_rgb(current_color[0], current_color[1], current_color[2]));
            }
            gl::set_active_texture_unit(0);

            // Decals use the resolved *opaque* receiver depth and are painted
            // onto those opaque/alpha-tested pixels. Draw them BEFORE regular
            // translucent surfaces so glass, foliage, forcefields, etc. can
            // blend over a decal instead of the decal incorrectly covering them.
            // Material and UV caches are shared with the ordinary GPU 3DO path.
            gl::enable(gl::capability::blend);
            gl::set_depth_mask(false);
            // Cached world geometry already owns depth in the opaque pass.
            // Transparent native world rendering remains unchanged.
            if(the_config->enable_decals) {
                draw_gpu_3do_preview(true,false,true);
                ogs->game_transparency_pass_program.use_program();
            }

            // Draw sorted GPU world translucency on the composed scene,
            // after decals and before other translucent world/3DO overlays.
            draw_world_cache_transparency();
            ogs->game_transparency_pass_program.use_program();
            restore_native_fluid_depth();

            // Unsupported special-blend native batches remain an explicit
            // world-cache isolation limitation until full native ownership.
            if(!isolate_native_world_batches()) {
                draw_batch(&ogs->game_transparency_pass_program,
                           world_transparent_batch,
                           &trimdl->world_transparent_trimdl,
                           /*force opaque*/ false,posterize_lighting,
                           batch_blend_filter::all);
                draw_batch(&ogs->game_transparency_pass_program,
                           world_batch,&trimdl->world_trimdl,
                           /*force opaque*/ false,posterize_lighting,
                           batch_blend_filter::special);
            }

            // SPR 0x40 flare quads use center-only occlusion against the
            // resolved opaque depth texture. Never write or test ordinary
            // per-fragment depth: the sprite's EDGES may cover world geometry.
            if(world_flare_batch.size()!=0U) {
                if(the_config->profile_renderer &&
                   flare_present_trace_count++<6U)
                    LOG_INFO("SPR flare stage=gl_pass triangles=",
                             world_flare_batch.size(),
                             " shader_flare_mode=1");
                gl::disable(gl::capability::depth_test);
                gl::set_depth_mask(false);
                ogs->game_transparency_pass_program.set_flare_mode(true);
                draw_batch(&ogs->game_transparency_pass_program,
                           world_flare_batch,&trimdl->flare_trimdl,
                           /*force opaque*/ false,posterize_lighting,
                           batch_blend_filter::all);
                ogs->game_transparency_pass_program.set_flare_mode(false);
                gl::enable(gl::capability::depth_test);
            }

            // GPU-resident ordinary 3DO translucent faces join the alpha
            // pass after native world geometry. Projected SPRs were submitted
            // earlier so translucent world pixels may correctly cover them.
            if(scene_gpu_timing)scene_gpu_queries.timestamp(1U,false);
            if(scene_gpu_timing)scene_gpu_queries.timestamp(2U,true);
            draw_gpu_3do_preview(true);
            if(scene_gpu_timing)scene_gpu_queries.timestamp(2U,false);
            ogs->game_transparency_pass_program.use_program();
            restore_native_fluid_depth();

            // Weapon/HUD depth is intentionally separate from world lights.
            ogs->game_transparency_pass_program.set_translucent_spotlight_count(0);
            ogs->game_transparency_pass_program.translucent_sun.set_state(
                false,false,0,make_point(0.0f,0.0f,1.0f),
                make_point(0.0f,0.0f,0.0f,0.0f),
                the_config->sun_shadow_near_distance,1.0f,0.0f);
            // Redraw gun overlay after z-clear
            gl::set_depth_mask(true);
            gl::clear({gl::clear_flag::depth});

            draw_batch(&ogs->game_transparency_pass_program,
                       gun_batch,
                       &trimdl->gun_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);
            draw_batch(&ogs->game_transparency_pass_program,
                       gun_transparent_batch,
                       &trimdl->gun_transparent_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);

            // Preserve the original gun transparency behavior for normal
            // materials; special blend materials must NEVER write depth.
            draw_batch(&ogs->game_transparency_pass_program,
                       gun_transparent_batch,
                       &trimdl->gun_transparent_trimdl, /*force opaque*/
                       false,
                       posterize_lighting);

            gl::set_depth_mask(false);
            draw_batch(&ogs->game_transparency_pass_program,
                       gun_transparent_batch,
                       &trimdl->gun_transparent_trimdl,
                       /*force opaque*/ false,
                       posterize_lighting,
                       batch_blend_filter::special);

            // Gun materials whose native JK geometry was opaque but whose
            // replacement requests additive/multiply should also be blended.
            draw_batch(&ogs->game_transparency_pass_program,
                       gun_batch,
                       &trimdl->gun_trimdl,
                       /*force opaque*/ false,
                       posterize_lighting,
                       batch_blend_filter::special);

            // Draw overlay
            gl::disable(gl::capability::depth_test);
            gl::set_depth_mask(false);
            draw_batch(&ogs->game_transparency_pass_program,
                       overlay_batch,
                       &trimdl->overlay_trimdl,
                       /*force opaque*/ false,
                       posterize_lighting,
                       batch_blend_filter::all);

            // HUD and menu draw calls assume the original alpha blend state.
            // Restore it after any special additive/multiply draw.
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::enable(gl::capability::depth_test);
            gl::enable(gl::capability::blend);
            gl::set_depth_mask(true);
        }

        void begin_game() override {}

        void end_game() override {}

        static point<4, float> d3dtl_to_point(size<2, float> const &scr_scale,
                                              direction<2, float> const &screen_offset,
                                              D3DTLVERTEX const &p)
        {
            // Reassign w for full-screen overlay vertices
            float w = 1.0f;
            if(p.rhw != 0.0f) {
                w = 1.0f / p.rhw;
            }

            // Convert pretransformed vertex to phony view space
            return make_point(w * ((p.sx * get<x>(scr_scale)) - 1.0f + get<x>(screen_offset)),
                              w * ((-p.sy * get<y>(scr_scale)) + 1.0f - get<y>(screen_offset)),
                              w * (-p.sz),
                              w);
        }

        void execute_game(IDirect3DExecuteBuffer *cmdbuf, IDirect3DViewport *vp) override
        {
            bool const profiling = the_config->profile_renderer;
            if(profiling) {
                auto const entered = profile_clock::now();
                if(!frame_had_execute) {
                    if(last_present_end != profile_clock::time_point{}) {
                        profile_frame.before_first_execute_us +=
                            elapsed_profile_us(last_present_end, entered);
                    }
                    frame_had_execute = true;
                }
                else {
                    profile_frame.between_executes_us +=
                        elapsed_profile_us(last_execute_end, entered);
                }
                ++profile_frame.execute_calls;
            }
            profile_scope timer(profiling,
                profile_frame.timings[profile_idx(profile_stage::execute)]);
            D3DEXECUTEDATA ed;
            cmdbuf->GetExecuteData(&ed);
            if(profiling) {
                profile_frame.execute_vertices += ed.dwVertexCount;
                profile_frame.execute_instruction_bytes += ed.dwInstructionLength;
            }

            D3DEXECUTEBUFFERDESC ebd;
            cmdbuf->Lock(&ebd);

            D3DVIEWPORT vpd;
            vp->GetViewport(&vpd);

            auto vertex_span =
                make_span((D3DTLVERTEX const *)((char const *)ebd.lpData + ed.dwVertexOffset),
                          ed.dwVertexCount);

            // JK experimental RGB+smooth-normal build: parallel transformed normals.
            // Keep this separate from specular, which carries full-quality vertex RGB tint.
            constexpr uintptr_t normal_magic_address = 0x00900f00U;
            constexpr uintptr_t normal_slots_address = 0x00900f20U;
            constexpr size_t normal_slot_count = 32768U;
            auto native_normals = (uint32_t const *)nullptr;
            if(engine_camera_detail::readable(normal_magic_address, sizeof(uint32_t)) &&
               engine_camera_detail::readable(normal_slots_address,
                                             normal_slot_count * sizeof(uint32_t)) &&
               *reinterpret_cast<uint32_t const *>(normal_magic_address) == 0x314D524EU) {
                native_normals = reinterpret_cast<uint32_t const *>(normal_slots_address);
            }

            auto cmd_span = make_span((char const *)ebd.lpData + ed.dwInstructionOffset,
                                      ed.dwInstructionLength);

            while(!cmd_span.empty()) {
                D3DINSTRUCTION inst = *(D3DINSTRUCTION const *)cmd_span.data();
                cmd_span = cmd_span.subspan(sizeof(D3DINSTRUCTION), span_to_end);

                for(size_t i = 0; i < inst.wCount; ++i) {
                    switch(inst.bOpcode) {
                    case D3DOP_EXIT:
                        break;

                    case D3DOP_PROCESSVERTICES: {
                        auto const *payload = (D3DPROCESSVERTICES const *)cmd_span.data();
                        if(payload->dwFlags != D3DPROCESSVERTICES_COPY || payload->wStart != 0 ||
                           payload->wDest != 0) {
                            report_unimplemented_function(str(fmt("Process vertices opcode: ",
                                                                  payload->dwFlags,
                                                                  " ",
                                                                  payload->dwCount,
                                                                  " ",
                                                                  payload->wStart,
                                                                  " ",
                                                                  payload->wDest)));
                        }
                    } break;

                    case D3DOP_STATERENDER: {
                        auto const *payload = (D3DSTATE const *)cmd_span.data();
                        switch(payload->drstRenderStateType) {
                        case D3DRENDERSTATE_TEXTUREHANDLE:
                            current_material = material_instance_id((size_t)payload->dwArg[0]);
                            break;

                        // Silently ignore some useless commands
                        case D3DRENDERSTATE_ANTIALIAS:
                        case D3DRENDERSTATE_TEXTUREPERSPECTIVE:
                        case D3DRENDERSTATE_FILLMODE:
                        case D3DRENDERSTATE_TEXTUREMAG:
                        case D3DRENDERSTATE_TEXTUREMIN:
                        case D3DRENDERSTATE_SRCBLEND:
                        case D3DRENDERSTATE_WRAPU:
                        case D3DRENDERSTATE_WRAPV:
                        case D3DRENDERSTATE_DESTBLEND:
                        case D3DRENDERSTATE_ALPHAFUNC:
                        case D3DRENDERSTATE_DITHERENABLE:
                        case D3DRENDERSTATE_FOGENABLE:
                        case D3DRENDERSTATE_SUBPIXEL:
                        case D3DRENDERSTATE_SUBPIXELX:
                        case D3DRENDERSTATE_TEXTUREMAPBLEND:
                        case D3DRENDERSTATE_STIPPLEDALPHA:
                        case D3DRENDERSTATE_SHADEMODE:
                        case D3DRENDERSTATE_ZENABLE:
                        case D3DRENDERSTATE_SPECULARENABLE:
                        case D3DRENDERSTATE_ALPHATESTENABLE:
                        case D3DRENDERSTATE_CULLMODE:
                        case D3DRENDERSTATE_ZFUNC:
                        case D3DRENDERSTATE_MONOENABLE:
                            break;

                        case D3DRENDERSTATE_ALPHABLENDENABLE:
                            is_transparent = (payload->dwArg[0] != 0);
                            update_current_batch();
                            break;

                        case D3DRENDERSTATE_ZWRITEENABLE:
                            if(!payload->dwArg[0]) {
                                // ACTUALLY means drawing the weapon overlay.
                                is_gun = true;
                                update_current_batch();
                            }
                            break;

                        default:
                            LOG_WARNING("Ignored unknown state render opcode: ",
                                        (int)payload->dtstTransformStateType);
                            break;
                        }
                    } break;

                    case D3DOP_TRIANGLE: {
                        auto const *payload = (D3DTRIANGLE const *)cmd_span.data();

                        auto const &v1 = vertex_span.data()[payload->v1];
                        auto const &v2 = vertex_span.data()[payload->v2];
                        auto const &v3 = vertex_span.data()[payload->v3];
                        auto native1 = native_normals && payload->v1 < normal_slot_count
                                           ? native_normals[payload->v1] : 0U;
                        auto native2 = native_normals && payload->v2 < normal_slot_count
                                           ? native_normals[payload->v2] : 0U;
                        auto native3 = native_normals && payload->v3 < normal_slot_count
                                           ? native_normals[payload->v3] : 0U;

                        // Preserve the legacy 8-bit premultiply-then-gamma
                        // result using precomputed values instead of powf()
                        // nine times per submitted triangle.
                        auto unpack_diffuse = [](D3DTLVERTEX const &v) {
                            return gamma18_premultiplied_rgba8_lut(
                                (uint8_t)RGBA_GETRED(v.color),
                                (uint8_t)RGBA_GETGREEN(v.color),
                                (uint8_t)RGBA_GETBLUE(v.color),
                                (uint8_t)RGBA_GETALPHA(v.color));
                        };
                        auto c1 = unpack_diffuse(v1);
                        auto c2 = unpack_diffuse(v2);
                        auto c3 = unpack_diffuse(v3);

                        // The patched JK executable stores optional 3DO RGB multipliers in
                        // D3DTLVERTEX.specular. Vanilla output uses specular=0, so treat zero
                        // as neutral white to preserve compatibility with an unpatched engine.
                        auto unpack_vertex_tint = [](DWORD packed) {
                            if(packed == 0U || packed == 0xB6A9D000U) {
                                return color::fill(1.0f);
                            }
                            return to_float_color(color_rgba8((uint8_t)RGBA_GETRED(packed),
                                                              (uint8_t)RGBA_GETGREEN(packed),
                                                              (uint8_t)RGBA_GETBLUE(packed),
                                                              (uint8_t)RGBA_GETALPHA(packed)));
                        };
                        auto tint1 = unpack_vertex_tint(v1.specular);
                        auto tint2 = unpack_vertex_tint(v2.specular);
                        auto tint3 = unpack_vertex_tint(v3.specular);

                        // JK's vertex colors have already been linearized above. Apply the RGB
                        // multipliers in linear space, leaving existing baked/dynamic lighting
                        // intact and preserving the original alpha channel.
                        c1 = extend(get<rgb>(c1) * get<rgb>(tint1), get<a>(c1));
                        c2 = extend(get<rgb>(c2) * get<rgb>(tint2), get<a>(c2));
                        c3 = extend(get<rgb>(c3) * get<rgb>(tint3), get<a>(c3));

                        // An exact native vertex marker means the three
                        // vertices came from an authored SPR with type 0x40.
                        // Route separately; a regular world triangle can
                        // never accidentally inherit flare depth behavior.
                        bool const is_flare=!is_gun &&
                            v1.specular==0xB6A9D000U &&
                            v2.specular==0xB6A9D000U &&
                            v3.specular==0xB6A9D000U;
                        if(is_flare && the_config->profile_renderer &&
                           flare_gpu_trace_count++<6U)
                            LOG_INFO("SPR flare stage=d3d_triangle material=",
                                     current_material.get(),
                                     " is_transparent=",is_transparent,
                                     " specular_marker=",v1.specular);
                        // Check if the current triangle is part of an overlay
                        if(is_gun &&
                           almost_equal(make_point(v1.sx, v1.sy, v1.sz), point<3, float>::zero()) &&
                           almost_equal(v2.sx, get<x>(internal_scr_res_f)) &&
                           (almost_equal(v2.sy, 0.0f) ||
                            almost_equal(v2.sy, get<y>(internal_scr_res_f))) &&
                           almost_equal(v2.sz, 0.0f) &&
                           (almost_equal(v3.sx, 0.0f) ||
                            almost_equal(v3.sx, get<x>(internal_scr_res_f))) &&
                           almost_equal(v3.sy, get<y>(internal_scr_res_f)) &&
                           almost_equal(v3.sz, 0.0f)) {
                            // Check if the current triangle is part of an overlay
                            overlay_batch.insert(
                                triangle(triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v1),
                                                         make_point(v1.tu, v1.tv),
                                                         c1, native1),
                                         triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v2),
                                                         make_point(v2.tu, v2.tv),
                                                         c2, native2),
                                         triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v3),
                                                         make_point(v3.tu, v3.tv),
                                                         c3, native3),
                                         current_material));
                        }
                        else {
                            (is_flare ? static_cast<triangle_batch *>(&world_flare_batch)
                                      : current_triangle_batch)->insert(
                                triangle(triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v1),
                                                         make_point(v1.tu, v1.tv),
                                                         c1, native1),
                                         triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v2),
                                                         make_point(v2.tu, v2.tv),
                                                         c2, native2),
                                         triangle_vertex(d3dtl_to_point(internal_scr_res_scale_f,
                                                                        internal_scr_offset_f,
                                                                        v3),
                                                         make_point(v3.tu, v3.tv),
                                                         c3, native3),
                                         current_material));
                        }
                    } break;

                    default:
                        LOG_WARNING(
                            "Unimplemented execute buffer opcode ", inst.bOpcode, " was ignored");
                    }

                    cmd_span = cmd_span.subspan(inst.bSize, span_to_end);
                }
            }

            cmdbuf->Unlock();
            if(profiling) {
                last_execute_end = profile_clock::now();
            }
        }

        // Destroy the context-owned GL texture-buffer and all CPU sector
        // state when a level changes or the renderer closes.
        void reset_sector_crossing()
        {
            if(hGLRC && wglGetCurrentContext()==hGLRC) {
                if(sector_crossing_plane_texture)
                    glDeleteTextures(1,&sector_crossing_plane_texture);
                if(sector_crossing_plane_buffer)
                    glDeleteBuffers(1,&sector_crossing_plane_buffer);
            }
            sector_crossing_plane_texture=0;
            sector_crossing_plane_buffer=0;
            sector_crossing_world={};
            sector_crossing_world_signature={};
            sector_crossing_ranges.clear();
        }

        // Keep GL_TEXTURE_BUFFER resources tied to the GL context and
        // SithWorld revision. Never issue deletion after context loss.
        void reset_continuous_ambient_gpu()
        {
            if(hGLRC && wglGetCurrentContext()==hGLRC) {
                if(continuous_ambient_key_texture)
                    glDeleteTextures(1,&continuous_ambient_key_texture);
                if(continuous_ambient_value_texture)
                    glDeleteTextures(1,&continuous_ambient_value_texture);
                if(continuous_ambient_key_buffer)
                    glDeleteBuffers(1,&continuous_ambient_key_buffer);
                if(continuous_ambient_value_buffer)
                    glDeleteBuffers(1,&continuous_ambient_value_buffer);
            }
            continuous_ambient_key_texture=0;
            continuous_ambient_value_texture=0;
            continuous_ambient_key_buffer=0;
            continuous_ambient_value_buffer=0;
            continuous_ambient_gpu_cache.reset();
            continuous_ambient_sector_addresses.clear();
            continuous_ambient_live_floors.clear();
            continuous_ambient_gpu_enabled_this_frame=false;
        }

        // Build the CPU field used by the GPU continuous ambient sampler.
        // Build once per world-cache revision and report sample coverage.
        void stage_continuous_ambient_cpu()
        {
            if(!the_config->experimental_continuous_ambient_gpu ||
               !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame ||
               !world_cache_target || !world_cache_target->ready)
                return;
            auto const &cache=*world_cache_target;
            bool const overbright=the_config->enable_baked_overbright;
            bool const rebuild=!continuous_ambient_cpu_cache ||
                continuous_ambient_cpu_world!=cache.loaded_world ||
                continuous_ambient_cpu_revision!=cache.revision ||
                continuous_ambient_cpu_overbright!=overbright;
            if(rebuild) {
                reset_continuous_ambient_gpu();
                continuous_ambient_cpu_world=cache.loaded_world;
                continuous_ambient_cpu_revision=cache.revision;
                continuous_ambient_cpu_overbright=overbright;
                continuous_ambient_cpu_valid=false;
                if(!continuous_ambient_cpu_cache)
                    continuous_ambient_cpu_cache=
                        std::make_unique<continuous_ambient_field>();
                auto const read=[](uintptr_t ptr,void *out,size_t len)->bool {
                    if(!len || !engine_camera_detail::readable(ptr,len))
                        return false;
                    std::memcpy(out,reinterpret_cast<void const *>(ptr),len);
                    return true;
                };
                continuous_ambient_capture_stats capture{};
                auto const started=profile_clock::now();
                bool const built=capture_continuous_ambient(
                    read,cache.loaded_world,cache.world.surfaces_addr,
                    cache.mesh,overbright,1.0f,*continuous_ambient_cpu_cache,
                    capture);
                continuous_ambient_cpu_valid=built &&
                    continuous_ambient_cpu_cache->node_count()>0U;
                auto const counters=continuous_ambient_cpu_cache->stats();
                LOG_INFO("CONTINUOUS AMBIENT CPU snapshot world=",
                    cache.loaded_world,
                    " built=",built?1:0,
                    " valid=",continuous_ambient_cpu_valid?1:0,
                    " surfaces=",capture.surfaces_sampled,
                    " rejected=",capture.surfaces_skipped,
                    " authored_vertices=",capture.vertices_sampled,
                    " raw_nodes=",counters.raw_nodes,
                    " final_nodes=",counters.final_nodes,
                    " fallback_sectors=",counters.fallback_sectors,
                    " passable_portals=",capture.passable_portals,
                    " triangles=",counters.portal_triangles,
                    " stitched_locations=",counters.stitched_locations,
                    " shared_nodes=",counters.shared_nodes,
                    " oversized_portals=",counters.skipped_oversized_portals,
                    " cpu_us=",elapsed_profile_us(
                        started,profile_clock::now()));
            }
            // Sample the actual owning-Thing origins at a low diagnostic
            // frequency, never modifying the rendered ambient. Zero live
            // floors are enough to determine whether static voxel coverage
            // exists; a separate GPU stage will handle live ExtraLight.
            if(!continuous_ambient_cpu_valid ||
               !the_config->profile_renderer ||
               gpu_3do_present_serial%120U!=0U)return;
            auto const zero_floor=[](uint32_t,float &floor)->bool {
                floor=0.0f;return true;
            };
            uint32_t eligible=0,sampled=0,missing=0;
            float min_light=2.0f,max_light=0.0f;
            for(auto const &inst:frame_gpu_3do_instances) {
                if(!inst.geometry || inst.shadow_only || inst.camera_hud ||
                   inst.skybox_vertex_lighting || inst.world_lighting_exempt ||
                   inst.is_projected_decal || inst.is_sprite_quad ||
                   !inst.owning_sector_valid ||
                   !inst.owning_thing_world_position_valid)
                    continue;
                ++eligible;
                float brightness=0.0f;
                if(continuous_ambient_cpu_cache->sample(
                       inst.owning_sector_id,inst.owning_thing_world_position,
                       zero_floor,brightness)) {
                    ++sampled;
                    min_light=std::min(min_light,brightness);
                    max_light=std::max(max_light,brightness);
                } else ++missing;
            }
            LOG_INFO("CONTINUOUS AMBIENT CPU coverage eligible=",eligible,
                " sampled=",sampled," missing=",missing,
                " min=",sampled?min_light:0.0f,
                " max=",sampled?max_light:0.0f);
        }

        // Stage B1: independent, opt-in GL 3.3 continuous voxel
        // illumination. All static keys upload ONCE per world revision;
        // only the small shared-live values buffer changes on sector fades.
        // A failed read/upload/capability check preserves native lighting.
        void stage_continuous_ambient_gpu()
        {
            continuous_ambient_gpu_enabled_this_frame=false;
            if(!the_config->experimental_continuous_ambient_gpu ||
               the_config->enable_sector_crossing_lighting ||
               !continuous_ambient_cpu_valid ||
               !continuous_ambient_cpu_cache ||
               !world_cache_target || !world_cache_target->ready ||
               mode!=renderer_mode::ingame ||
               frame_gpu_3do_instances.empty())return;
            static GLint fragment_units=0,vertex_units=0,combined_units=0;
            if(fragment_units==0) {
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
                glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&vertex_units);
                glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&combined_units);
            }
            if(fragment_units<=texture_units::forward_continuous_value_buffer ||
               vertex_units<2 || combined_units<=
                    texture_units::forward_continuous_value_buffer)return;
            auto const read=[](uintptr_t p,void *out,size_t count)->bool {
                if(!count || !engine_camera_detail::readable(p,count))
                    return false;
                std::memcpy(out,reinterpret_cast<void const *>(p),count);
                return true;
            };
            if(!continuous_ambient_gpu_cache) {
                auto table=std::make_unique<continuous_ambient_gpu_table>();
                if(!table->build(*continuous_ambient_cpu_cache))return;
                GLint max_texels=0;
                glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&max_texels);
                if(max_texels<1 ||
                   table->capacity()>static_cast<uint32_t>(max_texels))
                    return;
                uint32_t count=0,base=0;
                uintptr_t world=world_cache_target->loaded_world;
                if(!read(world+0x4cU,&count,4U) ||
                   !read(world+0x50U,&base,4U) ||
                   !count || count>65536U ||
                   !decal_native_address_ok(base))return;
                std::unordered_map<uint32_t,uintptr_t> sectors;
                sectors.reserve(count);
                for(uint32_t i=0;i<count;++i) {
                    uintptr_t const ptr=uintptr_t(base)+uintptr_t(i)*0x98U;
                    uint32_t id=0;
                    if(!read(ptr,&id,4U))return;
                    sectors[id]=ptr;
                }
                GLuint key_buffer=0,value_buffer=0,key_tex=0,value_tex=0;
                glGenBuffers(1,&key_buffer);
                glGenBuffers(1,&value_buffer);
                glGenTextures(1,&key_tex);
                glGenTextures(1,&value_tex);
                if(!key_buffer || !value_buffer || !key_tex || !value_tex) {
                    if(key_tex)glDeleteTextures(1,&key_tex);
                    if(value_tex)glDeleteTextures(1,&value_tex);
                    if(key_buffer)glDeleteBuffers(1,&key_buffer);
                    if(value_buffer)glDeleteBuffers(1,&value_buffer);
                    return;
                }
                glBindBuffer(GL_TEXTURE_BUFFER,key_buffer);
                glBufferData(GL_TEXTURE_BUFFER,
                    static_cast<GLsizeiptr>(table->key_bytes()),
                    table->keys().data(),GL_STATIC_DRAW);
                glActiveTexture(GL_TEXTURE0+
                    texture_units::forward_continuous_key_buffer);
                glBindTexture(GL_TEXTURE_BUFFER,key_tex);
                glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32UI,key_buffer);
                glBindBuffer(GL_TEXTURE_BUFFER,value_buffer);
                glBufferData(GL_TEXTURE_BUFFER,
                    static_cast<GLsizeiptr>(table->value_bytes()),
                    table->values().data(),GL_DYNAMIC_DRAW);
                glActiveTexture(GL_TEXTURE0+
                    texture_units::forward_continuous_value_buffer);
                glBindTexture(GL_TEXTURE_BUFFER,value_tex);
                glTexBuffer(GL_TEXTURE_BUFFER,GL_RG32F,value_buffer);
                glBindBuffer(GL_TEXTURE_BUFFER,0);
                glActiveTexture(GL_TEXTURE0);
                if(glGetError()!=GL_NO_ERROR) {
                    glDeleteTextures(1,&key_tex);
                    glDeleteTextures(1,&value_tex);
                    glDeleteBuffers(1,&key_buffer);
                    glDeleteBuffers(1,&value_buffer);
                    return;
                }
                continuous_ambient_key_buffer=key_buffer;
                continuous_ambient_value_buffer=value_buffer;
                continuous_ambient_key_texture=key_tex;
                continuous_ambient_value_texture=value_tex;
                continuous_ambient_sector_addresses=std::move(sectors);
                continuous_ambient_gpu_cache=std::move(table);
                LOG_INFO("CONTINUOUS AMBIENT GPU upload nodes=",
                    continuous_ambient_gpu_cache->node_count(),
                    " slots=",continuous_ambient_gpu_cache->capacity(),
                    " max_probes=",continuous_ambient_gpu_cache->max_probes(),
                    " static_bytes=",continuous_ambient_gpu_cache->key_bytes(),
                    " value_bytes=",continuous_ambient_gpu_cache->value_bytes());
            }
            float global_extra=0.0f;
            if(!read(0x008D4098U,&global_extra,sizeof(global_extra)) ||
               !std::isfinite(global_extra) || std::fabs(global_extra)>8.0f)
                global_extra=0.0f;
            std::unordered_map<uint32_t,float> floors;
            floors.reserve(continuous_ambient_sector_addresses.size());
            for(auto const &entry:continuous_ambient_sector_addresses) {
                float extra=0.0f;
                if(!read(entry.second+0x08U,&extra,4U) ||
                   !std::isfinite(extra))return;
                floors.emplace(entry.first,clamp_authored_extra_floor(
                    extra,global_extra,the_config->enable_baked_overbright));
            }
            // Update before publishing live floors. A failed sector read
            // cannot partially modify the texture or the previous snapshot.
            if(floors!=continuous_ambient_live_floors) {
                auto const lookup=[&](uint32_t id,float &v) {
                    auto it=floors.find(id);
                    if(it==floors.end())return false;
                    v=it->second;return true;
                };
                if(!continuous_ambient_gpu_cache->update_shared_floors(
                    lookup,continuous_ambient_cpu_cache->range()))return;
                glBindBuffer(GL_TEXTURE_BUFFER,continuous_ambient_value_buffer);
                glBufferSubData(GL_TEXTURE_BUFFER,0,
                    static_cast<GLsizeiptr>(
                        continuous_ambient_gpu_cache->value_bytes()),
                    continuous_ambient_gpu_cache->values().data());
                glBindBuffer(GL_TEXTURE_BUFFER,0);
                if(glGetError()!=GL_NO_ERROR)return;
                continuous_ambient_live_floors=std::move(floors);
            }
            uint32_t applied=0;
            for(auto &inst:frame_gpu_3do_instances) {
                if(!inst.geometry || inst.shadow_only || inst.camera_hud ||
                   inst.skybox_vertex_lighting || inst.world_lighting_exempt ||
                   inst.is_projected_decal || inst.is_sprite_quad ||
                   !inst.owning_sector_valid)continue;
                continuous_ambient_field::point lo{},hi{};
                std::array<uint8_t,8> corners{};
                if(!continuous_ambient_cpu_cache->get_fallback(
                       inst.owning_sector_id,lo,hi,corners))continue;
                auto floor=continuous_ambient_live_floors.find(
                    inst.owning_sector_id);
                if(floor==continuous_ambient_live_floors.end())continue;
                gpu_3do_ambient_volume v{};
                v.bounds_min=lo;v.bounds_max=hi;v.samples=corners;
                v.range=continuous_ambient_cpu_cache->range();
                v.live_floor=floor->second;
                inst.continuous_ambient_fallback=v;
                ++applied;
            }
            continuous_ambient_gpu_enabled_this_frame=applied>0U;
            if(the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("CONTINUOUS AMBIENT GPU instances=",applied,
                         " slots=",continuous_ambient_gpu_cache->capacity());
        }

        void stage_sector_crossing()
        {
            if(!the_config->enable_sector_crossing_lighting ||
               !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame ||
               frame_gpu_3do_instances.empty())return;
            // Spotlight cookie sampler2D units 12..15 and sun shadow
            // sampler2DShadow units 16..18 must never alias the plane
            // samplerBuffer. On GL 3.3 devices with only 16 fragment
            // units, keep vanilla per-Thing sector lighting instead.
            static GLint fragment_units=0;
            if(fragment_units==0)
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
            if(fragment_units<=gpu_3do_sector_plane_texture_unit) {
                // Level changes still invalidate the cached sector data.
                if(sector_crossing_plane_texture)reset_sector_crossing();
                return;
            }
            uint32_t world=0;
            if(!engine_material_debug::read_object(
                    engine_material_debug::current_world_global,world) ||
               !decal_native_address_ok(world)) {
                reset_sector_crossing();return;
            }
            auto const read=[](uintptr_t ptr,void *out,size_t len)->bool {
                if(!len || !engine_camera_detail::readable(ptr,len))
                    return false;
                std::memcpy(out,reinterpret_cast<void const *>(ptr),len);
                return true;
            };
            // A new level can recycle the same SithWorld pointer. Compare
            // all six static sector/vertex/surface table identity fields.
            constexpr std::array<uint32_t,6> offsets{{
                0x4cU,0x50U,0x88U,0x8cU,0xa8U,0xacU
            }};
            std::array<uint32_t,6> signature{};
            for(size_t i=0;i<offsets.size();++i) {
                if(!read(uintptr_t(world)+offsets[i],&signature[i],4U)) {
                    reset_sector_crossing();return;
                }
            }
            if(sector_crossing_world.world!=world ||
               sector_crossing_world_signature!=signature ||
               !sector_crossing_plane_texture) {
                reset_sector_crossing();
                sector_crossing_snapshot snapshot{};
                if(!sector_crossing_capture(read,world,snapshot))return;
                std::vector<GLfloat> planes;
                std::vector<sector_crossing_plane_range> ranges(
                    snapshot.sectors.size());
                for(size_t i=0;i<snapshot.sectors.size();++i) {
                    auto const &region=snapshot.sectors[i];
                    if(!region.valid || !region.adjacency_valid ||
                       region.planes.empty() ||
                       region.planes.size()>sector_crossing_gpu_max_planes)
                        continue;
                    if(planes.size()/4U>
                       size_t(std::numeric_limits<uint32_t>::max())-
                           region.planes.size())return;
                    auto &range=ranges[i];
                    range.first=static_cast<uint32_t>(planes.size()/4U);
                    range.count=static_cast<uint32_t>(region.planes.size());
                    for(auto const &p:region.planes) {
                        planes.push_back(p.normal[0]);
                        planes.push_back(p.normal[1]);
                        planes.push_back(p.normal[2]);
                        planes.push_back(p.offset);
                    }
                }
                if(planes.empty())return;
                GLint max_texels=0;
                glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&max_texels);
                if(max_texels<1 ||
                   planes.size()/4U>static_cast<size_t>(max_texels))
                    return;
                GLuint buffer=0,texture=0;
                glGenBuffers(1,&buffer);
                glBindBuffer(GL_TEXTURE_BUFFER,buffer);
                glBufferData(GL_TEXTURE_BUFFER,
                    static_cast<GLsizeiptr>(planes.size()*sizeof(GLfloat)),
                    planes.data(),GL_STATIC_DRAW);
                glGenTextures(1,&texture);
                glActiveTexture(GL_TEXTURE0+
                    gpu_3do_sector_plane_texture_unit);
                glBindTexture(GL_TEXTURE_BUFFER,texture);
                glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,buffer);
                glBindTexture(GL_TEXTURE_BUFFER,0);
                glActiveTexture(GL_TEXTURE0);
                glBindBuffer(GL_TEXTURE_BUFFER,0);
                sector_crossing_plane_buffer=buffer;
                sector_crossing_plane_texture=texture;
                sector_crossing_world=std::move(snapshot);
                sector_crossing_ranges=std::move(ranges);
                sector_crossing_world_signature=signature;
                if(the_config->profile_renderer)
                    LOG_INFO("SECTOR CROSSING snapshot world=",world,
                        " sectors=",sector_crossing_world.sectors.size(),
                        " valid=",sector_crossing_world.valid_count,
                        " uploaded_planes=",planes.size()/4U);
            }
            if(!sector_crossing_plane_texture)return;
            // Share the JK identity-CMP rule with world caching;
            // snapshot once per frame instead of per crossing Thing.
            uint32_t identity_cmp=0;
            if(!read(0x0073A414U,&identity_cmp,sizeof(identity_cmp)) ||
               !decal_native_address_ok(identity_cmp))
                identity_cmp=0;
            // Counts are per-120-frame windows. Profiling computes the
            // conservative owner proof even when the culling flag is OFF,
            // so the expected potential is visible without altering lighting.
            static thread_local sector_crossing_stage_stats profile_crossing{};
            static thread_local uint64_t profile_crossing_us=0U;
            static thread_local uint32_t profile_crossing_frames=0U;
            bool const profile=the_config->profile_renderer;
            auto const crossing_start=profile
                ?profile_clock::now():profile_clock::time_point{};
            uint32_t crossing=0;
            for(auto &instance:frame_gpu_3do_instances) {
                instance.crossing_lighting=sector_crossing_stage_instance(
                    sector_crossing_world,sector_crossing_ranges,
                    instance,read,true,identity_cmp,
                    the_config->enable_baked_overbright,
                    the_config->experimental_sector_crossing_owner_cull,
                    profile?&profile_crossing:nullptr);
                if(instance.crossing_lighting)++crossing;
            }
            if(profile) {
                profile_crossing_us+=elapsed_profile_us(
                    crossing_start,profile_clock::now());
                ++profile_crossing_frames;
            }
            if(profile && gpu_3do_present_serial%120U==0U) {
                LOG_INFO("SECTOR CROSSING model_instances=",
                    frame_gpu_3do_instances.size(),
                    " crossing=",crossing,
                    " budget=",sector_crossing_candidates::capacity,
                    " owner_fast_path=",
                    the_config->experimental_sector_crossing_owner_cull?1:0);
                LOG_INFO("SECTOR CROSSING owner-proof window_frames=",
                    profile_crossing_frames,
                    " considered=",profile_crossing.considered,
                    " eligible=",profile_crossing.eligible,
                    " valid_spheres=",profile_crossing.sphere_valid,
                    " proved_interior=",profile_crossing.proof_interior,
                    " proved_sphere=",profile_crossing.proof_sphere,
                    " proved_oriented_box=",
                    profile_crossing.proof_oriented_box,
                    " early_owner_exits=",profile_crossing.owner_culled,
                    " proved_multisector_baseline=",
                    profile_crossing.proof_multisector_baseline,
                    " multisector_candidates=",
                    profile_crossing.multi_candidates,
                    " staged=",profile_crossing.staged,
                    " stage_cpu_us_per_frame=",
                    profile_crossing_us/
                        std::max<uint32_t>(1U,profile_crossing_frames),
                    " NOTE=CPU_only; profile overhead includes owner checks");
                profile_crossing={};
                profile_crossing_us=0U;
                profile_crossing_frames=0U;
            }
        }

        void merge_thing_light_colors()
        {
            uint32_t world=0U;
            if(!engine_material_debug::read_object(
                   engine_material_debug::current_world_global,world) ||
               world<0x10000U)return;
            auto &registry=thing_light_rgb::get_registry();
            registry.prepare_world(world);
            auto const now=std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto const entries=registry.things(now);
            if(entries.empty()) {
                if(the_config && the_config->profile_renderer_verbose &&
                   gpu_3do_present_serial%120U==0U) {
                    auto const retained=registry.static_template_count();
                    if(retained>0U)
                        LOG_INFO("GPU THING LIGHT RGB: static_templates=",
                                 retained," runtime_colored_things=0");
                }
                return;
            }

            auto const read=[](uintptr_t ptr,void *dst,size_t bytes) {
                if(ptr<0x10000U || !bytes ||
                   !engine_camera_detail::readable(ptr,bytes))return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),bytes);
                return true;
            };
            uint32_t camera=0U;
            int32_t count=0;
            if(!read(0x0073A418U,&camera,4U) ||
               !read(uintptr_t(camera)+0x58U,&count,4U) ||
               count<0 || count>64)return;
            std::array<saber_light_rgb::camera_light,64> lights{};
            size_t found=0;
            for(int i=0;i<count;++i) {
                uint32_t ptr=0U,id=0U,enabled=0U;
                float intensity=0.f;
                if(!read(uintptr_t(camera)+0x5cU+size_t(i)*4U,&ptr,4U) ||
                   !read(ptr,&id,4U) || id>=64U ||
                   !read(uintptr_t(ptr)+8U,&enabled,4U) ||
                   !read(uintptr_t(ptr)+0x18U,&intensity,4U))
                    return;
                if(!enabled || !std::isfinite(intensity) ||
                   intensity<=0.f)continue;
                auto &light=lights[found++];
                light.id=id;light.intensity=intensity;
                if(!read(uintptr_t(camera)+0x15cU+size_t(id)*12U,
                         light.position.data(),12U))return;
            }
            // Intermittent white lights can come from several distinct
            // phases: a recycled/inactive Thing, a light not collected by
            // JK's native camera, coincident lights (unsafe to disambiguate),
            // or a valid saber SideMat override. Preserve the strict matching
            // policy, but expose the failure category in verbose profiling.
            size_t matched=0,stale=0,inactive=0,missing=0,ambiguous=0,
                   saber_override=0;
            bool const trace=the_config && the_config->profile_renderer_verbose &&
                gpu_3do_present_serial%120U==0U;
            unsigned sample_count=0U;
            for(auto const &entry:entries) {
                uint32_t flags=0U,signature=0U,kind=0U;
                std::array<float,3> position{};
                float intensity=0.f;
                if(!read(entry.address,&flags,4U) ||
                   !read(entry.address+0x0cU,&kind,4U) ||
                   !read(entry.address+0x74U,&signature,4U) ||
                   !read(entry.address+0xb8U,position.data(),12U) ||
                   !read(entry.address+0x118U,&intensity,4U) ||
                   signature!=entry.signature || kind==0U || kind>11U) {
                    ++stale;
                    continue;
                }
                if((flags&1U)==0U ||
                   (flags&(0x2U|0x10U|0x80000U))!=0U ||
                   !std::isfinite(intensity) || intensity<=0.f) {
                    ++inactive;
                    continue;
                }
                uint32_t id=0U;
                if(!saber_light_rgb::find_unique_native_light(
                       lights.data(),found,position,intensity,id)) {
                    // Count candidates using the same strict tolerances as
                    // find_unique_native_light. Do not color a potentially
                    // unrelated Thing when the match is ambiguous.
                    size_t candidates=0;
                    if(trace) {
                        for(size_t i=0;i<found;++i) {
                            auto const &l=lights[i];
                            float const tolerance=
                                std::max(0.0001f,0.001f*intensity);
                            if(std::fabs(l.intensity-intensity)>tolerance)
                                continue;
                            double d2=0.0;
                            for(unsigned k=0;k<3U;++k) {
                                double const d=double(l.position[k])-
                                               double(position[k]);
                                d2+=d*d;
                            }
                            if(d2<=1.e-6)++candidates;
                        }
                    }
                    if(candidates>1U)++ambiguous;
                    else ++missing;
                    if(trace && sample_count++<4U)
                        LOG_INFO("GPU THING LIGHT RGB unmatched: owner=",
                                 entry.address," signature=",signature,
                                 " pos=",position[0],"/",position[1],"/",
                                         position[2],
                                 " intensity=",intensity,
                                 " camera_count=",count,
                                 " exact_candidates=",candidates,
                                 " rgb=",entry.to[0],"/",entry.to[1],"/",
                                         entry.to[2]);
                    continue;
                }
                if(frame_saber_light_colors.valid[id]) {
                    ++saber_override;
                    continue; // saber SideMat hue takes precedence
                }
                frame_saber_light_colors.colors[id]=entry.to;
                frame_saber_light_colors.valid[id]=true;
                ++matched;
            }
            if(trace)
                LOG_INFO("GPU THING LIGHT RGB: matched=",matched,
                         " registered=",entries.size(),
                         " native_camera_lights=",found,
                         " stale=",stale," inactive=",inactive,
                         " missing=",missing," ambiguous=",ambiguous,
                         " saber_override=",saber_override);
        }

        // Choose a real reflected plane. An explicit planar MAT always wins
        // over a Fluid-only diagnostic fallback; the nearest candidate of
        // that tier is used for the one-target first implementation.
        void select_planar_capture_plane(engine_camera const &camera)
        {
            if(!world_cache_target || !world_cache_target->ready) {
                planar_capture_plane.reset();
                return;
            }
            auto const &cache=*world_cache_target;
            bool const changed=planar_plane_world!=cache.loaded_world ||
                planar_plane_world_revision!=cache.revision;
            if(!changed && planar_plane_scanned_frame!=~uint64_t(0) &&
               gpu_3do_present_serial-planar_plane_scanned_frame<120U)
                return;
            planar_plane_scanned_frame=gpu_3do_present_serial;
            planar_plane_world=cache.loaded_world;
            planar_plane_world_revision=cache.revision;
            planar_capture_plane.reset();
            planar_plane_from_material=false;
            float best_distance=std::numeric_limits<float>::infinity();
            int best_priority=-1;
            size_t matched_planar=0,matched_fluid=0,matched_passable=0;
            std::map<std::pair<uintptr_t,int32_t>,gpu_3do_texture> mats;
            for(auto const &surface:cache.mesh.surfaces) {
                // Reflection RECEIVERS may be passable adjoins (notably water).
                // Exclude sky apertures, but never reject a valid Fluid/planar
                // surface merely because its adjoin allows traversal.
                // The mirrored SCENE render still skips passable portals.
                if(surface.count<3U ||
                   (surface.surface_flags&0x600U)!=0U ||
                   surface.first_index>=cache.mesh.indices.size() ||
                   surface.material==0U)
                    continue;
                auto const first=cache.mesh.indices[surface.first_index];
                if(first>=cache.mesh.vertices.size())continue;
                auto const &v=cache.mesh.vertices[first];
                auto const normal=v.normal;
                float const len=std::sqrt(normal[0]*normal[0]+
                    normal[1]*normal[1]+normal[2]*normal[2]);
                if(!(std::isfinite(len) && len>1.e-5f))continue;
                // A MAT is often shared by hundreds of water polygons.
                // Material identity is independent of the individual plane.
                auto const mat_key=std::make_pair(surface.material,surface.cel);
                auto it=mats.find(mat_key);
                if(it==mats.end()) {
                    gpu_3do_group group{};
                    group.material=surface.material;
                    group.cel=surface.cel;
                    it=mats.emplace(mat_key,
                        resolve_gpu_3do_material(group)).first;
                }
                auto const &mat=it->second;
                int const priority=mat.reflection_mode==4 ? 2 :
                    mat.blend==material_blend_mode::fluid ? 1 : 0;
                if(priority==0)continue;
                if(priority==2)++matched_planar;
                else ++matched_fluid;
                if(surface.passable_adjoin)++matched_passable;
                planar_reflection_plane plane{};
                plane.normal=normal;
                plane.offset=normal[0]*v.position[0]+
                    normal[1]*v.position[1]+normal[2]*v.position[2];
                if(!normalize_planar_reflection_plane(plane,plane))continue;
                float const distance=std::fabs(
                    engine_camera_detail::dot(camera.position,plane.normal)-
                    plane.offset);
                if(priority<best_priority ||
                   (priority==best_priority && distance>=best_distance))
                    continue;
                best_priority=priority;
                best_distance=distance;
                planar_capture_plane=plane;
                planar_plane_from_material=priority==2;
            }
            if(the_config->profile_renderer || the_config->planar_reflection_debug_view)
                LOG_INFO("PLANAR plane select: world=",cache.loaded_world,
                         " source=",planar_capture_plane
                              ?(planar_plane_from_material?"material":"fluid")
                              :"none",
                         " scale=",the_config->planar_reflection_resolution_scale,
                         " candidate_surfaces=",cache.mesh.surfaces.size(),
                         " planar_matches=",matched_planar,
                         " fluid_matches=",matched_fluid,
                         " passable_reflection_receivers=",matched_passable);
        }

        void render_planar_capture()
        {
            planar_capture_ready=false;
            if(!the_config->experimental_planar_reflection_capture ||
               !the_config->use_gpu_cache ||
               !world_cache_target || !world_cache_target->ready ||
               !ogs || !frame_world_cache_camera.valid ||
               !frame_world_cache_camera.perspective)
                return;
            auto const &camera=frame_world_cache_camera.camera;
            select_planar_capture_plane(camera);
            if(!planar_capture_plane)return;
            auto const reflected=make_planar_reflection_camera(
                camera,*planar_capture_plane);
            auto const setup=make_planar_reflection_render_setup(
                get<x>(internal_scr_res),get<y>(internal_scr_res),
                the_config->planar_reflection_resolution_scale,
                camera,reflected,*planar_capture_plane);
            if(!setup.valid)return;
            float const aspect=float(setup.width)/float(setup.height);
            float const half_fov=std::tan(
                camera.fov*0.008726646259971648f);
            float const far_plane=frame_world_cache_camera.far_plane;
            if(!std::isfinite(half_fov) || half_fov<=0.0f ||
               !std::isfinite(far_plane) || far_plane<=0.1f)return;
            if(!planar_capture_target)
                planar_capture_target=
                    std::make_unique<planar_reflection_capture_target>();
            if(!planar_capture_target->ensure(setup.width,setup.height)) {
                static bool warned=false;
                if(!warned) {
                    warned=true;
                    LOG_WARNING("PLANAR capture framebuffer unavailable at ",
                        setup.width,"x",setup.height);
                }
                return;
            }
            if(!planar_capture_program)
                planar_capture_program=
                    std::make_unique<planar_reflection_capture_shader>(
                        the_config->data_path);
            auto &shader=*planar_capture_program;
            auto const view=planar_camera_native_view(reflected);
            auto const begun=std::chrono::steady_clock::now();
            size_t world_drawn=0,model_drawn=0,model_candidates=0;
            // Acquisition does NOT mutate rdCamera; its refresh callback
            // updates only each Thing's native cached animation matrices.
            planar_reflection_offscreen_gpu offscreen{};
            if(engine_material_debug::matches_tested_executable()) {
                uint32_t world=0;
                if(engine_material_debug::read_object(
                       engine_material_debug::current_world_global,world)) {
                    auto read=[](uintptr_t at,void *dst,size_t bytes) {
                        return engine_material_debug::read_bytes(at,dst,bytes);
                    };
                    planar_reflection_thing_scan scan{};
                    if(collect_planar_reflection_things(read,world,scan)) {
                        auto &things=scan.models;
                        // Favor nearby Things, but never use the primary
                        // camera's frustum/portal subset as eligibility.
                        std::sort(things.begin(),things.end(),
                            [&](auto const &a,auto const &b) {
                                auto dist2=[&](auto const &p) {
                                    float result=0;
                                    for(unsigned axis=0;axis<3;++axis) {
                                        float const d=p.position[axis]-
                                                      reflected.position[axis];
                                        result+=d*d;
                                    }
                                    return result;
                                };
                                return dist2(a)<dist2(b);
                            });
                        model_candidates=things.size();
                        constexpr size_t max_native_things=128U;
                        offscreen=acquire_planar_reflection_offscreen_gpu(
                            things,max_native_things);
                    }
                }
            }
            // Prefer complete offscreen native hierarchy snapshots; add only
            // primary-camera nodes of Things that were not independently
            // acquired. Each Thing with a complete hierarchy is drawn once.
            std::unordered_set<uint64_t> acquired_owners;
            for(auto const &inst:offscreen.nodes)
                if(inst.shadow_owner)acquired_owners.insert(inst.shadow_owner);
            planar_reflection_gl_scope restore;
            glBindFramebuffer(GL_FRAMEBUFFER,planar_capture_target->framebuffer());
            glViewport(0,0,setup.width,setup.height);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(0.055f,0.075f,0.105f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            shader.use_program();
            shader.set_projection(half_fov,aspect,far_plane);
            shader.set_clip(setup.clip_view);
            shader.set_modelview(view,false);
            auto submit_material=[&](gpu_3do_texture const &mat,
                                      bool solid)->bool {
                if(mat.reflection_mode==4 ||
                   mat.special || mat.emissive_only ||
                   mat.blend==material_blend_mode::fluid ||
                   mat.blend==material_blend_mode::additive ||
                   mat.blend==material_blend_mode::multiply ||
                   !std::isfinite(mat.opacity) || mat.opacity<0.999f)
                    return false;
                bool const textured=!solid && mat.albedo_source==1 &&
                                     mat.has_image;
                if(!textured && !(solid || mat.albedo_source==2))return false;
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 textured?mat.texture:gl::default_texture);
                shader.set_material(mat.width,mat.height,mat.tint,
                    mat.albedo_scroll,textured,
                    (mat.masked || mat.dithered || mat.alpha)
                        ?mat.alpha_cutoff:0.0f);
                return true;
            };
            auto &cache=*world_cache_target;
            // These ranges include all cached world sectors. Unlike
            // frame_render_candidates, they are NOT main-portal-culled.
            gl::bind_vertex_array(cache.vao);
            glFrontFace(GL_CW); // reflected camera reverses handedness
            for(auto const &surface:cache.mesh.surfaces) {
                if(surface.count==0U || surface.passable_adjoin ||
                   (surface.surface_flags&0x600U)!=0U ||
                   surface.first_index>=cache.mesh.indices.size())
                    continue;
                gpu_3do_group group{};
                group.material=surface.material;
                group.cel=surface.cel;
                bool const solid=!surface.has_uv;
                auto const mat=solid
                    ?resolve_gpu_3do_solid_material(group,false)
                    :resolve_gpu_3do_material(group);
                if(!submit_material(mat,solid))continue;
                gl::draw_elements(gl::element_type::triangles,
                    static_cast<GLsizei>(surface.count),
                    gl::index_type::uint32,
                    static_cast<ptrdiff_t>(
                        surface.first_index*sizeof(uint32_t)));
                ++world_drawn;
            }
            auto draw_model=[&](gpu_3do_instance const &inst) {
                if(inst.shadow_only || inst.camera_hud ||
                   inst.is_projected_decal || inst.is_sprite_quad ||
                   !inst.geometry || inst.geometry->groups.empty() ||
                   !gpu_3do_matrix_finite(inst.transform))return;
                auto glmesh=gpu_3do_gl_cache.find(inst.id);
                if(glmesh==gpu_3do_gl_cache.end()) {
                    if(gpu_3do_gl_cache.size()>8192U)return;
                    glmesh=gpu_3do_gl_cache.emplace(inst.id,
                        std::make_unique<gpu_3do_gl_mesh>(
                            *inst.geometry)).first;
                }
                auto const modelview=gpu_3do_native_modelview(
                    view,inst.transform);
                if(!gpu_3do_matrix_finite(modelview))return;
                shader.set_modelview(modelview,true);
                gl::bind_vertex_array(glmesh->second->vao);
                // Both the reflected camera and negative-scale models
                // reverse winding. Two reversals restore the normal front.
                glFrontFace(gpu_3do_reversed_winding(inst.transform)
                    ?GL_CCW:GL_CW);
                for(auto const &group:inst.geometry->groups) {
                    bool const solid=group.solid_color ||
                        inst.effective_geometry_mode==3U;
                    auto const mat=solid
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group);
                    if(!submit_material(mat,solid))continue;
                    if(group.nonplanar || (group.face_type&1U)) {
                        glDisable(GL_CULL_FACE);
                    } else glEnable(GL_CULL_FACE);
                    gl::draw_elements(gl::element_type::triangles,
                        static_cast<GLsizei>(group.count),
                        gl::index_type::uint32,
                        static_cast<ptrdiff_t>(
                            group.offset*sizeof(uint32_t)));
                    ++model_drawn;
                }
            };
            for(auto const &inst:offscreen.nodes)draw_model(inst);
            for(auto const &inst:frame_gpu_3do_instances)
                if(inst.shadow_owner==0U ||
                   !acquired_owners.count(inst.shadow_owner))
                    draw_model(inst);
            gl::set_active_texture_unit(0);
            planar_capture_ready=true;
            if((gpu_3do_present_serial%120U)==0U) {
                auto const us=std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        std::chrono::steady_clock::now()-begun).count();
                LOG_INFO("PLANAR capture: resolution=",setup.width,"x",
                         setup.height," scale=",
                         the_config->planar_reflection_resolution_scale,
                         " plane_source=",
                         planar_plane_from_material?"material":"fluid",
                         " world_faces=",world_drawn,
                         " model_groups=",model_drawn,
                         " native_thing_candidates=",model_candidates,
                         " acquired_3do_nodes=",offscreen.stats.ready_nodes,
                         " incomplete_3do_things=",offscreen.stats.incomplete_things,
                         " ms_cpu=",float(us)/1000.0f,
                         " NOTE=debug_capture_only_no_material_sampling");
            }
        }

        void present_game() override
        {
            poll_audio_modules();
            bool const profiling = the_config->profile_renderer;
            auto const started = profile_clock::now();
            if(profiling) {
                if(frame_had_execute) {
                    profile_frame.after_last_execute_us +=
                        elapsed_profile_us(last_execute_end, started);
                    ++profile_frame.frames_with_execute;
                }
                if(last_profile_frame != profile_clock::time_point{}) {
                    profile_frame.period_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            started - last_profile_frame).count());
                }
                last_profile_frame = started;
                profile_frame.tris = world_batch.size() + world_transparent_batch.size() +
                                      world_flare_batch.size() +
                                     gun_batch.size() + gun_transparent_batch.size() +
                                     overlay_batch.size();
                profile_frame.trans_tris =
                    world_transparent_batch.size() + world_flare_batch.size() +
                    gun_transparent_batch.size();
            }

            // Consume once, then render both opaque and translucent subsets.
            // Keeping one frame snapshot avoids losing the second pass.
            gpu_frame_material_cache.clear();
            frame_gpu_3do_instances=consume_gpu_3do_instances();
            // Discovery-only probe: walk ALL sectors rather than the primary
            // camera's portal-visible subset. No pose updates, GL draws, or
            // changes to native JK state occur here.
            if(the_config->experimental_planar_thing_discovery &&
               mode==renderer_mode::ingame &&
               gpu_3do_present_serial%120U==0U &&
               engine_material_debug::matches_tested_executable()) {
                uint32_t world=0;
                if(engine_material_debug::read_object(
                       engine_material_debug::current_world_global,world)) {
                    auto read=[](uintptr_t at,void *dst,size_t bytes)->bool {
                        return engine_material_debug::read_bytes(at,dst,bytes);
                    };
                    planar_reflection_thing_scan scan{};
                    if(collect_planar_reflection_things(read,world,scan)) {
                        auto const work=build_planar_reflection_workset(
                            scan,frame_gpu_3do_instances);
                        // Main-view ownership does not prove that ALL
                        // hierarchy nodes were submitted (per-mesh culling).
                        // Probe every discovered Thing independently,
                        // rotating to avoid large per-frame scans.
                        constexpr size_t sample_budget=16U;
                        std::vector<planar_reflection_model_thing> sample;
                        size_t const remaining=scan.models.size();
                        size_t const sample_count=std::min(
                            sample_budget,remaining);
                        sample.reserve(sample_count);
                        if(remaining) {
                            size_t const first=
                                ((gpu_3do_present_serial/120U)*sample_budget)%
                                remaining;
                            for(size_t i=0;i<sample_count;++i)
                                sample.push_back(scan.models[
                                    (first+i)%remaining]);
                        }
                        auto const offscreen=acquire_planar_reflection_offscreen_gpu(
                            sample,sample_budget);
                        LOG_INFO("PLANAR discovery world=",world,
                                 " sectors=",scan.sectors,
                                 " linked_things=",scan.linked_things,
                                 " reflected_3do_candidates=",scan.models.size(),
                                 " excluded_flags=",scan.excluded_flags,
                                 " non_models=",scan.non_models,
                                 " matched_things=",work.matched_things,
                                 " cached_nodes=",work.cached_instance_indices.size(),
                                 " uncovered_things=",work.uncovered_things.size(),
                                 " unowned_gpu_nodes=",work.unowned_gpu_nodes,
                                 " unmatched_gpu_nodes=",work.unmatched_gpu_nodes,
                                 " probed_things=",offscreen.stats.considered,
                                 " pose_ready=",offscreen.stats.posed_things,
                                 " pose_failed=",offscreen.stats.pose_failures,
                                 " native_nodes=",offscreen.stats.native_mesh_nodes,
                                 " gpu_reused=",offscreen.stats.reused_gpu_meshes,
                                 " gpu_snapshots=",offscreen.stats.new_gpu_snapshots,
                                 " material_unready=",offscreen.stats.material_unready,
                                 " incomplete_things=",offscreen.stats.incomplete_things,
                                 " ready_nodes=",offscreen.stats.ready_nodes,
                                 " NOTE=offscreen_acquisition_only_no_capture_yet");
                    } else {
                        LOG_WARNING("PLANAR discovery unavailable world=",world,
                                    " error=",planar_reflection_thing_error_name(scan.error));
                    }
                }
            }
            // One native camera frame, shared between cached world and 3DOs.
            frame_saber_light_colors=saber_light_rgb::get_registry().consume();
            // Ordinary colored Things annotate their EXISTING JK camera
            // lights by native rdLight ID. This must run after the camera
            // has gathered Sith sector lights, but before cached world/3DO
            // shader uniforms are created.
            merge_thing_light_colors();
            ++gpu_3do_present_serial;
            stage_world_cache();
            stage_continuous_ambient_cpu();
            stage_continuous_ambient_gpu();
            stage_sector_crossing();
            animation_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - animation_epoch).count();
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::mat_resolve)]);
                refresh_runtime_mat_links();
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::present_swap)]);
                end_frame();
            }
            // end_frame presented the prior snapshot; prepare a fresh one
            // for the 3DOs that are about to populate this scene.
            frame_gpu_3do_camera={};
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::hud_upload)]);
                update_hud_texture();
            }

            ogs->tribuf.swap_next();
            auto *trimdl = ogs->tribuf.get_current();
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::sort_opaque)]);
                world_batch.sort();
                gun_batch.sort();
                overlay_batch.sort();
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::sort_transparent)]);
                // Depth-writing dithered cutouts do not require painter-order
                // sorting. Preserve their submission order and sort only the
                // truly blended native triangles. std::stable_partition fails
                // to compile with JKGM's over-aligned triangle type on MSVC.
                // Instead, retain only cutouts in a reusable scratch buffer,
                // shift blends backward IN PLACE (preserving their order),
                // then restore the cutouts at the front. O(N), no allocations
                // after the scratch capacity has warmed up.
                native_cutout_scratch.clear();
                for(auto const &tri : world_transparent_batch) {
                    if(is_dithered_material(tri.material))
                        native_cutout_scratch.push_back(tri);
                }
                size_t const cutout_count=native_cutout_scratch.size();
                size_t const triangle_count=world_transparent_batch.size();
                if(cutout_count>0U && cutout_count<triangle_count) {
                    auto first=world_transparent_batch.begin();
                    size_t write=triangle_count;
                    for(size_t i=triangle_count;i>0U;--i) {
                        size_t const read=i-1U;
                        if(!is_dithered_material(first[read].material))
                            first[--write]=std::move(first[read]);
                    }
                    for(size_t i=0U;i<cutout_count;++i)
                        first[i]=std::move(native_cutout_scratch[i]);
                }
                world_transparent_batch.sort_from(cutout_count);
                world_flare_batch.sort();
                gun_transparent_batch.sort();
                if(profiling) {
                    profile_frame.sort_fallback_groups=
                        world_transparent_batch.sort_fallback_groups()+
                        world_flare_batch.sort_fallback_groups()+
                        gun_transparent_batch.sort_fallback_groups();
                    profile_frame.sort_fallback_triangles=
                        world_transparent_batch.sort_fallback_triangles()+
                        world_flare_batch.sort_fallback_triangles()+
                        gun_transparent_batch.sort_fallback_triangles();
                    if(profile_frame.sort_fallback_groups)
                        ++profile_frame.sort_fallback_frames;
                }
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::upload)]);
                fill_buffer(world_batch, &trimdl->world_trimdl);
                fill_buffer(world_transparent_batch, &trimdl->world_transparent_trimdl);
                fill_buffer(world_flare_batch, &trimdl->flare_trimdl);
                fill_buffer(gun_batch, &trimdl->gun_trimdl);
                fill_buffer(gun_transparent_batch, &trimdl->gun_transparent_trimdl);
                fill_buffer(overlay_batch, &trimdl->overlay_trimdl);
            }

            bool posterize_lighting = the_config->enable_posterized_lighting;
            {
                presentation_gpu_profile::completed_sample scene_completed{};
                scene_gpu_timing=profiling && scene_gpu_queries.begin(scene_completed);
                if(scene_completed.ready) {
                    for(size_t i=0;i<scene_gpu_ns.size();++i)
                        scene_gpu_ns[i]+=scene_completed.nanoseconds[i];
                    ++scene_gpu_samples;
                }
                if(scene_completed.skipped)++scene_gpu_skipped;
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::opaque_pass)]);
                draw_spotlight_shadow_map();
                draw_sun_shadow_map();
                // A 0x20 Thing is expected to receive no added sunlight
                // and contribute no shadow depth, but must never disable
                // the shadow map for unrelated objects. Trace *transitions*
                // on the very frame they happen, not the 120-frame sample.
                if(the_config->enable_sun_shadows &&
                   the_config->profile_renderer_verbose) {
                    size_t excluded=0,casters=0;
                    for(auto const &inst:frame_gpu_3do_instances) {
                        if(!inst.geometry || inst.is_projected_decal)continue;
                        if(inst.casts_sun_shadows)++casters;
                        else ++excluded;
                    }
                    // Exempt VFX can be created/destroyed every frame.
                    // Report actual readiness transitions immediately,
                    // sample normal churn periodically, and highlight only
                    // large changes in the number of excluded instances.
                    bool const major_exclusion_change=
                        excluded>sun_shadow_previous_excluded+8U ||
                        sun_shadow_previous_excluded>excluded+8U;
                    if(!sun_shadow_observed ||
                       sun_shadow_previous_ready!=sun_shadow_ready ||
                       major_exclusion_change ||
                       (gpu_3do_present_serial%120U)==0U) {
                        LOG_INFO("SUN shadow frame state: ready=",
                            sun_shadow_ready?1:0,
                            " near=",sun_shadow_cascade_ready[0]?1:0,
                            " far=",sun_shadow_cascade_ready[1]?1:0,
                            " casters=",casters," excluded_instances=",excluded,
                            " world_receivers=",
                                sun_shadow_target
                                ?sun_shadow_target->world.receivers.size():0U,
                            " reason=",sun_shadow_frame_failure
                                ?sun_shadow_frame_failure:"none");
                    }
                    sun_shadow_observed=true;
                    sun_shadow_previous_ready=sun_shadow_ready;
                    sun_shadow_previous_excluded=excluded;
                }
                if(scene_gpu_timing)scene_gpu_queries.timestamp(0U,true);
                draw_game_gbuffer_pass(trimdl, posterize_lighting);
                if(scene_gpu_timing)scene_gpu_queries.timestamp(0U,false);
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::transparent_pass)]);
                draw_game_transparency_pass(trimdl, posterize_lighting);
            }
            if(the_config->experimental_planar_reflection_capture)
                render_planar_capture();
            if(scene_gpu_timing)scene_gpu_queries.end();
            scene_gpu_timing=false;
            // DirectDraw HUD is composited AFTER post-processing in end_frame,
            // when this rendered scene is presented on the following frame.

            // Preserve Camera Thing geometry, camera AND resolved materials
            // from the same source frame. The next present() must never
            // re-read native MAT pointers (animated cels and transient GUI
            // materials can be changed/freed before that later draw).
            deferred_gpu_3do_instances.clear();
            clear_deferred_gpu_hud_materials();
            deferred_gpu_3do_camera={};
            size_t hud_cached_mats=0,hud_missing_mats=0,hud_instances=0;
            if(frame_has_exempt_gpu) {
                deferred_gpu_3do_camera=frame_gpu_3do_camera;
                for(auto &inst:frame_gpu_3do_instances) {
                    if(!inst.camera_hud) continue;
                    ++hud_instances;
                    if(inst.geometry) for(auto const &group:inst.geometry->groups) {
                        bool const solid=inst.effective_geometry_mode==3 ||
                                         group.solid_color;
                        uint64_t const key=
                            (uint64_t(group.material)<<32) |
                            uint32_t(group.cel) |
                            (solid?(uint64_t(1)<<63):0U) |
                            (solid?(uint64_t(1)<<62):0U);
                        auto found=gpu_frame_material_cache.find(key);
                        if(found!=gpu_frame_material_cache.end()) {
                            if(deferred_gpu_hud_material_cache.emplace(
                                   key,found->second).second) {
                                // A borrowed GL image is not stable across
                                // the render/present frame boundary.
                                retain_deferred_gpu_hud_material(found->second);
                                ++hud_cached_mats;
                            }
                        } else ++hud_missing_mats;
                    }
                    deferred_gpu_3do_instances.push_back(std::move(inst));
                }
                if(the_config->profile_renderer &&
                   the_config->profile_renderer_verbose &&
                   gpu_3do_present_serial%120U==0U)
                    LOG_INFO("Camera HUD frame material snapshot instances=",
                             hud_instances," mats=",hud_cached_mats,
                             " misses=",hud_missing_mats,
                             " alpha_sort=",the_config->camera_hud_depth_sort?1:0,
                             " gpu_HUD=",the_config->enable_gpu_camera_hud?1:0);
            }
            frame_has_exempt_gpu=false;

            // Procedural particle clouds and camera-facing polyline faces are
            // generated anew every frame. Keep their GPU upload alive across
            // opaque/transparent passes, then reclaim it immediately rather
            // than retaining 64 frames of stale particle buffers.
            for(auto const &inst:frame_gpu_3do_instances) {
                if(!inst.transient_procedural)continue;
                gpu_3do_gl_cache.erase(inst.id);
                gpu_3do_last_drawn.erase(inst.id);
            }

            // Transient 3DOs (text, HUD, effects) may have already been freed
            // by JK. Expire their GPU resources, keeping recently reused meshes
            // without retaining every allocation for the entire level.
            // Sweep periodically to avoid iterating the cache on every frame.
            if((the_config->use_gpu_cache ||
                 the_config->enable_decals ||
                (the_config->enable_spotlights && the_config->enable_spotlight_shadows) ||
                the_config->enable_sun_shadows) &&
               (gpu_3do_present_serial%16U)==0U) {
                for(auto it=gpu_3do_gl_cache.begin();it!=gpu_3do_gl_cache.end();) {
                    auto const used=gpu_3do_last_drawn.find(it->first);
                    if(used==gpu_3do_last_drawn.end() ||
                       gpu_3do_present_serial-used->second>64U) {
                        if(used!=gpu_3do_last_drawn.end())
                            gpu_3do_last_drawn.erase(used);
                        it=gpu_3do_gl_cache.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // Reset state:
            frame_gpu_3do_instances.clear();
            is_gun = false;
            is_transparent = false;
            current_triangle_batch = &world_batch;
            current_material = material_instance_id(0U);
            world_batch.clear();
            world_transparent_batch.clear();
            world_flare_batch.clear();
            gun_batch.clear();
            gun_transparent_batch.clear();
            overlay_batch.clear();
            if(profiling) {
                auto const stats=consume_gpu_3do_native_stats();
                profile_frame.gpu_candidates+=stats.attempted;
                profile_frame.gpu_culled+=stats.culled;
                profile_frame.gpu_queued+=stats.queued;
                profile_frame.gpu_snapshots+=stats.snapshots;
                profile_frame.gpu_source_checks+=stats.source_checks;
                profile_frame.gpu_source_ns+=stats.source_ns;
                profile_frame.gpu_source_invalidations+=stats.source_invalidations;
                profile_frame.gpu_failed_snapshot_reuses+=stats.failed_snapshot_reuses;
                profile_frame.gpu_ready_checks+=stats.ready_checks;
                profile_frame.gpu_ready_material_refs+=stats.ready_material_refs;
                profile_frame.gpu_ready_ns+=stats.ready_ns;
                profile_frame.gpu_ready_cache_hits+=stats.ready_cache_hits;
                profile_frame.gpu_empty_mesh_native+=stats.empty_mesh_native;
                profile_frame.gpu_native_span_cache_hits+=stats.native_span_cache_hits;
                profile_frame.gpu_native_span_queries+=stats.native_span_queries;
                profile_frame.gpu_geometry_native+=stats.geometry_native;
                profile_frame.gpu_snapshot_native+=stats.snapshot_native;
                profile_frame.gpu_material_native+=stats.material_native;
                profile_frame.gpu_lighting_native+=stats.lighting_native;
                auto const native = consume_native_3do_sample();
                profile_frame.native_model_calls += native.model_calls;
                profile_frame.native_model_drawn += native.model_drawn;
                profile_frame.native_mesh_calls += native.mesh_calls;
                profile_frame.native_face_calls += native.face_calls;
                profile_frame.native_face_emitted += native.face_emitted;
                profile_frame.native_model_ns += native.model_ns;
                profile_frame.native_mesh_ns += native.mesh_ns;
                profile_frame.native_face_ns += native.face_ns;
                profile_frame.native_model_hook_ns += native.model_hook_ns;
                profile_frame.native_model_engine_ns += native.model_engine_ns;
                profile_frame.native_model_engine_self_ns += native.model_engine_self_ns;
                profile_frame.native_mesh_queue_ns += native.mesh_queue_ns;
                profile_frame.native_mesh_native_ns += native.mesh_native_ns;
                profile_frame.native_mesh_decal_ns += native.mesh_decal_ns;
                profile_frame.native_mesh_gpu_calls += native.mesh_gpu_calls;
                profile_frame.native_mesh_native_calls += native.mesh_native_calls;
                auto const sprites=consume_native_sprite_sample();
                for(size_t i=0;i<sprites.type_calls.size();++i)
                    profile_frame.sprite_type_calls[i]+=sprites.type_calls[i];
                profile_frame.sprite_other_type_calls+=sprites.other_type_calls;
                profile_frame.sprite_hidden_calls+=sprites.hidden_calls;
                profile_frame.sprite_native_calls+=sprites.native_calls;
                profile_frame.sprite_native_success+=sprites.native_success;
                profile_frame.sprite_native_ns+=sprites.native_ns;
                profile_frame.sprite_native_proc_faces+=sprites.native_proc_faces;
                profile_frame.sprite_projected_gpu+=sprites.projected_gpu;
                profile_frame.sprite_projected_warmups+=sprites.projected_warmups;
                profile_frame.sprite_gpu_cached_quads+=sprites.gpu_cached_quads;
                profile_frame.sprite_gpu_quad_reuses+=sprites.gpu_quad_reuses;
                profile_frame.sprite_gpu_quad_fallbacks+=sprites.gpu_quad_fallbacks;
                profile_frame.sprite_gpu_quad_mat_wait+=sprites.gpu_quad_mat_wait;
                for(size_t i=0;i<sprites.gpu_quad_rejects.size();++i)
                    profile_frame.sprite_gpu_quad_rejects[i]+=
                        sprites.gpu_quad_rejects[i];
            }
            record_profile(started, profile_clock::now());
            if(profiling) {
                // Recorded *after* logging so the log cost does not count as JK work.
                last_present_end = profile_clock::now();
                last_execute_end = {};
                frame_had_execute = false;
            }
        }

        void depth_clear_game() override
        {
            // JK calls this once per frame, immediately after present.
            // Ignore: the framebuffer is already cleared elsewhere.
        }

        IDirectDraw *get_directdraw() override
        {
            return &ddraw1;
        }

        IDirectDraw2 *get_directdraw2() override
        {
            return &ddraw2;
        }

        IDirect3D *get_direct3d() override
        {
            return &d3d1;
        }

        IDirect3DDevice *get_direct3ddevice() override
        {
            return &d3ddevice1;
        }

        IDirect3DViewport *get_direct3dviewport() override
        {
            return &d3dviewport1;
        }

        IDirectDrawSurface *get_directdraw_primary_surface() override
        {
            if(mode == renderer_mode::menu) {
                return &ddraw1_primary_menu_surface;
            }

            return &ddraw1_primary_surface;
        }

        IDirectDrawSurface *get_directdraw_backbuffer_surface() override
        {
            return &ddraw1_backbuffer_surface;
        }

        IDirectDrawSurface *get_directdraw_backbuffer_menu_surface() override
        {
            return &ddraw1_backbuffer_menu_surface;
        }

        IDirectDrawSurface *get_directdraw_zbuffer_surface(DDSURFACEDESC const &desc) override
        {
            ddraw1_zbuffer_surface.sd = desc;
            return &ddraw1_zbuffer_surface;
        }

        IDirectDrawSurface *get_directdraw_offscreen_surface(DDSURFACEDESC const &desc) override
        {
            if(mode == renderer_mode::menu) {
                if(desc.dwHeight == 64) {
                    // JK uses a 64-pixel tall secondary surface for drawing the credit scroll.
                    ddraw1_offscreen_credit_surface.set_surface_desc(desc);
                    return &ddraw1_offscreen_credit_surface;
                }

                ddraw1_offscreen_menu_surface.set_surface_desc(desc);
                return &ddraw1_offscreen_menu_surface;
            }

            ddraw1_offscreen_surface.set_surface_desc(desc);
            return &ddraw1_offscreen_surface;
        }

        IDirectDrawSurface *
            get_directdraw_sysmem_texture_surface(DDSURFACEDESC const &desc) override
        {
            auto get_matching_buffer = [&] {
                size_t num_pixels = desc.dwWidth * desc.dwHeight;
                for(auto &tex : sysmem_texture_surfaces) {
                    if(tex->refct <= 0 && tex->num_pixels == num_pixels) {
                        return tex.get();
                    }
                }

                sysmem_texture_surfaces.push_back(
                    std::make_unique<sysmem_texture_surface>(num_pixels));
                return sysmem_texture_surfaces.back().get();
            };

            auto *rv = get_matching_buffer();
            rv->set_surface_desc(desc);
            rv->AddRef();
            return rv;
        }

        IDirectDrawSurface *
            get_directdraw_vidmem_texture_surface(DDSURFACEDESC const &desc) override
        {
            auto get_matching_buffer = [&] {
                for(auto &tex : vidmem_texture_surfaces) {
                    if(tex->refct <= 0) {
                        // Recycled D3D material IDs can still be referenced by
                        // a JK MAT until its next native cache update. Reject
                        // all earlier positive residency and MAT associations,
                        // even if the new surface is never uploaded.
                        ++tex->texture_generation;
                        notify_texture_uploaded();
                        tex->refct = 0;
                        tex->clear();
                        return tex.get();
                    }
                }

                vidmem_texture_surfaces.push_back(std::make_unique<vidmem_texture_surface>(
                    this, material_instance_id(vidmem_texture_surfaces.size() + 1)));
                return vidmem_texture_surfaces.back().get();
            };

            auto *rv = get_matching_buffer();
            rv->set_surface_desc(desc);
            rv->AddRef();
            return rv;
        }

        IDirectDrawPalette *get_directdraw_palette(span<PALETTEENTRY const> entries) override
        {
            std::copy(entries.begin(), entries.end(), ddraw1_palette.entries.begin());
            ddraw1_palette.recompute_palette();
            return &ddraw1_palette;
        }

        IDirect3DExecuteBuffer *create_direct3dexecutebuffer(size_t bufsz) override
        {
            auto get_matching_buffer = [&] {
                // Look for expired execute buffer of the same size
                for(auto &em : execute_buffers) {
                    if(em->refct <= 0 && em->bufsz == bufsz) {
                        return em.get();
                    }
                }

                execute_buffers.push_back(std::make_unique<execute_buffer>(bufsz));
                return execute_buffers.back().get();
            };

            auto *rv = get_matching_buffer();
            rv->AddRef();
            return rv;
        }
    };
}

std::unique_ptr<jkgm::renderer> jkgm::create_renderer(HINSTANCE dll_instance,
                                                      config const *the_config)
{
    return std::make_unique<jkgm::renderer_impl>(dll_instance, the_config);
}
