#pragma once
// Two-tier JK-style radius culling for independent reflected cameras.
// rdModel3_Draw rejects the model radius BEFORE evaluating joint poses,
// then rdModel3_DrawMesh rejects each posed mesh radius. Preserve these
// early exits without touching the primary rdCamera/portal visibility.
#include "gpu_3do_frustum.hpp"
#include "planar_reflection_camera.hpp"
#include "planar_reflection_things.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {

// Upper bound on the norm of a 3x3 transform. JK's ordinary mesh path
// uses its longest axis; for orthogonal rotations/scales that is exact.
// For unusual sheared transforms use the spectral norm bound so a
// potentially visible stretched limb is never rejected prematurely.
inline float planar_reflection_radius_scale(std::array<float,9> const &m)
{
    for(float v:m)if(!std::isfinite(v))return 0.0f;
    std::array<float,3> norms{};
    for(int i=0;i<3;++i) {
        float len2=0.0f;
        for(int j=0;j<3;++j)len2+=m[3*i+j]*m[3*i+j];
        if(!std::isfinite(len2))return 0.0f;
        norms[i]=std::sqrt(len2);
    }
    bool orthogonal=true;
    for(int i=0;i<3;++i)for(int j=i+1;j<3;++j) {
        float dot=0.0f;
        for(int k=0;k<3;++k)dot+=m[3*i+k]*m[3*j+k];
        if(!std::isfinite(dot) ||
           std::fabs(dot)>0.0001f*std::max(1.0f,norms[i]*norms[j]))
            orthogonal=false;
    }
    float bound=std::max(1.0f,std::max(norms[0],std::max(norms[1],norms[2])));
    if(!orthogonal) {
        // ||M||_2 <= sqrt(||M||_1 * ||M||_inf).
        float col_sum=0.0f,row_sum=0.0f;
        for(int i=0;i<3;++i) {
            float c=0.0f,r=0.0f;
            for(int j=0;j<3;++j) {
                c+=std::fabs(m[3*i+j]);
                r+=std::fabs(m[3*j+i]);
            }
            col_sum=std::max(col_sum,c);
            row_sum=std::max(row_sum,r);
        }
        bound=std::max(bound,std::sqrt(col_sum*row_sum));
    }
    return std::isfinite(bound)?bound:0.0f;
}

inline bool planar_reflection_sphere_outside_frustum(
    gpu_3do_native_frustum const &frustum,
    gpu_3do_matrix34 const &world_to_view,
    std::array<float,3> const &world_center,float world_radius)
{
    if(!gpu_3do_matrix_finite(world_to_view) ||
       !std::isfinite(world_radius) || world_radius<=0.0f)
        return false; // fail open if radius or matrix is not trustworthy
    for(float v:world_center)if(!std::isfinite(v))return false;
    auto const p=gpu_3do_native_transform_point(world_to_view,world_center);
    return gpu_3do_sphere_outside(frustum,true,
        p[0],p[1],p[2],world_radius+0.02f);
}

template<class Read>
bool planar_reflection_thing_outside_frustum(
    Read const &read,planar_reflection_model_thing const &thing,
    gpu_3do_native_frustum const &frustum,
    gpu_3do_matrix34 const &world_to_view)
{
    // JKGM's x86 rdModel3 radius is at +0x5c; validated by the linked
    // numHNodes/+0x54 and hierarchy/+0x58 native layout.
    // SithThing::orient starts at +0x88, position at +0xb8. Read the
    // basis to allow conservative authored scaling/shear.
    if(!planar_reflection_valid_native_pointer(thing.model) ||
       !planar_reflection_valid_native_pointer(thing.thing))
        return false;
    float model_radius=0.0f;
    std::array<float,9> basis{};
    if(!read(thing.model+0x5cU,&model_radius,sizeof(model_radius)) ||
       !read(thing.thing+0x88U,basis.data(),sizeof(basis)))
        return false;
    float const scale=planar_reflection_radius_scale(basis);
    if(scale<=0.0f || !std::isfinite(model_radius) || model_radius<=0.0f)
        return false;
    return planar_reflection_sphere_outside_frustum(
        frustum,world_to_view,thing.position,model_radius*scale);
}

inline bool planar_reflection_mesh_outside_frustum(
    gpu_3do_native_frustum const &frustum,
    gpu_3do_matrix34 const &world_to_view,
    gpu_3do_matrix34 const &mesh_to_world,float mesh_radius)
{
    if(!gpu_3do_matrix_finite(mesh_to_world) ||
       !std::isfinite(mesh_radius) || mesh_radius<=0.0f)
        return false;
    std::array<float,9> linear{};
    std::copy_n(mesh_to_world.begin(),9U,linear.begin());
    float const scale=planar_reflection_radius_scale(linear);
    if(scale<=0.0f)return false;
    // Source mesh center lives at the animated node's translation.
    // No pose is rebuilt here: callers already obtained this placement.
    auto const view_mesh=gpu_3do_native_modelview(world_to_view,mesh_to_world);
    if(!gpu_3do_matrix_finite(view_mesh))return false;
    return gpu_3do_sphere_outside(frustum,true,
        view_mesh[9],view_mesh[10],view_mesh[11],
        mesh_radius*scale+0.02f);
}
} // namespace jkgm
