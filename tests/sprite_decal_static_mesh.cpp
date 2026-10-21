// Standalone static-3DO decal receiver regression tests.
#include "../renderer/sprite_decal_static_mesh.hpp"
#include <cassert>
#include <cmath>
int main() {
    using namespace jkgm;
    gpu_3do_matrix34 const identity{{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    std::vector<decal_vec3> const square{
        {{-1,-1,0}},{{1,-1,0}},{{1,1,0}},{{-1,1,0}}};
    decal_world_surface_geometry face{};
    assert(build_static_sprite_receiver(identity,square,0,face));
    assert(std::fabs(face.normal[2]-1.0f)<1e-5f);
    decal_world_geometry cache{};
    cache.valid_world=true;
    cache.surface_count=1;
    cache.receivers.push_back(face);
    cache.acceleration.build(cache.receivers);
    decal_projector proj{};
    proj.origin={{0,0,-0.1f}};
    proj.look={{0,0,-1}};
    proj.up={{0,1,0}};
    proj.right={{1,0,0}};
    proj.depth=0.25f;
    // The receiver is behind this projector; it must not be picked.
    assert(project_cached_decal_world(cache,proj).triangles.empty());
    proj.origin={{0,0,0.1f}};
    assert(project_cached_decal_world(cache,proj).triangles.size()==2);
    gpu_3do_matrix34 translated=identity;
    translated[9]=3.0f;
    assert(build_static_sprite_receiver(translated,square,1,face));
    assert(std::fabs(face.polygon[0][0]-2.0f)<1e-5f);
    assert(std::fabs(face.normal[2]-1.0f)<1e-5f);
    std::vector<decal_vec3> const collinear{
        {{0,0,0}},{{1,0,0}},{{2,0,0}}};
    assert(!build_static_sprite_receiver(identity,collinear,0,face));
    return 0;
}
