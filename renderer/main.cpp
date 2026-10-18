#include "base/default_logger.hpp"
#include "base/log.hpp"
#include "base/span.hpp"
#include "common/config.hpp"
#include "common/error_reporter.hpp"
#include "fog.hpp"
#include "postfx.hpp"
#include "renderer.hpp"
#include "runtime_material.hpp"
#include "native_3do_profile.hpp"
#include "mesh_bounds_culling.hpp"
#include "engine_material_debug.hpp"
#include "sun.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <limits>
#include <string>
#include <detours/detours.h>

static std::unique_ptr<jkgm::renderer> the_renderer;
static std::unique_ptr<jkgm::config> the_config;
static bool renderer_hooks_active = false;

static bool native_3do_hooks_active = false;
static bool native_3do_timing_active = false;
static bool mesh_bounds_culling_active = false;

// Function boundaries and prologues independently verified in both the
// user's SHA-locked jk23.exe and standalone jkgm.exe. Not portable to other JKs.
namespace {
    using native_model_func = int (__cdecl *)(void *, void *);
    using native_mesh_func = void (__cdecl *)(void *, void *);
    using native_face_func = int (__cdecl *)(void *, int);
    constexpr uintptr_t native_model_addr = 0x00446060U;
    constexpr uintptr_t native_mesh_addr = 0x00446360U;
    constexpr uintptr_t native_face_addr = 0x004467D0U;
    constexpr unsigned char native_model_bytes[] =
        {0x83,0xec,0x18,0x8b,0x0d,0x18,0xa4,0x73,0x00};
    constexpr unsigned char native_mesh_bytes[] =
        {0x81,0xec,0x90,0x00,0x00,0x00,0x53,0x55};
    constexpr unsigned char native_face_bytes[] =
        {0x83,0xec,0x0c,0x53,0x55,0x56,0x57,0xe8};
    native_model_func RealNativeModel =
        reinterpret_cast<native_model_func>(native_model_addr);
    native_mesh_func RealNativeMesh =
        reinterpret_cast<native_mesh_func>(native_mesh_addr);
    native_face_func RealNativeFace =
        reinterpret_cast<native_face_func>(native_face_addr);
    jkgm::native_3do_sample native_totals;

    struct bounds_cache_entry {
        float const *vertices;
        int count;
        jkgm::mesh_bounds::bounds box;
    };
    std::unordered_map<void const *, bounds_cache_entry> mesh_bounds_cache;

    using matrix_multiply_func = void (__cdecl *)(void *, void const *, void const *);
    constexpr uintptr_t multiply_addr = 0x00440E00U;
    constexpr unsigned char multiply_bytes[] =
        {0x8b,0x44,0x24,0x08,0x8b,0x4c,0x24,0x0c};
    matrix_multiply_func NativeMultiply =
        reinterpret_cast<matrix_multiply_func>(multiply_addr);

