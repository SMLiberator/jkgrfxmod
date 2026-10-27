#include "renderer.hpp"
#include "frame_profile.hpp"
#include "presentation_gpu_profile.hpp"
#include "native_3do_profile.hpp"
#include "gpu_3do_preview.hpp"
#include "gpu_3do_uv.hpp"
#include "native_sprite_profile.hpp"
#include "audio_module_probe.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_alpha.hpp"
#include "native_jk_palette_effects.hpp"
#include "retro_palette.hpp"
#include "jk_colormap_layout.hpp"
#include "gpu_3do_frustum.hpp"
#include "fast_vertex_colors.hpp"
#include "runtime_material.hpp"
#include "spotlight.hpp"
#include "spotlight_shadow_gpu.hpp"
#include "sun_shadow_gpu.hpp"
#include "sun_dynamic_shadow_gpu.hpp"
#include "backbuffer_menu_surface.hpp"
#include "backbuffer_surface.hpp"
#include "base/file_block.hpp"
#include "base/file_stream.hpp"
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
#include <map>
#include <set>
#include <string>
#include <sstream>
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
            gl::texture_view texture = gl::default_texture;
            gl::texture_view texture2 = gl::default_texture;
            float albedo_transition=0.0f;
            int albedo_source2=2;
            gl::texture_view emission = gl::default_texture;
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
            bool has_emission=false, emissive_only=false;
            bool post_exempt=false;
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
        std::vector<gpu_3do_instance> frame_gpu_3do_instances;
        // Retained across the one-frame render/present boundary.
        std::vector<gpu_3do_instance> deferred_gpu_3do_instances;
        bool frame_has_exempt_gpu=false;
        bool frame_has_exempt_native=false;
        std::set<uint64_t> frame_exempt_gpu_meshes;
        enum class exempt_buffer { world, world_alpha, gun, gun_alpha };
        struct exempt_range {
            exempt_buffer source;
            material_instance_id material;
            size_t first_vertex;
            size_t vertex_count;
        };
        std::vector<exempt_range> deferred_native_ranges;
        std::unique_ptr<renderer_compositor> frame_compositor;
        std::unique_ptr<renderer_ao> frame_ao;
        std::unique_ptr<renderer_screen> frame_screen;

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
        std::unique_ptr<sun_dynamic_shadow_gpu> sun_dynamic_target;
        std::vector<sun_shadow_projection> sun_dynamic_views;
        std::vector<sun_dynamic_shadow_tile> sun_dynamic_tiles;
        std::vector<float> sun_dynamic_caster_end_depth; // normalized depth of each caster's farthest AABB point
        // Track only owners with a successfully rasterized atlas tile.
        std::unordered_set<uint64_t> sun_dynamic_atlas_owners;
        std::array<sun_shadow_projection,2> sun_shadow_views{};
        bool sun_shadow_ready=false;
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

        static uint64_t elapsed_profile_us(profile_clock::time_point a,
                                           profile_clock::time_point b)
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
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
            if(the_config->use_model_cache || the_config->enable_sprite_decals) {
                LOG_INFO("PERF gpu_3do fallback_native_per_frame text=",
                         profile_window.gpu_text_native/frames,
                         " geometry=",profile_window.gpu_geometry_native/frames,
                         " snapshot=",profile_window.gpu_snapshot_native/frames,
                         " material=",profile_window.gpu_material_native/frames,
                         " lighting_or_camera=",profile_window.gpu_lighting_native/frames);
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
                LOG_INFO("PERF gpu_3do batch GL_calls_avg=",
                         profile_window.gpu_draw_calls/frames,
                         " source_ranges_avg=",
                         profile_window.gpu_draw_ranges/frames,
                         " multi_calls_avg=",
                         profile_window.gpu_multidraw_calls/frames,
                         " NOTE=only_opaque_ranges_combined");
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
                         " NOTE=counts_include_opaque_and_alpha_passes");
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
            release_spotlight_images();
            spotlight_shadow_target.reset();
            sun_shadow_target.reset();
            sun_dynamic_target.reset();
            // Context-owned GL queries must be deleted before the context
            // disappears. If the context is already gone, its objects go
            // away with it; do not issue GL calls on an invalid context.
            if(hGLRC && wglGetCurrentContext()==hGLRC)
                present_gpu_queries.release();
        }

        void set_renderer_mode(renderer_mode mode) override
        {
            if(this->mode != mode) {
                if(the_config->use_model_cache || the_config->enable_sprite_decals ||
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
                sun_shadow_cascade_ready={{false,false}};
                sun_shadow_cascade_cached={{false,false}};
                sun_shadow_static_cached={{false,false}};
                sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
            sun_dynamic_caster_end_depth.clear();
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

        // Execute AFTER the complete post/trail/Retro presentation, directly
        // into the final sRGB framebuffer. No extra full-resolution mask/FBO.
        // The opaque G-buffer view-depth texture still belongs to this frame;
        // world-space UI tests against it in the fragment shader.
        void draw_post_exempt()
        {
            if(deferred_native_ranges.empty() &&
               deferred_gpu_3do_instances.empty()) return;

            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            gl::disable(gl::capability::depth_test);
            gl::set_depth_mask(false);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::blend);
            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);

            if(!deferred_native_ranges.empty()) {
                auto *trimdl=ogs->tribuf.get_current();
                auto &prog=ogs->game_transparency_pass_program;
                prog.use_program();
                prog.set_screen_resolution(static_cast<size<2,float>>(conf_scr_res));
                prog.set_albedo_map_sampler(0);
                prog.set_emissive_map_sampler(1);
                prog.set_displacement_map_sampler(2);
                prog.set_surface_map_sampler(3);
                prog.set_env_map_sampler(4);
                prog.set_normal_map_samplers(6,7);
                prog.set_scene_depth_sampler(5);
                prog.set_flare_mode(false);
                prog.set_translucent_spotlight_count(0);
                apply_engine_camera(prog);
                auto const &fog=get_fog_settings();
                auto const fog_color=fog.color.value();
                auto const fog_info=fog.info.value();
                prog.set_fog(make_point(fog_info[0],fog_info[1],
                    the_config->enable_fog?fog_info[2]:0.0f,0.0f),
                    color_rgb(fog_color[0],fog_color[1],fog_color[2]));
                gl::set_active_texture_unit(5);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                                 frame_screen->get_resolved_depth_texture());
                gl::set_active_texture_unit(0);

                for(auto const &range:deferred_native_ranges) {
                    triangle_buffer_model *model=nullptr;
                    int mode=1;
                    switch(range.source) {
                    case exempt_buffer::world:
                        model=&trimdl->world_trimdl;break;
                    case exempt_buffer::world_alpha:
                        model=&trimdl->world_transparent_trimdl;break;
                    case exempt_buffer::gun:
                        model=&trimdl->gun_trimdl;mode=2;break;
                    case exempt_buffer::gun_alpha:
                        model=&trimdl->gun_transparent_trimdl;mode=2;break;
                    }
                    prog.set_post_exempt_mode(mode);
                    bind_material(&prog,range.material,false,
                                  the_config->enable_posterized_lighting);
                    gl::bind_vertex_array(model->vao);
                    gl::draw_arrays(gl::element_type::triangles,
                                    range.first_vertex,range.vertex_count);
                }
                prog.set_post_exempt_mode(0);
                set_native_depth_bias(0.0f);
            }

            // Cached mesh instances retain their transform and face/UV groups;
            // the existing range renderer sorts/blends them without re-upload.
            if(!deferred_gpu_3do_instances.empty())
                draw_gpu_3do_preview(true,true);

            gl::set_blend_function(gl::blend_function::one,
                                   gl::blend_function::one_minus_source_alpha);
            gl::set_active_texture_unit(0);
            gl::disable(gl::capability::blend);
            gl::set_depth_mask(true);
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
                deferred_native_ranges.clear();
                deferred_gpu_3do_instances.clear();
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
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_composite)]);
                if(gpu_timing)present_gpu_queries.timestamp(2U,true);
                frame_compositor->end_frame(ogs.get(),conf_scr_res,scene);
                draw_post_exempt();
                // Vanilla DirectDraw HUD is 2D screen-space UI, not a scene
                // light source. Keep it out of HDR exposure, bloom, grading,
                // spatial blur, Retro quantization and frame-trail history.
                // Draw last so it also overlays 3D post-exempt materials.
                if(mode==renderer_mode::ingame) {
                    profile_scope hud_cpu(measure,profile_frame.timings[
                        profile_idx(profile_stage::hud_draw)]);
                    draw_hud();
                }
                if(gpu_timing)present_gpu_queries.timestamp(2U,false);
            }
            if(gpu_timing)present_gpu_queries.end();
            {
                profile_scope cpu(measure,profile_frame.timings[
                    profile_idx(profile_stage::present_swapbuffers)]);
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

            if(the_config->optimize_hud_uploads) {
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
                if(the_config->optimize_hud_uploads) {
                    auto &mirror=hud_uploaded_pixels[hud_back_index];
                    std::memcpy(mirror.data()+offset,
                                ogs->hud_texture_data.data()+offset,
                                num_pixels*sizeof(color_rgba8));
                }
            }
            if(the_config->optimize_hud_uploads)
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
            // Composite the DirectDraw HUD over the final sRGB presentation,
            // not into the linear 3D scene before tone mapping and bloom.
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
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

        bool is_post_exempt_material(material_instance_id id) const
        {
            if(!the_config->enable_post || id.get()==0U ||
               id.get()>vidmem_texture_surfaces.size()) return false;
            auto const &surface=vidmem_texture_surfaces[id.get()-1];
            return surface && surface->skip_post_processing;
        }

        // Preserve original GPU buffer offsets: no geometry duplication or
        // additional upload. Adjacent same-MAT glyphs form one draw range.
        void capture_exempt_ranges(exempt_buffer src,triangle_batch const &batch)
        {
            size_t offset=0;
            for(auto const &tri:batch) {
                if(is_post_exempt_material(tri.material)) {
                    if(!deferred_native_ranges.empty()) {
                        auto &last=deferred_native_ranges.back();
                        if(last.source==src && last.material==tri.material &&
                           last.first_vertex+last.vertex_count==offset) {
                            last.vertex_count+=3;
                            offset+=3;
                            continue;
                        }
                    }
                    deferred_native_ranges.push_back({src,tri.material,offset,3U});
                }
                offset+=3;
            }
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
                if(is_post_exempt_material(tri.material)) {
                    selected=false;
                    frame_has_exempt_native=true;
                }
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
            mdl->maybe_grow_buffers(tb.capacity() * 3);

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
            if(!chosen) return out;
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
                    out.texture2=texture_cache->get_texture_handle(*to_albedo);
                    out.albedo_source2=1;
                    out.alpha=out.alpha ||
                        texture_cache->has_nonopaque_alpha(*to_albedo);
                } else out.albedo_source2=2;
            }
            out.masked=surface->alpha_mode==material_alpha_mode::mask;
            out.dithered=surface->alpha_mode==material_alpha_mode::dithered;
            out.blend=surface->blend_mode;
            out.post_exempt=the_config->enable_post && surface->skip_post_processing;
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
            if(the_config->enable_reflections && strength>0) {
                out.reflection_strength=strength;
                out.roughness=surface->roughness;
                out.metalness=surface->metalness;
                out.normal_strength=surface->normal_strength;
                out.normal_strength2=surface->normal_strength2;
                out.normal_scroll=scroll_offset(surface->normal_scroll);
                out.normal_scroll2=scroll_offset(surface->normal_scroll2);
                out.reflection_mode=static_cast<int>(surface->reflection_mode);
                if(env_from) {
                    out.environment=texture_cache->get_texture_handle(*env_from);
                    out.has_environment=true;
                }
                if(env_weight>0.0f) {
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
            // Preserve the native MAT's effective original mip-0 size.
            // Its UV reference size is clamped to 256 at draw submission,
            // rather than changing shared source geometry or decal UVs.
            out.width=static_cast<float>(chosen->width << mip);
            out.height=static_cast<float>(chosen->height << mip);
            return out;
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
            profile_scope gpu_profiler(the_config->profile_renderer,
                                       profile_frame.gpu_preview_cpu_us);
            auto const cam=read_active_render_camera();
            if(!cam.valid) return;
            gpu_3do_matrix34 native_view{};
            if(!read_active_rd_camera_view(native_view)) return;

            float far_plane=1000.0f;
            gpu_3do_native_frustum native_frustum{};
            bool have_perspective_frustum=false;
            constexpr uintptr_t current_camera=0x0073A418U;
            if(engine_camera_detail::readable(current_camera,sizeof(uintptr_t))) {
                auto const camera_ptr=*reinterpret_cast<uintptr_t const *>(current_camera);
                if(camera_ptr>=0x10000U &&
                   engine_camera_detail::readable(camera_ptr+0x48,sizeof(uintptr_t))) {
                    auto const frustum_ptr=*reinterpret_cast<uintptr_t const *>(camera_ptr+0x48);
                    if(frustum_ptr>=0x10000U &&
                       engine_camera_detail::readable(
                           frustum_ptr,sizeof(native_frustum))) {
                        std::memcpy(&native_frustum,
                                    reinterpret_cast<void const *>(frustum_ptr),
                                    sizeof(native_frustum));
                        if(std::isfinite(native_frustum.far_plane) &&
                           native_frustum.far_plane>0.1f)
                            far_plane=native_frustum.far_plane;
                        if(engine_camera_detail::readable(camera_ptr,sizeof(int32_t))) {
                            int32_t project_type=0;
                            std::memcpy(&project_type,
                                        reinterpret_cast<void const *>(camera_ptr),
                                        sizeof(project_type));
                            have_perspective_frustum=project_type==1;
                        }
                    }
                }
            }
            auto &prog=*ogs->gpu_3do_preview_program;
            prog.use_program();
            set_translucent_spotlights(prog,transparent && !post_exempt &&
                                      !decals_only);
            set_translucent_sun(prog,transparent && !post_exempt && !decals_only);
            prog.set_scene_depth(8);
            prog.set_post_exempt(post_exempt);
            prog.set_decal_receiver_lighting(decals_only,10,11);
            if(decals_only) {
                // Reuse the unused alpha of the opaque emissive G-buffer for
                // the receiver's JK light intensity. No extra render target,
                // color/depth copy or feedback from the active framebuffer.
                gl::set_active_texture_unit(10);
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
            float const sun_power=the_config->enable_reflections
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
                gpu_3do_texture texture;
                gpu_3do_alpha_mode policy;
                gpu_3do_matrix34 const *modelview;
                float depth;
                size_t order;
            };
            std::vector<pending_draw> draws;
            // Keep composed matrices stable across sorting and draw submission.
            std::vector<gpu_3do_matrix34> modelviews;
            modelviews.reserve(instances.size());
            for(auto const &inst:instances) {
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
                    uint64_t const mat_key=
                        (uint64_t(group.material)<<32) |
                        uint32_t(group.cel);
                    auto found=gpu_frame_material_cache.find(mat_key);
                    if(found==gpu_frame_material_cache.end()) {
                        auto item=resolve_gpu_3do_material(group);
                        found=gpu_frame_material_cache.emplace(mat_key,std::move(item)).first;
                        if(the_config->profile_renderer)
                            ++profile_frame.gpu_material_resolves;
                    }
                    auto const &tex=found->second;
                    if(!post_exempt && tex.post_exempt) {
                        frame_has_exempt_gpu=true;
                        frame_exempt_gpu_meshes.insert(inst.id);
                    }
                    if(tex.post_exempt!=post_exempt) continue;
                    // Projected SPRs are always alpha-composited over
                    // the existing opaque architecture and depth-tested.
                    // The MAT may be fully opaque; its native SPR instance
                    // opacity (including SetSpriteInfo) is independent.
                    auto const policy=inst.is_projected_decal
                        ? gpu_3do_alpha_mode::blend_only
                        : choose_gpu_3do_alpha_mode(
                            (group.face_type&0x2U)!=0,
                            tex.alpha,tex.masked,
                            tex.special||tex.emissive_only,
                            tex.opacity,tex.dithered);
                    if(!post_exempt && !gpu_3do_draws_in_pass(policy,transparent)) continue;
                    float depth=0.0f;
                    if(transparent) {
                        auto const &m=inst.transform;
                        auto const &c=group.centroid;
                        float const wx=m[9]+m[0]*c[0]+m[3]*c[1]+m[6]*c[2];
                        float const wy=m[10]+m[1]*c[0]+m[4]*c[1]+m[7]*c[2];
                        float const wz=m[11]+m[2]*c[0]+m[5]*c[1]+m[8]*c[2];
                        depth=(wx-cam.position[0])*cam.forward[0]+
                              (wy-cam.position[1])*cam.forward[1]+
                              (wz-cam.position[2])*cam.forward[2];
                    }
                    draws.push_back({&inst,&group,tex,policy,&modelviews.back(),
                                     depth,draws.size()});
                }
            }
            if(!transparent) {
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
                        return a.order<b.order;
                    });
            }
            if(transparent) {
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
            gpu_3do_instance const *active_instance=nullptr;
            bool prev_specular_active=false;
            bool prev_albedo_transition=false;
            bool prev_advanced_material=false;
            bool prev_parallax=false;
            std::vector<GLsizei> multidraw_counts;
            std::vector<void const *> multidraw_offsets;
            for(size_t draw_start=0;draw_start<draws.size();) {
                auto const &draw=draws[draw_start];
                auto const &inst=*draw.instance;
                auto const &group=*draw.group;
                size_t draw_end=draw_start+1;
                if(!transparent) {
                    while(draw_end<draws.size()) {
                        auto const &other=draws[draw_end];
                        if(other.instance!=draw.instance ||
                           other.group->material!=group.material ||
                           other.group->cel!=group.cel ||
                           other.group->face_type!=group.face_type ||
                           other.group->nonplanar!=group.nonplanar ||
                           other.policy!=draw.policy)
                            break;
                        ++draw_end;
                    }
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
                    prog.set_lighting(inst);
                    // 0x20 suppresses JkGRfxMod light-reception effects on
                    // GPU-cached 3DOs, not the authored JK vertex lighting.
                    // Native D3D triangles do not carry per-Thing flags here.
                    prog.set_world_lighting_exempt(!inst.casts_sun_shadows);
                    prog.set_colormap_tint(inst.colormap_tint);
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
                    : ((group.face_type&0x2U)!=0 ? 90.0f/255.0f : 1.0f);
                if(transparent) {
                    switch(draw.texture.blend) {
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
                bind_color_texture(draw.texture.texture,0,draw.texture.filtered);
                bind_color_texture(draw.texture.emission,1,draw.texture.filtered);
                bool const has_albedo_transition=
                    draw.texture.albedo_transition>0.0f;
                if(has_albedo_transition) {
                    bind_color_texture(draw.texture.texture2,6,draw.texture.filtered);
                    prog.set_albedo_transition(draw.texture.albedo_source2,
                                               draw.texture.albedo_transition);
                } else if(prev_albedo_transition) {
                    prog.set_albedo_transition(2,0.0f);
                }
                prev_albedo_transition=has_albedo_transition;
                // Projected SPR decals always use receiver-plane depth
                // clipping; shared MAT material flags cannot disable it.
                bool const clip_decal=transparent && inst.is_projected_decal;
                bool const advanced=clip_decal ||
                    (transparent &&
                     draw.texture.blend==material_blend_mode::fluid);
                if(advanced)
                    prog.set_advanced_material(clip_decal,
                                               draw.texture.fluid_thickness);
                else if(prev_advanced_material)
                    prog.set_advanced_material(false,0.25f);
                prev_advanced_material=advanced;
                bool const parallax=draw.texture.displacement_factor!=0.0f;
                if(parallax) {
                    gl::set_active_texture_unit(9);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     draw.texture.displacement);
                    prog.set_parallax(draw.texture.displacement_factor);
                } else if(prev_parallax) {
                    prog.set_parallax(0.0f);
                }
                prev_parallax=parallax;
                // Only bind normal/surface maps when EnvMap, sun or the
                // opt-in dynamic Thing point-light GGX can use them.
                // Decals have no native point lights; ordinary per-vertex JK
                // diffuse lighting is entirely unaffected by this switch.
                bool const point_specular_active=
                    the_config->enable_reflections &&
                    the_config->enable_dynamic_light_specular &&
                    !inst.is_projected_decal && inst.num_lights>0U;
                bool const spec_active=draw.texture.reflection_strength>0 &&
                    (draw.texture.has_environment ||
                     draw.texture.has_environment2 || sun_active ||
                     point_specular_active);
                if(spec_active) {
                    gl::set_active_texture_unit(2);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     draw.texture.surface);
                    gl::set_active_texture_unit(3);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     draw.texture.normal);
                    if(draw.texture.has_normal2) {
                        gl::set_active_texture_unit(5);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         draw.texture.normal2);
                    }
                    if(draw.texture.has_environment) {
                        gl::set_active_texture_unit(4);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         draw.texture.environment);
                    }
                    if(draw.texture.environment_transition>0.0f &&
                       draw.texture.has_environment2) {
                        gl::set_active_texture_unit(7);
                        gl::bind_texture(gl::texture_bind_target::texture_2d,
                                         draw.texture.environment2);
                    }
                    prog.set_environment(draw.texture.has_environment,
                                         draw.texture.has_environment2,
                                         draw.texture.environment_transition,
                                         draw.texture.reflection_mode);
                    prog.set_point_specular(
                        make_point(draw.texture.reflection_strength,
                                   draw.texture.roughness,draw.texture.metalness,
                                   draw.texture.has_surface?1.0f:0.0f),
                        make_point(draw.texture.has_normal?1.0f:0.0f,
                                   draw.texture.normal_strength,
                                   draw.texture.has_normal2?1.0f:0.0f,
                                   draw.texture.normal_strength2),
                        draw.texture.normal_scroll,
                        draw.texture.normal_scroll2);
                } else if(prev_specular_active) {
                    prog.set_point_specular(
                        make_point(0.0f,0.5f,0.0f,0.0f),
                        make_point(0.0f,1.0f,0.0f,0.0f),
                        make_point(0.0f,0.0f),make_point(0.0f,0.0f));
                    prog.set_environment(false,false,0.0f,0);
                }
                prev_specular_active=spec_active;
                // JK's high-resolution MAT patch uses a 256x256 maximum UV
                // reference area for *models*, independently per dimension.
                // For a 64px MAT, 0..256 repeats four times; for >=256px,
                // the same UV span covers one complete texture.
                // Type-3 SPR decal UVs come from rdSprite and must continue
                // to use their original native dimensions.
                float const uv_width=inst.is_projected_decal
                    ? draw.texture.width
                    : gpu_3do_model_uv_extent(draw.texture.width);
                float const uv_height=inst.is_projected_decal
                    ? draw.texture.height
                    : gpu_3do_model_uv_extent(draw.texture.height);
                prog.set_surface(draw.texture.albedo_source,uv_width,
                                 uv_height,transparent,
                                 static_cast<int>(draw.texture.blend),
                                 static_cast<int>(draw.policy),
                                 draw.texture.alpha_cutoff,face_opacity,
                                 draw.texture.tint,draw.texture.albedo_scroll,
                                 draw.texture.emissive_only);
                prog.set_emissive(draw.texture.has_emission,
                                  draw.texture.emission_factor,
                                  draw.texture.emissive_scroll);
                if(draw_end==draw_start+1) {
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
                    if(draw_end>draw_start+1)
                        ++profile_frame.gpu_multidraw_calls;
                    for(size_t n=draw_start;n<draw_end;n++)
                        profile_frame.gpu_draw_triangles+=
                            draws[n].group->count/3;
                }
                draw_start=draw_end;
            }
            // The transparency pass's following JK batches assume normal
            // premultiplied alpha blending, unit 0 and disabled face culling.
            gl::disable(gl::capability::cull_face);
            if(transparent)
                gl::set_blend_function(gl::blend_function::one,
                    gl::blend_function::one_minus_source_alpha);
            gl::set_active_texture_unit(0);
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
            ogs->game_opaque_pass_program.set_screen_resolution(
                static_cast<size<2, float>>(conf_scr_res));
            ogs->game_opaque_pass_program.set_albedo_map_sampler(0);
            ogs->game_opaque_pass_program.set_emissive_map_sampler(1);
            ogs->game_opaque_pass_program.set_displacement_map_sampler(2);
            ogs->game_opaque_pass_program.set_surface_map_sampler(3);
            ogs->game_opaque_pass_program.set_env_map_sampler(4);
            ogs->game_opaque_pass_program.set_normal_map_samplers(6, 7);
            apply_engine_camera(ogs->game_opaque_pass_program);

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

            // Draw first pass (opaque world geometry)
            draw_batch(&ogs->game_opaque_pass_program,
                       world_batch,
                       &trimdl->world_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);

            // Draw second pass (transparent world geometry with alpha testing)
            draw_batch(&ogs->game_opaque_pass_program,
                       world_transparent_batch,
                       &trimdl->world_transparent_trimdl,
                       /*force opaque*/ true,
                       posterize_lighting);

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
                shadow_slot>=0 ? spotlight_shadow_view.range : 0.0f);
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



        template<typename Shader>
        void set_translucent_sun(Shader &prog,bool active)
        {
            auto const off=make_point(0.0f,0.0f,1.0f);
            auto const dark=make_point(0.0f,0.0f,0.0f,0.0f);
            auto &uniforms=prog.translucent_sun;
            uniforms.set_state(false,false,0,off,dark,
                               the_config->sun_shadow_near_distance,1.0f,0.0f);
            if(!active || !the_config->enable_sun_shadows)return;
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
            uniforms.set_state(true,shadows,dynamic,
                make_point(dir[0],dir[1],dir[2]),
                make_point(rgb[0],rgb[1],rgb[2],intensity),
                the_config->sun_shadow_near_distance,
                the_config->sun_shadow_receiver_bias_scale,
                the_config->sun_dynamic_shadow_distance_softness);
            auto set_projection=[&](int slot,sun_shadow_projection const &p,
                                    point<4,float> rect,float caster_end=0.0f) {
                auto row=[&](decal_vec3 const &axis,float size) {
                    float const inv=1.0f/std::max(size,0.00001f);
                    float const origin_projection=
                        -(p.origin[0]*axis[0]+p.origin[1]*axis[1]+
                          p.origin[2]*axis[2])*inv;
                    return make_point(axis[0]*inv,axis[1]*inv,
                                      axis[2]*inv,origin_projection);
                };
                uniforms.set_map(slot,row(p.right,p.width),
                    row(p.up,p.height),row(p.forward,p.range),rect,caster_end);
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
            if(dynamic>0) {
                for(int i=0;i<dynamic;++i) {
                    auto const &tile=sun_dynamic_tiles[i];
                    set_projection(i+2,sun_dynamic_views[i],
                        make_point(float(tile.x),float(tile.y),
                                   float(tile.size),float(tile.size)),
                        sun_dynamic_caster_end_depth[i]);
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
            auto const light_bounds=spotlight_shadow_volume_bounds(view);
            {
                for(auto const &instance:frame_gpu_3do_instances) {
                    // SithThing 0x20 skybox/UI meshes are visible but must
                    // never cost a spotlight depth pass or shadow the scene.
                    if(!instance.geometry || instance.is_projected_decal ||
                       !instance.casts_sun_shadows)
                        continue;
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
                        // A spotlight mounted within its source 3DO must not
                        // have that mesh's inside/backside block its own beam.
                        if(spotlight_shadow_origin_inside(chosen.origin,low,high))
                            continue;
                        // A blended effect never writes opaque shadow
                        // depth. Masked/mixed MATs cast cutout silhouettes
                        // using the same UVs/textures as visible geometry.
                        auto const tex=resolve_gpu_3do_material(group);
                        bool const cutout=tex.masked||tex.alpha||tex.dithered;
                        if(tex.albedo_source==0 || tex.special ||
                           tex.emissive_only || tex.post_exempt ||
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
                   tex.emissive_only || tex.post_exempt ||
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
                         " model_cache=",the_config->use_model_cache?1:0,
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
            sun_shadow_projection const &world_view)
        {
            sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
            sun_dynamic_caster_end_depth.clear();
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
            for(auto &entry:keyed) {
                auto &c=entry.second;
                if(!c.has_bounds)continue;
                auto const &lo=c.bounds.low,&hi=c.bounds.high;
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
            auto shadow_material=[&](gpu_3do_texture const &tex,bool cutout) {
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
                    tex.masked?tex.alpha_cutoff:(tex.dithered?0.5f:0.998f),
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
                sun_dynamic_shadow_tile tile{};
                if(the_config->sun_dynamic_shadow_adaptive_resolution) {
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
                } else {
                    int const slot=int(sun_dynamic_views.size());
                    tile={(slot%sun_dynamic_shadow_columns)*tile_size,
                          (slot/sun_dynamic_shadow_columns)*tile_size,
                          tile_size};
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
                        auto tex=resolve_gpu_3do_material(g);
                        bool cutout=tex.masked||tex.alpha||tex.dithered;
                        if(tex.albedo_source==0 || tex.special ||
                           tex.emissive_only || tex.post_exempt ||
                           tex.opacity<0.999f ||
                           (cutout && tex.albedo_source!=1))continue;
                        if(the_config->sun_shadow_two_sided_models ||
                           (g.face_type&1U) || g.nonplanar)
                            gl::disable(gl::capability::cull_face);
                        else {
                            gl::enable(gl::capability::cull_face);
                            gl::set_face_cull_mode(gpu_3do_reversed_winding(m)
                                ?gl::face_mode::front:gl::face_mode::back);
                        }
                        shadow_material(tex,cutout);
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
                    // A receiver's distance beyond the caster's light-space
                    // rear boundary approximates shadow separation. Use the
                    // entire Thing bounds, rather than one arbitrary mesh.
                    float caster_end_world=0.0f;
                    for(int k=0;k<3;++k)
                        caster_end_world+=view.forward[k]*
                            (view.forward[k]>=0.0f
                                ? object->bounds.high[k] : object->bounds.low[k]);
                    float origin_depth=decal_dot(view.origin,view.forward);
                    sun_dynamic_caster_end_depth.push_back(
                        (caster_end_world-origin_depth)/
                        std::max(view.range,0.001f));
                    sun_dynamic_atlas_owners.insert(object->key);
                } else if(the_config->sun_dynamic_shadow_adaptive_resolution) {
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
            if(the_config->profile_renderer && gpu_3do_present_serial%120U==0U) {
                size_t shaded_pixels=0;
                std::ostringstream tile_sizes;
                for(size_t i=0;i<sun_dynamic_tiles.size();++i) {
                    auto const &tile=sun_dynamic_tiles[i];
                    if(i!=0)tile_sizes<<",";
                    tile_sizes<<tile.size;
                    auto const inner=tile.size-2*sun_dynamic_shadow_inset;
                    shaded_pixels+=size_t(inner)*size_t(inner);
                }
                LOG_INFO("SUN dynamic shadow atlas Things=",
                    sun_dynamic_views.size()," candidates=",sorted.size(),
                    " base_tile=",tile_size,
                    " adaptive=",the_config->sun_dynamic_shadow_adaptive_resolution?1:0,
                    " tile_sizes=[",tile_sizes.str(),"]",
                    " depth_viewport_pixels=",shaded_pixels);
            }
        }

        void draw_sun_shadow_map()
        {
            sun_shadow_ready=false;
            sun_shadow_cascade_ready={{false,false}};
            sun_dynamic_views.clear();
            sun_dynamic_tiles.clear();
            sun_dynamic_caster_end_depth.clear();
            sun_dynamic_atlas_owners.clear();
            if(!the_config->enable_sun_shadows)return;
            auto sun_failure=[&](char const *reason) {
                if(the_config->profile_renderer &&
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
            if(!(intensity>0.0f) || !std::isfinite(intensity))return;

            uint32_t world=0;
            if(!engine_material_debug::read_object(
                   engine_material_debug::current_world_global,world) ||
               !decal_native_address_ok(world))return;
            decal_native_read const safe_read=[](uintptr_t ptr,void *dst,size_t n) {
                if(n==0 || !engine_camera_detail::readable(ptr,n))return false;
                std::memcpy(dst,reinterpret_cast<void const *>(ptr),n);
                return true;
            };
            if(!sun_shadow_target)
                sun_shadow_target=std::make_unique<sun_shadow_gpu>(
                    the_config->sun_shadow_resolution);
            auto &map=*sun_shadow_target;
            if(!map.update_world(safe_read,world)) {
                sun_failure("SithWorld shadow snapshot failed");
                return;
            }
            // Avoid three full sets of transformed group bounds (projection,
            // near cascade, far cascade), especially in crowded 3DO levels.
            // A level may have only a tiny native-world shell while its
            // playable architecture is made from 3DOs. Include submitted
            // model groups before fitting the sun map; otherwise both the
            // receiver lighting and model shadows fall outside the map.
            std::vector<decal_volume_aabb> model_bounds;
            struct sun_model_candidate {
                gpu_3do_instance const *instance;
                gpu_3do_group const *group;
                decal_volume_aabb bounds;
            };
            std::vector<sun_model_candidate> model_candidates;
            size_t skipped_skybox_instances=0;
            for(auto const &instance:frame_gpu_3do_instances) {
                if(!instance.casts_sun_shadows) {
                    ++skipped_skybox_instances;
                    continue;
                }
                if(!instance.geometry || instance.is_projected_decal)continue;
                auto const &m=instance.transform;
                for(auto const &group:instance.geometry->groups) {
                    if(group.count<3U || (group.face_type&0x2U))continue;
                    auto const bounds=sun_shadow_transformed_aabb(
                        group.bounds_min,group.bounds_max,m);
                    if(decal_finite(bounds.low) && decal_finite(bounds.high)) {
                        model_bounds.push_back(bounds);
                        model_candidates.push_back({&instance,&group,bounds});
                    }
                }
            }
            sun_shadow_projection world_view{};
            if(!make_sun_shadow_projection(map.world,direction,world_view,model_bounds)) {
                sun_failure("no world receivers or captured 3DO casters");
                return;
            }
            auto const camera=read_active_render_camera();
            if(!camera.valid) {
                sun_failure("render camera unavailable");
                return;
            }
            // Atlas first: exclude only successfully drawn Things from CSM.
            // Excess candidates retain the normal cascade fallback.
            draw_sun_dynamic_shadow_atlas(world_view);
            float const horizontal=std::tan(camera.fov*
                (3.14159265358979323846f/360.0f));
            float const vertical=horizontal*float(get<y>(conf_scr_res))/
                float(get<x>(conf_scr_res));
            for(int cascade=0;cascade<2;++cascade) {
            sun_shadow_projection view=world_view;
            float const distance=cascade==0
                ? the_config->sun_shadow_near_distance : the_config->sun_shadow_far_distance;
            if(!fit_sun_shadow_to_camera(view,camera.position,camera.right,
                    camera.up,camera.forward,horizontal,vertical,distance)) {
                sun_failure("camera-local cascade projection invalid");
                continue;
            }
            // Reuse an already rendered projection whenever the ACTUAL
            // camera frustum still fits within its coverage. Rotation and
            // translation no longer force redundant 4096px shadow redraws.
            // Keep the same sunlight depth-axis origin/range too: an updated
            // world AABB must never silently clip new casters in Z.
            bool locked_view=false;
            if(the_config->sun_shadow_stable_projection_cache &&
               sun_shadow_cascade_cached[cascade] &&
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
            if(!locked_view &&
               !stabilize_sun_shadow_projection(view,map.resolution)) {
                sun_failure("camera-local cascade stabilization failed");
                continue;
            }

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
            // Read live geomodes and MAT coverage each frame. COG changes to
            // a surface therefore invalidate the cached depth image.
            for(auto const &range:map.selected) {
                if(range.source_index>=map.world.receivers.size())continue;
                auto const &surface=map.world.receivers[range.source_index];
                uint32_t geomode=0;
                uintptr_t const address=map.world.surfaces_addr+
                    uintptr_t(surface.surface_index)*0x5cU;
                engine_material_debug::read_object(address+0x1cU,geomode);
                hash_u32(surface.surface_index);hash_u32(geomode);
                if(geomode<2U || !decal_finite(surface.normal)) {
                    ++world_hidden;
                    continue;
                }
                gpu_3do_group group{};group.material=surface.material;
                group.cel=surface.cel;
                int32_t cel=group.cel;
                engine_material_debug::read_object(address+0x38U,cel);
                group.cel=cel;
                uint64_t const key=(uint64_t(surface.material)<<32)|uint32_t(cel);
                auto found=gpu_frame_material_cache.find(key);
                if(found==gpu_frame_material_cache.end())
                    found=gpu_frame_material_cache.emplace(
                        key,resolve_gpu_3do_material(group)).first;
                auto const &tex=found->second;
                bool const cutout=tex.masked||tex.alpha||tex.dithered;
                // Keep cutout MAT silhouettes using their shadow UVs.
                if(tex.albedo_source==0 || tex.special || tex.emissive_only ||
                   tex.post_exempt || tex.opacity<0.999f ||
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
            // Keep the stable world state and add stationary 3DOs, but
            // NEVER include moving/unknown Thing geometry in this key.
            uint64_t static_signature=caster_signature;
            auto hash_static_u32=[&](uint32_t value) {
                for(int b=0;b<4;++b) {
                    static_signature^=uint64_t((value>>(b*8))&255U);
                    static_signature*=1099511628211ULL;
                }
            };
            auto hash_static_float=[&](float value) {
                uint32_t bits=0;
                std::memcpy(&bits,&value,sizeof(bits));
                hash_static_u32(bits);
            };

            // Include submitted 3DO transforms. The native hook is enabled
            // when this feature is on; model groups share their normal GPU
            // buffers with the visible renderer.
            auto const bounds=world_bounds;
            struct sun_model_draw {gpu_3do_instance const *instance;
                                   gpu_3do_group const *group;
                                   gpu_3do_texture texture;bool cutout;};
            std::vector<sun_model_draw> models,static_models;
            // The expensive group-box transforms were computed once before
            // fitting the sun projection; both cascades use those exact AABBs.
            // Share one per-frame material lookup across world and 3DOs,
            // rather than re-reading the native MAT for each cascade.
            auto cached_material=[&](gpu_3do_group const &group)
                ->gpu_3do_texture const & {
                uint64_t const key=(uint64_t(group.material)<<32)|
                    uint32_t(group.cel);
                auto found=gpu_frame_material_cache.find(key);
                if(found==gpu_frame_material_cache.end())
                    found=gpu_frame_material_cache.emplace(
                        key,resolve_gpu_3do_material(group)).first;
                return found->second;
            };
            for(auto const &prepared:model_candidates) {
                auto const &instance=*prepared.instance;
                auto const &group=*prepared.group;
                if(!sun_shadow_caster_in_static_cascades(
                       instance.shadow_owner,sun_dynamic_atlas_owners))continue;
                if(!decal_aabbs_overlap(
                       prepared.bounds.low,prepared.bounds.high,
                       bounds.low,bounds.high))continue;
                auto const &tex=cached_material(group);
                bool const cutout=tex.masked||tex.alpha||tex.dithered;
                if(tex.albedo_source==0 || tex.special || tex.emissive_only ||
                   tex.post_exempt || tex.opacity<0.999f ||
                   (cutout && tex.albedo_source!=1))continue;
                // Verified non-moving SithThings may join the cached static
                // layer. An unknown owner stays dynamic (conservative), and
                // even a path/physics Thing parked in place remains dynamic.
                bool const stable=sun_shadow_cache_stationary_model(
                    instance.shadow_owner,instance.dynamic_sun_caster,
                    the_config->enable_sun_static_cache,map.static_complete);
                (stable?static_models:models).push_back(
                    {&instance,&group,tex,cutout});
                auto hash_model=[&](auto &&integer,auto &&floating) {
                    integer(uint32_t(instance.id));
                    integer(uint32_t(instance.id>>32));
                    integer(group.offset);integer(group.count);
                    integer(uint32_t(instance.shadow_owner));
                    integer(uint32_t(instance.shadow_owner>>32));
                    integer(uint32_t(group.material));
                    integer(uint32_t(group.cel));
                    if(cutout) {
                        floating(tex.alpha_cutoff);floating(tex.opacity);
                        floating(get<x>(tex.albedo_scroll));
                        floating(get<y>(tex.albedo_scroll));
                        floating(tex.albedo_transition);
                    }
                    for(float v:instance.transform)floating(v);
                };
                hash_model(hash_u32,hash_float);
                if(stable)hash_model(hash_static_u32,hash_static_float);
            }
            if(the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN shadow caster batches world=",
                         counts.size()+world_cutouts.size(),
                         " 3do=",models.size()+static_models.size(),
                         " static_3do=",static_models.size()," skybox_skipped=",
                         skipped_skybox_instances," cascade=",cascade,
                         " map=",map.resolution,
                         " selected_world=",map.selected.size(),
                         " hidden_world=",world_hidden,
                         " backfacing_world=",world_backfacing,
                         " material_skipped_world=",world_material_skipped,
                         " two_sided_world=1");
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
            if(unchanged) {
                if(the_config->profile_renderer)
                    ++sun_profile_full_hits[cascade];
                sun_shadow_cascade_ready[cascade]=true;
                if(the_config->profile_renderer &&
                   gpu_3do_present_serial%120U==0U)
                    LOG_INFO("SUN cascade=",cascade," full_cache_hit=1");
                continue;
            }
            // Reusing a depth texture requires the exact same stabilized
            // light projection, not merely the same sun and static casters.
            bool static_same=the_config->enable_sun_static_cache &&
                map.static_complete &&
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
                ++sun_profile_working_redraws[cascade];
                if(static_same) ++sun_profile_static_hits[cascade];
                else ++sun_profile_static_redraws[cascade];
            }
            auto const &cascade_target=cascade==0 ? map.fbo : map.far_fbo;
            auto const &static_target=cascade==0 ? map.static_fbo : map.static_far_fbo;
            auto const dimensions=make_size(int(map.resolution),int(map.resolution));
            gl::set_viewport(make_box(make_point(0,0),dimensions));
            gl::disable(gl::capability::blend);
            gl::disable(gl::capability::cull_face);
            gl::enable(gl::capability::depth_test);
            gl::set_depth_mask(true);
            gl::set_depth_function(gl::comparison_function::less);
            if(!static_same) {
                gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                    (the_config->enable_sun_static_cache &&
                     map.static_complete) ? static_target : cascade_target);
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
            auto set_cutout=[&](gpu_3do_texture const &tex,bool world_uv) {
                float const width=world_uv?tex.width:gpu_3do_model_uv_extent(tex.width);
                float const height=world_uv?tex.height:gpu_3do_model_uv_extent(tex.height);
                bool const second=tex.albedo_transition>0.0f && tex.albedo_source2==1;
                prog.set_shadow_alpha(true,
                    make_point(1.0f/std::max(width,1.0f),
                               1.0f/std::max(height,1.0f)),
                    tex.albedo_scroll,
                    tex.masked?tex.alpha_cutoff:(tex.dithered?0.5f:0.998f),
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
                for(auto const &draw:draws) {
                    auto const &instance=*draw.instance;auto const &group=*draw.group;
                    auto it=gpu_3do_gl_cache.find(instance.id);
                    if(it==gpu_3do_gl_cache.end())
                        it=gpu_3do_gl_cache.emplace(instance.id,
                            std::make_unique<gpu_3do_gl_mesh>(*instance.geometry)).first;
                    auto const &m=instance.transform;
                    prog.set_model(make_point(m[0],m[1],m[2]),
                        make_point(m[3],m[4],m[5]),make_point(m[6],m[7],m[8]),
                        make_point(m[9],m[10],m[11]));
                    if(the_config->sun_shadow_two_sided_models ||
                       (group.face_type&0x1U)!=0 || group.nonplanar)
                        gl::disable(gl::capability::cull_face);
                    else {
                        gl::enable(gl::capability::cull_face);
                        gl::set_face_cull_mode(gpu_3do_reversed_winding(m)
                            ?gl::face_mode::front:gl::face_mode::back);
                    }
                    gl::bind_vertex_array(it->second->vao);
                    if(draw.cutout)set_cutout(draw.texture,false);
                    else prog.set_shadow_alpha(false,
                        make_point(1.0f,1.0f),make_point(0.0f,0.0f),
                        0.5f,1.0f,false,0.0f);
                    gl::draw_elements(gl::element_type::triangles,group.count,
                        gl::index_type::uint32,static_cast<ptrdiff_t>(
                            group.offset*sizeof(uint32_t)));
                }
            };
            if(!static_same) {
                draw_models(static_models);
                if(map.static_complete &&
                   the_config->enable_sun_static_cache) {
                    sun_shadow_static_cached[cascade]=true;
                    sun_shadow_static_worlds[cascade]=world;
                    sun_shadow_static_revisions[cascade]=map.revision;
                    sun_shadow_static_signatures[cascade]=static_signature;
                    sun_shadow_static_views[cascade]=view;
                    sun_shadow_static_directions[cascade]=direction;
                }
            }
            if(map.static_complete &&
               the_config->enable_sun_static_cache) {
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
            draw_models(models);
            glDisable(GL_POLYGON_OFFSET_FILL);
            gl::disable(gl::capability::cull_face);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::set_active_texture_unit(0);
            gl::set_viewport(make_box(make_point(0,0),conf_scr_res));
            if(the_config->profile_renderer &&
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
            sun_shadow_ready=sun_shadow_cascade_ready[0] &&
                             sun_shadow_cascade_ready[1];
            if(the_config->profile_renderer &&
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
                sun_profile_full_hits.fill(0);
                sun_profile_static_hits.fill(0);
                sun_profile_static_redraws.fill(0);
                sun_profile_working_redraws.fill(0);
            }
        }

        bool shadow_matches_light(spotlight_instance const &spot) const
        {
            if(!spotlight_shadow_ready ||
               spot.material!=spotlight_shadow_light.material)return false;
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
            auto &prog=ogs->game_post_opaque_composite_program;
            prog.set_spotlight_count(0);
            prog.set_sun_dynamic_count(0);
            prog.set_sun_dynamic_shadow_distance_softness(
                the_config->sun_dynamic_shadow_distance_softness);
            prog.set_fill_tjunction_gaps(the_config->fill_tjunction_gaps);
            prog.set_tjunction_gap_fill_size(int(the_config->tjunction_gap_fill_size));
            prog.set_debug_void_pixels(the_config->debug_void_pixels);
            prog.set_retain_hom_pixels(the_config->retain_hom_pixels);
            prog.set_sun_shadow_far_soft_filter(
                the_config->sun_shadow_far_soft_filter);
            prog.set_sun_shadow_receiver_bias_scale(
                the_config->sun_shadow_receiver_bias_scale);
            prog.set_spotlight_shadow(-1,0.0f,0.0f);
            prog.set_sun_shadow(false,false,make_point(0.0f,0.0f,1.0f),
                make_point(0.0f,0.0f,0.0f,0.0f),
                make_point(0.0f,0.0f,0.0f),make_point(1.0f,0.0f,0.0f),
                make_point(0.0f,1.0f,0.0f),make_point(0.0f,0.0f,1.0f),
                make_point(1.0f,1.0f,1.0f));
            // Direct sunlight must survive a failed/unavailable shadow map.
            auto const sun=get_sun_settings();
            float const sun_power=sun.intensity.value()[0];
            bool const sun_lit=the_config->enable_sun_shadows &&
                std::isfinite(sun_power) && sun_power>0.0f;
            auto const cam=read_active_render_camera();
            if(the_config->enable_sun_shadows && the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("SUN composite diffuse=",sun_lit && cam.valid?1:0,
                         " shadow=",sun_shadow_ready?1:0,
                         " intensity=",std::to_string(sun_power));
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
                                spotlight_shadow_view.range);
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
                if(active>0 || sun_lit) {
                    gl::set_active_texture_unit(5);
                    gl::bind_texture(gl::texture_bind_target::texture_2d,
                                     frame_screen->get_resolved_normal_texture());
                }
                if(sun_lit) {
                    bool const shadows=sun_shadow_ready && bool(sun_shadow_target);
                    auto const direction=normalized_sun_direction(sun.direction.value());
                    auto const rgb=sun.color.value();
                    prog.set_sun_shadow(true,shadows,
                        make_point(direction[0],direction[1],direction[2]),
                        make_point(rgb[0],rgb[1],rgb[2],sun_power),
                        make_point(sun_shadow_views[0].origin[0],sun_shadow_views[0].origin[1],sun_shadow_views[0].origin[2]),
                        make_point(sun_shadow_views[0].right[0],sun_shadow_views[0].right[1],sun_shadow_views[0].right[2]),
                        make_point(sun_shadow_views[0].up[0],sun_shadow_views[0].up[1],sun_shadow_views[0].up[2]),
                        make_point(sun_shadow_views[0].forward[0],sun_shadow_views[0].forward[1],sun_shadow_views[0].forward[2]),
                        make_point(sun_shadow_views[0].width,sun_shadow_views[0].height,sun_shadow_views[0].range));
                    if(shadows) {
                        auto const &far_view=sun_shadow_views[1];
                        prog.set_sun_shadow_far(
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
                            prog.set_sun_dynamic_atlas_rect(int(i),
                                make_point(float(tile.x),float(tile.y),
                                           float(tile.size),float(tile.size)));
                            prog.set_sun_dynamic_caster_depth(int(i),
                                sun_dynamic_caster_end_depth[i]);
                            prog.set_sun_dynamic(int(i),
                                make_point(v.origin[0],v.origin[1],v.origin[2]),
                                make_point(v.right[0],v.right[1],v.right[2]),
                                make_point(v.up[0],v.up[1],v.up[2]),
                                make_point(v.forward[0],v.forward[1],v.forward[2]),
                                make_point(v.width,v.height,v.range));
                        }
                        prog.set_sun_dynamic_count(int(sun_dynamic_views.size()));
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

        void draw_game_transparency_pass(triangle_buffer_models *trimdl, bool posterize_lighting)
        {
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
            ogs->game_transparency_pass_program.set_post_exempt_mode(0);
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
            // Sample the completed opaque pass; this is a different texture
            // from the framebuffer receiving the transparency draw.
            gl::set_active_texture_unit(5);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_depth_texture());
            gl::set_active_texture_unit(0);
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
            if(the_config->enable_sprite_decals) {
                draw_gpu_3do_preview(true,false,true);
                ogs->game_transparency_pass_program.use_program();
            }

            // Draw third pass (transparent world geometry with alpha blending)
            draw_batch(&ogs->game_transparency_pass_program,
                       world_transparent_batch,
                       &trimdl->world_transparent_trimdl,
                       /*force opaque*/ false,
                       posterize_lighting,
                       batch_blend_filter::all);

            // Blended and depth-clipped materials from originally opaque
            // world geometry render here, without writing G-buffer depth.
            draw_batch(&ogs->game_transparency_pass_program,
                       world_batch,
                       &trimdl->world_trimdl,
                       /*force opaque*/ false,
                       posterize_lighting,
                       batch_blend_filter::special);

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
            draw_gpu_3do_preview(true);
            ogs->game_transparency_pass_program.use_program();

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
            ++gpu_3do_present_serial;
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
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::opaque_pass)]);
                draw_spotlight_shadow_map();
                draw_sun_shadow_map();
                draw_game_gbuffer_pass(trimdl, posterize_lighting);
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::transparent_pass)]);
                draw_game_transparency_pass(trimdl, posterize_lighting);
            }
            // DirectDraw HUD is composited AFTER post-processing in end_frame,
            // when this rendered scene is presented on the following frame.

            // Present this frame's exempt ranges next time end_frame runs.
            deferred_native_ranges.clear();
            if(frame_has_exempt_native && the_config->enable_post) {
                capture_exempt_ranges(exempt_buffer::world,world_batch);
                capture_exempt_ranges(exempt_buffer::world_alpha,world_transparent_batch);
                capture_exempt_ranges(exempt_buffer::gun,gun_batch);
                capture_exempt_ranges(exempt_buffer::gun_alpha,gun_transparent_batch);
            }
            deferred_gpu_3do_instances.clear();
            if(frame_has_exempt_gpu && the_config->enable_post) {
                // Only retain meshes that actually contain flagged materials,
                // not every car/NPC in a level with one GUI font.
                for(auto &inst:frame_gpu_3do_instances)
                    if(frame_exempt_gpu_meshes.count(inst.id))
                        deferred_gpu_3do_instances.push_back(std::move(inst));
            }
            frame_has_exempt_gpu=false;
            frame_has_exempt_native=false;
            frame_exempt_gpu_meshes.clear();

            // Transient 3DOs (text, HUD, effects) may have already been freed
            // by JK. Expire their GPU resources, keeping recently reused meshes
            // without retaining every allocation for the entire level.
            // Sweep periodically to avoid iterating the cache on every frame.
            if((the_config->use_model_cache || the_config->enable_sprite_decals ||
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
                profile_frame.gpu_text_native+=stats.text_native;
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
