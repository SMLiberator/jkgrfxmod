#pragma once
#include "glad.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum PreviewTexture : std::size_t {
    BaseMap, SurfaceMap, EnvironmentMap, NormalMap, NormalMap2,
    EmissiveMap, MatcapMap, MatcapSurfaceMap, CoatSurfaceMap, CoatNormalMap,
    TextureCount
};

struct PreviewMaterial {
    std::array<std::string, TextureCount> files{};
    std::array<float, 4> albedo{1, 1, 1, 1};
    std::array<float, 3> emissive{0, 0, 0};
    std::array<float, 3> matcapFactor{1, 1, 1};
    float reflection = 0, roughness = 0.0f, metalness = 0;
    float normalStrength = 1, normalStrength2 = 1;
    float matcapRoughness = 0;
    float coat = 0, coatRoughness = .1f, coatTransmission = 1;
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
    void draw(int width, int height, const std::array<float,4> &orientation, bool plane);
    GLuint outputTexture() const { return output_; }

private:
    struct Vertex { float x,y,z,nx,ny,nz,u,v; };
    GLuint compileShader(GLenum type, const char *source);
    GLuint loadTexture(const std::string &path, bool srgb);
    GLuint createPreset();
    void reloadImages();
    void resize(int width, int height);
    void buildMeshes();

    PreviewMaterial material_;
    std::array<GLuint, TextureCount> textures_{};
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, fbo_ = 0, output_ = 0, depth_ = 0;
    std::string envOverride_, error_;
    int width_ = 0, height_ = 0, preset_ = 0;
    GLsizei sphereCount_ = 0, planeCount_ = 0;
};