    bool read_mesh_bounds(void const *mesh, jkgm::mesh_bounds::bounds &out,
                          int &count, int &face_count)
    {
        auto const *base = reinterpret_cast<uint8_t const *>(mesh);
        // Offsets verified in the patched EXE's rdModel3_DrawMesh disassembly.
        int const mode = *reinterpret_cast<int32_t const *>(base + 0x24);
        float const *vertices = *reinterpret_cast<float const *const *>(base + 0x30);
        count = *reinterpret_cast<int32_t const *>(base + 0x48);
        face_count = *reinterpret_cast<int32_t const *>(base + 0x50);
        if(mode == 0 || !vertices || count <= 0 || count > 65536) return false;

        auto cached = mesh_bounds_cache.find(mesh);
        if(cached != mesh_bounds_cache.end() &&
           cached->second.vertices == vertices && cached->second.count == count) {
            out = cached->second.box;
            return true;
        }

        // A malformed, noncontiguous or newly freed buffer falls back to JK.
        MEMORY_BASIC_INFORMATION mbi{};
        auto const bytes = static_cast<size_t>(count) * 3 * sizeof(float);
        if(VirtualQuery(vertices, &mbi, sizeof(mbi)) == 0 ||
           mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
           reinterpret_cast<uintptr_t>(vertices) + bytes >
             reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            return false;
        }
        using jkgm::mesh_bounds::vec3;
        vec3 mn{vertices[0],vertices[1],vertices[2]};
        vec3 mx=mn;
        for(int i=1;i<count;i++) {
            float const x=vertices[i*3], y=vertices[i*3+1], z=vertices[i*3+2];
            if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                return false;
            mn.x=std::min(mn.x,x); mn.y=std::min(mn.y,y); mn.z=std::min(mn.z,z);
            mx.x=std::max(mx.x,x); mx.y=std::max(mx.y,y); mx.z=std::max(mx.z,z);
        }
        if(!std::isfinite(mn.x) || !std::isfinite(mn.y) || !std::isfinite(mn.z))
            return false;
        out.center={(mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f,(mn.z+mx.z)*0.5f};
        out.extent={(mx.x-mn.x)*0.5f,(mx.y-mn.y)*0.5f,(mx.z-mn.z)*0.5f};
        if(!jkgm::mesh_bounds::valid(out)) return false;

        // Cache is per-level; scene/renderer mode changes invalidate it.
        if(mesh_bounds_cache.size() > 16384) mesh_bounds_cache.clear();
        mesh_bounds_cache[mesh]={vertices,count,out};
        return true;
    }

    bool reject_mesh_frustum(void *mesh, void *placement)
    {
        if(!placement) return false;
        auto const cam_addr = *reinterpret_cast<uintptr_t const *>(0x0073A418U);
        if(cam_addr < 0x10000U) return false;
        auto const *cam = reinterpret_cast<uint8_t const *>(cam_addr);
        int const project = *reinterpret_cast<int const *>(cam);
        if(project != 1) return false;  // perspective only
        auto const *frustum = *reinterpret_cast<jkgm::mesh_bounds::frustum const *const *>(cam+0x48);
        if(!frustum) return false;
        jkgm::mesh_bounds::bounds b{};
        int count=0, faces=0;
        if(!read_mesh_bounds(mesh,b,count,faces)) return false;
        if(native_3do_timing_active) ++native_totals.bounds_tested;
        jkgm::mesh_bounds::matrix34 combined{};
        NativeMultiply(&combined, cam+8, placement);
        if(jkgm::mesh_bounds::definitely_outside(b,combined,*frustum,project)) {
            if(native_3do_timing_active) {
                ++native_totals.bounds_rejected;
                native_totals.bounds_vertices_saved += static_cast<uint64_t>(count);
                if(faces>0) native_totals.bounds_faces_saved += static_cast<uint64_t>(faces);
            }
            return true;
        }
        return false;
    }


    uint64_t elapsed_native_ns(std::chrono::steady_clock::time_point start)
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start).count());
    }
    int __cdecl HookedNativeModel(void *thing, void *placement)
    {
        auto const start = native_3do_timing_active
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        int result = RealNativeModel(thing, placement);
        if(native_3do_timing_active) {
            native_totals.model_ns += elapsed_native_ns(start);
            ++native_totals.model_calls;
            if(result != 0) ++native_totals.model_drawn;
        }
        return result;
    }
    void __cdecl HookedNativeMesh(void *mesh, void *placement)
    {
        auto const start = native_3do_timing_active
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if(mesh_bounds_culling_active && reject_mesh_frustum(mesh, placement)) {
            // Preserve the current-mesh pointer side effect of the native routine.
            *reinterpret_cast<void **>(0x0073C1E8U) = mesh;
        }
        else {
            RealNativeMesh(mesh, placement);
        }
        if(native_3do_timing_active) {
            native_totals.mesh_ns += elapsed_native_ns(start);
            ++native_totals.mesh_calls;
        }
    }
    int __cdecl HookedNativeFace(void *face, int lightFlags)
    {
        auto const start = native_3do_timing_active
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        int const result = RealNativeFace(face, lightFlags);
        if(native_3do_timing_active) {
            native_totals.face_ns += elapsed_native_ns(start);
            ++native_totals.face_calls;
            if(result != 0) ++native_totals.face_emitted;
        }
        return result;
    }
    bool attach_native_3do_hooks(bool profiling, bool culling)
    {
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) != 0x00400000U ||
           std::memcmp(reinterpret_cast<void const *>(native_model_addr),
                       native_model_bytes, sizeof(native_model_bytes)) != 0 ||
           std::memcmp(reinterpret_cast<void const *>(native_mesh_addr),
                       native_mesh_bytes, sizeof(native_mesh_bytes)) != 0 ||
           std::memcmp(reinterpret_cast<void const *>(native_face_addr),
                       native_face_bytes, sizeof(native_face_bytes)) != 0 ||
           (culling && std::memcmp(reinterpret_cast<void const *>(multiply_addr),
                                  multiply_bytes, sizeof(multiply_bytes)) != 0)) {
            LOG_WARNING("Native 3DO profiler skipped: unrecognized executable");
            return false;
        }
        LONG error = DetourTransactionBegin();
        if(error != NO_ERROR) return false;
        error = DetourUpdateThread(GetCurrentThread());
        if(error == NO_ERROR && profiling)
            error = DetourAttach(&(PVOID &)RealNativeModel, HookedNativeModel);
        if(error == NO_ERROR)
            error = DetourAttach(&(PVOID &)RealNativeMesh, HookedNativeMesh);
        if(error == NO_ERROR && profiling)
            error = DetourAttach(&(PVOID &)RealNativeFace, HookedNativeFace);
        if(error != NO_ERROR) {
            DetourTransactionAbort();
            LOG_WARNING("Native 3DO profiler attach failed: ", error);
            return false;
        }
        error = DetourTransactionCommit();
        if(error != NO_ERROR) {
            LOG_WARNING("Native 3DO profiler commit failed: ", error);
            return false;
        }
        native_3do_timing_active = profiling;
        mesh_bounds_culling_active = culling;
        LOG_INFO("Native 3DO hooks attached; profiling=", profiling,
                 " mesh_bounds_culling=", culling);
        return true;
    }
    void detach_native_3do_hooks()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if(native_3do_timing_active)
            DetourDetach(&(PVOID &)RealNativeFace, HookedNativeFace);
        DetourDetach(&(PVOID &)RealNativeMesh, HookedNativeMesh);
        if(native_3do_timing_active)
            DetourDetach(&(PVOID &)RealNativeModel, HookedNativeModel);
        DetourTransactionCommit();
    }
} // namespace

jkgm::native_3do_sample jkgm::consume_native_3do_sample()
{
    auto result = native_totals;
    native_totals = {};
    return result;
}
bool jkgm::native_3do_available()
{
    return native_3do_hooks_active && native_3do_timing_active;
}
void jkgm::reset_mesh_bounds_cache()
{
    mesh_bounds_cache.clear();
}


// DirectX hooks

using DirectDrawCreate_type = HRESULT(WINAPI *)(GUID *lpGUID,
                                                LPDIRECTDRAW *lplpDD,
                                                IUnknown *pUnkOuter);
static DirectDrawCreate_type TrueDirectDrawCreate = nullptr;
HRESULT WINAPI HookedDirectDrawCreate(GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter)
{
    LOG_DEBUG("DirectDrawCreate hook called");
    *lplpDD = the_renderer->get_directdraw();
    return DD_OK;
}

using DirectDrawEnumerateA_type = HRESULT(WINAPI *)(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext);
static DirectDrawEnumerateA_type TrueDirectDrawEnumerateA = nullptr;
HRESULT WINAPI HookedDirectDrawEnumerateA(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext)
{
    LOG_DEBUG("DirectDrawEnumerateA hook called");
    return the_renderer->enumerate_devices(lpCallback, lpContext);
}

