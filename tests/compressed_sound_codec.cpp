#include "renderer/compressed_sound_codec.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> from_base64(char const *text)
{
    auto digit=[](char c)->int {
        if(c>='A'&&c<='Z')return c-'A';
        if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;
        if(c=='+')return 62;
        if(c=='/')return 63;
        return -1;
    };
    unsigned bits=0;
    int buffered=0;
    std::vector<uint8_t> v;
    for(char const*p=text;*p && *p!='=';++p) {
        int n=digit(*p);
        if(n<0)continue;
        bits=(bits<<6)|unsigned(n);
        buffered+=6;
        if(buffered>=8) {
            buffered-=8;
            v.push_back(uint8_t(bits>>buffered));
            bits&=(1U<<buffered)-1U;
        }
    }
    return v;
}

int main()
{
    std::vector<uint8_t> wave{7,8,9};
    for(char const* ext:{".ogg",".mp3",".flac",".wav"}) {
        assert(!jkgm::decode_compressed_sound_to_wave(ext,{},wave));
        assert(wave.empty());
        assert(!jkgm::decode_compressed_sound_to_wave(
            ext,{0,1,2,3,4,5},wave));
        assert(wave.empty());
    }
    // A real tiny mono 8-kHz MP3 generated with FFmpeg/LAME (not an
    // MP3 header stub). The WAV facade is what the native JK loader sees.
    auto mp3=from_base64(
"SUQzBAAAAAAAIlRTU0UAAAAOAAADTGF2ZjYxLjcuMTAzAAAAAAAAAAAAAAD/4zjAAAAAAAAAAAAASW5mbwAAAA8AAAADAAABsACqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqrV1dXV1dXV1dXV1dXV1dXV1dXV1dXV1dXV1dXV1dXV1dX///////////////////////////////////////////8AAAAATGF2YzYxLjE5AAAAAAAAAAAAAAAAJANoAAAAAAAAAbAzaNGLAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/4xjEAAtwlq0ZSAAA4M4//Ad3d3e/hERE/3d7/SSw7BWAuC8Pz6ggcxOH+CDrv8McBv0hjl3850+7pUki8i55zbzEmTIvGqT/4xjEDQ4owrABkzAAXij4yxRAPQGqiUDUgBjDL1ZEiRaqEkCeqT4lOloMgYJCvt8IAQyAWf/Fm0JzZ9Eqcjsz6qvNVWzOHJH/4xjEDwwgzcgBxhABQCCrBQCElAxHP//8cijlEiosFRBUV/wvv//+R0XzgrorKkxBTUUzLjEwMKqqqqqqqqqqqqqqqqqqqqo=");
    assert(jkgm::decode_compressed_sound_to_wave(".mp3",mp3,wave));
    assert(wave.size()>44);
    assert(std::memcmp(wave.data(),"RIFF",4)==0);
    assert(std::memcmp(wave.data()+8,"WAVEfmt ",8)==0);
    assert(std::memcmp(wave.data()+36,"data",4)==0);
    assert(wave[20]==1 && wave[22]==1); // PCM, mono
    assert(wave[24]==0x40 && wave[25]==0x1f); // 8000 Hz
    assert(wave[34]==16 && wave[35]==0); // 16-bit

    // Small real FLAC with the encoder's unused metadata padding stripped.
    // Regression coverage for lossless PCM16 conversion as well.
    auto flac=from_base64(
"ZkxhQ4AAACICQAJAAACpAACpAfQA8AAAARgzLL5Nzr2tNbu1WS8tP3Zh//h0CAABF+VOAAAFawoyDcUPtg/NDgUKlOWZT70maqdkEPdehSHj/rSALc6hQEwKBDBDT3MRhJCCQspo/1JBIJCl8MQMRlCgiIkrTc6hIhEUEZPzIYQwLEJCSTQYg0MEMFCISEwjpnMtCgiIpUcc1ESBMBMLTR/0ihCQkk7fowsREFgjBBk3OoUBMCgQwQ09zEYSQgkLKaP9SQSCQpfDEDEZQoIiJKxN3A==");
    assert(jkgm::decode_compressed_sound_to_wave(".flac",flac,wave));
    assert(wave.size()>44);
    assert(std::memcmp(wave.data(),"RIFF",4)==0);
    assert(std::memcmp(wave.data()+36,"data",4)==0);
    assert(wave[34]==16 && wave[22]==1);
    return 0;
}
