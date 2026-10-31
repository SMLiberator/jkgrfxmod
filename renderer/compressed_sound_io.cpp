#include "compressed_sound_io.hpp"
#include "compressed_sound_codec.hpp"
#include "base/log.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace jkgm {
namespace {
    // Confirmed from the supplied jkgm(5).exe: sithSound_Load at 004EF440
    // calls fileOpen via the HostServices pointer at 0082F0EC; the WAV parser
    // at 00437A30 calls fseek/read via 00866BC0. Both use the x86 callback
    // table layout (open +0x30, close +0x34, read +0x38, EOF +0x44,
    // tell +0x48, seek +0x4C, size +0x50).
    constexpr uintptr_t sith_host_global=0x0082F0ECU;
    constexpr uintptr_t std_host_global=0x00866BC0U;
    // Verified in the supplied jkgm(5).exe at sithSound_Load 0x004EF440.
    // The loader returns before fileOpen when sound_initialized is zero.
    constexpr uintptr_t sound_initialized_global=0x0084DF7CU;
    constexpr uintptr_t sound_hashtable_global=0x0084DF80U;
    constexpr uintptr_t last_loaded_world_global=0x00833A18U;
    constexpr uintptr_t sith_load_entry=0x004EF440U;
    constexpr uintptr_t wav_parser_entry=0x00437A30U;
    constexpr uint8_t sith_signature[]{
        0x81,0xEC,0x84,0x00,0x00,0x00,0xA1,0x18,0x3A,0x83,0x00
    };
    constexpr uint8_t wav_signature[]{
        0xA1,0xC0,0x6B,0x86,0x00,0x83,0xEC,0x14,0x53
    };
    constexpr size_t max_encoded_bytes=64U*1024U*1024U;
    constexpr size_t max_cached_bytes=128U*1024U*1024U;
    constexpr int virtual_fd_first=0x6F000000;
    using open_fn=int (__cdecl*)(char const*,char const*);
    using close_fn=int (__cdecl*)(int);
    using read_fn=size_t (__cdecl*)(int,void*,size_t);
    using eof_fn=int (__cdecl*)(int);
    using tell_fn=int (__cdecl*)(int);
    using seek_fn=int (__cdecl*)(int,int,int);
    using size_fn=int (__cdecl*)(int);
    struct callbacks {
        open_fn open=nullptr;
        close_fn close=nullptr;
        read_fn read=nullptr;
        eof_fn eof=nullptr;
        tell_fn tell=nullptr;
        seek_fn seek=nullptr;
        size_fn size=nullptr;
    };
    struct virtual_file {
        std::shared_ptr<std::vector<uint8_t>> wav;
        size_t offset=0;
        bool first_read_reported=false;
    };
    callbacks real{};
    std::mutex lock;
    std::unordered_map<int,virtual_file> streams;
    std::unordered_map<std::string,std::shared_ptr<std::vector<uint8_t>>> cached;
    size_t cached_bytes=0;
    int next_fd=virtual_fd_first;
    std::array<uintptr_t,2> host_tables{};
    size_t host_count=0;
    bool installed=false;
    thread_local unsigned sound_open_trace_depth=0;

