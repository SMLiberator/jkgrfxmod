#include "renderer/mesh_bounds_culling.hpp"
#include <cassert>
using namespace jkgm::mesh_bounds;
int main() {
    matrix34 id{{1,0,0},{0,1,0},{0,0,1},{0,0,0}};
    frustum f{1,1,100,0,0,0,0,1,-1,-1,1,1,1};
    bounds center{{0,10,0},{2,2,2}};
    assert(!definitely_outside(center,id,f,1));
    bounds left{{-40,10,0},{2,2,2}};
    assert(definitely_outside(left,id,f,1));
    bounds right{{40,10,0},{2,2,2}};
    assert(definitely_outside(right,id,f,1));
    bounds above{{0,10,40},{2,2,2}};
    assert(definitely_outside(above,id,f,1));
    bounds below{{0,10,-40},{2,2,2}};
    assert(definitely_outside(below,id,f,1));
    bounds behind{{0,-5,0},{2,2,2}};
    assert(definitely_outside(behind,id,f,1));
    bounds distant{{0,150,0},{2,2,2}};
    assert(definitely_outside(distant,id,f,1));
    // Straddling geometry stays visible, even if its center is offscreen.
    bounds crossing{{-40,10,0},{45,2,2}};
    assert(!definitely_outside(crossing,id,f,1));
    // Camera or projection uncertainty must never cause false culls.
    assert(!definitely_outside(left,id,f,0));
    f.right = -3;
    assert(!definitely_outside(left,id,f,1));
    return 0;
}
