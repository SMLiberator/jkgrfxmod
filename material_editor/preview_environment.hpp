#pragma once
#include <string>

// Selection of preview reflections is independent of the lighting preset.
// Runtime game capture can replace the global EnvMap, so the editor must
// allow the photographer's preset to override any authored material image.
enum class PreviewEnvironmentSource { MaterialPack, Preset, Custom };

struct PreviewEnvironmentImage {
    bool preset=false;
    bool stagedMaterial=false;
    std::string filename;
};

inline PreviewEnvironmentImage choosePreviewEnvironment(
        PreviewEnvironmentSource source,
        const std::string &materialFile,const std::string &customFile,
        bool materialImageIsStaged) {
    switch(source) {
    case PreviewEnvironmentSource::Preset:
        return {true,false,{}};
    case PreviewEnvironmentSource::Custom:
        return {false,false,customFile};
    case PreviewEnvironmentSource::MaterialPack:
        return {false,materialImageIsStaged,materialFile};
    }
    return {true,false,{}};
}
