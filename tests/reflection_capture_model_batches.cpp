#include "renderer/reflection_capture_batch_order.hpp"
#include <cassert>
#include <vector>
#include <iostream>
struct draw{int material;bool opaque;int index;};
static std::vector<int> order(std::vector<draw> const &d){
    std::vector<int> r;
    for(auto const &x:d)r.push_back(x.index);
    return r;
}
int main(){
    auto safe=[](draw const &x){return x.opaque;};
    auto less=[](draw const &a,draw const &b){
        return a.material!=b.material?a.material<b.material:a.index<b.index;
    };
    std::vector<draw> all{{3,true,0},{1,true,1},{3,true,2},{2,true,3},{1,true,4}};
    auto a=jkgm::order_reflection_capture_opaque_spans(all,safe,less);
    assert((order(all)==std::vector<int>{1,4,3,0,2}));
    assert(a.opaque_ranges==5 && a.source_order_ranges==0 && a.sorted_spans==1);
    std::vector<draw> mixed{{3,true,0},{1,true,1},{3,true,2},
                            {9,false,3},{2,true,4},{1,true,5},
                            {2,true,6},{8,false,7},{3,true,8},{1,true,9}};
    auto m=jkgm::order_reflection_capture_opaque_spans(mixed,safe,less);
    assert((order(mixed)==std::vector<int>{1,0,2,3,5,4,6,7,8,9}));
    assert(m.opaque_ranges==8 && m.source_order_ranges==2 && m.sorted_spans==2);
    std::vector<draw> pair{{9,true,0},{1,true,1}};
    assert(jkgm::order_reflection_capture_opaque_spans(pair,safe,less).sorted_spans==0);
    assert((order(pair)==std::vector<int>{0,1}));
    std::vector<draw> mask{{8,false,0},{1,false,1},{8,false,2}};
    assert(jkgm::order_reflection_capture_opaque_spans(mask,safe,less).sorted_spans==0);
    assert((order(mask)==std::vector<int>{0,1,2}));
    std::cout<<"Shared opaque batch ordering: OK\n";
}
