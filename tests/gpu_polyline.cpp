#include "renderer/gpu_polyline.hpp"
#include "renderer/saber_trail_history.hpp"
#include "renderer/native_saber_damage.hpp"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cmath>
int main() {
    jkgm::gpu_3do_matrix34 view{{0,-1,0,1,0,0,0,0,1,10,20,30}},inverse{};
    assert(jkgm::gpu_polyline_inverse_view(view,inverse));
    auto combined=jkgm::gpu_3do_native_modelview(view,inverse);
    for(int k=0;k<12;k++) {
        float const expected=(k==0 || k==4 || k==8)?1.0f:0.0f;
        assert(std::abs(combined[k]-expected)<1.e-4f);
    }
    jkgm::gpu_polyline_capture capture{};
    for(unsigned f=0;f<5;f++) {
        jkgm::gpu_polyline_face face{};
        face.material=f<3?0x22000U:0x24000U;
        face.count=f==3?5:4;
        face.cel=2;
        face.position[0]={{0,1,float(f)}};
        face.position[1]={{1,1,float(f)}};
        face.position[2]={{1,2,float(f)}};
        face.position[3]={{0,2,float(f)}};
        face.position[4]={{-0.5f,1.5f,float(f)}};
        // Per-face offsets are applied in native MAT-pixel units, just
        // like rdPrimit3_ClipFace; end caps must not inherit the side shift.
        face.uv_offset={{f<3?12.5f:0.0f,f<3?-27.25f:3.0f}};
        for(unsigned i=0;i<face.count;i++)face.uv[i]={{float(i)*16,5}};
        capture.faces.push_back(face);
    }
    auto mesh=jkgm::make_gpu_polyline_geometry(capture);
    assert(mesh && mesh->groups.size()==5);
    assert(mesh->indices.size()==33); // 4*2 tris + 1*3 tris
    assert(mesh->vertices.size()==21);
    assert(mesh->material_refs.size()==2);
    assert(mesh->groups[0].material==0x22000U);
    assert(mesh->groups[4].material==0x24000U);
    assert((mesh->groups[4].face_type&1U)!=0U);
    assert(std::abs(mesh->vertices[0].u-12.5f)<1.e-5f);
    assert(std::abs(mesh->vertices[0].v+22.25f)<1.e-5f);
    assert(std::abs(mesh->vertices[1].u-28.5f)<1.e-5f);
    assert(std::abs(mesh->vertices[12].u-0.0f)<1.e-5f);
    assert(std::abs(mesh->vertices[12].v-8.0f)<1.e-5f);
    // A later frame reads the live native offset afresh: no accumulation.
    capture.faces[0].uv_offset={{-4.0f,8.0f}};
    auto next_frame=jkgm::make_gpu_polyline_geometry(capture);
    assert(next_frame);
    assert(std::abs(next_frame->vertices[0].u+4.0f)<1.e-5f);
    assert(std::abs(next_frame->vertices[0].v-13.0f)<1.e-5f);
    assert(std::abs(mesh->vertices[0].u-12.5f)<1.e-5f);
    capture.faces[0].uv_offset={{12.5f,-27.25f}};
    // Every side gets a delayed strip, but the center fan is generated ONCE
    // to avoid three coplanar identical triangles fighting over depth.
    std::array<float,3> delayed_camera{{3.0f,4.0f,5.0f}};
    auto fan=jkgm::make_gpu_polyline_geometry(capture,&delayed_camera);
    assert(fan && fan->groups.size()==5);
    assert(fan->vertices.size()==36U);
    assert(fan->indices.size()==54U);
    assert(fan->groups[0].count==15U); // shared center fan once
    assert(fan->groups[1].count==12U);
    assert(fan->groups[2].count==12U);
    assert(fan->groups[3].count==mesh->groups[3].count);
    assert(fan->groups[4].count==mesh->groups[4].count);
    assert(fan->groups[0].material==mesh->groups[0].material);
    assert(fan->material_refs==mesh->material_refs);
    assert(std::abs(fan->vertices[0].u-12.5f)<1.e-5f);
    assert(std::abs(fan->vertices[1].u-20.5f)<1.e-5f);
    assert(std::abs(fan->vertices[4].u-20.5f)<1.e-5f);
    assert(std::abs(fan->vertices[1].x-
                    mesh->vertices[1].x)<1.e-6f);
    assert(std::abs(fan->vertices[4].x-
                    (mesh->vertices[0].x+3.f-0.5f))<1.e-5f);
    assert(std::abs(fan->vertices[8].u-52.5f)<1.e-5f);
    assert(std::abs(fan->vertices[8].v-(-22.25f))<1.e-5f);
    std::array<float,3> base_world{},tip_world{};
    assert(jkgm::gpu_polyline_blade_pose(
        capture,inverse,base_world,tip_world));
    double pose_gap=0;
    for(int i=0;i<3;++i)
        pose_gap+=double(tip_world[i]-base_world[i])*
                  double(tip_world[i]-base_world[i]);
    // Synthetic five-vertex tip cap is off-center in X; preserve its
    // measured cap centroid rather than assuming a unit-length blade.
    assert(std::abs(pose_gap-1.04)<1.e-5);

    // An actual 60-ms delay: both edges advance; when motion stops the
    // delayed blade catches up and the triangle becomes degenerate.
    jkgm::saber_trail_history history;
    jkgm::saber_trail_key const right{0x123400005678ULL,0},
                                  left{0x123400005678ULL,1};
    jkgm::saber_trail_vec3 origin{{0,0,0}},lag{};
    assert(!history.advance(right,1.00,true,origin,
                            {{0,1,0}},0.06,lag));
    assert(history.advance(right,1.02,true,origin,
                           {{0.5f,0.8660254f,0}},0.06,lag));
    assert(std::abs(lag[0])<1.e-5f);
    assert(!history.advance(left,1.02,false,origin,
                            {{0.5f,0.8660254f,0}},0.06,lag));
    assert(history.advance(right,1.04,true,origin,
                           {{0.8660254f,0.5f,0}},0.06,lag));
    assert(history.advance(right,1.07,false,origin,
                           {{0.8660254f,0.5f,0}},0.06,lag));
    assert(!history.advance(right,1.101,false,origin,
                            {{0.8660254f,0.5f,0}},0.06,lag));
    // A moving hilt alone must not generate a luminous triangle.
    history.clear();
    assert(!history.advance(right,2.0,true,origin,
                            {{0,1,0}},0.06,lag));
    assert(!history.advance(right,2.03,true,{{1,0,0}},
                            {{1,1,0}},0.06,lag));
    history.set_world(0x10000U);
    assert(!history.advance(right,2.04,false,origin,
                            {{0,1,0}},0.06,lag));

    // The executable's JKFLAG_SABERDAMAGE (+0x2a8, mask 0x2) is read
    // directly from the rdThing-linked SithThing, independent of COG
    // callbacks or KEY marker-hook installation.
    constexpr uintptr_t mem_base=0x200000U;
    constexpr uint32_t native_owner=uint32_t(mem_base+0x100U);
    constexpr uintptr_t native_rdthing=mem_base+0x500U;
    std::array<uint8_t,0x1000U> native_memory{};
    auto write32=[&](uintptr_t at,uint32_t value) {
        assert(at>=mem_base &&
               at-mem_base+sizeof(value)<=native_memory.size());
        std::memcpy(native_memory.data()+(at-mem_base),
                    &value,sizeof(value));
    };
    auto read_native=[&](uintptr_t at,void *dst,size_t size) {
        if(at<mem_base || size>native_memory.size() ||
           at-mem_base>native_memory.size()-size)return false;
        std::memcpy(dst,native_memory.data()+(at-mem_base),size);
        return true;
    };
    write32(native_rdthing+jkgm::jk_saber_rdthing_owner_offset,native_owner);
    write32(native_owner+jkgm::jk_saber_thing_type_offset,10U);
    write32(native_owner+jkgm::jk_saber_thing_signature_offset,21U);
    write32(native_owner+jkgm::jk_saber_thing_flags_offset,0U);
    jkgm::native_saber_damage_state saber_state{};
    assert(jkgm::read_native_saber_damage(
        read_native,native_rdthing,saber_state));
    assert(!saber_state.swing &&
           saber_state.owner==((uint64_t(native_owner)<<32)|21U));
    write32(native_owner+jkgm::jk_saber_thing_flags_offset,0x82U);
    assert(jkgm::read_native_saber_damage(
        read_native,native_rdthing,saber_state) && saber_state.swing);
    write32(native_owner+jkgm::jk_saber_thing_flags_offset,0x80U);
    assert(jkgm::read_native_saber_damage(
        read_native,native_rdthing,saber_state) && !saber_state.swing);
    // Initial signature zero must not suppress an otherwise valid saber.
    write32(native_owner+jkgm::jk_saber_thing_signature_offset,0U);
    assert(jkgm::read_native_saber_damage(
        read_native,native_rdthing,saber_state));
    write32(native_owner+jkgm::jk_saber_thing_type_offset,0U);
    assert(!jkgm::read_native_saber_damage(
        read_native,native_rdthing,saber_state));
    assert(!jkgm::read_native_saber_damage(
        read_native,mem_base+0xf00U,saber_state));

    // Malformed native offsets must fail capture, rather than poison the GPU.
    capture.faces[0].uv_offset={{INFINITY,0.0f}};
    assert(!jkgm::make_gpu_polyline_geometry(capture));
    capture.faces[0].uv_offset={{0,0}};
    capture.failed=true;
    assert(!jkgm::make_gpu_polyline_geometry(capture));
}
