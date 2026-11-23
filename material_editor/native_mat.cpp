#include "native_mat.hpp"
#include "base/md5.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <set>
#include <tuple>

namespace fs=std::filesystem;
namespace {
std::uint32_t u32(const std::vector<std::uint8_t> &a,size_t pos) {
    if(pos>a.size()||a.size()-pos<4)throw std::runtime_error("Unexpected end of MAT/CMP data");
    return std::uint32_t(a[pos])|std::uint32_t(a[pos+1])<<8|
           std::uint32_t(a[pos+2])<<16|std::uint32_t(a[pos+3])<<24;
}
std::string normalize(std::string s) {
    for(char &c:s) {
        if(c=='\\')c='/';
        else c=char(std::tolower(static_cast<unsigned char>(c)));
    }
    while(s.rfind("./",0)==0)s.erase(0,2);
    return s;
}
bool diskBytes(const fs::path &file,std::uint64_t offset,std::uint64_t length,
               std::vector<std::uint8_t> &out) {
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if(!stream)return false;
    auto end=stream.tellg();
    if(end<0)return false;
    std::uint64_t total=static_cast<std::uint64_t>(end);
    if(offset>total)return false;
    if(length==0)length=total-offset;
    if(length>total-offset||length>128U*1024U*1024U)return false;
    out.resize(static_cast<size_t>(length));
    stream.seekg(static_cast<std::streamoff>(offset),std::ios::beg);
    if(length && !stream.read(reinterpret_cast<char *>(out.data()),
                              static_cast<std::streamsize>(length)))return false;
    return true;
}
struct Cursor {
    const std::vector<std::uint8_t> &data;
    size_t pos=0;
    std::uint32_t next() {
        std::uint32_t value=u32(data,pos);
        pos+=4;return value;
    }
    void skip(size_t count) {
        if(pos>data.size()||count>data.size()-pos)
            throw std::runtime_error("Truncated MAT file");
        pos+=count;
    }
};
std::uint8_t scale5(unsigned value) {
    return static_cast<std::uint8_t>((value*255U+15U)/31U);
}
std::uint8_t scale6(unsigned value) {
    return static_cast<std::uint8_t>((value*255U+31U)/63U);
}
}