    bool readable(uintptr_t addr,size_t size)
    {
        if(addr<0x10000U || size==0)return false;
        MEMORY_BASIC_INFORMATION mb{};
        if(VirtualQuery(reinterpret_cast<void const*>(addr),&mb,sizeof(mb))
           !=sizeof(mb) || mb.State!=MEM_COMMIT ||
           (mb.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0)return false;
        uintptr_t const base=reinterpret_cast<uintptr_t>(mb.BaseAddress);
        return addr>=base && addr-base<=mb.RegionSize &&
               size<=mb.RegionSize-(addr-base);
    }
    std::string lower(std::string value)
    {
        for(char &c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }
    std::string extension(std::string const& path)
    {
        size_t const n=path.find_last_of('.');
        size_t const sep=path.find_last_of("/\\");
        if(n==std::string::npos ||
           (sep!=std::string::npos && sep>n))return {};
        return lower(path.substr(n));
    }
    // Only swap function table entries, not bytes in the engine's code.
    // All engine/gob I/O that is not one of our virtual handles is forwarded.
    bool write_slot(uintptr_t table,size_t offset,uintptr_t value)
    {
        uintptr_t const addr=table+offset;
        if(!readable(addr,sizeof(uint32_t)))return false;
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(addr),
                           sizeof(uint32_t),PAGE_READWRITE,&old))
            return false;
        uint32_t const v=uint32_t(value);
        std::memcpy(reinterpret_cast<void*>(addr),&v,sizeof(v));
        DWORD unused=0;
        VirtualProtect(reinterpret_cast<void*>(addr),sizeof(v),old,&unused);
        return true;
    }
    uintptr_t slot(uintptr_t table,size_t offset)
    {
        uint32_t addr=0;
        std::memcpy(&addr,reinterpret_cast<void const*>(table+offset),4);
        return addr;
    }
    std::shared_ptr<std::vector<uint8_t>> read_and_decode(
        int native_fd,std::string const& path,std::string const& ext)
    {
        std::string const key=lower(path);
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=cached.find(key);
            if(it!=cached.end())return it->second;
        }
        auto fail=[&](char const* reason)
            ->std::shared_ptr<std::vector<uint8_t>> {
            LOG_WARNING("JKGM compressed sound decode failed: ",path,
                        " reason=",reason);
            return {};
        };
        // Read via original HostServices callbacks, NEVER host stdio.
        // This preserves JK's loose-file and GOB/GOO search behavior.
        int length=real.size?real.size(native_fd):-1;
        if(length<0) {
            if(real.seek(native_fd,0,SEEK_END)!=0)
                return fail("size unavailable; seek end failed");
            length=real.tell(native_fd);
            if(real.seek(native_fd,0,SEEK_SET)!=0)
                return fail("size unavailable; seek start failed");
        }
        if(length<=0 || uint64_t(length)>max_encoded_bytes)
            return fail("empty or larger than 64 MiB");
        if(real.seek(native_fd,0,SEEK_SET)!=0)
            return fail("seek start failed");
        std::vector<uint8_t> input(size_t(length),uint8_t(0));
        size_t read_count=0;
        while(read_count<input.size()) {
            size_t const got=real.read(native_fd,input.data()+read_count,
                                       input.size()-read_count);
            if(got==0 || got>input.size()-read_count)
                return fail("encoded file read failed");
            read_count+=got;
        }
        std::vector<uint8_t> wav;
        if(!decode_compressed_sound_to_wave(ext,input,wav))
            return fail("invalid codec data, unsupported channels or PCM limit");
        auto result=std::make_shared<std::vector<uint8_t>>(std::move(wav));
        {
            std::lock_guard<std::mutex> guard(lock);
            // Bounded cache prevents repeated decode between Sith's header
            // pass and its later lazy PCM read. Open handles retain ownership.
            if(result->size()<=max_cached_bytes) {
                if(result->size()>max_cached_bytes-cached_bytes) {
                    cached.clear();
                    cached_bytes=0;
                }
                cached.emplace(key,result);
                cached_bytes+=result->size();
            }
        }
        std::ostringstream report;
        report<<"JKGM compressed sound decoded: "<<path
              <<" PCM wave bytes="<<result->size();
        LOG_INFO(report.str());
        return result;
    }
    int virtual_open(std::shared_ptr<std::vector<uint8_t>> wav)
    {
        std::lock_guard<std::mutex> guard(lock);
        if(streams.size()>=256U)return 0;
        for(int i=0;i<1024;++i) {
            int const fd=next_fd++;
            if(next_fd>=0x70000000)next_fd=virtual_fd_first;
            if(streams.emplace(fd,virtual_file{wav,0}).second)return fd;
        }
        return 0;
    }
    int __cdecl file_open(char const* path,char const* mode)
    {
        if(sound_open_trace_depth && path && mode)
            LOG_INFO("JKGM sound HostServices fileOpen entered: ",path,
                     " mode=",mode);
        if(!path || !mode)return 0;
        if(std::strchr(mode,'w') || std::strchr(mode,'a') ||
           std::strchr(mode,'+'))return real.open(path,mode);
        std::string const wanted(path);
        std::string const ext=extension(wanted);
        // Native WAV (and every other format) passes through unchanged.
        // Compressed formats are requested by their actual filenames only.
        if(ext!=".ogg" && ext!=".flac" && ext!=".mp3")
            return real.open(path,mode);

        LOG_INFO("JKGM compressed sound open requested: ",wanted,
                 " mode=",mode);
        std::shared_ptr<std::vector<uint8_t>> wav;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=cached.find(lower(wanted));
            if(it!=cached.end())wav=it->second;
        }
        if(!wav) {
            int const fd=real.open(path,"rb");
            if(!fd) {
                LOG_WARNING("JKGM compressed sound file not found: ",wanted);
                return 0;
            }
            wav=read_and_decode(fd,wanted,ext);
            real.close(fd);
        }
        if(!wav)return 0;
        int const virtual_fd=virtual_open(std::move(wav));
        if(!virtual_fd) {
            LOG_WARNING("JKGM compressed sound virtual handle exhausted: ",
                        wanted);
            return 0;
        }
        LOG_INFO("JKGM compressed sound virtual WAV opened: ",wanted,
                 " handle=",virtual_fd);
        return virtual_fd;
    }
    int __cdecl file_close(int fd)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            if(streams.erase(fd))return 0;
        }
        return real.close(fd);
    }
    size_t __cdecl file_read(int fd,void* dst,size_t bytes)
    {
        bool is_virtual=false,first_read=false;
        size_t count=0;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=streams.find(fd);
            if(it!=streams.end()) {
                auto &v=it->second;
                size_t const avail=v.wav->size()-v.offset;
                count=std::min(bytes,avail);
                if(count && !dst)return 0;
                if(count)std::memcpy(dst,v.wav->data()+v.offset,count);
                v.offset+=count;
                is_virtual=true;
                if(!v.first_read_reported) {
                    v.first_read_reported=true;
                    first_read=true;
                }
            }
        }
        // Never log while holding the virtual-stream mutex: logging may
        // itself perform file I/O via the original engine callbacks.
        if(is_virtual) {
            if(first_read)
                LOG_INFO("JKGM compressed PCM first read: handle=",fd,
                         " requested=",bytes," returned=",count);
            return count;
        }
        return real.read(fd,dst,bytes);
    }
    int __cdecl file_seek(int fd,int displacement,int origin)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=streams.find(fd);
            if(it!=streams.end()) {
                auto &v=it->second;
                int64_t const base=origin==SEEK_SET?0:
                    origin==SEEK_CUR?int64_t(v.offset):
                    origin==SEEK_END?int64_t(v.wav->size()):-1;
                int64_t const target=base+displacement;
                if(base<0 || target<0 ||
                   target>int64_t(v.wav->size()))return -1;
                v.offset=size_t(target);
                return 0;
            }
        }
        return real.seek(fd,displacement,origin);
    }
    int __cdecl file_tell(int fd)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=streams.find(fd);
            if(it!=streams.end())return int(it->second.offset);
        }
        return real.tell(fd);
    }
    int __cdecl file_eof(int fd)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=streams.find(fd);
            if(it!=streams.end())
                return it->second.offset>=it->second.wav->size()?1:0;
        }
        return real.eof(fd);
    }
    int __cdecl file_size(int fd)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it=streams.find(fd);
            if(it!=streams.end())return int(it->second.wav->size());
        }
        return real.size(fd);
    }
    struct entry {size_t offset;uintptr_t replacement;uintptr_t original;};
    std::array<entry,7> entries() {
        return {{
            {0x30U,reinterpret_cast<uintptr_t>(&file_open),
                reinterpret_cast<uintptr_t>(real.open)},
            {0x34U,reinterpret_cast<uintptr_t>(&file_close),
                reinterpret_cast<uintptr_t>(real.close)},
            {0x38U,reinterpret_cast<uintptr_t>(&file_read),
                reinterpret_cast<uintptr_t>(real.read)},
            {0x44U,reinterpret_cast<uintptr_t>(&file_eof),
                reinterpret_cast<uintptr_t>(real.eof)},
            {0x48U,reinterpret_cast<uintptr_t>(&file_tell),
                reinterpret_cast<uintptr_t>(real.tell)},
            {0x4CU,reinterpret_cast<uintptr_t>(&file_seek),
                reinterpret_cast<uintptr_t>(real.seek)},
            {0x50U,reinterpret_cast<uintptr_t>(&file_size),
                reinterpret_cast<uintptr_t>(real.size)}
        }};
    }
}
bool compressed_sound_executable_supported()
{
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       !readable(sith_load_entry,sizeof(sith_signature)) ||
       !readable(wav_parser_entry,sizeof(wav_signature)))return false;
    return std::memcmp(reinterpret_cast<void const*>(sith_load_entry),
                       sith_signature,sizeof(sith_signature))==0 &&
           std::memcmp(reinterpret_cast<void const*>(wav_parser_entry),
                       wav_signature,sizeof(wav_signature))==0;
}
bool install_compressed_sound_file_services()
{
    if(installed)return true;
    if(!readable(sith_host_global,4) || !readable(std_host_global,4))
        return false;
    uint32_t a=0,b=0;
    std::memcpy(&a,reinterpret_cast<void const*>(sith_host_global),4);
    std::memcpy(&b,reinterpret_cast<void const*>(std_host_global),4);
    if(!readable(a,0x54U) || !readable(b,0x54U))return false;
    callbacks const found{
        reinterpret_cast<open_fn>(slot(a,0x30)),
        reinterpret_cast<close_fn>(slot(a,0x34)),
        reinterpret_cast<read_fn>(slot(a,0x38)),
        reinterpret_cast<eof_fn>(slot(a,0x44)),
        reinterpret_cast<tell_fn>(slot(a,0x48)),
        reinterpret_cast<seek_fn>(slot(a,0x4C)),
        reinterpret_cast<size_fn>(slot(a,0x50))
    };
    if(!found.open||!found.close||!found.read||!found.eof||
       !found.tell||!found.seek||!found.size)return false;
    for(size_t offset:{0x30U,0x34U,0x38U,0x44U,0x48U,0x4CU,0x50U})
        if(slot(a,offset)!=slot(b,offset))return false;
    real=found;
    std::array<uintptr_t,2> const tables{{a,b}};
    host_tables=tables;
    host_count=a==b?1:2;
    size_t patched=0;
    auto const hooks=entries();
    for(size_t t=0;t<host_count;++t) {
        for(auto const& h:hooks) {
            if(!write_slot(tables[t],h.offset,h.replacement)) {
                for(size_t k=0;k<patched;++k) {
                    size_t const tt=k/hooks.size(),ii=k%hooks.size();
                    write_slot(tables[tt],hooks[ii].offset,
                               hooks[ii].original);
                }
                host_count=0;real={};
                return false;
            }
            ++patched;
        }
    }
    installed=true;
    LOG_INFO("JKGM compressed OGG/FLAC/MP3 HostServices WAV bridge installed");
    return true;
}
void begin_compressed_sound_open_trace()
{
    ++sound_open_trace_depth;
}
void end_compressed_sound_open_trace()
{
    if(sound_open_trace_depth)--sound_open_trace_depth;
}
void diagnose_compressed_sound_file_services(char const* filename)
{
    if(!filename)return;
    LOG_INFO("JKGM sound bridge diagnostic: installed=",installed?1:0);
    if(readable(sound_initialized_global,4) &&
       readable(sound_hashtable_global,4) &&
       readable(last_loaded_world_global,4)) {
        uint32_t sound_initialized=0,hash_table=0,world=0;
        std::memcpy(&sound_initialized,
                    reinterpret_cast<void const*>(sound_initialized_global),4);
        std::memcpy(&hash_table,
                    reinterpret_cast<void const*>(sound_hashtable_global),4);
        std::memcpy(&world,
                    reinterpret_cast<void const*>(last_loaded_world_global),4);
        LOG_INFO("JKGM sound native loader state: initialized=",
                 sound_initialized," hashtable=",uintptr_t(hash_table),
                 " last_world=",uintptr_t(world));
    }
    for(uintptr_t global:{sith_host_global,std_host_global}) {
        if(!readable(global,4)) {
            LOG_WARNING("JKGM sound HostServices global unreadable: ",global);
            continue;
        }
        uint32_t table=0;
        std::memcpy(&table,reinterpret_cast<void const*>(global),4);
        if(!readable(table,0x54U)) {
            LOG_WARNING("JKGM sound HostServices table unreadable: global=",
                        global," table=",uintptr_t(table));
            continue;
        }
        uintptr_t const open_ptr=slot(table,0x30U);
        uintptr_t const expected=reinterpret_cast<uintptr_t>(&file_open);
        LOG_INFO("JKGM sound HostServices live table: global=",global,
                 " table=",uintptr_t(table),
                 " fileOpen=",open_ptr,
                 " bridge_active=",(open_ptr==expected?1:0));
    }
    if(!installed || !real.open || !real.close) {
        LOG_WARNING("JKGM sound resource probe unavailable: bridge not installed");
        return;
    }
    // Probe using the original JK callbacks. Do not decode, cache or open
    // virtual handles here; this only establishes whether JK can find files.
    for(char const* dir:{"sound\\","voice\\"}) {
        std::string const path=std::string(dir)+filename;
        int const fd=real.open(path.c_str(),"rb");
        if(fd) {
            int const size=real.size?real.size(fd):-1;
            real.close(fd);
            LOG_INFO("JKGM sound resource probe: ",path,
                     " found=1 size=",size);
        } else {
            LOG_INFO("JKGM sound resource probe: ",path," found=0");
        }
    }
}
void uninstall_compressed_sound_file_services()
{
    if(!installed)return;
    auto const hooks=entries();
    for(size_t t=0;t<host_count;++t)
        for(auto const& h:hooks)
            if(readable(host_tables[t]+h.offset,4) &&
               slot(host_tables[t],h.offset)==h.replacement)
                write_slot(host_tables[t],h.offset,h.original);
    installed=false;host_count=0;
    std::lock_guard<std::mutex> guard(lock);
    streams.clear();cached.clear();cached_bytes=0;
}
}