// Win32 API hooks

using CreateWindowExA_type = HWND(WINAPI *)(DWORD dwExStyle,
                                            LPCSTR lpClassName,
                                            LPCSTR lpWindowName,
                                            DWORD dwStyle,
                                            int X,
                                            int Y,
                                            int nWidth,
                                            int nHeight,
                                            HWND hWndParent,
                                            HMENU hMenu,
                                            HINSTANCE hInstance,
                                            LPVOID lpParam);

static CreateWindowExA_type TrueCreateWindowExA = nullptr;

static bool main_window_seen = false;
static HWND jk_window = NULL;
static std::string const jk_window_class_name = "wKernel";

HWND WINAPI HookedCreateWindowExA(DWORD dwExStyle,
                                  LPCSTR lpClassName,
                                  LPCSTR lpWindowName,
                                  DWORD dwStyle,
                                  int X,
                                  int Y,
                                  int nWidth,
                                  int nHeight,
                                  HWND hWndParent,
                                  HMENU hMenu,
                                  HINSTANCE hInstance,
                                  LPVOID lpParam)
{
    if(!main_window_seen) {
        LOG_DEBUG("CreateWindowExA hook called");
    }

    auto rv = TrueCreateWindowExA(dwExStyle,
                                  lpClassName,
                                  lpWindowName,
                                  dwStyle,
                                  X,
                                  Y,
                                  nWidth,
                                  nHeight,
                                  hWndParent,
                                  hMenu,
                                  hInstance,
                                  lpParam);

    if(!main_window_seen && jk_window_class_name == lpClassName) {
        LOG_DEBUG("Detected main window creation");
        main_window_seen = true;

        the_renderer->initialize(hInstance, rv);
    }

    return rv;
}

static HBITMAP const phony_dibsection_bitmap = (HBITMAP)0x70FFFF10;
static std::array<char, 640 * 480> phony_dibsection_memory;

using CreateDIBSection_type = HBITMAP(WINAPI *)(HDC hDC,
                                                BITMAPINFO const *pbmi,
                                                UINT usage,
                                                VOID **ppvBits,
                                                HANDLE hSection,
                                                DWORD offset);
static CreateDIBSection_type TrueCreateDIBSection = nullptr;
HBITMAP WINAPI HookedCreateDIBSection(HDC hDC,
                                      BITMAPINFO const *pbmi,
                                      UINT usage,
                                      VOID **ppvBits,
                                      HANDLE hSection,
                                      DWORD offset)
{
    LOG_DEBUG("CreateDIBSection hook called");
    *ppvBits = phony_dibsection_memory.data();
    the_renderer->set_menu_source(phony_dibsection_bitmap,
                                  reinterpret_cast<char const *>(*ppvBits));
    return phony_dibsection_bitmap;
}

using GetCursorPos_type = BOOL(WINAPI *)(LPPOINT lpPoint);
static GetCursorPos_type TrueGetCursorPos = nullptr;
BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint)
{
    POINT real_pos;
    auto rv = TrueGetCursorPos(&real_pos);

    if(rv != FALSE) {
        auto conv_pos =
            the_renderer->get_cursor_pos(jkgm::make_point((int)real_pos.x, (int)real_pos.y));
        lpPoint->x = (LONG)jkgm::get<0>(conv_pos);
        lpPoint->y = (LONG)jkgm::get<1>(conv_pos);
    }

    return rv;
}

using SetDIBColorTable_type = UINT(WINAPI *)(HDC hDC,
                                             UINT iStart,
                                             UINT cEntries,
                                             RGBQUAD const *prgbq);
static SetDIBColorTable_type TrueSetDIBColorTable = nullptr;
UINT WINAPI HookedSetDIBColorTable(HDC hDC, UINT iStart, UINT cEntries, RGBQUAD const *prgbq)
{
    the_renderer->set_menu_palette(iStart, jkgm::make_span(prgbq, cEntries));
    return TrueSetDIBColorTable(hDC, iStart, cEntries, prgbq);
}

using GdiFlush_type = BOOL(WINAPI *)();
static GdiFlush_type TrueGdiFlush = nullptr;
BOOL WINAPI HookedGdiFlush()
{
    bool rv = TrueGdiFlush();
    the_renderer->present_menu_gdi();
    return rv;
}

using DeleteObject_type = BOOL(WINAPI *)(HGDIOBJ ho);
static DeleteObject_type TrueDeleteObject = nullptr;
BOOL WINAPI HookedDeleteObject(HGDIOBJ ho)
{
    if(ho == (HGDIOBJ)phony_dibsection_bitmap) {
        the_renderer->maybe_clear_menu_source(ho);
        return TRUE;
    }

    return TrueDeleteObject(ho);
}

using ChangeDisplaySettingsA_type = LONG(WINAPI *)(DEVMODEA *lpDevMode, DWORD dwFlags);
static ChangeDisplaySettingsA_type TrueChangeDisplaySettingsA = nullptr;
LONG WINAPI HookedChangeDisplaySettingsA(DEVMODEA *lpDevMode, DWORD dwFlags)
{
    LOG_DEBUG("Suppressing ChangeDisplaySettingsA");
    return DISP_CHANGE_SUCCESSFUL;
}

using ChangeDisplaySettingsW_type = LONG(WINAPI *)(DEVMODEW *lpDevMode, DWORD dwFlags);
static ChangeDisplaySettingsW_type TrueChangeDisplaySettingsW = nullptr;
LONG WINAPI HookedChangeDisplaySettingsW(DEVMODEW *lpDevMode, DWORD dwFlags)
{
    LOG_DEBUG("Called ChangeDisplaySettingsW");
    return TrueChangeDisplaySettingsW(lpDevMode, dwFlags);
}

