#include "renderer/compressed_sound_read.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

int main()
{
    // An archive-backed resource supports only forward reads; deliberately
    // expose no seek, tell or size callbacks. Short reads are normal.
    std::vector<uint8_t> original(43000);
    for(size_t i=0;i<original.size();++i)
        original[i]=uint8_t((i*17U+3U)&255U);

    size_t pos=0,reads=0;
    std::vector<uint8_t> received;
    assert(jkgm::read_sound_source_sequential(
        [&](uint8_t* dst,size_t requested)->size_t {
            ++reads;
            size_t const n=std::min({requested,size_t(713),
                                     original.size()-pos});
            if(n)std::memcpy(dst,original.data()+pos,n);
            pos+=n;
            return n;
        },64U*1024U*1024U,received));
    assert(received==original && pos==original.size() && reads>60);

    // A buffer exactly at the limit is valid, but even one byte beyond
    // must fail without exposing any partially decoded bytes.
    for(size_t length:{size_t(16384),size_t(16385)}) {
        pos=0;
        std::vector<uint8_t> data(length,42);
        bool const success=jkgm::read_sound_source_sequential(
            [&](uint8_t* dst,size_t requested)->size_t {
                size_t const n=std::min(requested,data.size()-pos);
                if(n)std::memcpy(dst,data.data()+pos,n);
                pos+=n;
                return n;
            },size_t(16384),received);
        assert(success==(length==16384));
        assert(success ? received==data : received.empty());
    }

    // Empty and faulty backends are rejected.
    assert(!jkgm::read_sound_source_sequential(
        [](uint8_t*,size_t)->size_t {return 0;},4096,received));
    assert(received.empty());
    assert(!jkgm::read_sound_source_sequential(
        [](uint8_t*,size_t requested)->size_t {return requested+1;},
        65536,received));
    assert(received.empty());
    return 0;
}
