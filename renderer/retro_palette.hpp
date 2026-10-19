#pragma once
// Balanced kd-tree nearest-colour RGB8 lookup, built only when CMP #0 changes.
// Its 64^3 RGBA8 atlas allows one texel fetch per final screen pixel,
// with no per-pixel 256-colour search or dithering.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>
namespace jkgm {
constexpr int retro_lut_steps=64,retro_lut_width=512,retro_lut_height=512;
using retro_palette_rgb=std::array<uint8_t,768>;
struct retro_kd_tree {
    struct node { std::array<int,3> rgb{}; int left=-1,right=-1; };
    std::array<node,256> nodes{};
    int root=-1;
    explicit retro_kd_tree(retro_palette_rgb const &pal) {
        std::array<int,256> indices{};
        for(int i=0;i<256;i++) {
            indices[i]=i;
            for(int c=0;c<3;c++) nodes[i].rgb[c]=int(pal[3*i+c]);
        }
        root=build(indices,0,256,0);
    }
    int build(std::array<int,256> &ids,int begin,int end,int depth) {
        if(begin==end) return -1;
        int mid=(begin+end)/2,axis=depth%3;
        std::nth_element(ids.begin()+begin,ids.begin()+mid,
                         ids.begin()+end,[&](int a,int b) {
            int av=nodes[a].rgb[axis],bv=nodes[b].rgb[axis];
            return av==bv?a<b:av<bv;
        });
        int i=ids[mid];
        nodes[i].left=build(ids,begin,mid,depth+1);
        nodes[i].right=build(ids,mid+1,end,depth+1);
        return i;
    }
    void find(int i,std::array<int,3> const &rgb,int axis,int &best,
              int &bestdist) const {
        if(i<0) return;
        int d=0;
        for(int c=0;c<3;c++) {
            int diff=nodes[i].rgb[c]-rgb[c]; d+=diff*diff;
        }
        if(d<bestdist || (d==bestdist && i<best)) {
            bestdist=d;best=i;
        }
        int delta=rgb[axis]-nodes[i].rgb[axis];
        int next=(axis+1)%3;
        find(delta<0?nodes[i].left:nodes[i].right,rgb,next,best,bestdist);
        if(delta*delta<=bestdist)
            find(delta<0?nodes[i].right:nodes[i].left,rgb,next,best,bestdist);
    }
    int nearest(std::array<int,3> const &rgb) const {
        int best=255,dist=std::numeric_limits<int>::max();
        find(root,rgb,0,best,dist);
        return best;
    }
};
inline std::array<int,2> retro_atlas_position(int r,int g,int b) {
    return {{r+64*(g&7),b*8+(g>>3)}};
}
inline std::vector<uint8_t> build_retro_palette_atlas(
    retro_palette_rgb const &pal) {
    retro_kd_tree const tree(pal);
    std::vector<uint8_t> pixels(512*512*4,255);
    for(int b=0;b<64;b++) for(int g=0;g<64;g++) for(int r=0;r<64;r++) {
        std::array<int,3> rgb{{(r*255+31)/63,(g*255+31)/63,(b*255+31)/63}};
        int index=tree.nearest(rgb);
        auto p=retro_atlas_position(r,g,b);
        auto offset=4*(p[1]*512+p[0]);
        for(int c=0;c<3;c++) pixels[offset+c]=pal[3*index+c];
    }
    return pixels;
}
}
