#include "../renderer/cog_sector_queries.hpp"
#include <array>
#include <cassert>
int main() {
    jkgm::cog_sector_queries q;
    std::array<float,3> p{{1,2,3}};
    auto match=[](int index,std::array<float,3> const& pos)->int {
        if(pos[0]==1 && (index==0||index==2||index==4))
            return 10+index*3;
        if(pos[0]==9 && index==1)return 44;
        return -1;
    };
    assert(q.first(1,p,6,match)==-1);
    q.set_world(100);
    assert(q.first(1,p,6,match)==10);
    assert(q.next(1,10,6,match)==16);
    assert(q.next(1,10,6,match)==-1);
    assert(q.next(1,16,6,match)==22);
    assert(q.next(1,22,6,match)==-1);
    assert(q.first(1,p,6,match)==10);
    assert(q.first(2,{{9,0,0}},6,match)==44);
    assert(q.next(1,10,6,match)==16);
    assert(q.next(2,44,6,match)==-1);
    assert(q.first(1,{{9,0,0}},6,match)==44);
    assert(q.next(1,16,6,match)==-1);
    q.set_world(200);
    assert(q.next(1,44,6,match)==-1);
    assert(q.next(2,44,6,match)==-1);
    assert(q.first(1,{{8,0,0}},6,match)==-1);
    assert(q.first(1,p,6,match)==10);
    q.set_world(200);
    assert(q.next(1,10,6,match)==16);
    q.clear();
    assert(q.next(1,16,6,match)==-1);
}