// COG verbs (distance fog)
//
// The community patch's JK.dll exports InitNewCogVerbs, which jk.exe calls at startup
// with a function for registering COG verbs. We wrap that export: run the original,
// then register our own verbs through the same function.
//
// Verb handlers talk to jk.exe's COG interpreter through a few helper functions inside
// jk.exe, exactly like the handlers in JK.dll do. Those addresses belong to one specific
// build of jk.exe, so before registering anything we compare the first bytes of every
// helper against what we expect. If anything differs we register nothing, and the game
// runs normally without the fog verbs.
//
// COG pushes verb arguments left to right, so a handler pops them in reverse order.

namespace {
    using cog_handler_type = void(__cdecl *)(void *ctx);
    using cog_register_verb_type = void(__cdecl *)(void *ctx,
                                                   cog_handler_type handler,
                                                   char const *name);
    using JkInitNewCogVerbs_type = void(__cdecl *)(cog_register_verb_type register_verb,
                                                   void *ctx);

    using cog_pop_float_type = float(__cdecl *)(void *ctx);
    using cog_pop_int_type = int(__cdecl *)(void *ctx);
    using cog_pop_vector_type = void(__cdecl *)(void *ctx, float *out_xyz);
    using cog_push_float_type = void(__cdecl *)(void *ctx, float value);
    using cog_push_int_type = void(__cdecl *)(void *ctx, int value);
    using cog_push_vector_type = void(__cdecl *)(void *ctx, float const *xyz);

    constexpr uintptr_t pop_float_address = 0x4E2780;
    constexpr uintptr_t pop_int_address = 0x4E2850;
    constexpr uintptr_t pop_vector_address = 0x4E2970;
    constexpr uintptr_t push_float_address = 0x4E3650;
    constexpr uintptr_t push_int_address = 0x4E35D0;
    constexpr uintptr_t push_vector_address = 0x4E36E0;

    cog_pop_float_type const cog_pop_float = reinterpret_cast<cog_pop_float_type>(pop_float_address);
    cog_pop_int_type const cog_pop_int = reinterpret_cast<cog_pop_int_type>(pop_int_address);
    cog_pop_vector_type const cog_pop_vector =
        reinterpret_cast<cog_pop_vector_type>(pop_vector_address);
    cog_push_float_type const cog_push_float =
        reinterpret_cast<cog_push_float_type>(push_float_address);
    cog_push_int_type const cog_push_int = reinterpret_cast<cog_push_int_type>(push_int_address);
    cog_push_vector_type const cog_push_vector =
        reinterpret_cast<cog_push_vector_type>(push_vector_address);

    struct cog_helper_signature {
        uintptr_t address;
        std::array<uint8_t, 8> prologue;
    };

