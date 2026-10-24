#pragma once
// Non-invasive sound backend diagnostics for JKGM. Deliberately does NOT
// load DLLs, create devices or patch the engine's audio playback routines.
#include <Windows.h>
#include <array>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

namespace jkgm {
class audio_module_probe {
    DWORD last_sample_=0;
    bool sampled_=false;
    std::string previous_;
public:
    std::optional<std::string> poll()
    {
        DWORD const now=GetTickCount();
        if(sampled_ && DWORD(now-last_sample_)<1500U)return std::nullopt;
        last_sample_=now;
        // Module presence proves that a DLL was loaded, not that A3D is
        // enabled, that a 3D sound buffer exists, or that HRTF is working.
        constexpr std::array<char const *,7> names{{
            "dsound.dll","a3d.dll","a3dapi.dll","a3dsnd.dll",
            "dsoal-aldrv.dll","openal32.dll","winmm.dll"
        }};
        std::ostringstream current;
        bool any=false;
        for(auto const *name:names) {
            HMODULE const dll=GetModuleHandleA(name);
            if(!dll)continue;
            any=true;
            char location[MAX_PATH]{};
            DWORD const length=GetModuleFileNameA(dll,location,MAX_PATH);
            current<<name<<"=[";
            if(length>0 && length<MAX_PATH)current<<location;
            else current<<"<path unavailable>";
            current<<"] ";
            if(std::strcmp(name,"dsound.dll")==0)
                current<<"DirectSoundCreate_export="
                       <<(GetProcAddress(dll,"DirectSoundCreate")?1:0)<<" ";
        }
        if(!any)current<<"none";
        std::string const value=current.str();
        bool const changed=!sampled_ || value!=previous_;
        sampled_=true;
        if(!changed)return std::nullopt;
        previous_=value;
        return value;
    }
};
}
