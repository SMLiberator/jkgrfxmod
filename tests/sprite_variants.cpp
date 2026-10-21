#include "renderer/sprite_variants.hpp"
#include <array>
#include <cassert>
#include <random>
#include <string>
using namespace jkgm;

int main() {
    std::array<std::string,11> a{{
        "spark01.mat","0","0.2","0.25","4","0","1",
        "0","0","0","0"
    }};
    sprite_variant_spec first{};
    assert(parse_sprite_variant(a,first));
    assert(first.material=="spark01.mat");
    assert(first.type==0 && first.width==0.2f && first.height==0.25f);
    assert(first.geometry==4 && first.lighting==0 && first.texture==1);

    // Every legacy SPR field may change independently on an alternative.
    auto b=a;
    b={{"spark02.mat","3","1.5","2.5","3","2","2",
        "0.75","-0.1","0.15","4"}};
    sprite_variant_spec second{};
    assert(parse_sprite_variant(b,second));
    assert(second.material=="spark02.mat" && second.type==3);
    assert(second.width==1.5f && second.height==2.5f);
    assert(second.geometry==3 && second.lighting==2 &&
           second.texture==2 && second.extra_light==0.75f);
    assert(second.offset[0]==-0.1f && second.offset[1]==0.15f &&
           second.offset[2]==4.0f);
    b[1]="127";
    assert(parse_sprite_variant(b,second) && second.type==127);
    b[1]="128";
    assert(!parse_sprite_variant(b,second));
    b=a;
    b[2]="0";
    assert(!parse_sprite_variant(b,second));
    b[2]="nan";
    assert(!parse_sprite_variant(b,second));
    b=a;
    b[3]="inf";
    assert(!parse_sprite_variant(b,second));
    b=a;
    b[4]="4garbage";
    assert(!parse_sprite_variant(b,second));
    b=a;
    b[8]="1e9999";
    assert(!parse_sprite_variant(b,second));
    b=a;
    b[0]=std::string(32,'a');
    assert(!parse_sprite_variant(b,second));

    std::mt19937 rng(1337);
    assert(choose_sprite_variant(0,rng)==0);
    assert(choose_sprite_variant(1,rng)==0);
    bool seen[4]{};
    for(int i=0;i<2048;++i) {
        auto const selected=choose_sprite_variant(4,rng);
        assert(selected<4);
        seen[selected]=true;
    }
    for(bool item:seen)assert(item);
}
