#include "renderer/native_sprite_profile.hpp"
#include <cassert>
int main() {
    jkgm::native_sprite_sample a{},b{};
    a.type_calls[0]=10;a.type_calls[3]=2;
    a.native_calls=10;a.native_success=9;
    a.native_ns=500000;a.native_proc_faces=8;
    a.projected_gpu=1;a.projected_warmups=1;
    b.type_calls[0]=5;b.type_calls[4]=3;
    b.native_calls=8;b.native_success=7;
    b.native_ns=250000;b.native_proc_faces=7;
    b.hidden_calls=2;b.other_type_calls=1;
    a.add(b);
    assert(a.type_calls[0]==15 && a.type_calls[3]==2 && a.type_calls[4]==3);
    assert(a.native_calls==18 && a.native_success==16);
    assert(a.native_ns==750000 && a.native_proc_faces==15);
    assert(a.projected_gpu==1 && a.projected_warmups==1);
    assert(a.hidden_calls==2 && a.other_type_calls==1);
    return 0;
}
