#include "renderer/native_mat_upload_policy.hpp"
#include <cassert>
int main() {
    using namespace jkgm;
    static_assert(native_mat_default_upload_variant==0,
                  "Native default must not force optional 16-bit format");
    static_assert(native_mat_default_format_slot(false,false)==0x0055C7F4U);
    static_assert(native_mat_default_format_slot(false,true)==0x0055C7F4U);
    static_assert(native_mat_default_format_slot(true,false)==0x0055C7F4U);
    static_assert(native_mat_default_format_slot(true,true)==0x0055C7F8U);
    assert(native_mat_default_upload_variant==0);
}
