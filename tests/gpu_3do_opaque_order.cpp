// Verify cached material-group order against the previous draw sorter.
#include "renderer/gpu_3do_preview.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace jkgm;
struct draw_case {
    size_t instance,group,order;
    float depth;
};
static bool opaque_less(draw_case const &a,draw_case const &b,
                        gpu_3do_geometry const &g) {
    if(a.instance!=b.instance)return a.instance<b.instance;
    auto const &x=g.groups[a.group],&y=g.groups[b.group];
    if(x.material!=y.material)return x.material<y.material;
    if(x.cel!=y.cel)return x.cel<y.cel;
    if(x.face_type!=y.face_type)return x.face_type<y.face_type;
    if(x.nonplanar!=y.nonplanar)return x.nonplanar<y.nonplanar;
    if(x.solid_color!=y.solid_color)return x.solid_color<y.solid_color;
    return a.order<b.order;
}
static bool selected(size_t inst,size_t group) {
    return (inst+group)%4!=1; // per-Thing visibility/material filtering
}
static void equal_draws(std::vector<draw_case> const &a,
                        std::vector<draw_case> const &b) {
    assert(a.size()==b.size());
    for(size_t i=0;i<a.size();++i) {
        assert(a[i].instance==b[i].instance);
        assert(a[i].group==b[i].group);
    }
}
int main() {
    gpu_3do_geometry empty;
    assert(empty.cached_opaque_group_order().empty());
    gpu_3do_geometry mesh;
    auto add=[&](uintptr_t mat,int cel,uint32_t face,bool np,bool solid) {
        gpu_3do_group g{};
        g.material=mat;g.cel=cel;g.face_type=face;
        g.nonplanar=np;g.solid_color=solid;
        mesh.groups.push_back(g);
    };
    add(9,1,2,false,false);
    add(3,-1,0,false,false);
    add(9,1,2,false,false);
    add(3,-1,0,true,false);
    add(9,0,1,false,true);
    add(9,0,1,false,false);
    add(3,-1,0,false,false);
    add(9,1,2,false,true);
    add(9,1,2,true,false);
    add(3,-1,0,false,true);
    auto const &indices=mesh.cached_opaque_group_order();
    assert(&indices==&mesh.cached_opaque_group_order());
    assert(indices.size()==mesh.groups.size());
    std::vector<size_t> expected{1,6,9,3,5,4,0,2,7,8};
    assert(indices==expected);
    std::vector<draw_case> old_opaque,cached_opaque;
    std::vector<draw_case> old_alpha,cached_alpha;
    size_t accepted_count=0,base=0;
    for(size_t inst=0;inst<3;++inst) {
        for(size_t idx=0;idx<mesh.groups.size();++idx) {
            if(!selected(inst,idx))continue;
            draw_case d{inst,idx,accepted_count++,float((idx+inst)%3)};
            old_opaque.push_back(d);
            old_alpha.push_back(d);
        }
        for(size_t idx:indices) {
            if(!selected(inst,idx))continue;
            draw_case d{inst,idx,base+idx,float((idx+inst)%3)};
            cached_opaque.push_back(d);
            cached_alpha.push_back(d);
        }
        base+=mesh.groups.size();
    }
    std::sort(old_opaque.begin(),old_opaque.end(),
        [&](draw_case const &a,draw_case const &b) {
            return opaque_less(a,b,mesh);
        });
    equal_draws(old_opaque,cached_opaque);
    auto depth_less=[](draw_case const &a,draw_case const &b) {
        if(a.depth==b.depth)return a.order<b.order;
        return a.depth>b.depth;
    };
    std::sort(old_alpha.begin(),old_alpha.end(),depth_less);
    std::sort(cached_alpha.begin(),cached_alpha.end(),depth_less);
    equal_draws(old_alpha,cached_alpha);
    gpu_3do_geometry one;
    one.groups.emplace_back();
    assert(one.cached_opaque_group_order()==std::vector<size_t>{0});
    std::cout<<"GPU 3DO cached opaque grouping and alpha tie parity OK\n";
}
