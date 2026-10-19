#include "common/projected_decal.hpp"
#include <cassert>
#include <cmath>
int main() {
    jkgm::projected_decal_config d;
    jkgm::projected_decal_basis b;
    assert(!jkgm::prepare_projected_decal(d,b)); // no image = disabled
    d.image="jkgm/decals/test.png";
    assert(jkgm::prepare_projected_decal(d,b));
    assert(b.forward[2]==-1.0f);
    assert(b.right[0]==1.0f);
    assert(b.up[1]==1.0f);
    d.direction={{0,2,0}};
    d.up={{0,1,0}};
    assert(!jkgm::prepare_projected_decal(d,b)); // parallel directions
    d.up={{0,0,1}};
    assert(jkgm::prepare_projected_decal(d,b));
    d.size[0]=0;
    assert(!jkgm::prepare_projected_decal(d,b));
    d.size[0]=4;
    d.opacity=-0.1f;
    assert(!jkgm::prepare_projected_decal(d,b));
    d.opacity=1.0f;
    d.normal_threshold=1.1f;
    assert(!jkgm::prepare_projected_decal(d,b));
    d.normal_threshold=0.25f;
    assert(jkgm::prepare_projected_decal(d,b));
}
