#pragma once
// Passive JKGM audio diagnostics. Never hooks, loads, creates or modifies
// DirectSound or A3D objects; all engine reads are gated on the audited EXE.
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

namespace jkgm {
class audio_module_probe {
    DWORD last_sample_=0;
    bool sampled_=false;
    std::string previous_;

    static bool readable(uintptr_t address,size_t length)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if(VirtualQuery(reinterpret_cast<void const *>(address),&mbi,
                        sizeof(mbi))!=sizeof(mbi))return false;
        if(mbi.State!=MEM_COMMIT ||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0)return false;
        DWORD const protection=mbi.Protect&0xffU;
        if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE &&
           protection!=PAGE_WRITECOPY && protection!=PAGE_EXECUTE_READ &&
           protection!=PAGE_EXECUTE_READWRITE &&
           protection!=PAGE_EXECUTE_WRITECOPY)return false;
        uintptr_t const base=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if(address<base || length>mbi.RegionSize)return false;
        return address-base<=mbi.RegionSize-length;
    }

    static std::string engine_a3d_snapshot()
    {
        // Audited jkgm(5).exe, with fixed image base 0x00400000:
        // 0x437140 - sound-device initializer
        // 0x563744 - sound-device COM interface (A3D or DS fallback)
        // 0x563754 - engine's A3D-path indicator
        // 0x563758 - A3D-only secondary COM interface
        // 0x56375c - primary sound-buffer interface
        // 0x563760 - additional A3D interface
        // These pointers are NOT proof of hardware A3D or HRTF.
        constexpr uintptr_t signature_address=0x00437140U;
        constexpr std::array<uint8_t,10> signature{{
            0x33,0xc0,0x39,0x05,0x44,0x37,0x56,0x00,0x74,0x09
        }};
        constexpr uintptr_t state_address=0x00563744U;
        constexpr size_t state_bytes=0x20U;
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=
               0x00400000U ||
           !readable(signature_address,signature.size()) ||
           !readable(state_address,state_bytes) ||
           std::memcmp(reinterpret_cast<void const *>(signature_address),
                       signature.data(),signature.size())!=0)
            return "A3D engine state: unsupported executable/signature";

        auto read32=[](uintptr_t address)->uint32_t {
            uint32_t value=0;
            std::memcpy(&value,reinterpret_cast<void const *>(address),
                        sizeof(value));
            return value;
        };
        uint32_t const device=read32(0x00563744U);
        uint32_t const flag=read32(0x00563754U);
        uint32_t const secondary=read32(0x00563758U);
        uint32_t const primary=read32(0x0056375cU);
        uint32_t const extension=read32(0x00563760U);
        std::ostringstream report;
        report<<std::hex
              <<"A3D engine state: sound_device=0x"<<device
              <<" a3d_path_flag="<<std::dec<<flag<<std::hex
              <<" a3d_secondary=0x"<<secondary
              <<" primary_buffer=0x"<<primary
              <<" a3d_extension=0x"<<extension;
        return report.str();
    }

public:
    std::optional<std::string> poll()
    {
        DWORD const now=GetTickCount();
        if(sampled_ && DWORD(now-last_sample_)<1500U)return std::nullopt;
        last_sample_=now;
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
            if(std::strcmp(name,"a3d.dll")==0)
                current<<"DllGetClassObject_export="
                       <<(GetProcAddress(dll,"DllGetClassObject")?1:0)<<" ";
        }
        if(!any)current<<"none ";
        current<<engine_a3d_snapshot();
        std::string const value=current.str();
        bool const changed=!sampled_ || value!=previous_;
        sampled_=true;
        if(!changed)return std::nullopt;
        previous_=value;
        return value;
    }
};
}