    constexpr std::array<cog_helper_signature, 6> cog_helper_signatures = {{
        {pop_float_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x20, 0x8b}},
        {pop_int_address, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x4d}},
        {pop_vector_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x10, 0x8b}},
        {push_float_address, {0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x56}},
        {push_int_address, {0x83, 0xec, 0x10, 0x53, 0x8b, 0x5c, 0x24, 0x1c}},
        {push_vector_address, {0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x53}},
    }};

    bool cog_helpers_match()
    {
        for(auto const &sig : cog_helper_signatures) {
            MEMORY_BASIC_INFORMATION mbi{};
            if(VirtualQuery(reinterpret_cast<void const *>(sig.address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if(mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
                return false;
            }

            auto const region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if(sig.address + sig.prologue.size() > region_end) {
                return false;
            }

            if(std::memcmp(reinterpret_cast<void const *>(sig.address),
                           sig.prologue.data(),
                           sig.prologue.size()) != 0) {
                return false;
            }
        }

        return true;
    }

    float clamp_float(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // SetFogColor(vector rgb, float fadeTime)
    //   rgb components 0.0 - 1.0, e.g. '0.45 0.50 0.58'
    //   fadeTime in seconds; 0.0 changes the color immediately
    void __cdecl cog_set_fog_color(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx); // last argument comes off first
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, rgb);

        jkgm::get_fog_settings().color.fade_to({clamp_float(rgb[0], 0.0f, 1.0f),
                                                clamp_float(rgb[1], 0.0f, 1.0f),
                                                clamp_float(rgb[2], 0.0f, 1.0f)},
                                               fade_time);
    }

    // vector GetFogColor()    the color showing right now (partway through a fade, if fading)
    void __cdecl cog_get_fog_color(void *ctx)
    {
        auto const rgb = jkgm::get_fog_settings().color.value();
        cog_push_vector(ctx, rgb.data());
    }

    // SetFogInfo(vector info, float fadeTime)
    //   info = 'start end opacity': start distance, end distance, opacity 0.0 - 1.0
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_fog_info(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float info[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, info);

        float const start = info[0] < 0.0f ? 0.0f : info[0];
        float const end = info[1] < start + 0.001f ? start + 0.001f : info[1];
        float const opacity = clamp_float(info[2], 0.0f, 1.0f);

        jkgm::get_fog_settings().info.fade_to({start, end, opacity}, fade_time);
    }

    // vector GetFogInfo()    'start end opacity' showing right now
    void __cdecl cog_get_fog_info(void *ctx)
    {
        auto const info = jkgm::get_fog_settings().info.value();
        cog_push_vector(ctx, info.data());
    }

    // ResetFog()    back to the defaults in fog.hpp (fog disabled), immediately
    void __cdecl cog_reset_fog(void * /*ctx*/)
    {
        jkgm::get_fog_settings() = jkgm::fog_settings();
    }

    // SetSunInfo(vector direction, vector color, float intensity, float animTime)
    // Direction points TOWARD the sun in world space; color is normalized RGB.
    void __cdecl cog_set_sun_info(void *ctx)
    {
        float const duration = cog_pop_float(ctx);
        float const intensity = cog_pop_float(ctx);
        float rgb[3] = {1.0f, 1.0f, 1.0f};
        float dir[3] = {0.0f, 0.0f, 1.0f};
        cog_pop_vector(ctx, rgb);
        cog_pop_vector(ctx, dir);

        if(!std::isfinite(intensity) || !std::isfinite(rgb[0]) ||
           !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]) ||
           !std::isfinite(dir[0]) || !std::isfinite(dir[1]) ||
           !std::isfinite(dir[2])) {
            LOG_WARNING("SetSunInfo received non-finite arguments");
            return;
        }
        float const sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        if(!(sq > 1.0e-10f) || !std::isfinite(sq)) {
            LOG_WARNING("SetSunInfo requires a nonzero finite direction");
            return;
        }

        auto const unit = jkgm::normalized_sun_direction({dir[0],dir[1],dir[2]});
        auto const now = jkgm::effect_clock_now();
        float const time = std::isfinite(duration) ? std::max(duration, 0.0f) : 0.0f;
        auto &sun = jkgm::get_sun_settings();
        sun.direction.fade_to(unit, time, now);
        sun.color.fade_to({clamp_float(rgb[0], 0.0f, 1.0f),
                           clamp_float(rgb[1], 0.0f, 1.0f),
                           clamp_float(rgb[2], 0.0f, 1.0f)}, time, now);
        sun.intensity.fade_to({clamp_float(intensity, 0.0f, 16.0f)}, time, now);
    }

    void __cdecl cog_get_sun_direction(void *ctx)
    {
        auto const dir = jkgm::normalized_sun_direction(
            jkgm::get_sun_settings().direction.value());
        cog_push_vector(ctx, dir.data());
    }
    void __cdecl cog_get_sun_color(void *ctx)
    {
        auto const rgb = jkgm::get_sun_settings().color.value();
        cog_push_vector(ctx, rgb.data());
    }
    void __cdecl cog_get_sun_intensity(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_sun_settings().intensity.value()[0]);
    }
    void __cdecl cog_reset_sun(void * /*ctx*/)
    {
        jkgm::get_sun_settings() = jkgm::sun_settings();
    }

    // SetFilmGrain(vector params, float fadeTime)
    //   params = 'amount size rate'
    //     amount: 0.0 - 1.0, 0 turns grain off (0.05 - 0.15 is a typical range)
    //     size:   grain size in screen pixels, 1.0 or more
    //     rate:   grain pattern changes per second, 0 freezes it (24 looks filmic)
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_film_grain(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 1.0f, 24.0f};
        cog_pop_vector(ctx, params);

        jkgm::get_film_grain_settings().params.fade_to(
            {clamp_float(params[0], 0.0f, 1.0f),
             params[1] < 1.0f ? 1.0f : params[1],
             params[2] < 0.0f ? 0.0f : params[2]},
            fade_time);
    }

    // vector GetFilmGrain()    'amount size rate' showing right now
    void __cdecl cog_get_film_grain(void *ctx)
    {
        auto const params = jkgm::get_film_grain_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetFilmGrain()    grain off, immediately
    void __cdecl cog_reset_film_grain(void * /*ctx*/)
    {
        jkgm::get_film_grain_settings() = jkgm::film_grain_settings();
    }

    // SetVignette(vector rgb, float strength, float fadeTime)
    //   rgb:      color the screen edges are pulled towards, 0.0 - 1.0 each
    //             ('0 0 0' is the classic dark vignette, '1 0 0' a red damage flash)
    //   strength: 0.0 - 2.0; 0 = off, 1 = classic vignette, between 1 and 2 it closes in like an
    //             iris until the whole screen is the vignette color at 2
    //   fadeTime: seconds; 0.0 changes the values immediately
    void __cdecl cog_set_vignette(void *ctx)
    {
        // Arguments come off in reverse order: fadeTime, strength, then rgb.
        float const fade_time = cog_pop_float(ctx);
        float const strength = clamp_float(cog_pop_float(ctx), 0.0f, 2.0f);
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, rgb);

        std::array<float, 4> const target = {clamp_float(rgb[0], 0.0f, 1.0f),
                                             clamp_float(rgb[1], 0.0f, 1.0f),
                                             clamp_float(rgb[2], 0.0f, 1.0f),
                                             strength};

        auto &params = jkgm::get_vignette_settings().params;

        // If the vignette is invisible right now, take on the new color straight away and fade
        // only the strength. Otherwise fading in from black to red would pass through dark red.
        auto start = params.value();
        if(start[3] <= 0.001f) {
            start[0] = target[0];
            start[1] = target[1];
            start[2] = target[2];
            params.set(start);
        }

        params.fade_to(target, fade_time);
    }

    // vector GetVignetteRGB()    the edge color right now
    void __cdecl cog_get_vignette_rgb(void *ctx)
    {
        auto const params = jkgm::get_vignette_settings().params.value();
        float const rgb[3] = {params[0], params[1], params[2]};
        cog_push_vector(ctx, rgb);
    }

    // float GetVignetteStrength()    the strength right now
    void __cdecl cog_get_vignette_strength(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_vignette_settings().params.value()[3]);
    }

    // ResetVignette()    black edges at full strength (the original look), immediately
    void __cdecl cog_reset_vignette(void * /*ctx*/)
    {
        jkgm::get_vignette_settings() = jkgm::vignette_settings();
    }

    // SetColorFilter(vector params, float fadeTime)
    //   params = 'brightness contrast saturation'
    //     brightness: -1.0 - 1.0 (0 = unchanged)
    //     contrast:   0.0 - 4.0 (1 = unchanged)
    //     saturation: 0.0 - 4.0 (1 = unchanged, 0 = black and white)
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_color_filter(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 1.0f, 1.0f};
        cog_pop_vector(ctx, params);

        jkgm::get_color_filter_settings().params.fade_to({clamp_float(params[0], -1.0f, 1.0f),
                                                          clamp_float(params[1], 0.0f, 4.0f),
                                                          clamp_float(params[2], 0.0f, 4.0f)},
                                                         fade_time);
    }

    // vector GetColorFilter()    'brightness contrast saturation' right now
    void __cdecl cog_get_color_filter(void *ctx)
    {
        auto const params = jkgm::get_color_filter_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetColorFilter()    back to the defaults in postfx.hpp, immediately
    void __cdecl cog_reset_color_filter(void * /*ctx*/)
    {
        jkgm::get_color_filter_settings() = jkgm::color_filter_settings();
    }

    // SetFrameTrail(float strength, float fadeTime)
    //   strength: 0 = no history; 1 = freeze the last composited frame
    //   fadeTime: interpolation time in seconds (0 = immediate)
    void __cdecl cog_set_frame_trail(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float const strength = clamp_float(cog_pop_float(ctx), 0.0f, 1.0f);
        jkgm::get_frame_blend_settings().strength.fade_to({strength}, fade_time);
    }

    // float GetFrameTrail()
    void __cdecl cog_get_frame_trail(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_frame_blend_settings().strength.value()[0]);
    }

    // ResetFrameTrail() = disable temporal blending and discard old frame history.
    void __cdecl cog_reset_frame_trail(void * /*ctx*/)
    {
        jkgm::get_frame_blend_settings().reset();
    }

    // SetDistanceBlur(vector(start,end,strength), float fadeTime)
    // Distances in JK units, strength 0..1.
    void __cdecl cog_set_distance_blur(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, params);

        float const start = std::max(params[0], 0.0f);
        float const end = std::max(params[1], start + 0.001f);
        jkgm::get_distance_blur_settings().params.fade_to(
            {start, end, clamp_float(params[2], 0.0f, 1.0f)}, fade_time);
    }

    // vector GetDistanceBlur()
    void __cdecl cog_get_distance_blur(void *ctx)
    {
        auto const params = jkgm::get_distance_blur_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetDistanceBlur()
    void __cdecl cog_reset_distance_blur(void * /*ctx*/)
    {
        jkgm::get_distance_blur_settings() = jkgm::distance_blur_settings();
    }

    // SetPeripheralBlur(vector(inner,outer,strength), float fadeTime)
    // Radii are normalized screen-center-to-corner distances, 0..1.
    void __cdecl cog_set_peripheral_blur(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, params);

        float const inner = clamp_float(params[0], 0.0f, 1.0f);
        float const outer = clamp_float(params[1], inner + 0.001f, 1.001f);
        jkgm::get_peripheral_blur_settings().params.fade_to(
            {inner, outer, clamp_float(params[2], 0.0f, 1.0f)}, fade_time);
    }

    // vector GetPeripheralBlur()
    void __cdecl cog_get_peripheral_blur(void *ctx)
    {
        auto const params = jkgm::get_peripheral_blur_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetPeripheralBlur()
    void __cdecl cog_reset_peripheral_blur(void * /*ctx*/)
    {
        jkgm::get_peripheral_blur_settings() = jkgm::peripheral_blur_settings();
    }

    // Associate commands issued from a level's startup COG with that world
    // *before* the renderer's next frame notices the level transition.
    jkgm::runtime_material_controls &cog_material_controls()
    {
        uint32_t world = 0;
        if(jkgm::engine_material_debug::matches_tested_executable() &&
           jkgm::engine_material_debug::read_object(
               jkgm::engine_material_debug::current_world_global, world)) {
            jkgm::get_runtime_material_controls().prepare_world(world);
        }
        return jkgm::get_runtime_material_controls();
    }

    // Runtime material effects use packed JK MAT indices. This is the same
    // integer embedded in a COG material variable; PopMaterial in the engine
    // returns its native pointer, but PopInt returns its stable numeric index.
    bool valid_mat_index(int id)
    {
        return id >= 0 && id <= 65535 && (id & 0x7fff) < 16384;
    }

    bool valid_image_id(int id)
    {
        return id >= 0 && id <= 65535;
    }

    void __cdecl cog_set_global_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int source = cog_pop_int(ctx);
        if(valid_mat_index(source) && valid_image_id(image)) {
            cog_material_controls().set_global_env(
                static_cast<uint32_t>(source), image, fade);
            LOG_DEBUG("COG SetGlobalEnvMap: source MAT=", source,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetGlobalEnvMap rejected: source MAT=", source,
                         " image=", image);
    }

    void __cdecl cog_reset_global_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        cog_material_controls().reset_global_env(fade);
        LOG_DEBUG("COG ResetGlobalEnvMap: fade=", std::to_string(fade));
    }

    void __cdecl cog_set_mat_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat) && valid_image_id(image)) {
            cog_material_controls().set_mat_env(
                static_cast<uint32_t>(mat), static_cast<uint32_t>(mat), image, fade);
            LOG_DEBUG("COG SetMatEnvMap: MAT=", mat, " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMatEnvMap rejected: MAT=", mat, " image=", image);
    }

    // Resolve image in sourceMat's pack, apply to targetMat.
    void __cdecl cog_set_mat_env_map_from(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int source = cog_pop_int(ctx);
        int target = cog_pop_int(ctx);
        if(valid_mat_index(target) && valid_mat_index(source) && valid_image_id(image)) {
            cog_material_controls().set_mat_env(
                static_cast<uint32_t>(target), static_cast<uint32_t>(source), image, fade);
            LOG_DEBUG("COG SetMatEnvMapFrom: target=", target, " source=", source,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMatEnvMapFrom rejected: target=", target,
                         " source=", source, " image=", image);
    }

    void __cdecl cog_reset_mat_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_env(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMatEnvMap: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMatEnvMap rejected: MAT=", mat);
    }

    void __cdecl cog_set_material_albedo(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat) && valid_image_id(image)) {
            cog_material_controls().set_mat_albedo(
                static_cast<uint32_t>(mat), image, fade);
            LOG_DEBUG("COG SetMaterialAlbedo: MAT=", mat,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMaterialAlbedo rejected: MAT=", mat,
                         " image=", image);
    }

    void __cdecl cog_reset_material_albedo(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_albedo(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMaterialAlbedo: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMaterialAlbedo rejected: MAT=", mat);
    }

    void __cdecl cog_set_material_color(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        float rgb[3]{1.0f, 1.0f, 1.0f};
        cog_pop_vector(ctx, rgb);
        int mat = cog_pop_int(ctx);
        if(!valid_mat_index(mat) ||
           !std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2])) {
            LOG_WARNING("COG SetMaterialColor rejected: MAT=", mat,
                        " rgb=", std::to_string(rgb[0]), ",", std::to_string(rgb[1]), ",", std::to_string(rgb[2]));
            return;
        }
        cog_material_controls().set_mat_color(
            static_cast<uint32_t>(mat),
            {{clamp_float(rgb[0], 0.0f, 4.0f),
              clamp_float(rgb[1], 0.0f, 4.0f),
              clamp_float(rgb[2], 0.0f, 4.0f)}},
            fade);
        LOG_DEBUG("COG SetMaterialColor: MAT=", mat,
                  " rgb=", std::to_string(rgb[0]), ",", std::to_string(rgb[1]), ",", std::to_string(rgb[2]), " fade=", std::to_string(fade));
    }

    void __cdecl cog_reset_material_color(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_color(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMaterialColor: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMaterialColor rejected: MAT=", mat);
    }

    struct cog_verb_entry {
        char const *name;
        cog_handler_type handler;
    };

    // The array size is deduced from the list, so adding or removing a verb can never leave
    // empty entries behind (those would be registered with a null name and handler).
    constexpr std::array fx_verbs = {
        cog_verb_entry{"setfogcolor", cog_set_fog_color},
        cog_verb_entry{"getfogcolor", cog_get_fog_color},
        cog_verb_entry{"setfoginfo", cog_set_fog_info},
        cog_verb_entry{"getfoginfo", cog_get_fog_info},
        cog_verb_entry{"resetfog", cog_reset_fog},
        cog_verb_entry{"setsuninfo", cog_set_sun_info},
        cog_verb_entry{"getsundirection", cog_get_sun_direction},
        cog_verb_entry{"getsuncolor", cog_get_sun_color},
        cog_verb_entry{"getsunintensity", cog_get_sun_intensity},
        cog_verb_entry{"resetsun", cog_reset_sun},
        cog_verb_entry{"setfilmgrain", cog_set_film_grain},
        cog_verb_entry{"getfilmgrain", cog_get_film_grain},
        cog_verb_entry{"resetfilmgrain", cog_reset_film_grain},
        cog_verb_entry{"setvignette", cog_set_vignette},
        cog_verb_entry{"getvignettergb", cog_get_vignette_rgb},
        cog_verb_entry{"getvignettestrength", cog_get_vignette_strength},
        cog_verb_entry{"resetvignette", cog_reset_vignette},
        cog_verb_entry{"setcolorfilter", cog_set_color_filter},
        cog_verb_entry{"getcolorfilter", cog_get_color_filter},
        cog_verb_entry{"resetcolorfilter", cog_reset_color_filter},
        cog_verb_entry{"setframetrail", cog_set_frame_trail},
        cog_verb_entry{"getframetrail", cog_get_frame_trail},
        cog_verb_entry{"resetframetrail", cog_reset_frame_trail},
        cog_verb_entry{"setdistanceblur", cog_set_distance_blur},
        cog_verb_entry{"getdistanceblur", cog_get_distance_blur},
        cog_verb_entry{"resetdistanceblur", cog_reset_distance_blur},
        cog_verb_entry{"setperipheralblur", cog_set_peripheral_blur},
        cog_verb_entry{"getperipheralblur", cog_get_peripheral_blur},
        cog_verb_entry{"resetperipheralblur", cog_reset_peripheral_blur},
        cog_verb_entry{"setglobalenvmap", cog_set_global_env_map},
        cog_verb_entry{"resetglobalenvmap", cog_reset_global_env_map},
        cog_verb_entry{"setmatenvmap", cog_set_mat_env_map},
        cog_verb_entry{"setmatenvmapfrom", cog_set_mat_env_map_from},
        cog_verb_entry{"resetmatenvmap", cog_reset_mat_env_map},
        cog_verb_entry{"setmaterialalbedo", cog_set_material_albedo},
        cog_verb_entry{"resetmaterialalbedo", cog_reset_material_albedo},
        cog_verb_entry{"setmaterialcolor", cog_set_material_color},
        cog_verb_entry{"resetmaterialcolor", cog_reset_material_color},
    };

    JkInitNewCogVerbs_type TrueJkInitNewCogVerbs = nullptr;

    void __cdecl HookedJkInitNewCogVerbs(cog_register_verb_type register_verb, void *ctx)
    {
        TrueJkInitNewCogVerbs(register_verb, ctx);

        if(!cog_helpers_match()) {
            LOG_ERROR("jk.exe does not match the build the fog COG verbs were written for; "
                      "fog COG verbs not registered");
            return;
        }

        for(auto const &verb : fx_verbs) {
            register_verb(ctx, verb.handler, verb.name);
        }

        LOG_DEBUG("Registered fog, sunlight, post-processing, and material COG verbs");
    }
}

