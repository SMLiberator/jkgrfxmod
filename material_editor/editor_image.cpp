#include "editor_image.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "common/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace editor_images {
namespace {
float srgbToLinear(float value) {
    return value<=0.04045f?value/12.92f:
           std::pow((value+0.055f)/1.055f,2.4f);
}
float linearToSrgb(float value) {
    return value<=0.0031308f?12.92f*value:
           1.055f*std::pow(value,1.0f/2.4f)-0.055f;
}
std::uint8_t quantize(float value) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(std::clamp(value,0.0f,1.0f)*255.0f),0L,255L));
}
bool powerOfTwo(int dimension) {
    return dimension>0&&(dimension&(dimension-1))==0;
}
void checkDimensions(int w,int h) {
    if(!powerOfTwo(w)||!powerOfTwo(h)||w>8192||h>8192||
       std::uint64_t(w)*h>16777216ULL)
        throw std::runtime_error(
            "Image dimensions must be powers of two (up to 16M pixels).");
}
void collect(void *context,void *data,int size) {
    if(size<=0)return;
    auto &out=*static_cast<std::vector<std::uint8_t> *>(context);
    const auto *start=static_cast<const std::uint8_t *>(data);
    out.insert(out.end(),start,start+size);
}
} // namespace

Image fromRGBA(const std::uint8_t *rgba,int width,int height,
               int sourceChannels,Role role,bool sourcePack) {
    if(!rgba)throw std::runtime_error("Missing decoded image pixels.");
    checkDimensions(width,height);
    Image result;
    result.width=width;result.height=height;
    result.sourceChannels=sourceChannels;
    const bool isAlbedo=role==Role::Albedo;
    const bool isColor=role==Role::Color||isAlbedo;
    // Source albedo must be straight-alpha: the pack compiler will
    // premultiply it when building compiled metadata. The editor's
    // compiled mode stores the finished premultiplied representation.
    result.premultiplied=isColor&&!(isAlbedo&&sourcePack);
    bool needsAlpha=false;
    if(isAlbedo)
        for(std::size_t i=0;i<std::size_t(width)*height;++i)
            if(rgba[4*i+3]!=255){needsAlpha=true;break;}
    result.channels=role==Role::Height?1:needsAlpha?4:3;
    result.pixels.resize(std::size_t(width)*height*result.channels);
    for(std::size_t p=0;p<std::size_t(width)*height;++p) {
        const auto *input=rgba+4*p;
        auto *output=result.pixels.data()+p*result.channels;
        if(role==Role::Height) {
            output[0]=input[0];
            continue;
        }
        const float alpha=float(input[3])/255.0f;
        for(int channel=0;channel<3;++channel)
            output[channel]=result.premultiplied?
                quantize(linearToSrgb(
                    srgbToLinear(float(input[channel])/255.0f)*alpha)):
                input[channel];
        if(result.channels==4)
            output[3]=input[3];
    }
    return result;
}
Image load(const std::filesystem::path &path,Role role,bool sourcePack) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("Unable to open selected texture.");
    const auto length=stream.tellg();
    if(length<=0||length>std::streamoff(256*1024*1024))
        throw std::runtime_error("Texture file is empty or too large.");
    stream.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if(!stream)throw std::runtime_error("Cannot read selected texture.");
    stbi_set_flip_vertically_on_load(0);
    int width=0,height=0,sourceChannels=0;
    auto *decoded=stbi_load_from_memory(bytes.data(),int(bytes.size()),
                                        &width,&height,&sourceChannels,4);
    if(!decoded)
        throw std::runtime_error("Unsupported or corrupt source image.");
    try {
        auto image=fromRGBA(decoded,width,height,sourceChannels,role,sourcePack);
        stbi_image_free(decoded);
        return image;
    } catch(...) {
        stbi_image_free(decoded);
        throw;
    }
}
std::vector<std::uint8_t> png(const Image &image) {
    if(!image.valid())throw std::runtime_error("Invalid staged image.");
    std::vector<std::uint8_t> encoded;
    if(!stbi_write_png_to_func(collect,&encoded,image.width,image.height,
                               image.channels,image.pixels.data(),
                               image.width*image.channels)||encoded.empty())
        throw std::runtime_error("Failed to encode PNG texture.");
    return encoded;
}
} // namespace editor_images
