#include "renderer/texture_alpha.hpp"
#include <cassert>
int main()
{
    char opaque[]={0,0,0,char(255), 0,char(255),0,char(255)};
    char cutout[]={0,0,0,char(255),0,0,0,0};
    char blended[]={0,0,0,char(255),0,0,0,char(128)};
    auto check=[](char const *data,size_t n) {
        return jkgm::texture_has_nonopaque_alpha(jkgm::make_span(data,n));
    };
    assert(!check(opaque,sizeof(opaque)));
    assert(check(cutout,sizeof(cutout)));
    assert(check(blended,sizeof(blended)));
    return 0;
}