bool attach_hooks()
{
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Injected functions

    // COG verbs, if the community patch's JK.dll is loaded (its export #1 is InitNewCogVerbs)
    if(HMODULE jk_dll = GetModuleHandleA("JK.dll")) {
        TrueJkInitNewCogVerbs =
            (JkInitNewCogVerbs_type)GetProcAddress(jk_dll, MAKEINTRESOURCEA(1));
        if(TrueJkInitNewCogVerbs != nullptr) {
            DetourAttach(&(PVOID &)TrueJkInitNewCogVerbs, HookedJkInitNewCogVerbs);
        }
        else {
            LOG_DEBUG("JK.dll does not export InitNewCogVerbs; fog COG verbs unavailable");
        }
    }
    else {
        LOG_DEBUG("JK.dll is not loaded; fog COG verbs unavailable");
    }

    // DirectX
    TrueDirectDrawCreate =
        (DirectDrawCreate_type)DetourFindFunction("ddraw.dll", "DirectDrawCreate");
    DetourAttach(&(PVOID &)TrueDirectDrawCreate, HookedDirectDrawCreate);

    TrueDirectDrawEnumerateA =
        (DirectDrawEnumerateA_type)DetourFindFunction("ddraw.dll", "DirectDrawEnumerateA");
    DetourAttach(&(PVOID &)TrueDirectDrawEnumerateA, HookedDirectDrawEnumerateA);

    // Win32 API
    TrueCreateWindowExA = (CreateWindowExA_type)DetourFindFunction("user32.dll", "CreateWindowExA");
    DetourAttach(&(PVOID &)TrueCreateWindowExA, HookedCreateWindowExA);

    TrueGetCursorPos = (GetCursorPos_type)DetourFindFunction("user32.dll", "GetCursorPos");
    DetourAttach(&(PVOID &)TrueGetCursorPos, HookedGetCursorPos);

    TrueCreateDIBSection =
        (CreateDIBSection_type)DetourFindFunction("gdi32.dll", "CreateDIBSection");
    DetourAttach(&(PVOID &)TrueCreateDIBSection, HookedCreateDIBSection);

    TrueSetDIBColorTable =
        (SetDIBColorTable_type)DetourFindFunction("gdi32.dll", "SetDIBColorTable");
    DetourAttach(&(PVOID &)TrueSetDIBColorTable, HookedSetDIBColorTable);

    TrueGdiFlush = (GdiFlush_type)DetourFindFunction("gdi32.dll", "GdiFlush");
    DetourAttach(&(PVOID &)TrueGdiFlush, HookedGdiFlush);

    TrueDeleteObject = (DeleteObject_type)DetourFindFunction("gdi32.dll", "DeleteObject");
    DetourAttach(&(PVOID &)TrueDeleteObject, HookedDeleteObject);

    TrueChangeDisplaySettingsA =
        (ChangeDisplaySettingsA_type)DetourFindFunction("user32.dll", "ChangeDisplaySettingsA");
    DetourAttach(&(PVOID &)TrueChangeDisplaySettingsA, HookedChangeDisplaySettingsA);

    TrueChangeDisplaySettingsW =
        (ChangeDisplaySettingsW_type)DetourFindFunction("user32.dll", "ChangeDisplaySettingsW");
    DetourAttach(&(PVOID &)TrueChangeDisplaySettingsW, HookedChangeDisplaySettingsW);

    LONG error = DetourTransactionCommit();
    return error == NO_ERROR;
}

