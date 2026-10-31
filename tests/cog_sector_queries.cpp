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

    jkgm::cog_sector_id_lookup direct;
    int calls=0;
    int const ids[]={0,1,2,3,4};
    auto read=[&](int i){++calls;return ids[i];};
    assert(direct.find(100,200,5,3,read)==3 && calls==1);
    assert(direct.find(100,200,5,4,read)==4 && calls==2);
    assert(direct.find(100,200,5,99,read)==-1 && calls==7);
    assert(direct.find(100,200,5,99,read)==-1 && calls==7);
    assert(direct.find(100,200,5,-1,read)==-1 && calls==7);

    int const sparse[]={12,44,5,72};
    int visits=0;
    auto sparse_read=[&](int i){++visits;return sparse[i];};
    assert(direct.find(200,300,4,44,sparse_read)==1);
    assert(visits==4);
    assert(direct.find(200,300,4,72,sparse_read)==3);
    assert(visits==4);
    // Native sector-array changes invalidate the lookup, even if the
    // owning world pointer is unchanged.
    assert(direct.find(200,304,4,44,sparse_read)==1 && visits==8);
    assert(direct.find(200,304,4,99,sparse_read)==-1 && visits==8);
}
