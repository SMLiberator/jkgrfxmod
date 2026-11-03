#include "renderer/native_mat_warm_queue.hpp"
#include <cassert>
using namespace jkgm;
int main() {
    native_mat_warm_queue q;
    q.prepare_world(0x1111,0x2000);
    q.request(0x13000,1);
    q.request(0x13000,1);
    q.request(0x14000,0);
    q.request(0,0);
    q.request(0xffff,0); // JK native pointers below 0x10000 are invalid
    q.request(0x15000,-2);
    assert(q.size()==2);
    auto work=q.due(1);
    assert(work.size()==1);
    assert(work[0].material==0x13000 && work[0].cel==1);
    q.finish(work[0],false);
    work=q.due(4);
    assert(work.size()==1 && work[0].material==0x14000);
    q.finish(work[0],true);
    assert(q.size()==1);
    for(int i=0;i<28;i++)assert(q.due(4).empty());
    work=q.due(4);
    assert(work.size()==1 && work[0].material==0x13000);
    q.finish(work[0],true);
    assert(q.size()==0);
    // When the soft per-tick time budget expires, omitted uploads
    // remain immediately due; only attempted failures cool down.
    q.request(0x13000,1);
    q.request(0x14000,2);
    work=q.due(2);
    assert(work.size()==2);
    q.finish(work[0],true);
    q.release_unattempted(work[1]);
    work=q.due(2);
    assert(work.size()==1 && work[0].material==0x14000 &&
           work[0].cel==2);
    q.finish(work[0],true);
    assert(q.size()==0);

    q.request(0x13000,-1);
    q.prepare_world(0x1111,0x2000);
    assert(q.size()==1);
    q.prepare_world(0x1111,0x14000);
    assert(q.size()==0);
    q.request(0x13000,1);
    q.prepare_world(0x2222,0x14000);
    assert(q.size()==0);
    return 0;
}
