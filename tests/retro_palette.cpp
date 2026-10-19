#include "renderer/retro_palette.hpp"
#include <cassert>
int main() {
    jkgm::retro_palette_rgb palette{};
    palette[3]=palette[4]=palette[5]=255;
    palette[6]=255;palette[10]=255;palette[14]=255;
    jkgm::retro_kd_tree const kd(palette);
    assert(kd.nearest({{0,0,0}})==0);
    assert(kd.nearest({{255,255,255}})==1);
    assert(kd.nearest({{255,0,0}})==2);
    assert(kd.nearest({{0,255,0}})==3);
    assert(kd.nearest({{0,0,255}})==4);
    for(int i=0;i<200;i++) {
        std::array<int,3> rgb{{(i*91)%256,(i*43)%256,(i*173)%256}};
        int best=0,dist=999999;
        for(int p=0;p<256;p++) {
            int d=0;for(int c=0;c<3;c++) {
                int v=rgb[c]-int(palette[3*p+c]);d+=v*v;
            }
            if(d<dist){dist=d;best=p;}
        }
        assert(kd.nearest(rgb)==best);
    }
    auto lut=jkgm::build_retro_palette_atlas(palette);
    assert(lut.size()==512u*512u*4u);
    auto red=jkgm::retro_atlas_position(63,0,0);
    int k=4*(red[1]*512+red[0]);
    assert(lut[k]==255 && lut[k+1]==0 && lut[k+2]==0);
}
