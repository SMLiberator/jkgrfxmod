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
    // Regression against JKGM's ACTUAL side geometry: the three
    // SideMat faces have THREE vertices apiece, not four! Each is
    // [tip_edge0, base_center, tip_edge1]. The original implementation
    // generated NO fan at all despite logging "fan emitted".
    jkgm::gpu_polyline_capture native_capture{};
    native_capture.faces.resize(5);
    for(unsigned fi=0;fi<5;++fi) {
        auto &face=native_capture.faces[fi];
        face.material=fi<3?0x22000U:0x24000U;
        face.count=fi<3U?3U:(fi==3U?5U:4U);
        face.geo=4;
        face.cel=2;
        face.uv_offset={{0.0f,0.0f}};
    }
    auto set_side=[&](unsigned f,float x0,float x2) {
        auto &side=native_capture.faces[f];
        side.position[0]={{x0,0.f,1.f}};
        side.position[1]={{0.f,0.f,0.f}};
        side.position[2]={{x2,0.f,1.f}};
        side.uv[0]={{0.f,0.f}};
        side.uv[1]={{32.f,128.f}};
        side.uv[2]={{64.f,0.f}};
    };
    set_side(0,-.06f,0.f);
    set_side(1,0.f,.06f);
    set_side(2,.06f,-.06f);
    auto &base_cap=native_capture.faces[3];
    base_cap.position[0]={{0,0,0}}; // center of pentagonal BASE face
    base_cap.position[1]={{-.02f,-.02f,0.f}};
    base_cap.position[2]={{ .02f,-.02f,0.f}};
    base_cap.position[3]={{ .02f, .02f,0.f}};
    base_cap.position[4]={{-.02f, .02f,0.f}};
    auto &tip_cap=native_capture.faces[4];
    tip_cap.position[0]={{-.02f,-.02f,1.f}};
    tip_cap.position[1]={{ .02f,-.02f,1.f}};
    tip_cap.position[2]={{ .02f, .02f,1.f}};
    tip_cap.position[3]={{-.02f, .02f,1.f}};
    std::array<float,3> base_camera{},tip_camera{};
    assert(jkgm::gpu_polyline_cap_pose_camera(
        native_capture,base_camera,tip_camera));
    assert(std::abs(base_camera[2])<1.e-6f);
    assert(std::abs(tip_camera[2]-1.f)<1.e-6f);
    std::array<float,3> native_base_world{},native_tip_world{};
    assert(jkgm::gpu_polyline_blade_pose(
        native_capture,inverse,native_base_world,native_tip_world));
    double native_length2=0.0;
    for(int n=0;n<3;++n)
        native_length2+=double(native_tip_world[n]-native_base_world[n])*
                        double(native_tip_world[n]-native_base_world[n]);
    assert(std::abs(native_length2-1.0)<1.e-5);

    // Original five faces = 3*1 + 3 + 2 = 8 triangles.
    auto native_mesh=jkgm::make_gpu_polyline_geometry(native_capture);
    assert(native_mesh && native_mesh->indices.size()==24U);
    assert(native_mesh->vertices.size()==18U);
    // The full native blade must remain BIT-FOR-BIT geometrically unchanged
    // when the delayed blade is enabled. The delayed side triangles share
    // the hilt, but both tip-rim vertices move with the historical tip.
    std::array<float,3> delayed_camera{{.5f,0.0f,1.0f}};
    auto fan=jkgm::make_gpu_polyline_geometry(
        native_capture,&delayed_camera);
    assert(fan && fan->groups.size()==5U);
    // 8 native tris + 3 delayed sides + 2 tip cap tris + 1 3D fan.
    assert(fan->vertices.size()==34U);
    assert(fan->indices.size()==42U);
    assert(fan->groups[0].count==6U);
    assert(fan->groups[1].count==9U);
    assert(fan->groups[2].count==6U);
    assert(fan->groups[3].count==9U);
    assert(fan->groups[4].count==12U);
    assert(fan->material_refs.size()==2U);
    unsigned const native_begin[5]={0,3,6,9,14};
    unsigned const fan_begin[5]={0,6,15,21,26};
    for(unsigned f=0;f<5U;++f) {
        for(unsigned k=0;k<native_capture.faces[f].count;++k) {
            auto const &original=native_mesh->vertices[native_begin[f]+k];
            auto const &live=fan->vertices[fan_begin[f]+k];
            assert(original.x==live.x && original.y==live.y &&
                   original.z==live.z);
            assert(original.u==live.u && original.v==live.v);
        }
        assert((fan->groups[f].face_type&1U)!=0U);
    }
    assert(std::abs(fan->vertices[3].x-.44f)<1.e-5f);
    assert(std::abs(fan->vertices[4].z)<1.e-5f); // SAME hilt
    assert(std::abs(fan->vertices[5].x-.5f)<1.e-5f);
    assert(std::abs(fan->vertices[3].u-0.f)<1.e-5f);
    assert(std::abs(fan->vertices[5].u-64.f)<1.e-5f);
    // The joining triangle is exactly [hilt, current tip, delayed tip].
    assert(fan->vertices[12].x==base_camera[0] &&
           fan->vertices[12].z==base_camera[2]);
    assert(fan->vertices[13].x==tip_camera[0] &&
           fan->vertices[13].z==tip_camera[2]);
    assert(fan->vertices[14].x==delayed_camera[0] &&
           fan->vertices[14].z==delayed_camera[2]);
    // Native SideMat center column: base and both tip positions get
    // their source MAT UVs, including the correct vertical progression.
    assert(fan->vertices[12].u==32.f && fan->vertices[12].v==128.f);
    assert(fan->vertices[13].u==32.f && fan->vertices[13].v==0.f);
    assert(fan->vertices[14].u==32.f && fan->vertices[14].v==0.f);
    assert(fan->vertices[30].x==
           native_capture.faces[4].position[0][0]+.5f);
    assert(fan->vertices[30].u==native_mesh->vertices[14].u);
    // The joining triangle's winding refers to the THREE true centers.
    assert(fan->indices[12]==12U &&
           fan->indices[13]==13U && fan->indices[14]==14U);

    // A depth-only swing MUST stay edge-on, not rotate into screen space.
    // Camera X/Z is the screen plane; Y is depth. The true fan is
    // visible in 3D but its projected X/Z area is correctly zero.
    std::array<float,3> depth_tip{{0.f,.5f,1.f}};
    auto depth_fan=jkgm::make_gpu_polyline_geometry(
        native_capture,&depth_tip);
    assert(depth_fan);
    assert(std::abs(depth_fan->vertices[14].x)<1.e-6f);
    assert(std::abs(depth_fan->vertices[14].y-.5f)<1.e-6f);
    assert(std::abs(depth_fan->vertices[14].z-1.f)<1.e-6f);
    assert(std::abs(depth_fan->vertices[9].y)<1.e-6f);
    assert(std::abs(depth_fan->vertices[10].z)<1.e-6f);
    // All three center points are collinear in screen X/Z, not in 3D.
    float const view_area2=
        (depth_fan->vertices[13].x-depth_fan->vertices[12].x)*
        (depth_fan->vertices[14].z-depth_fan->vertices[12].z)-
        (depth_fan->vertices[13].z-depth_fan->vertices[12].z)*
        (depth_fan->vertices[14].x-depth_fan->vertices[12].x);
    assert(std::abs(view_area2)<1.e-6f);

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
