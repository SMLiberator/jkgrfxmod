#include "base/default_logger.hpp"
#include "base/log.hpp"
#include "base/span.hpp"
#include "common/config.hpp"
#include "common/error_reporter.hpp"
#include "fog.hpp"
#include "renderer.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <detours/detours.h>

static std::unique_ptr<jkgm::renderer> the_renderer;
static std::unique_ptr<jkgm::config> the_config;

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

    // SetFogEnabled(int enabled)
    void __cdecl cog_set_fog_enabled(void *ctx)
    {
        jkgm::get_fog_settings().enabled = cog_pop_int(ctx) != 0;
    }

    // int GetFogEnabled()
    void __cdecl cog_get_fog_enabled(void *ctx)
    {
        cog_push_int(ctx, jkgm::get_fog_settings().enabled ? 1 : 0);
    }

    // SetFogColor(vector rgb)    components 0.0 - 1.0, e.g. '0.45 0.50 0.58'
    void __cdecl cog_set_fog_color(void *ctx)
    {
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, rgb);

        auto &fog = jkgm::get_fog_settings();
        fog.color_r = clamp_float(rgb[0], 0.0f, 1.0f);
        fog.color_g = clamp_float(rgb[1], 0.0f, 1.0f);
        fog.color_b = clamp_float(rgb[2], 0.0f, 1.0f);
    }

    // vector GetFogColor()
    void __cdecl cog_get_fog_color(void *ctx)
    {
        auto const &fog = jkgm::get_fog_settings();
        float const rgb[3] = {fog.color_r, fog.color_g, fog.color_b};
        cog_push_vector(ctx, rgb);
    }

    // SetFogInfo(vector info)		start distance, end distance, opacity
    void __cdecl cog_set_fog_info(void *ctx)
    {
        float info[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, info);

        auto &fog = jkgm::get_fog_settings();
        fog.start = info[0] < 0.0f ? 0.0f : info[0];
        fog.end = info[1] < fog.start + 0.001f ? fog.start + 0.001f : info[1];
        fog.opacity = clamp_float(info[2], 0.0f, 1.0f);
    }

    // vector GetFogInfo()
    void __cdecl cog_get_fog_info(void *ctx)
    {
        auto const &fog = jkgm::get_fog_settings();
        float const info[3] = {fog.start, fog.end, fog.opacity};
        cog_push_vector(ctx, rgb);
    }

    // ResetFog()    back to the defaults in fog.hpp (fog disabled)
    void __cdecl cog_reset_fog(void * /*ctx*/)
    {
        jkgm::get_fog_settings() = jkgm::fog_settings();
    }

    struct cog_verb_entry {
        char const *name;
        cog_handler_type handler;
    };

    constexpr std::array<cog_verb_entry, 10> fog_verbs = {{
        {"setfogenabled", cog_set_fog_enabled},
        {"getfogenabled", cog_get_fog_enabled},
        {"setfogcolor", cog_set_fog_color},
        {"getfogcolor", cog_get_fog_color},
        {"setfoginfo", cog_set_fog_info},
        {"getfoginfo", cog_get_fog_info},
        {"resetfog", cog_reset_fog},
    }};

    JkInitNewCogVerbs_type TrueJkInitNewCogVerbs = nullptr;

    void __cdecl HookedJkInitNewCogVerbs(cog_register_verb_type register_verb, void *ctx)
    {
        TrueJkInitNewCogVerbs(register_verb, ctx);

        if(!cog_helpers_match()) {
            LOG_ERROR("jk.exe does not match the build the fog COG verbs were written for; "
                      "fog COG verbs not registered");
            return;
        }

        for(auto const &verb : fog_verbs) {
            register_verb(ctx, verb.handler, verb.name);
        }

        LOG_DEBUG("Registered fog COG verbs");
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

        the_renderer = jkgm::create_renderer(hModule, the_config.get());

        LOG_DEBUG("Attaching renderer to process");

        if(!attach_hooks()) {
            LOG_ERROR("Failed to commit Detours hook transaction");
            jkgm::report_error_message(
                "Critical failure: JkGfxMod failed to attach to this application");
            return FALSE;
        }

        LOG_DEBUG("Finished attaching renderer to process");
    }
    else if(ul_reason_for_call == DLL_PROCESS_DETACH) {
        detach_hooks();
    }

    return TRUE;
}
