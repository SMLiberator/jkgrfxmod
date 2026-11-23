#include "preview_environment.hpp"
#include <iostream>
#include <stdexcept>
void check(bool condition,const char *what) {
    if(!condition)throw std::runtime_error(what);
}
int main() {
    try {
        using Source=PreviewEnvironmentSource;
        auto picked=choosePreviewEnvironment(
            Source::MaterialPack,"pack.png","custom.hdr",true);
        check(!picked.preset&&picked.stagedMaterial&&
              picked.filename=="pack.png","material uses staged pack image");
        picked=choosePreviewEnvironment(
            Source::Preset,"pack.png","custom.hdr",true);
        check(picked.preset&&!picked.stagedMaterial&&picked.filename.empty(),
              "preset must override all material/pack EnvMaps");
        picked=choosePreviewEnvironment(
            Source::Custom,"pack.png","custom.hdr",true);
        check(!picked.preset&&!picked.stagedMaterial&&
              picked.filename=="custom.hdr","custom image overrides material");
        picked=choosePreviewEnvironment(Source::MaterialPack,"","custom.hdr",false);
        check(!picked.preset&&!picked.stagedMaterial&&picked.filename.empty(),
              "unassigned material can fall back to preset");
        picked=choosePreviewEnvironment(Source::Custom,"pack.png","",false);
        check(picked.filename.empty()&&!picked.stagedMaterial,
              "empty custom path can safely fall back to preset");
        std::cout<<"Environment source selection tests passed\n";
    } catch(const std::exception &e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
