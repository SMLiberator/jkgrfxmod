#pragma once
#include "glad.h"
#include "editor_image.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class PreviewShape { Sphere, Plane, Cube };
// Keep camera framing in sync with the actual cube mesh dimensions.
constexpr float kPreviewCubeHalfExtent = 0.85f;

enum PreviewTexture : std::size_t {
    BaseMap, SurfaceMap, EnvironmentMap, NormalMap, NormalMap2,
    EmissiveMap, MatcapMap, MatcapSurfaceMap, CoatSurfaceMap, CoatNormalMap,
    DisplacementMap, TextureCount
};

// Describes the decoded image currently bound to a material preview slot.
struct PreviewImageInfo {
    int width=0,height=0,channels=0;
    std::uintmax_t bytes=0;
    bool valid() const {return width>0&&height>0;}
};

struct PreviewMaterial {
    std::array<std::string, TextureCount> files{};
    std::array<std::shared_ptr<const editor_images::Image>,TextureCount>
        stagedImages{}; // decoded edited pixels, before any disk write
    std::array<float, 4> albedo{1, 1, 1, 1};
    std::array<float, 3> emissive{0, 0, 0};
    std::array<float, 3> matcapFactor{1, 1, 1};
    float reflection = 0, roughness = 0.0f, metalness = 0;
    float normalStrength = 1, normalStrength2 = 1;
    float displacementFactor = 0; // signed, same as compiled metadata
    float matcapRoughness = 0;
    float coat = 0, coatRoughness = .1f, coatTransmission = 1;
    int alphaMode = 0; // 0 smooth/blend, 1 mask, 2 dithered
    int blendMode = 0; // 0 normal, 1 additive, 2 multiply, 3 fluid
    float alphaCutoff = 0.5f;
    // Raw source textures have straight alpha; compiled albedo is premultiplied.
    bool albedoPremultiplied = true;
    // Only used when the material requests its original JK MAT albedo.
    std::string nativeSourceKey;
    int nativeWidth = 0, nativeHeight = 0;
    std::vector<std::uint8_t> nativeRGBA; // straight-alpha, top-left origin
};

class MaterialPreview {
public:
    MaterialPreview();
    ~MaterialPreview();
    bool valid() const { return program_ != 0; }
    const std::string &error() const { return error_; }
    void setMaterial(const PreviewMaterial &mat);
    void setPreset(int preset);
    void setEnvironmentOverride(const std::string &path);
    // Normal studio preview; Fresnel toggle affects EnvMap only (not metadata).
    void draw(int width, int height, float orbitYaw, float orbitPitch,
              float orbitDistance, const std::array<float,3> &orbitTarget,
              PreviewShape shape, bool useEnvFresnel, bool orthographic);
    GLuint outputTexture() const { return output_; }
    GLuint imageTexture(PreviewTexture slot) const {return textures_[slot];}
    const PreviewImageInfo &imageInfo(PreviewTexture slot) const {
        return imageInfo_[slot];
    }
    // On-demand 256-pixel grayscale view of R/G/B/A; cached until reload.
    GLuint channelTexture(PreviewTexture slot,int channel);
    const std::string &environmentDescription() const { return envDescription_; }
    bool hasLoadedEnvironmentFile() const { return loadedEnvironmentFile_; }

private:
    struct Vertex { float x,y,z,nx,ny,nz,u,v; };
    GLuint compileShader(GLenum type, const char *source);
    GLuint loadTexture(const std::string &path, bool srgb,
                       PreviewImageInfo *details=nullptr);
    GLuint uploadStaged(const editor_images::Image &image,bool srgb,
                        PreviewImageInfo *details);
    GLuint createPreset();
    GLuint uploadNativeAlbedo();
    void reloadImages();
    void resize(int width, int height);
    void buildMeshes();

    PreviewMaterial material_;
    std::array<GLuint, TextureCount> textures_{};
    std::array<PreviewImageInfo, TextureCount> imageInfo_{};
    std::array<std::array<GLuint,4>, TextureCount> channelTextures_{};
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, fbo_ = 0, output_ = 0, depth_ = 0;
    std::string envOverride_, envDescription_, error_;
    bool loadedEnvironmentFile_ = false;
    int width_ = 0, height_ = 0, preset_ = 0;
    GLsizei sphereCount_ = 0, planeCount_ = 0, cubeCount_ = 0;
    float planeAspect_ = 1.0f; // loaded albedo texture, else emissive
};
