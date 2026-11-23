#include "editor_animation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {
    if(!value)throw std::runtime_error(message);
}
bool close(float a,float b,float tolerance=0.00001f) {
    return std::fabs(a-b)<tolerance;
}
int main() {
    try {
        using namespace editor_animation;
        const auto scroll=scrollOffset({1.25f,-0.75f},5.0);
        check(close(scroll[0],0.25f)&&close(scroll[1],-0.75f),
              "signed UV scroll wrapping");
        const auto large=scrollOffset({0.5f,0.5f},100000000.5);
        check(close(large[0],0.25f)&&close(large[1],0.25f),
              "double precision scrolling");
        Pulse p;
        check(close(multiplier(p,10.0,1),1.0f),"disabled pulse is neutral");
        p.enabled=true;p.min=0.2f;p.max=2.0f;p.rate=2.0f;
        check(close(multiplier(p,0.0,1),0.2f),"sine begins at minimum");
        check(close(multiplier(p,1.0,1),2.0f),"sine peaks mid-cycle");
        check(close(multiplier(p,2.0,1),0.2f),"sine loops");
        p.wave=Wave::Square;
        check(close(multiplier(p,0.25,1),2.0f),"square high half");
        check(close(multiplier(p,1.25,1),0.2f),"square low half");
        p.phase=0.5f;
        check(close(multiplier(p,0.0,1),0.2f),"phase offsets waveform");
        p.phase=0.0f;p.wave=Wave::Flicker;
        check(close(multiplier(p,0.7,4),multiplier(p,0.3,4)),
              "flicker is constant within interval");
        check(close(multiplier(p,0.7,4),multiplier(p,0.7,4)),
              "flicker deterministic");
        p.wave=Wave::Random;
        const float a=multiplier(p,0.0,4),b=multiplier(p,2.0,4);
        check(close(a,p.min+(p.max-p.min)*pulseRandom(4,0)),
              "random initial target");
        check(close(b,p.min+(p.max-p.min)*pulseRandom(4,1)),
              "random next target");
        check(close(multiplier(p,1.0,4),(a+b)*0.5f),
              "random interpolation midpoint");
        check(close(multiplier(p,1.0,4),multiplier(p,1.0,4)),
              "random interpolation deterministic");
        std::cout<<"Editor animation tests passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
