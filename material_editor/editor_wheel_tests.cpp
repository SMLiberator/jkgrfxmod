#include "editor_wheel.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
bool close(float a,float b) {
    return std::fabs(a-b)<0.00001f;
}
int main() {
    try {
        using editor_wheel::adjust;
        float value=0.5f;
        check(adjust(value,1.0f,0,1)&&close(value,0.55f),
              "one positive wheel tick must add 0.05");
        check(adjust(value,-2.0f,0,1)&&close(value,0.45f),
              "two negative ticks must subtract 0.10");
        check(adjust(value,0.5f,0,1)&&close(value,0.475f),
              "fractional precision wheel deltas must be proportional");
        check(!adjust(value,0.0f,0,1)&&close(value,0.475f),
              "zero wheel delta must not edit a material");
        value=0.98f;
        check(adjust(value,1.0f,0,1)&&close(value,1.0f),
              "bounded sliders must clamp at their upper limit");
        check(!adjust(value,1.0f,0,1),
              "wheel at the upper limit must be a no-op");
        value=0.02f;
        check(adjust(value,-1.0f,0,1)&&close(value,0.0f),
              "bounded sliders must clamp at their lower limit");
        value=-0.125f;
        check(adjust(value,-1.0f)&&close(value,-0.175f),
              "signed scrolling/displacement values must remain signed");
        value=0.001f;
        check(!adjust(value,-1.0f,0.001f,10.0f)&&close(value,0.001f),
              "pulse cycle rate must stay positive");
        value=1.0f;
        check(adjust(value,1.0f,0,std::numeric_limits<float>::max())&&
              close(value,1.05f),
              "unbounded reflection strength can exceed the slider range");
        check(!adjust(value,std::numeric_limits<float>::infinity(),0,2),
              "nonfinite wheel input must never change values");
        std::cout<<"Numeric wheel adjustment tests passed\n";
    } catch(const std::exception &e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
