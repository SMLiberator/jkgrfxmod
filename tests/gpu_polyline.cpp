#include "renderer/gpu_polyline.hpp"
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
    // Malformed native offsets must fail capture, rather than poison the GPU.
    capture.faces[0].uv_offset={{INFINITY,0.0f}};
    assert(!jkgm::make_gpu_polyline_geometry(capture));
    capture.faces[0].uv_offset={{0,0}};
    capture.failed=true;
    assert(!jkgm::make_gpu_polyline_geometry(capture));
}