void NativeMatSource::setRoot(const std::string &directory) {
    setRoots(directory.empty()?std::vector<std::string>{}:
             std::vector<std::string>{directory});
}
void NativeMatSource::setRoots(const std::vector<std::string> &directories) {
    roots_.clear();
    files_.clear();
    // Normalize and de-duplicate roots before scanning. Do not index the
    // same folder twice, but preserve the priority order chosen by the user.
    for(const auto &name:directories) {
        if(name.empty())continue;
        const auto full=fs::absolute(fs::u8path(name)).lexically_normal().u8string();
        if(std::find(roots_.begin(),roots_.end(),full)==roots_.end())
            roots_.push_back(full);
    }
    // Build from lowest to highest priority. For each root use an isolated
    // map so local loose files win over local GOB/GOO entries, then merge
    // only names not available in the higher-priority root.
    for(auto folder=roots_.rbegin();folder!=roots_.rend();++folder) {
        auto lowerPriority=std::move(files_);
        files_.clear();
        const fs::path base=fs::u8path(*folder);
        std::error_code ec;
        if(fs::is_directory(base,ec)) {
            for(fs::recursive_directory_iterator it(
                    base,fs::directory_options::skip_permission_denied,ec),end;
                it!=end&&!ec;it.increment(ec)) {
                if(!it->is_regular_file(ec))continue;
                const std::string ext=normalize(it->path().extension().u8string());
                if(ext==".gob"||ext==".goo")indexArchive(it->path());
            }
            ec.clear();
            // Loose MAT/CMP files override this folder's archives.
            for(fs::recursive_directory_iterator it(
                    base,fs::directory_options::skip_permission_denied,ec),end;
                it!=end&&!ec;it.increment(ec)) {
                if(!it->is_regular_file(ec))continue;
                const std::string ext=normalize(it->path().extension().u8string());
                if(ext!=".mat"&&ext!=".cmp")continue;
                const auto relative=fs::relative(it->path(),base,ec);
                if(ec)break;
                const std::string key=normalize(relative.generic_u8string());
                if(key.find("..")!=std::string::npos)continue;
                files_[key]={it->path(),0,0,false};
                files_.emplace(normalize(it->path().filename().u8string()),
                               Entry{it->path(),0,0,false});
            }
        }
        for(const auto &entry:lowerPriority)
            files_.emplace(entry.first,entry.second);
    }
}
void NativeMatSource::setPalette(const std::string &file) {
    palette_=file;
}
void NativeMatSource::indexArchive(const fs::path &archive) {
    std::ifstream file(archive,std::ios::binary|std::ios::ate);
    if(!file)return;
    auto size=file.tellg();
    if(size<16)return;
    std::uint64_t total=static_cast<std::uint64_t>(size);
    std::array<unsigned char,16> header{};
    file.seekg(0);
    if(!file.read(reinterpret_cast<char *>(header.data()),header.size())||
       std::memcmp(header.data(),"GOB ",4)!=0)return;
    auto word=[&](size_t p) {
        return std::uint32_t(header[p])|
               std::uint32_t(header[p+1])<<8|
               std::uint32_t(header[p+2])<<16|
               std::uint32_t(header[p+3])<<24;
    };
    std::uint32_t count=word(12),indexOffset=word(4);
    if(count==0||count>200000)return;
    // The project's GOB reader consumes the entries immediately after the
    // 16-byte header. Accept the stored offset too for alternate archives.
    auto tryIndex=[&](std::uint64_t first,
                      std::vector<std::pair<std::string,Entry>> &entries)->bool {
        if(first>total||std::uint64_t(count)*136U>total-first)return false;
        file.clear();file.seekg(static_cast<std::streamoff>(first));
        entries.clear();
        for(std::uint32_t i=0;i<count;++i) {
            std::array<unsigned char,136> e{};
            if(!file.read(reinterpret_cast<char *>(e.data()),e.size()))return false;
            auto v=[&](size_t p) {
                return std::uint32_t(e[p])|
                       std::uint32_t(e[p+1])<<8|
                       std::uint32_t(e[p+2])<<16|
                       std::uint32_t(e[p+3])<<24;
            };
            std::uint64_t off=v(0),len=v(4);
            if(off>total||len>total-off)return false;
            size_t namelen=0;
            while(namelen<128&&e[8+namelen])++namelen;
            if(namelen==0||namelen==128)return false;
            std::string name(reinterpret_cast<const char *>(e.data()+8),namelen);
            name=normalize(name);
            if(name.find("..")!=std::string::npos)return false;
            std::string ext=normalize(fs::path(name).extension().string());
            if(ext==".mat"||ext==".cmp")
                entries.push_back({name,{archive,off,len,true}});
        }
        return true;
    };
    std::vector<std::pair<std::string,Entry>> entries;
    if(!tryIndex(16,entries) && !tryIndex(indexOffset,entries))return;
    for(auto &entry:entries)files_.emplace(std::move(entry.first),entry.second);
}
bool NativeMatSource::readAsset(const std::string &name,
                                std::vector<std::uint8_t> &bytes,
                                std::string &source) const {
    fs::path full=fs::u8path(name);
    if(full.is_absolute() && diskBytes(full,0,0,bytes)) {
        source=full.u8string();return true;
    }
    auto it=files_.find(normalize(name));
    if(it==files_.end()) {
        std::string key="/"+normalize(name);
        for(auto candidate=files_.begin();candidate!=files_.end();++candidate) {
            if(candidate->first.size()>=key.size() &&
               candidate->first.compare(candidate->first.size()-key.size(),
                                        key.size(),key)==0) {
                it=candidate;break;
            }
        }
    }
    if(it==files_.end())return false;
    const Entry &entry=it->second;
    if(!diskBytes(entry.filename,entry.offset,entry.length,bytes))return false;
    source=entry.filename.u8string();
    if(entry.archive)source+=" [GOB/GOO]";
    return true;
}
bool NativeMatSource::load(const std::string &matName,int cel,
                           NativeMatImage &out,std::string &error) const {
    out={};
    if(roots_.empty()) {
        error="Add a JK/MotS assets folder under File to locate the original MAT.";
        return false;
    }
    try {
        std::vector<std::uint8_t> bytes;
        std::string source;
        if(!readAsset(matName,bytes,source)) {
            error="MAT not found in configured assets folders: "+matName;
            return false;
        }
        if(bytes.size()<76||std::memcmp(bytes.data(),"MAT ",4)!=0)
            throw std::runtime_error("Not a valid MAT file");
        Cursor reader{bytes,4};
        if(reader.next()!=0x32)
            throw std::runtime_error("Unsupported MAT version");
        reader.next(); // MAT type
        std::uint32_t records=reader.next(), textures=reader.next();
        std::uint32_t transparentIndex=reader.next(), bitdepth=reader.next();
        if(bitdepth!=8&&bitdepth!=16)
            throw std::runtime_error("Unsupported MAT color depth");
        if(records>4096||textures>4096||cel<0||std::uint32_t(cel)>=records)
            throw std::runtime_error("Invalid MAT cel or record count");
        reader.skip(48); // 12 reserved words
        struct Cel {std::uint32_t type=0,color=0,texture=0;};
        Cel requested;
        for(std::uint32_t i=0;i<records;++i) {
            Cel current;
            current.type=reader.next();
            current.color=reader.next();
            reader.skip(16);
            if(current.type==8) {
                reader.skip(12);
                current.texture=reader.next();
            } else if(current.type!=0)
                throw std::runtime_error("Unsupported MAT cel type");
            if(i==std::uint32_t(cel))requested=current;
        }
        std::vector<std::uint8_t> image;
        std::uint32_t w=1,h=1;
        bool hasTransparency=false;
        if(requested.type==8&&requested.texture>=textures)
            throw std::runtime_error("MAT cel references invalid texture");
        for(std::uint32_t t=0;t<textures;++t) {
            auto width=reader.next(),height=reader.next();
            bool transparent=reader.next()!=0;
            reader.skip(8);
            auto mips=reader.next();
            if(!width||!height||width>4096||height>4096||
               mips>16||!mips)
                throw std::runtime_error("Invalid MAT texture dimensions");
            for(std::uint32_t mip=0;mip<mips;++mip) {
                std::uint32_t mw=std::max(width>>mip,1U);
                std::uint32_t mh=std::max(height>>mip,1U);
                size_t len=size_t(mw)*mh*(bitdepth/8);
                if(t==requested.texture&&mip==0&&requested.type==8) {
                    w=mw;h=mh;hasTransparency=transparent;
                    if(reader.pos>bytes.size()||len>bytes.size()-reader.pos)
                        throw std::runtime_error("Truncated MAT texture");
                    image.assign(bytes.begin()+reader.pos,bytes.begin()+reader.pos+len);
                }
                reader.skip(len);
            }
        }
        std::array<std::uint8_t,768> palette{};
        if(bitdepth==8) {
            std::vector<std::uint8_t> cmp;
            std::string cmpSource;
            bool gotCmp=(!palette_.empty()&&readAsset(palette_,cmp,cmpSource))||
                        readAsset("misc/cmp/dflt.cmp",cmp,cmpSource)||
                        readAsset("dflt.cmp",cmp,cmpSource);
            if(!gotCmp) {
                error="Indexed MAT needs a CMP palette. Select a .cmp file.";
                return false;
            }
            if(cmp.size()<64+palette.size()||
               std::memcmp(cmp.data(),"CMP ",4)!=0||u32(cmp,4)!=0x1e)
                throw std::runtime_error("Invalid CMP palette");
            std::copy_n(cmp.begin()+64,palette.size(),palette.begin());
        }
        if(requested.type==0) {
            out.width=out.height=2;
            out.rgba.resize(16,255);
            auto idx=requested.color&255U;
            for(int i=0;i<4;++i) {
                out.rgba[size_t(i)*4+0]=palette[size_t(idx)*3+0];
                out.rgba[size_t(i)*4+1]=palette[size_t(idx)*3+1];
                out.rgba[size_t(i)*4+2]=palette[size_t(idx)*3+2];
            }
        } else {
            out.width=int(w);out.height=int(h);
            out.rgba.resize(size_t(w)*h*4);
            for(size_t i=0;i<size_t(w)*h;++i) {
                std::uint8_t r=0,g=0,b=0,a=255;
                if(bitdepth==8) {
                    unsigned p=image[i];
                    r=palette[p*3+0];g=palette[p*3+1];b=palette[p*3+2];
                    if(hasTransparency&&p==transparentIndex)a=0;
                } else {
                    std::uint16_t p=std::uint16_t(image[i*2])|
                                    std::uint16_t(image[i*2+1])<<8;
                    if(hasTransparency) {
                        a=(p&0x8000)?255:0;
                        r=scale5((p>>10)&31);g=scale5((p>>5)&31);
                        b=scale5(p&31);
                    } else {
                        r=scale5((p>>11)&31);g=scale6((p>>5)&63);
                        b=scale5(p&31);
                    }
                }
                out.rgba[4*i+0]=r;out.rgba[4*i+1]=g;
                out.rgba[4*i+2]=b;out.rgba[4*i+3]=a;
            }
        }
        out.description=matName+" (cel "+std::to_string(cel)+") from "+source;
        error.clear();
        return true;
    } catch(const std::exception &e) {
        error=std::string("Could not decode MAT: ")+e.what();
        out={};
        return false;
    }
}