bool detach_hooks()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Detach injected functions
    if(TrueJkInitNewCogVerbs != nullptr) {
        DetourDetach(&(PVOID &)TrueJkInitNewCogVerbs, HookedJkInitNewCogVerbs);
    }
    DetourDetach(&(PVOID &)TrueDirectDrawCreate, HookedDirectDrawCreate);
    DetourDetach(&(PVOID &)TrueDirectDrawEnumerateA, HookedDirectDrawEnumerateA);
    DetourDetach(&(PVOID &)TrueCreateWindowExA, HookedCreateWindowExA);
    DetourDetach(&(PVOID &)TrueGetCursorPos, HookedGetCursorPos);
    DetourDetach(&(PVOID &)TrueCreateDIBSection, HookedCreateDIBSection);
    DetourDetach(&(PVOID &)TrueSetDIBColorTable, HookedSetDIBColorTable);
    DetourDetach(&(PVOID &)TrueGdiFlush, HookedGdiFlush);
    DetourDetach(&(PVOID &)TrueDeleteObject, HookedDeleteObject);
    DetourDetach(&(PVOID &)TrueChangeDisplaySettingsA, HookedChangeDisplaySettingsA);
    DetourDetach(&(PVOID &)TrueChangeDisplaySettingsW, HookedChangeDisplaySettingsW);

    LONG error = DetourTransactionCommit();
    return error == NO_ERROR;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    if(DetourIsHelperProcess()) {
        return TRUE;
    }

    if(ul_reason_for_call == DLL_PROCESS_ATTACH) {
        jkgm::setup_default_logging();
        the_config = jkgm::load_config_file();

        if(the_config->log_path.has_value()) {
            jkgm::add_file_logging(*the_config->log_path);
        }

        // The dedicated JKGM executable can load renderer.dll before the
        // original game entrypoint. Keep the built-in Direct3D renderer fully
        // untouched when explicitly requested, including COG/Win32 hooks.
        if(the_config->use_vanilla_renderer) {
            LOG_INFO("use_vanilla_renderer=true: native JK Direct3D path selected");
            return TRUE;
        }

        the_renderer = jkgm::create_renderer(hModule, the_config.get());

        LOG_DEBUG("Attaching renderer to process");

        if(!attach_hooks()) {
            LOG_ERROR("Failed to commit Detours hook transaction");
            jkgm::report_error_message(
                "Critical failure: JkGfxMod failed to attach to this application");
            return FALSE;
        }

        renderer_hooks_active = true;
        if(the_config->profile_renderer || the_config->enable_mesh_bounds_culling) {
            native_3do_hooks_active = attach_native_3do_hooks(
                the_config->profile_renderer, the_config->enable_mesh_bounds_culling);
        }
        LOG_DEBUG("Finished attaching renderer to process");
    }
    else if(ul_reason_for_call == DLL_PROCESS_DETACH && renderer_hooks_active) {
        if(native_3do_hooks_active) {
            detach_native_3do_hooks();
            native_3do_hooks_active = false;
        }
        detach_hooks();
        renderer_hooks_active = false;
    }

    return TRUE;
}
