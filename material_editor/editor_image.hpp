#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Editor-side image staging. Color textures are premultiplied in LINEAR
// space and encoded as sRGB bytes, matching compile/main.cpp. Data maps
// bypass gamma conversion. Alpha-free slots are stored as RGB/R PNGs.
namespace editor_images {
enum class Role { Albedo, Color, Data, Height };
struct Image {
    int width=0,height=0,channels=0,sourceChannels=0;
    bool premultiplied=false;
    std::vector<std::uint8_t> pixels; // tightly packed, top-down
    bool valid() const {
        return width>0&&height>0&&
            (channels==1||channels==3||channels==4)&&
            pixels.size()==std::size_t(width)*height*channels;
    }
};
Image fromRGBA(const std::uint8_t *rgba,int width,int height,
               int sourceChannels,Role role,bool sourcePack);
Image load(const std::filesystem::path &file,Role role,bool sourcePack);
std::vector<std::uint8_t> png(const Image &image);
// Reuse a compatible existing PNG in the same folder as script.json or
// metadata.json. Never copy or convert that image when another material
// chooses it. Images in other folders/non-PNGs still use normal staging.
std::optional<std::string> packLocalPngReference(
    const std::filesystem::path &document,
    const std::filesystem::path &selected);
} // namespace editor_images
