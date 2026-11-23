#include "editor_image.hpp"
#include "common/stb_image.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace editor_images;
void expect(bool good,const char *reason) {
    if(!good)throw std::runtime_error(reason);
}
void testColorAlpha() {
    // Test 2x2 power-of-two images. RGB white * 50% alpha must become
    // about 188 in sRGB (not 128, which would be incorrect gamma-space PMA).
    const std::array<std::uint8_t,16> rgba={
        255,255,255,128, 255,0,0,255,
        0,255,0,0, 10,20,30,255
    };
    const auto emissive=fromRGBA(rgba.data(),2,2,4,Role::Color,true);
    expect(emissive.valid()&&emissive.channels==3,"Emissive must be RGB");
    expect(emissive.premultiplied,"Emissive must bake alpha into RGB");
    expect(emissive.pixels[0]>=187&&emissive.pixels[0]<=189,
           "Emissive should premultiply in linear RGB");
    expect(emissive.pixels[3]==255&&emissive.pixels[4]==0,
           "Opaque pixels should be unchanged");
    expect(emissive.pixels[6]==0&&emissive.pixels[7]==0&&
           emissive.pixels[8]==0,"Invisible emissive must have zero RGB");

    const auto compiled=fromRGBA(rgba.data(),2,2,4,Role::Albedo,false);
    expect(compiled.channels==4&&compiled.premultiplied,
           "Transparent compiled albedo must retain alpha");
    expect(compiled.pixels[0]>=187&&compiled.pixels[0]<=189&&
           compiled.pixels[3]==128,"Compiled PMA RGB and alpha");
    const auto source=fromRGBA(rgba.data(),2,2,4,Role::Albedo,true);
    expect(source.channels==4&&!source.premultiplied&&
           source.pixels[0]==255&&source.pixels[3]==128,
           "Source albedo must remain straight to avoid double PMA");
    const auto opaque=fromRGBA(rgba.data()+4,1,1,4,Role::Albedo,false);
    expect(opaque.channels==3,"Fully opaque albedo must save as RGB");
}
void testDataChannels() {
    const std::array<std::uint8_t,4> rgba={123,45,67,10};
    const auto surface=fromRGBA(rgba.data(),1,1,4,Role::Data,false);
    expect(surface.channels==3&&!surface.premultiplied,
           "Surface channels must be linear RGB");
    expect(surface.pixels==std::vector<std::uint8_t>({123,45,67}),
           "Surface must preserve all R/G/B exactly");
    const auto height=fromRGBA(rgba.data(),1,1,4,Role::Height,false);
    expect(height.channels==1&&height.pixels[0]==123,
           "Displacement only requires the R channel");
    bool rejected=false;
    try {fromRGBA(rgba.data(),3,1,4,Role::Data,false);}
    catch(const std::exception&) {rejected=true;}
    expect(rejected,"Compiler-incompatible non-power-of-two must be rejected");
}
void testPngChannels() {
    const std::array<std::uint8_t,16> rgba={
        255,100,50,128, 255,0,0,255,
        0,255,0,0, 10,20,30,255
    };
    for(const auto role:{Role::Color,Role::Data,Role::Height,
                          Role::Albedo}) {
        const auto image=fromRGBA(rgba.data(),2,2,4,role,false);
        const auto bytes=png(image);
        expect(bytes.size()>25,"PNG missing signature/IHDR");
        // PNG IHDR color type: 0=grayscale, 2=RGB, 6=RGBA.
        expect(bytes[25]==(image.channels==1?0:
                           image.channels==3?2:6),
               "PNG must contain requested number of channels");
        int w=0,h=0,ch=0;
        stbi_set_flip_vertically_on_load(0);
        auto *decoded=stbi_load_from_memory(bytes.data(),int(bytes.size()),
                                            &w,&h,&ch,0);
        expect(decoded!=nullptr,"Encoded PNG must be decodable");
        expect(w==2&&h==2&&ch==image.channels,
               "PNG decoded channels/dimensions differ");
        stbi_image_free(decoded);
    }
}
void testPackLocalReuse() {
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path()/
        ("jkgm_image_reuse_"+std::to_string(std::chrono::steady_clock::now()
                                           .time_since_epoch().count()));
    struct Clean {
        fs::path path;
        ~Clean() {std::error_code ec;fs::remove_all(path,ec);}
    } cleanup{base};
    const auto pack=base/"pack";
    const auto external=base/"external";
    const auto nested=pack/"subfolder";
    fs::create_directories(pack);
    fs::create_directories(external);
    fs::create_directories(nested);
    const auto json=pack/"metadata.json";
    const std::array<std::uint8_t,16> white={
        255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255
    };
    const auto content=png(fromRGBA(white.data(),2,2,4,Role::Albedo,false));
    const auto write=[&](const fs::path &path){
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
    };
    write(pack/"shared.png");
    write(pack/"SECOND.PNG");
    write(pack/"source.jpg");
    write(external/"shared.png");
    write(nested/"nested.png");
    auto reuse=packLocalPngReference(json,pack/"shared.png");
    expect(reuse.has_value()&&*reuse=="shared.png",
           "Pack-local existing PNG should be referenced unchanged");
    reuse=packLocalPngReference(json,pack/"SECOND.PNG");
    expect(reuse.has_value()&&*reuse=="SECOND.PNG",
           "Filename case must be preserved for a local PNG");
    expect(!packLocalPngReference(json,external/"shared.png").has_value(),
           "External PNG should retain the staged import workflow");
    expect(!packLocalPngReference(json,nested/"nested.png").has_value(),
           "Nested image is not directly pack-local");
    expect(!packLocalPngReference(json,pack/"source.jpg").has_value(),
           "Non-PNG files need conversion to the supported pack format");
    expect(!packLocalPngReference(json,pack/"missing.png").has_value(),
           "A nonexistent image cannot be reused");
    auto repeated=packLocalPngReference(json,pack/"shared.png");
    expect(repeated==std::optional<std::string>("shared.png"),
           "Multiple materials should resolve to the same filename");
}
int main() {
    try {
        testColorAlpha();testDataChannels();testPngChannels();
        testPackLocalReuse();
        std::cout<<"Image premultiplication and compact PNG tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<"Image codec test failed: "<<e.what()<<"\n";
        return 1;
    }
}
