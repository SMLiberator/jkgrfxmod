#include "renderer/jk_palette_effects.hpp"
#include <cassert>
#include <array>
using namespace jkgm;
int main()
{
    std::array<jk_palette_request,32> req{};
    std::array<int32_t,4> enabled{{1,1,1,1}};
    auto p=accumulate_jk_palette_effects(req,0,enabled);
    assert(!p.active && p.fade==1);
    req[0].valid=1; req[0].filter[0]=1;
    req[0].tint[0]=0.25f;
    req[0].add[1]=32; req[0].fade=0.8f;
    req[1].valid=1; req[1].filter[2]=1;
    req[1].tint[0]=0.2f; req[1].add[1]=13; req[1].fade=0.6f;
    p=accumulate_jk_palette_effects(req,2,enabled);
    assert(p.active && p.filter[0] && !p.filter[1] && p.filter[2]);
    assert(p.tint[0]>0.4499f && p.tint[0]<0.4501f);
    assert(p.add[1]==45 && p.fade==0.6f);
    assert(!accumulate_jk_palette_effects(req,3,enabled).active);
    enabled={{0,0,0,0}};
    assert(!accumulate_jk_palette_effects(req,2,enabled).active);
    req[1].valid=0;
    enabled={{1,1,1,1}};
    p=accumulate_jk_palette_effects(req,1,enabled);
    assert(p.active && p.fade==0.8f);
    return 0;
}
