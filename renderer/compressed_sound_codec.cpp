// Self-contained decoders: no runtime DLLs, ffmpeg, or replacement mixer.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "thirdparty/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "thirdparty/dr_flac.h"
#define STB_VORBIS_NO_STDIO
#include "thirdparty/stb_vorbis.c"
#include "compressed_sound_codec.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

namespace jkgm {
namespace {
    constexpr size_t max_wave_pcm=192U*1024U*1024U;
    void put16(std::vector<uint8_t> &v,size_t pos,uint32_t n) {
        v[pos]=uint8_t(n);v[pos+1]=uint8_t(n>>8);
    }
    void put32(std::vector<uint8_t> &v,size_t pos,uint32_t n) {
        for(int i=0;i<4;++i)v[pos+i]=uint8_t(n>>(8*i));
    }
    bool to_wave(void const* data,uint64_t frames,
                 unsigned channels,unsigned rate,
                 std::vector<uint8_t>& out)
    {
        // JK's old sound API expects 8/16-bit mono or stereo PCM. Keep
        // decoded files at 16 bits without involving a new playback backend.
        if(!data||frames==0||channels<1||channels>2||rate<4000||
           rate>192000||frames>max_wave_pcm/(size_t(channels)*2U))
            return false;
        size_t const pcm_bytes=size_t(frames)*channels*2U;
        if(pcm_bytes>UINT32_MAX-44U)return false;
        std::vector<uint8_t> wave(pcm_bytes+44U,0);
        std::memcpy(wave.data(),"RIFF",4);
        put32(wave,4,uint32_t(pcm_bytes+36U));
        std::memcpy(wave.data()+8,"WAVEfmt ",8);
        put32(wave,16,16);
        put16(wave,20,1); // PCM
        put16(wave,22,channels);
        put32(wave,24,rate);
        put32(wave,28,rate*channels*2U);
        put16(wave,32,channels*2U);
        put16(wave,34,16);
        std::memcpy(wave.data()+36,"data",4);
        put32(wave,40,uint32_t(pcm_bytes));
        std::memcpy(wave.data()+44,data,pcm_bytes);
        out=std::move(wave);
        return true;
    }
}
bool decode_compressed_sound_to_wave(
    std::string_view extension,
    std::vector<uint8_t> const& compressed,
    std::vector<uint8_t>& wave)
{
    wave.clear();
    if(compressed.empty() || compressed.size()>64U*1024U*1024U)
        return false;
    if(extension==".ogg") {
        short* pcm=nullptr;
        int channels=0,rate=0;
        int count=stb_vorbis_decode_memory(compressed.data(),
            int(compressed.size()),&channels,&rate,&pcm);
        bool ok=count>0&&to_wave(pcm,uint64_t(count),unsigned(channels),
                                 unsigned(rate),wave);
        std::free(pcm);
        return ok;
    }
    if(extension==".flac") {
        unsigned channels=0,rate=0;
        drflac_uint64 frames=0;
        drflac_int16* pcm=drflac_open_memory_and_read_pcm_frames_s16(
            compressed.data(),compressed.size(),&channels,&rate,&frames,nullptr);
        bool ok=to_wave(pcm,frames,channels,rate,wave);
        drflac_free(pcm,nullptr);
        return ok;
    }
    if(extension==".mp3") {
        drmp3_config config{};
        drmp3_uint64 frames=0;
        drmp3_int16* pcm=drmp3_open_memory_and_read_pcm_frames_s16(
            compressed.data(),compressed.size(),&config,&frames,nullptr);
        bool ok=to_wave(pcm,frames,config.channels,config.sampleRate,wave);
        drmp3_free(pcm,nullptr);
        return ok;
    }
    return false;
}
}
