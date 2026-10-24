// Standalone, read-only DirectSound3D capability test for Wine or Windows.
// Does not play audio, modify the registry, or patch a game process.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmreg.h> // WAVEFORMATEX and WAVE_FORMAT_PCM
#include <dsound.h>
#include <cstdio>
#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"user32.lib")

static void result(char const *name,HRESULT h) {
    std::printf("%s: 0x%08lx (%s)\n",name,
      static_cast<unsigned long>(h),SUCCEEDED(h)?"OK":"FAILED");
}
int main() {
    std::puts("JkGR GfxMod DirectSound3D test (not an A3D test)");
    IDirectSound *sound=nullptr;
    HRESULT h=DirectSoundCreate(nullptr,&sound,nullptr);
    result("DirectSoundCreate",h);
    if(FAILED(h)||!sound)return 1;
    DSCAPS caps{};
    caps.dwSize=sizeof(caps);
    h=sound->GetCaps(&caps);
    result("GetCaps",h);
    if(SUCCEEDED(h)) {
        std::printf("caps=0x%08lx hardware_3D=%lu\n",
          static_cast<unsigned long>(caps.dwFlags),
          static_cast<unsigned long>(caps.dwMaxHw3DAllBuffers));
        std::puts("Zero hardware 3D buffers does not rule out software 3D.");
    }
    h=sound->SetCooperativeLevel(GetDesktopWindow(),DSSCL_PRIORITY);
    result("SetCooperativeLevel",h);
    WAVEFORMATEX fmt{};
    fmt.wFormatTag=WAVE_FORMAT_PCM;
    fmt.nChannels=1;
    fmt.nSamplesPerSec=22050;
    fmt.wBitsPerSample=16;
    fmt.nBlockAlign=2;
    fmt.nAvgBytesPerSec=44100;
    DSBUFFERDESC desc{};
    desc.dwSize=sizeof(desc);
    desc.dwFlags=DSBCAPS_CTRL3D|DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes=44100;
    desc.lpwfxFormat=&fmt;
    IDirectSoundBuffer *buf=nullptr;
    h=sound->CreateSoundBuffer(&desc,&buf,nullptr);
    result("Create 3D mono buffer",h);
    if(SUCCEEDED(h)&&buf) {
        IDirectSound3DBuffer *three_d=nullptr;
        h=buf->QueryInterface(IID_IDirectSound3DBuffer,
           reinterpret_cast<void **>(&three_d));
        result("QueryInterface IDirectSound3DBuffer",h);
        if(three_d)three_d->Release();
        buf->Release();
    }
    sound->Release();
    std::puts("DirectSound3D alone does not confirm Aureal A3D or HRTF.");
    return 0;
}