bool NativeMatSource::signatures(const std::string &matName,int cel,
                                 std::vector<std::string> &out,
                                 std::string &error) const {
    out.clear();
    try {
        std::vector<std::uint8_t> bytes;
        std::string source;
        if(!readAsset(matName,bytes,source))
            throw std::runtime_error("MAT not found in configured game assets: "+matName);
        if(bytes.size()<76||std::memcmp(bytes.data(),"MAT ",4)!=0)
            throw std::runtime_error("Not a MAT file: "+matName);
        Cursor reader{bytes,4};
        if(reader.next()!=0x32)
            throw std::runtime_error("Unsupported MAT version");
        reader.next(); // MAT type
        const std::uint32_t records=reader.next(),textures=reader.next();
        const std::uint32_t transparentIndex=reader.next(),bitdepth=reader.next();
        if((bitdepth!=8&&bitdepth!=16)||records>4096||textures>4096||
           cel<0||std::uint32_t(cel)>=records)
            throw std::runtime_error("Invalid MAT bitdepth, cel or record count");
        reader.skip(48);
        std::uint32_t requestedType=0,requestedTexture=0;
        for(std::uint32_t i=0;i<records;++i) {
            const std::uint32_t type=reader.next();
            reader.next(); // Color index
            reader.skip(16);
            if(type==8) {
                reader.skip(12);
                const auto texture=reader.next();
                if(i==std::uint32_t(cel))requestedTexture=texture;
            } else if(type!=0)
                throw std::runtime_error("Unsupported MAT cel type");
            if(i==std::uint32_t(cel))requestedType=type;
        }
        // The compiler requires a texture record for replacement matching.
        // Never invent a hash for solid-color MAT cels.
        if(requestedType!=8||requestedTexture>=textures)
            throw std::runtime_error("MAT cel has no valid texture record");

        struct Mip {std::uint32_t width,height;std::vector<std::uint8_t> data;};
        std::vector<Mip> mips;
        bool usesTransparency=false;
        for(std::uint32_t tex=0;tex<textures;++tex) {
            auto width=reader.next(),height=reader.next();
            const bool transparent=reader.next()!=0;
            reader.skip(8);
            const auto mipCount=reader.next();
            if(width==0||height==0||width>4096||height>4096||
               mipCount==0||mipCount>16)
                throw std::runtime_error("Invalid MAT mipmap dimensions/count");
            if(tex==requestedTexture)usesTransparency=transparent;
            for(std::uint32_t mip=0;mip<mipCount;++mip) {
                const size_t count=std::size_t(width)*height*(bitdepth/8);
                if(reader.pos>bytes.size()||count>bytes.size()-reader.pos)
                    throw std::runtime_error("Truncated MAT texture data");
                if(tex==requestedTexture) {
                    Mip selected;
                    selected.width=width;selected.height=height;
                    selected.data.assign(bytes.begin()+reader.pos,
                                         bytes.begin()+reader.pos+count);
                    mips.push_back(std::move(selected));
                }
                reader.skip(count);
                // Use the compiler's exact shrinking dimensions, including
                // zero-sized later mips if an unusual MAT contains them.
                width>>=1;height>>=1;
            }
        }
        std::set<std::string> hashes; // same sorted unique set as compiler
        if(bitdepth==16) {
            for(const auto &mip:mips) {
                jkgm::md5_hasher hash;
                hash.add(jkgm::make_span(&mip.width,1).as_const_bytes());
                hash.add(jkgm::make_span(&mip.height,1).as_const_bytes());
                hash.add(jkgm::make_span(mip.data).as_const_bytes());
                hashes.insert(static_cast<std::string>(hash.finish()));
            }
        } else {
            // The compiler loads EVERY visible colormap through its VFS.
            // Include every indexed CMP, not just the one used by the MAT
            // thumbnail. Avoid duplicate full-path and basename aliases.
            std::set<std::tuple<std::string,std::uint64_t,std::uint64_t>> seen;
            std::vector<std::array<std::uint8_t,768>> colormaps;
            for(const auto &item:files_) {
                const auto &name=item.first;
                if(name.size()<4||name.compare(name.size()-4,4,".cmp")!=0)
                    continue;
                const auto &entry=item.second;
                const auto identity=std::make_tuple(
                    entry.filename.u8string(),entry.offset,entry.length);
                if(!seen.insert(identity).second)continue;
                std::vector<std::uint8_t> cmp;
                if(!diskBytes(entry.filename,entry.offset,entry.length,cmp)||
                   cmp.size()<64+768||std::memcmp(cmp.data(),"CMP ",4)!=0||
                   u32(cmp,4)!=0x1e)
                    throw std::runtime_error("Invalid indexed CMP: "+name);
                std::array<std::uint8_t,768> palette{};
                std::copy_n(cmp.begin()+64,768,palette.begin());
                colormaps.push_back(palette);
            }
            if(colormaps.empty())
                throw std::runtime_error(
                    "8-bit MAT signatures require indexed CMPs. "
                    "Configure the JK/MotS game assets folders under File.");
            for(const auto &mip:mips)for(const auto &palette:colormaps) {
                jkgm::md5_hasher hash;
                hash.add(jkgm::make_span(&mip.width,1).as_const_bytes());
                hash.add(jkgm::make_span(&mip.height,1).as_const_bytes());
                for(const std::uint8_t index:mip.data) {
                    std::uint16_t value=0;
                    if(!(usesTransparency&&index==transparentIndex)) {
                        const std::uint8_t rr=palette[std::size_t(index)*3];
                        const std::uint8_t gg=palette[std::size_t(index)*3+1];
                        const std::uint8_t bb=palette[std::size_t(index)*3+2];
                        if(usesTransparency) {
                            value=std::uint16_t((1u<<15)|
                                (std::uint16_t(rr>>3)<<10)|
                                (std::uint16_t(gg>>3)<<5)|(bb>>3));
                        } else {
                            value=std::uint16_t(
                                (std::uint16_t(rr>>3)<<11)|
                                (std::uint16_t(gg>>2)<<5)|(bb>>3));
                        }
                    }
                    hash.add(jkgm::make_span(&value,1).as_const_bytes());
                }
                hashes.insert(static_cast<std::string>(hash.finish()));
            }
        }
        if(hashes.empty())throw std::runtime_error("No MAT signatures generated");
        out.assign(hashes.begin(),hashes.end());
        error.clear();
        return true;
    } catch(const std::exception &e) {
        out.clear();
        error=std::string("Cannot sign ")+matName+" (cel "+
              std::to_string(cel)+"): "+e.what();
        return false;
    }
}
