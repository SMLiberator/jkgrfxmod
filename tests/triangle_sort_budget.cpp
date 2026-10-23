// Regression tests for bounded translucent sorting. Independent of GL/math.
#include "renderer/triangle_sort_budget.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numeric>
#include <vector>

struct vertex {
    float depth=0.0f;
    int index=0;
};

int main() {
    using jkgm::triangle_sort_budget;
    triangle_sort_budget normal;
    assert(normal.permit(500,0,0));
    assert(normal.permit(1200,5,0));
    assert(normal.scanned==1700);
    assert(!normal.permit(500,triangle_sort_budget::max_depth,0));
    assert(!normal.permit(100,0,triangle_sort_budget::max_stalled_pivots));
    assert(!normal.permit(triangle_sort_budget::max_scanned,0,0));
    assert(normal.permit(100,0,0)); // remaining budget survives a rejection

    std::vector<vertex> faces;
    for(int i=0;i<1000;++i)faces.push_back({float(i%97),i});
    // Depth-writing alpha-masked prefix must never be touched.
    const auto prefix=faces[0];
    triangle_sort_budget cap;
    assert(!cap.permit(faces.size(),triangle_sort_budget::max_depth,0));
    cap.fallback(faces.begin()+1,faces.end(),[](vertex const &v) {
        return v.depth;
    });
    assert(faces[0].index==prefix.index);
    for(size_t i=2;i<faces.size();++i)
        assert(faces[i-1].depth>=faces[i].depth);
    assert(cap.fallback_groups==1);
    assert(cap.fallback_triangles==999);

    // Last-resort sort should also handle large groups with predictable
    // ordering; no recursive geometric work is permitted after exhaustion.
    triangle_sort_budget exhausted;
    assert(!exhausted.permit(triangle_sort_budget::max_scanned+1,0,0));
    exhausted.fallback(faces.begin()+1,faces.end(),[](vertex const &v) {
        return v.depth;
    });
    assert(exhausted.fallback_groups==1);
    assert(exhausted.fallback_triangles==999);
    return 0;
}
