#include "native_compressed_sound_hook.hpp"

#include "base/log.hpp"
#include "compressed_sound_io.hpp"
#include <Windows.h>
#include <detours/detours.h>

namespace {
using sith_sound_load_fn=void* (__cdecl*)(char*,int);
sith_sound_load_fn RealSithSoundLoad=
    reinterpret_cast<sith_sound_load_fn>(0x004EF440U);

void* __cdecl HookedSithSoundLoad(char* filename,int eager)
{
    // HostServices callbacks can be reset by JK's resource initialization.
    // Revalidate/rebind on each load before calling the native loader.
    if(!jkgm::install_compressed_sound_file_services()) {
        static bool warned=false;
        if(!warned) {
            warned=true;
            LOG_WARNING("JKGM compressed sound bridge unavailable: HostServices "
                        "table mismatch or not yet initialized");
        }
    }
    return RealSithSoundLoad(filename,eager);
}
}

namespace jkgm::native_compressed_sound_hook {
bool request()
{
    if(!jkgm::compressed_sound_executable_supported()) {
        LOG_WARNING("JKGM compressed sound hook skipped: unsupported EXE");
        return false;
    }
    LONG const err=DetourAttach(&(PVOID&)RealSithSoundLoad,
                                HookedSithSoundLoad);
    if(err!=NO_ERROR) {
        LOG_WARNING("JKGM compressed sound hook attach failed: ",err);
        return false;
    }
    return true;
}

void detach()
{
    DetourDetach(&(PVOID&)RealSithSoundLoad,HookedSithSoundLoad);
}
}
