#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jkgm {
    struct hud_dirty_rect {
        int left=0;
        int top=0;
        int right=0;
        int bottom=0;
    };

    struct hud_dirty_regions {
        std::vector<hud_dirty_rect> rects;
        std::uint64_t changed_pixels=0;
        std::uint64_t upload_pixels=0;
        bool full_upload=false;
    };

    // Find changed RGB565 rows before conversion. Full-width memcmp quickly
    // rejects unchanged rows, while 32-row bands limit GL upload call count.
    // Unknown/direct Lock writes are covered: no dirty notifications needed.
    inline hud_dirty_regions find_hud_dirty_regions(
        std::uint16_t const *source,std::uint16_t const *previous,
        int width,int height,bool force_full)
    {
        hud_dirty_regions out;
        if(!source || width<=0 || height<=0)return out;
        auto const total=std::uint64_t(width)*std::uint64_t(height);
        auto full=[&]() {
            out.rects.clear();
            out.rects.push_back({0,0,width,height});
            out.full_upload=true;
            out.upload_pixels=total;
        };
        if(force_full || !previous) {
            full();
            return out;
        }
        constexpr int rows_per_band=32;
        constexpr std::size_t max_regions=12;
        for(int start=0;start<height;start+=rows_per_band) {
            int const stop=std::min(height,start+rows_per_band);
            int left=width,right=0,top=height,bottom=0;
            for(int y=start;y<stop;++y) {
                auto const offset=std::size_t(y)*std::size_t(width);
                auto const *a=source+offset;
                auto const *b=previous+offset;
                if(std::memcmp(a,b,std::size_t(width)*sizeof(std::uint16_t))==0)
                    continue;
                int first=0,last=width;
                while(first<width && a[first]==b[first])++first;
                while(last>first && a[last-1]==b[last-1])--last;
                for(int x=first;x<last;++x)
                    if(a[x]!=b[x])++out.changed_pixels;
                left=std::min(left,first);
                right=std::max(right,last);
                top=std::min(top,y);
                bottom=y+1;
            }
            if(right<=left)continue;
            // Adjacent bands with identical X coverage can share an upload.
            if(!out.rects.empty() && out.rects.back().bottom==top &&
               out.rects.back().left==left && out.rects.back().right==right) {
                out.rects.back().bottom=bottom;
            }
            else out.rects.push_back({left,top,right,bottom});
        }
        for(auto const &r:out.rects)
            out.upload_pixels+=std::uint64_t(r.right-r.left)*std::uint64_t(r.bottom-r.top);
        // Too many subuploads or excessive region padding: one full upload
        // beats dozens of driver calls at high resolutions.
        if(out.rects.size()>max_regions || out.upload_pixels*2U>=total)
            full();
        return out;
    }
}
