#include "renderer/native_cog_vm_fast.hpp"
#include <cassert>
#include <array>
#include <random>
#include <limits>
using namespace jkgm::cog_vm_fast;
static uint32_t bits(float f) {
    uint32_t n;std::memcpy(&n,&f,4);return n;
}
int main() {
    std::array<value,64> stack{};
    std::array<value,2> symbols{{{3,0x12345678,123,456},{3,0x0f0f0f0f,789,321}}};
    auto resolve=[&](uint32_t id)->value const* {return id<symbols.size()?&symbols[id]:nullptr;};
    std::mt19937 rng(148);
    for(unsigned i=0;i<100000;++i) {
        uint32_t a=uint32_t(rng()),b=uint32_t(rng());
        symbols[0].bits=a;symbols[1].bits=b;
        for(uint32_t op=20;op<=25;++op) {
            stack[0]={1,0,0,0};stack[1]={1,1,0,0};uint32_t depth=2;
            uint32_t expected[]={uint32_t(a&&b),uint32_t(a||b),uint32_t(a!=b),a&b,a|b,a^b};
            assert(binary(stack.data(),depth,op,true,resolve));
            assert(depth==1 && stack[0].type==3 && stack[0].bits==expected[op-20]);
            assert(stack[0].y==123 && stack[0].z==456 && symbols[0].bits==a);
        }
    }
    uint32_t depth=2;
    stack[0]={3,17,1,2};stack[1]={1,1234,3,4};
    auto before=stack;
    assert(!binary(stack.data(),depth,23,true,resolve));
    assert(depth==2 && std::memcmp(stack.data(),before.data(),sizeof(stack))==0);
    depth=1;
    assert(!binary(stack.data(),depth,20,true,resolve)&&depth==1);
    depth=65;
    assert(!binary(stack.data(),depth,20,true,resolve)&&depth==65);
#if defined(_MSC_VER) && defined(_M_IX86)
    for(int i=-100;i<=100;++i)for(int j=-100;j<=100;++j) {
        value a{3,uint32_t(i),12,34},b{2,bits(float(j)),56,78};
        for(uint32_t op: {8U,9U,10U,11U,15U,16U,17U,18U,19U}) {
            stack[0]=a;stack[1]=b;depth=2;
            if(op==11 && j==0) {
                assert(!binary(stack.data(),depth,op,false,resolve)&&depth==2);
                continue;
            }
            uint32_t expected=0;
            switch(op) {
            case 8:expected=bits(float(i+j));break;
            case 9:expected=bits(float(i-j));break;
            case 10:expected=bits(float(i)*float(j));break;
            case 11:expected=bits(float(double(i)/double(j)));break;
            case 15:expected=i>j;break;
            case 16:expected=i<j;break;
            case 17:expected=i==j;break;
            case 18:expected=i<=j;break;
            case 19:expected=i>=j;break;
            }
            assert(binary(stack.data(),depth,op,false,resolve));
            assert(depth==1 && stack[0].bits==expected &&
                   stack[0].type==(op<=11?2U:3U) && stack[0].y==56 && stack[0].z==78);
        }
    }
    // Native left integer retains precision while right int rounds to flex.
    stack[0]={3,16777217U,0,0};stack[1]={3,16777217U,0,0};depth=2;
    // Test the current x87 precision separately; arithmetic itself must not
    // replace the FILD/FSTP sequence with an integer subtraction.
    uint32_t r=0,tag=0;
    assert(numeric_binary(9,stack[0],stack[1],r,tag));
    assert(tag==2 && (r==bits(1.0f)||r==bits(0.0f)));
    stack[0]={2,0x7fc00000,0,0};stack[1]={3,1,0,0};depth=2;
    assert(!binary(stack.data(),depth,8,false,resolve)&&depth==2);
    stack[0]={0,1,0,0};depth=2;
    assert(!binary(stack.data(),depth,8,false,resolve)&&depth==2);
#endif
}
