#include "renderer/sun_dynamic_shadow_layout.hpp"
#include <cassert>
#include <cmath>
#include <vector>
using namespace jkgm;
int main()
{
    assert(choose_sun_dynamic_shadow_tile(8.0f,256)==64);
    assert(choose_sun_dynamic_shadow_tile(90.0f,256)==128);
    assert(choose_sun_dynamic_shadow_tile(210.0f,256)==256);
    assert(choose_sun_dynamic_shadow_tile(350.0f,256)==512);
    assert(choose_sun_dynamic_shadow_tile(900.0f,256)==512);
    assert(choose_sun_dynamic_shadow_tile(900.0f,64)==128);
    assert(choose_sun_dynamic_shadow_tile(NAN,256)==64);
    // Eight possible casters: never allocate two 512 tiles while the
    // remaining six need at least 64 pixels each.
    assert(sun_dynamic_shadow_budget_tile(512,0,1024*512,7)==512);
    assert(sun_dynamic_shadow_budget_tile(512,512*512,1024*512,6)==256);
    assert(sun_dynamic_shadow_budget_tile(256,0,1024*512,7)==256);
    sun_dynamic_shadow_layout pack(1024,512);
    sun_dynamic_shadow_tile big{},medium{},small{},tiny{};
    assert(pack.allocate(512,big));
    assert(pack.allocate(256,medium));
    assert(pack.allocate(128,small));
    assert(pack.allocate(64,tiny));
    std::vector<sun_dynamic_shadow_tile> rects{big,medium,small,tiny};
    for(size_t i=0;i<rects.size();++i) {
        auto const &a=rects[i];
        assert(a.size>=64 && a.x>=0 && a.y>=0 &&
               a.x+a.size<=1024 && a.y+a.size<=512);
        for(size_t j=i+1;j<rects.size();++j) {
            auto const &b=rects[j];
            assert(a.x+a.size<=b.x || b.x+b.size<=a.x ||
                   a.y+a.size<=b.y || b.y+b.size<=a.y);
        }
    }
    pack.undo_last();
    sun_dynamic_shadow_tile reclaimed{};
    assert(pack.allocate(64,reclaimed));
    assert(reclaimed.x==tiny.x && reclaimed.y==tiny.y);
    sun_dynamic_shadow_layout limited(256,256);
    sun_dynamic_shadow_tile one{},none{};
    assert(limited.allocate(256,one));
    assert(!limited.allocate(64,none));
    limited.undo_last();
    assert(limited.allocate(64,none));
    sun_dynamic_shadow_layout crowded(512,512);
    for(int i=0;i<3;++i) {
        sun_dynamic_shadow_tile tile{};
        assert(crowded.allocate(256,tile));
        assert(tile.size==256);
    }
    sun_dynamic_shadow_tile downgraded{};
    assert(crowded.allocate(512,downgraded));
    assert(downgraded.size==256); // 512 does not fit, 256 does
    return 0;
}
