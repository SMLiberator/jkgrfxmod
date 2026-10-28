#include "renderer/sprite_decal_clip.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
static bool eq(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    decal_projector p{};
    p.depth=0.25f;
    // Facing polygon: a 2x2 wall directly in front of the projector.
    std::vector<decal_vec3> wall={
        {{-1,0.1f,-1}},{{-1,0.1f,1}},{{1,0.1f,1}},{{1,0.1f,-1}}
    };
    auto poly=clip_projected_decal(p,wall,{{0,-1,0}});
    assert(poly.size()==4);
    float minU=100,maxU=-100,minV=100,maxV=-100;
    for(auto const &v:poly) {
        auto uv=projected_decal_uv(v,p);
        minU=std::min(minU,uv[0]);maxU=std::max(maxU,uv[0]);
        minV=std::min(minV,uv[1]);maxV=std::max(maxV,uv[1]);
        assert(v.depth>=0 && v.depth<=p.depth);
    }
    assert(eq(minU,0) && eq(maxU,1));
    assert(eq(minV,0) && eq(maxV,1));
    // Legacy clip helper still rejects reversed walls by default.
    assert(clip_projected_decal(p,wall,{{0,1,0}}).empty());
    // Runtime experiment bypasses the normal test, preserving volume bounds.
    assert(clip_projected_decal(p,wall,{{0,1,0}},0.05f,false).size()==4);
    // A strictly perpendicular wall still has zero projected UV area; the
    // limiter removal alone cannot make its texture wrap around the edge.
    std::vector<decal_vec3> perpendicular={
        {{0.1f,0.0f,-0.5f}},{{0.1f,0.25f,-0.5f}},
        {{0.1f,0.25f,0.5f}},{{0.1f,0.0f,0.5f}}
    };
    assert(clip_projected_decal(p,perpendicular,{{1,0,0}},0.05f,false).empty());
    // A neighboring wall crossing the box edge produces a partial polygon.
    std::vector<decal_vec3> half_wall={
        {{0,0.1f,-1}},{{0,0.1f,1}},{{1,0.1f,1}},{{1,0.1f,-1}}
    };
    poly=clip_projected_decal(p,half_wall,{{0,-1,0}});
    assert(poly.size()>=3);
    for(auto const &v:poly) {
        auto const uv=projected_decal_uv(v,p);
        assert(uv[0]>=0.5f-0.0001f && uv[0]<=1.0001f);
    }
    // A clipped 3DO quad split into two triangles used to produce a
    // repeated corner in EACH clipped polygon, and the GPU builder then
    // rejected the whole decal because a triangle fan had zero-area pieces.
    decal_projector square{};
    square.origin={{3,4,5.175f}};
    square.right={{1,0,0}};
    square.up={{0,1,0}};
    square.look={{0,0,-1}};
    square.width=1.0f;square.height=1.0f;square.depth=0.25f;
    std::vector<decal_vec3> triangle_a={
        {{2,3,5}},{{4,3,5}},{{4,5,5}}};
    std::vector<decal_vec3> triangle_b={
        {{2,3,5}},{{4,5,5}},{{2,5,5}}};
    auto const no_duplicates=[&](std::vector<decal_vertex> const &vertices) {
        assert(vertices.size()==3);
        for(size_t i=0;i<vertices.size();++i) {
            auto const &a=vertices[i];
            auto const &b=vertices[(i+1)%vertices.size()];
            float const dx=a.u-b.u,dy=a.v-b.v,dz=a.depth-b.depth;
            assert(dx*dx+dy*dy+dz*dz>1.e-8f);
        }
    };
    no_duplicates(clip_projected_decal(
        square,triangle_a,{{0,0,1}},0.05f,false));
    no_duplicates(clip_projected_decal(
        square,triangle_b,{{0,0,1}},0.05f,false));
    // Depth-volume rejection prevents projecting onto a wall far behind.
    for(auto &v:wall)v[1]=1;
    assert(clip_projected_decal(p,wall,{{0,-1,0}}).empty());
    // Projector axes must be orthogonal and unit length.
    p.right={{0,0,0}};
    assert(clip_projected_decal(p,half_wall,{{0,-1,0}}).empty());
}
