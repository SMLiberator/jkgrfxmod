#include "hud_dirty_regions.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using jkgm::find_hud_dirty_regions;

static void verify_reconstruction(int width,int height,std::vector<std::uint16_t> const &before,
                                  std::vector<std::uint16_t> const &after,bool force_full=false)
{
    auto changes=find_hud_dirty_regions(after.data(),before.data(),width,height,force_full);
    auto reconstructed=before;
    for(auto const &r:changes.rects) {
        assert(0<=r.left && r.left<r.right && r.right<=width);
        assert(0<=r.top && r.top<r.bottom && r.bottom<=height);
        for(int y=r.top;y<r.bottom;++y)
            for(int x=r.left;x<r.right;++x)
                reconstructed[std::size_t(y)*width+x]=after[std::size_t(y)*width+x];
    }
    assert(reconstructed==after);
}

int main()
{
    constexpr int w=129,h=81;
    std::vector<std::uint16_t> before(w*h,0xf81f),after=before;
    auto unchanged=find_hud_dirty_regions(after.data(),before.data(),w,h,false);
    assert(unchanged.rects.empty() && unchanged.upload_pixels==0 && !unchanged.full_upload);
    after[10*w+15]=0xff;
    auto one=find_hud_dirty_regions(after.data(),before.data(),w,h,false);
    assert(one.rects.size()==1 && !one.full_upload && one.changed_pixels==1);
    assert(one.rects[0].left==15 && one.rects[0].right==16);
    verify_reconstruction(w,h,before,after);
    after[80*w+128]=0xfe;
    verify_reconstruction(w,h,before,after);
    auto first=find_hud_dirty_regions(after.data(),before.data(),w,h,true);
    assert(first.full_upload && first.rects.size()==1 && first.upload_pixels==w*h);
    verify_reconstruction(w,h,before,after,true);
    std::fill(after.begin(),after.end(),0x1234);
    auto all=find_hud_dirty_regions(after.data(),before.data(),w,h,false);
    assert(all.full_upload);
    verify_reconstruction(w,h,before,after);
    // Transparent pixels must be cleared when content disappears.
    before=after;
    after[1*w+5]=0xf81f;
    verify_reconstruction(w,h,before,after);
    // Unaligned dimensions, multiple isolated bands and non-adjacent rows.
    for(int x=0;x<w;++x)after[47*w+x]=0x001f;
    verify_reconstruction(w,h,before,after);
    // Odd sizes and fresh initialization, including a null previous buffer.
    auto initial=find_hud_dirty_regions(after.data(),nullptr,w,h,false);
    assert(initial.full_upload && initial.upload_pixels==w*h);
    assert(find_hud_dirty_regions(nullptr,nullptr,w,h,false).rects.empty());
}
