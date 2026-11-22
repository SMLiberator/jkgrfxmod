#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// The editor uses the same MAT/CMP formats as compile/raw_material and
// compile/colormap without linking the compiler or game engine runtime.
struct NativeMatImage {
    int width=0, height=0;
    std::vector<std::uint8_t> rgba; // straight-alpha, top-to-bottom sRGB
    std::string description;
};

class NativeMatSource {
public:
    // Index loose files and GOB/GOO entries under the selected JK/MotS root.
    // Setting an empty root clears the source.
    void setRoot(const std::string &directory);
    void setPalette(const std::string &file);
    const std::string &root() const {return root_;}
    const std::string &palette() const {return palette_;}
    size_t indexedFiles() const {return files_.size();}
    bool load(const std::string &matName,int cel,NativeMatImage &out,
              std::string &error) const;
private:
    struct Entry {
        std::filesystem::path filename;
        std::uint64_t offset=0, length=0;
        bool archive=false;
    };
    std::map<std::string,Entry> files_;
    std::string root_,palette_;
    void indexArchive(const std::filesystem::path &archive);
    bool readAsset(const std::string &name,std::vector<std::uint8_t> &bytes,
                   std::string &source) const;
};
