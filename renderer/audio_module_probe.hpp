#pragma once
// JKGM audio diagnostics: engine reads plus a read-only listener COM getter.
// Never hooks, loads or creates DirectSound/A3D objects. The COM query is only
// made for an audited EXE and a vtable method owned by loaded dsound.dll.
#include <Windows.h>
#include <dsound.h>
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
        // 0x563760 - DirectSound3D listener interface
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
        uint32_t const listener=read32(0x00563760U);
        std::ostringstream report;
        report<<std::hex
              <<"A3D engine state: sound_device=0x"<<device
              <<" a3d_path_flag="<<std::dec<<flag<<std::hex
              <<" a3d_secondary=0x"<<secondary
              <<" primary_buffer=0x"<<primary
              <<" ds3d_listener=0x"<<listener;


        // Verify that the interface's GetAllParameters vtable entry is
        // implemented inside the already loaded DSOUND.dll. This is a
        // read-only COM getter; it does NOT create devices, call setters,
        // rewrite vtables or alter the game's audio state. Do not call an
        // unknown third-party implementation through an untrusted pointer.
        if(listener && readable(listener,sizeof(uint32_t))) {
            uint32_t const vtable=read32(listener);
            constexpr uintptr_t get_all_index=3U*sizeof(uint32_t);
            if(vtable && readable(uintptr_t(vtable)+get_all_index,
                                  sizeof(uint32_t))) {
                uint32_t const method=
                    read32(uintptr_t(vtable)+get_all_index);
                MEMORY_BASIC_INFORMATION method_info{};
                HMODULE const dsound=GetModuleHandleA("dsound.dll");
                if(dsound && method &&
                   VirtualQuery(reinterpret_cast<void const *>(
                       uintptr_t(method)),&method_info,
                       sizeof(method_info))==sizeof(method_info) &&
                   method_info.State==MEM_COMMIT &&
                   method_info.AllocationBase==dsound &&
                   (method_info.Protect&(PAGE_GUARD|PAGE_NOACCESS))==0 &&
                   (method_info.Protect&0xffU)==PAGE_EXECUTE_READ) {
                    auto *iface=reinterpret_cast<IDirectSound3DListener *>(
                        uintptr_t(listener));
                    DS3DLISTENER actual{};
                    actual.dwSize=sizeof(actual);
                    HRESULT const hr=iface->GetAllParameters(&actual);
                    if(SUCCEEDED(hr)) {
                        auto const precision=report.precision();
                        report<<std::dec<<std::fixed;
                        report.precision(2);
                        report<<" ds3d_actual_pos=("
                              <<actual.vPosition.x<<","
                              <<actual.vPosition.y<<","
                              <<actual.vPosition.z<<")"
                              <<" ds3d_actual_front=("
                              <<actual.vOrientFront.x<<","
                              <<actual.vOrientFront.y<<","
                              <<actual.vOrientFront.z<<")"
                              <<" ds3d_actual_up=("
                              <<actual.vOrientTop.x<<","
                              <<actual.vOrientTop.y<<","
                              <<actual.vOrientTop.z<<")";
                        report.unsetf(std::ios_base::floatfield);
                        report.precision(precision);
                        report<<std::hex;
                    } else {
                        report<<" ds3d_readback_hr=0x"<<std::hex
                              <<static_cast<uint32_t>(hr);
                    }
                } else {
                    report<<" ds3d_readback=unverified-dsound-method";
                }
            } else {
                report<<" ds3d_readback=invalid-vtable";
            }
        } else {
            report<<" ds3d_readback=no-listener";
        }

        // Audio tick (0x004DC580) supplies the listener's orientation and
        // position to 0x00437770. Sample the ORIGINAL engine-side inputs
        // instead of calling COM methods or patching the game's code.
        constexpr uintptr_t tick_address=0x004DC580U;
        constexpr std::array<uint8_t,8> tick_signature{{
            0xa1,0x4c,0xf1,0x82,0x00,0x83,0xec,0x10
        }};
        constexpr uintptr_t listener_call=0x004DC9CEU;
        constexpr std::array<uint8_t,5> call_signature{{
            0xe8,0x9d,0xad,0xf5,0xff
        }};
        if(readable(tick_address,tick_signature.size()) &&
           readable(listener_call,call_signature.size()) &&
           std::memcmp(reinterpret_cast<void const *>(tick_address),
                       tick_signature.data(),tick_signature.size())==0 &&
           std::memcmp(reinterpret_cast<void const *>(listener_call),
                       call_signature.data(),call_signature.size())==0 &&
           readable(0x0082F14CU,sizeof(uint32_t))) {
            uint32_t const camera=read32(0x0082F14CU);
            report<<" listener_input_camera=0x"<<camera;
            if(readable(0x0083602CU,sizeof(uint32_t)))
                report<<" sound_slots="<<std::dec
                      <<read32(0x0083602CU)<<std::hex;
            if(camera && readable(uintptr_t(camera)+0x40U,0x30U)) {
                float front[3]{},up[3]{},position[3]{};
                std::memcpy(front,reinterpret_cast<void const *>(
                    uintptr_t(camera)+0x40U),sizeof(front));
                std::memcpy(up,reinterpret_cast<void const *>(
                    uintptr_t(camera)+0x4CU),sizeof(up));
                std::memcpy(position,reinterpret_cast<void const *>(
                    uintptr_t(camera)+0x64U),sizeof(position));
                // Rounded values keep the low-frequency log readable.
                auto const previous_precision=report.precision();
                report<<std::dec<<std::fixed;
                report.precision(2);
                report<<" listener_input_pos=("<<position[0]<<","
                      <<position[1]<<","<<position[2]<<")"
                      <<" listener_input_front=("<<front[0]<<","
                      <<front[1]<<","<<front[2]<<")"
                      <<" listener_input_up=("<<up[0]<<","
                      <<up[1]<<","<<up[2]<<")";
                report.unsetf(std::ios_base::floatfield);
                report.precision(previous_precision);
            }
        } else {
            report<<" listener_input=signature-unverified";
        }
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
