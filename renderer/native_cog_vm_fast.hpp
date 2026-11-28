#pragma once
// Scalar fast paths for the original 32-bit JK COG interpreter.
// Native operand tags and auxiliary words are preserved; no cached symbol
// values, bytecode changes, or modifications to external verb ABI.
#include <cstdint>
#include <cstring>

namespace jkgm::cog_vm_fast {
struct value {
    uint32_t type, bits, y, z;
};
static_assert(sizeof(value)==16);
inline bool finite_scalar(value const& v) noexcept {
    return v.type==3 || (v.type==2 && (v.bits&0x7f800000U)!=0x7f800000U);
}
inline bool integer_binary(uint32_t op,uint32_t a,uint32_t b,uint32_t& result) noexcept {
    switch(op) {
    case 20: result=(a!=0 && b!=0); return true;
    case 21: result=(a!=0 || b!=0); return true;
    case 22: result=(a!=b); return true;
    case 23: result=a&b; return true;
    case 24: result=a|b; return true;
    case 25: result=a^b; return true;
    default: return false;
    }
}
// Preserve the asymmetric native precision: the right operand is rounded
// to float before the operation; the left integer is loaded with FILD and
// stays in x87 precision until the result is stored. Do not use SSE or
// C++ float expressions for this. Modulo, nonfinite input, and zero
// division remain in the original routine. The current x87 CW is used.
inline bool numeric_binary(uint32_t op,value const& left,value const& right,
                           uint32_t& result,uint32_t& tag) noexcept {
    if(!finite_scalar(left)||!finite_scalar(right) ||
       !((op>=8 && op<=11)||(op>=15 && op<=19)))return false;
#if defined(_MSC_VER) && defined(_M_IX86)
    uint32_t lhs_bits=left.bits,rhs_bits=right.bits;
    uint32_t lhs_kind=left.type;
    uint32_t result_bits=0;
    unsigned short status=0;
    if(right.type==3) {
        __asm {
            fild dword ptr [rhs_bits]
            fstp dword ptr [rhs_bits]
        }
    }
    if(op==11 && !(rhs_bits&0x7fffffffU))return false;
    __asm {
        cmp lhs_kind,3
        jne load_float
        fild dword ptr [lhs_bits]
        jmp loaded
    load_float:
        fld dword ptr [lhs_bits]
    loaded:
        mov eax,op
        cmp eax,8
        je add_op
        cmp eax,9
        je sub_op
        cmp eax,10
        je mul_op
        cmp eax,11
        je div_op
        fcomp dword ptr [rhs_bits]
        fnstsw ax
        mov status,ax
        jmp done
    add_op:
        fadd dword ptr [rhs_bits]
        jmp store_result
    sub_op:
        fsub dword ptr [rhs_bits]
        jmp store_result
    mul_op:
        fmul dword ptr [rhs_bits]
        jmp store_result
    div_op:
        fdiv dword ptr [rhs_bits]
    store_result:
        fstp dword ptr [result_bits]
    done:
    }
    if(op<=11) {result=result_bits;tag=2;return true;}
    tag=3;
    // Exact conditions used by the native FCOMP/FNSTSW branches.
    switch(op) {
    case 15: result=(status&0x4100U)==0;break;
    case 16: result=(status&0x0100U)!=0;break;
    case 17: result=(status&0x4000U)!=0;break;
    case 18: result=(status&0x4100U)!=0;break;
    case 19: result=(status&0x0100U)==0;break;
    }
    return true;
#else
    // The optimization is specific to the native x86 build.
    (void)result;(void)tag;
    return false;
#endif
}
// Resolve at most one symbol level, like the native VM. The resolver
// returns a pointer into the current local/global symbol table or null.
// On failure, not one byte of the stack or stack position is changed.
template<class Resolve>
inline bool binary(value* stack,uint32_t& depth,uint32_t op,bool integer,
                   Resolve&& resolve) noexcept {
    if(depth<2 || depth>64)return false;
    value* dest=stack+depth-2;
    auto const* a=dest->type==1?resolve(dest->bits):dest;
    auto const* b=dest[1].type==1?resolve(dest[1].bits):dest+1;
    if(!a||!b)return false;
    uint32_t result=0,tag=3;
    if(integer) {
        // Flex-to-int changes the native x87 control word; leave it native
        // for this first experiment, along with pointer-backed symbols.
        if(a->type!=3||b->type!=3||!integer_binary(op,a->bits,b->bits,result))
            return false;
    } else if(!numeric_binary(op,*a,*b,result,tag))return false;
    // Native integer helper copies auxiliary words from the left resolved
    // operand; native floating helper copies them from the right operand.
    auto const* aux=integer?a:b;
    value const v{tag,result,aux->y,aux->z};
    *dest=v;
    --depth;
    return true;
}
}
