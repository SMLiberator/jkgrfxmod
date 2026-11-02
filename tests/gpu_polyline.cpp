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
        for(unsigned i=0;i<face.count;i++)face.uv[i]={{float(i)*16,0}};
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
    capture.failed=true;
    assert(!jkgm::make_gpu_polyline_geometry(capture));
}
