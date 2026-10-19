#include "renderer.hpp"
#include "frame_profile.hpp"
#include "native_3do_profile.hpp"
#include "gpu_3do_preview.hpp"
#include "gpu_3do_alpha.hpp"
#include "fast_vertex_colors.hpp"
#include "runtime_material.hpp"
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

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
            gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
            gl::buffer_data(gl::buffer_bind_target::element_array,
                            make_span(source.indices).as_const_bytes(),
                            gl::buffer_usage::static_draw);
        }
    };

    class renderer_impl : public renderer {
    private:
        config const *the_config;

        renderer_mode mode = renderer_mode::menu;
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
        std::unordered_map<uint64_t,std::unique_ptr<gpu_3do_gl_mesh>> gpu_3do_gl_cache;
        std::vector<gpu_3do_instance> frame_gpu_3do_instances;
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
            LOG_INFO("PERF sorting_us opaque=", avg(profile_stage::sort_opaque),
                     " transparent=", avg(profile_stage::sort_transparent),
                     " peaks=", peak_sort_opaque, "/", peak_sort_trans,
                     " upload_us=", avg(profile_stage::upload),
                     " nativeMAT_us=", avg(profile_stage::mat_resolve));
            LOG_INFO("PERF passes_us opaque=", avg(profile_stage::opaque_pass),
                     " transparent=", avg(profile_stage::transparent_pass),
                     " post_swap=", avg(profile_stage::present_swap),
                     " HUDupload=", avg(profile_stage::hud_upload),
                     " HUDdraw=", avg(profile_stage::hud_draw));
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
        triangle_batch gun_batch;
        sorted_triangle_batch gun_transparent_batch;
        triangle_batch overlay_batch;

        bool is_gun = false;
        bool is_transparent = false;
        triangle_batch *current_triangle_batch = &world_batch;

        material_instance_id current_material = material_instance_id(0U);

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

        void set_renderer_mode(renderer_mode mode) override
        {
            if(this->mode != mode) {
                if(the_config->use_gpu_renderer) {
                    // Levels recycle native mesh pointers; discard both copies.
                    reset_gpu_3do_instances();
                    gpu_3do_gl_cache.clear();
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
                // Pausing/menu transitions may evict and reload D3D textures.
                // Refresh the native MAT resolver on the next game frame,
                // instead of letting the previous one-second timer cause a flash.
                force_runtime_refresh = true;
                runtime_fast_retries = 0;
                // A scene transition cannot reuse old frame history.
                // Do not do this in begin_game/end_game (called each frame).
                get_frame_blend_settings().reset();
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

        void begin_frame()
        {
            gl::set_active_texture_unit(0);
            frame_screen->begin_frame();
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
            return texture;
        }

        // "base" is the compiled-pack image (or the original JK albedo).
        // The FROM/TO keys are resolved in their own originating MAT's pack.
        // Missing source files never silently replace the original image.
        void resolve_runtime_transition(runtime_image_transition const &transition,
                                        std::optional<srgb_texture_id> base,
                                        std::optional<srgb_texture_id> &from,
                                        std::optional<srgb_texture_id> &to,
                                        float &weight)
        {
            from = base;
            to = base;
            weight = 0.0f;
            if(transition.from) {
                from = resolve_runtime_image(*transition.from);
                if(!from) return;
            }
            if(transition.to) {
                to = resolve_runtime_image(*transition.to);
                if(!to) {
                    from = base;
                    to = base;
                    return;
                }
            }
            weight = transition.progress();
            if(weight >= 1.0f) {
                from = to;
                weight = 0.0f;
            }
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

        void end_frame()
        {
            if(mode == renderer_mode::ingame && !the_config->profile_renderer) {
                log_mat_links(); // Don't add diagnostic scans to profiling overhead.
            }
            // Compose renderbuffer onto window:
            frame_screen->end_frame();
            // Optional depth/peripheral blur occurs before bloom, color processing
            // and frame blending. Keep the source texture untouched for no-blur mode.
            gl::texture_view scene = frame_screen->get_resolved_final_texture();
            if(mode == renderer_mode::ingame) {
                scene = apply_spatial_blur(
                    ogs.get(), conf_scr_res, scene,
                    frame_screen->get_resolved_depth_texture());
            }
            frame_compositor->end_frame(ogs.get(), conf_scr_res, scene);

            SwapBuffers(hDC);

            begin_frame();
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
        void update_hud_texture()
        {
            if(curr_slab == 0) {
                // Convert HUD bitmap into valid texture data once every 4 frames
                ZeroMemory(ogs->hud_texture_data.data(), ogs->hud_texture_data.size());

                for(size_t i = 0; i < ddraw1_backbuffer_surface.buffer.size(); ++i) {
                    auto const &in_em = ddraw1_backbuffer_surface.buffer[i];

                    // Convert from RGB565 to RGBA8888
                    ogs->hud_texture_data[i] = rgb565_key_to_srgb_a8(
                        in_em, /*transparent?*/ in_em == ddraw1_backbuffer_surface.color_key);
                }
            }

            // Blit 1/nslabs of the HUD texture data into GPU texture
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->hud_texture_back);

            int slab_rows = get<y>(internal_scr_res) / hud_texture_num_slabs;
            int start_row = slab_rows * curr_slab;

            int stop_row = slab_rows * (curr_slab + 1);
            if(curr_slab >= (hud_texture_num_slabs - 1)) {
                stop_row = get<y>(internal_scr_res);
            }

            auto region =
                make_box(make_point(0, start_row), make_point(get<x>(internal_scr_res), stop_row));
            auto offset = start_row * get<x>(internal_scr_res);
            auto num_pixels = volume(region);

            gl::tex_sub_image_2d(
                gl::texture_bind_target::texture_2d,
                /*level*/ 0,
                region,
                gl::texture_pixel_format::rgba,
                gl::texture_pixel_type::uint8,
                make_span(ogs->hud_texture_data).subspan(offset, num_pixels).as_const_bytes());

            ++curr_slab;
            if(curr_slab >= hud_texture_num_slabs) {
                // Swap and present after all 4 slabs have been copied
                std::fill(ddraw1_backbuffer_surface.buffer.begin(),
                          ddraw1_backbuffer_surface.buffer.end(),
                          ddraw1_backbuffer_surface.color_key);

                std::swap(ogs->hud_texture_front, ogs->hud_texture_back);
                curr_slab = 0;
            }
        }

        void draw_hud()
        {
            gl::enable(gl::capability::blend);
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

        // Depth-clipped polygons count as special even with normal blending:
        // they must never enter the opaque pass or modify the G-buffer depth.
        bool is_special_material(material_instance_id id) const
        {
            if(id.get() == 0U) {
                return false;
            }
            auto const &mat = vidmem_texture_surfaces.at(id.get() - 1);
            return mat->blend_mode != material_blend_mode::normal || mat->depth_clip;
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

        void bind_material(shaders::game_pass_shader *prog,
                           material_instance_id id,
                           bool force_opaque,
                           bool posterize_lighting)
        {
            if(the_config->profile_renderer) ++profile_frame.binds;
            // Only the transparent shader actually declares blend_mode; setting
            // it on the opaque shader is ignored (uniform location -1).
            auto const blend_mode = get_material_blend_mode(id);
            prog->set_blend_mode(static_cast<int>(blend_mode));
            prog->set_fluid_thickness(
                id.get() != 0U ? vidmem_texture_surfaces.at(id.get() - 1)->fluid_thickness
                               : 0.25f);
            prog->set_depth_clip(id.get() != 0U &&
                                 vidmem_texture_surfaces.at(id.get() - 1)->depth_clip);

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
                prog->set_reflection(make_point(0.0f, 0.5f, 0.0f, 0.0f),
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
                bool const global_allowed = mat->replacement_material &&
                    !mat->replacement_material->has_explicit_env_map;
                if(the_config->enable_reflections &&
                   global_allowed && runtime_controls.global_env) {
                    resolve_runtime_transition(*runtime_controls.global_env,
                                               mat->env_map, env_from, env_to, env_weight);
                    env_command = runtime_controls.global_env->to.has_value() ||
                                  runtime_controls.global_env->from.has_value();
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
                    ((mat->alpha_mode == material_alpha_mode::mask) && (!force_opaque)) ? 1.0f
                                                                                        : 0.0f,
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


        struct gpu_3do_texture {
            gl::texture_view texture = gl::default_texture;
            gl::texture_view emission = gl::default_texture;
            float width=1.0f, height=1.0f;
            float alpha_cutoff=0.5f;
            float opacity=1.0f;
            // 0=invalid/unresolved MAT (diagnostic magenta), 1=texture,
            // 2=intentionally no albedo map (neutral white).
            int albedo_source=0;
            bool has_image=false, alpha=false, masked=false, special=false;
            bool has_emission=false;
            bool filtered=true;
            material_blend_mode blend=material_blend_mode::normal;
            point<4,float> tint=make_point(1.0f,1.0f,1.0f,1.0f);
            point<4,float> emission_factor=make_point(0.0f,0.0f,0.0f,0.0f);
            point<2,float> albedo_scroll=make_point(0.0f,0.0f);
            point<2,float> emissive_scroll=make_point(0.0f,0.0f);
        };

        gpu_3do_texture resolve_gpu_3do_material(gpu_3do_group const &group)
        {
            gpu_3do_texture out;
            if(group.material==0 ||
               !engine_material_debug::matches_tested_executable())
                return out;

            using namespace engine_material_debug;
            native_material mat{};
            if(!read_object(group.material,mat) || mat.num_texinfo==0 ||
               mat.num_texinfo>16) return out;

            int32_t cel = group.cel < 0
                ? static_cast<int32_t>(mat.current_cel) : group.cel;
            cel = std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info) || !info.texture) return out;
            native_texture tex{};
            if(!read_object(info.texture,tex)) return out;
            out.alpha=(tex.header[0]&1U)!=0;
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
                if(out.alpha && valid_surface(alpha)) chosen=&alpha;
                else if(valid_surface(opaque)) chosen=&opaque;
                else if(valid_surface(alpha)) chosen=&alpha;
                if(chosen) { mip=i; break; }
            }
            if(!chosen) return out;
            auto const id=static_cast<size_t>(chosen->texture_handle);
            if(id==0 || id>vidmem_texture_surfaces.size()) return out;
            auto const &surface=vidmem_texture_surfaces[id-1];
            if(!surface) return out;
            // A replacement can deliberately have no albedo map, e.g.
            // emissive-only brake lights with albedo_factor 0 0 0 0.
            // Do not discard the *entire* material and its emissive map.
            if(surface->albedo_map) {
                out.texture=texture_cache->get_texture_handle(*surface->albedo_map);
                out.albedo_source=1;
                out.has_image=true;
            } else {
                out.albedo_source=2; // neutral texture; tint controls base color
                out.alpha=false; // no actual alpha texture to split
            }
            out.masked=surface->alpha_mode==material_alpha_mode::mask;
            out.blend=surface->blend_mode;
            out.special=surface->depth_clip ||
                        surface->blend_mode!=material_blend_mode::normal;
            out.filtered=surface->texture_filtering.value_or(
                the_config->enable_texture_filtering);
            out.alpha_cutoff=surface->alpha_cutoff;
            auto const &tint=surface->albedo_factor;
            out.tint=make_point(get<0>(tint),get<1>(tint),
                                get<2>(tint),get<3>(tint));
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
            // Emissive-only materials often specify (0,0,0,0) as their
            // albedo factor. For this case alpha=0 means "no base color",
            // not "hide the emissive fragment". Keep the dark base but allow
            // emission to participate in depth and color composition.
            if(gpu_3do_needs_emissive_coverage(
                   out.albedo_source==1,out.opacity,
                   get<0>(out.emission_factor),get<1>(out.emission_factor),
                   get<2>(out.emission_factor)) &&
               out.albedo_source==2) {
                out.tint=make_point(get<0>(tint),get<1>(tint),get<2>(tint),1.0f);
                out.opacity=1.0f;
            }
            // JK stores local 3DO UVs in first-mipmap pixel units.
            // Normalize them against the equivalent original mip-0 size.
            out.width=static_cast<float>(chosen->width << mip);
            out.height=static_cast<float>(chosen->height << mip);
            return out;
        }

        // Separate opaque and alpha passes. Native JK no longer processes
        // individual faces in either path: only GPU indexed ranges remain.
        void draw_gpu_3do_preview(bool transparent)
        {
            if(!ogs->gpu_3do_preview_program || frame_gpu_3do_instances.empty())
                return;
            auto const cam=read_active_render_camera();
            if(!cam.valid) return;

            float far_plane=1000.0f;
            constexpr uintptr_t current_camera=0x0073A418U;
            if(engine_camera_detail::readable(current_camera,sizeof(uintptr_t))) {
                auto const camera_ptr=*reinterpret_cast<uintptr_t const *>(current_camera);
                if(camera_ptr>=0x10000U &&
                   engine_camera_detail::readable(camera_ptr+0x48,sizeof(uintptr_t))) {
                    auto const frustum_ptr=*reinterpret_cast<uintptr_t const *>(camera_ptr+0x48);
                    if(frustum_ptr>=0x10000U &&
                       engine_camera_detail::readable(frustum_ptr+8,sizeof(float))) {
                        float const native_far=*reinterpret_cast<float const *>(frustum_ptr+8);
                        if(std::isfinite(native_far) && native_far>0.1f)
                            far_plane=native_far;
                    }
                }
            }
            auto &prog=*ogs->gpu_3do_preview_program;
            prog.use_program();
            auto const axes=[](std::array<float,3> const &v) {
                return make_point(v[0],v[1],v[2],0.0f);
            };
            float const aspect=get<x>(internal_scr_res_f)/get<y>(internal_scr_res_f);
            float const tan_half_fov=std::tan(cam.fov*0.008726646259971648f);
            prog.set_camera(axes(cam.right),axes(cam.forward),axes(cam.up),
                            axes(cam.position),
                            make_point(tan_half_fov,aspect,1.0f/far_plane,0.0f));
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
                float depth;
                size_t order;
            };
            std::vector<pending_draw> draws;
            for(auto const &inst:frame_gpu_3do_instances) {
                if(!inst.geometry) continue;
                for(auto const &group:inst.geometry->groups) {
                    auto const tex=resolve_gpu_3do_material(group);
                    auto const policy=choose_gpu_3do_alpha_mode(
                        (group.face_type&0x2U)!=0,tex.alpha,tex.masked,
                        tex.special,tex.opacity);
                    if(!gpu_3do_draws_in_pass(policy,transparent)) continue;
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
                    draws.push_back({&inst,&group,tex,policy,depth,draws.size()});
                }
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
            for(auto const &draw:draws) {
                auto const &inst=*draw.instance;
                auto const &group=*draw.group;
                auto it=gpu_3do_gl_cache.find(inst.id);
                if(it==gpu_3do_gl_cache.end()) {
                    auto ptr=std::make_unique<gpu_3do_gl_mesh>(*inst.geometry);
                    it=gpu_3do_gl_cache.emplace(inst.id,std::move(ptr)).first;
                    LOG_INFO("GPU 3DO: uploaded face-corner mesh id=",inst.id,
                             " triangles=",it->second->num_indices/3,
                             " MAT groups=",inst.geometry->groups.size());
                }
                if(active_instance!=draw.instance) {
                    // Multiple cars can instance the same cached mesh ID with
                    // different world matrices. Compare instances, not IDs.
                    auto const &m=inst.transform;
                    prog.set_model(make_point(m[0],m[1],m[2],0.0f),
                                   make_point(m[3],m[4],m[5],0.0f),
                                   make_point(m[6],m[7],m[8],0.0f),
                                   make_point(m[9],m[10],m[11],1.0f));
                    gl::bind_vertex_array(it->second->vao);
                    active_instance=draw.instance;
                }
                if((group.face_type&0x1U)!=0) {
                    gl::disable(gl::capability::cull_face);
                } else {
                    gl::enable(gl::capability::cull_face);
                    gl::set_face_cull_mode(gl::face_mode::back);
                }
                // The native translucent face flag forces alpha even when
                // its MAT texture is fully opaque (JK uses alpha=90/255).
                float const face_opacity=(group.face_type&0x2U)!=0
                    ? 90.0f/255.0f : 1.0f;
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
                prog.set_surface(draw.texture.albedo_source,draw.texture.width,
                                 draw.texture.height,transparent,
                                 static_cast<int>(draw.policy),
                                 draw.texture.alpha_cutoff,face_opacity,
                                 draw.texture.tint,draw.texture.albedo_scroll);
                prog.set_emissive(draw.texture.has_emission,
                                  draw.texture.emission_factor,
                                  draw.texture.emissive_scroll);
                gl::draw_elements(gl::element_type::triangles,group.count,
                                  gl::index_type::uint32,
                                  static_cast<ptrdiff_t>(group.offset*sizeof(uint32_t)));
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

        void draw_game_opaque_composite()
        {
            gl::disable(gl::capability::depth_test);

            ogs->game_post_opaque_composite_program.use_program();
            ogs->game_post_opaque_composite_program.set_color_sampler(0);
            ogs->game_post_opaque_composite_program.set_emissive_sampler(1);
            ogs->game_post_opaque_composite_program.set_occlusion_sampler(2);

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

            frame_screen->begin_compose_opaque_pass();
            draw_game_opaque_composite();
            frame_screen->end_compose_opaque_pass();
        }

        void draw_game_transparency_pass(triangle_buffer_models *trimdl, bool posterize_lighting)
        {
            frame_screen->begin_transparency_pass();

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

            // Draw third pass (transparent world geometry with alpha blending)
            gl::enable(gl::capability::blend);
            gl::set_depth_mask(false);
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

            // GPU-resident 3DO translucent faces join the existing alpha pass.
            draw_gpu_3do_preview(true);
            ogs->game_transparency_pass_program.use_program();

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
                            if(packed == 0U) {
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
                            current_triangle_batch->insert(
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
                                     gun_batch.size() + gun_transparent_batch.size() +
                                     overlay_batch.size();
                profile_frame.trans_tris =
                    world_transparent_batch.size() + gun_transparent_batch.size();
            }

            // Consume once, then render both opaque and translucent subsets.
            // Keeping one frame snapshot avoids losing the second pass.
            frame_gpu_3do_instances=consume_gpu_3do_instances();
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
                world_transparent_batch.sort();
                gun_transparent_batch.sort();
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::upload)]);
                fill_buffer(world_batch, &trimdl->world_trimdl);
                fill_buffer(world_transparent_batch, &trimdl->world_transparent_trimdl);
                fill_buffer(gun_batch, &trimdl->gun_trimdl);
                fill_buffer(gun_transparent_batch, &trimdl->gun_transparent_trimdl);
                fill_buffer(overlay_batch, &trimdl->overlay_trimdl);
            }

            bool posterize_lighting = the_config->enable_posterized_lighting;
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::opaque_pass)]);
                draw_game_gbuffer_pass(trimdl, posterize_lighting);
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::transparent_pass)]);
                draw_game_transparency_pass(trimdl, posterize_lighting);
            }
            {
                profile_scope timer(profiling,
                    profile_frame.timings[profile_idx(profile_stage::hud_draw)]);
                draw_hud();
            }

            // Reset state:
            frame_gpu_3do_instances.clear();
            is_gun = false;
            is_transparent = false;
            current_triangle_batch = &world_batch;
            current_material = material_instance_id(0U);
            world_batch.clear();
            world_transparent_batch.clear();
            gun_batch.clear();
            gun_transparent_batch.clear();
            overlay_batch.clear();
            if(profiling) {
                auto const native = consume_native_3do_sample();
                profile_frame.native_model_calls += native.model_calls;
                profile_frame.native_model_drawn += native.model_drawn;
                profile_frame.native_mesh_calls += native.mesh_calls;
                profile_frame.native_face_calls += native.face_calls;
                profile_frame.native_face_emitted += native.face_emitted;
                profile_frame.native_model_ns += native.model_ns;
                profile_frame.native_mesh_ns += native.mesh_ns;
                profile_frame.native_face_ns += native.face_ns;
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
