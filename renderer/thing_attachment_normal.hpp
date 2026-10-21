#pragma once
// JKGM's EXISTING attachment face normal, independent of collide=2 queries.
// This exposes true polygon face normals for attached world surfaces and
// attached Thing faces; free collision-hit normals are transient elsewhere.
#include "thing_node_native.hpp"
#include <cmath>
#include <cstdint>

namespace jkgm {
    namespace thing_attachment_layout {
        constexpr uintptr_t thing_kind=0x0c;
        constexpr uintptr_t thing_attach_flags=0x34;
        constexpr uintptr_t thing_attached_face=0x44; // sithSurfaceInfo* or rdFace*
        constexpr uintptr_t thing_attached_parent=0x58;
        constexpr uintptr_t thing_orientation=0x88;
        constexpr uintptr_t face_normal=0x34; // rdFace.normal in JK 32-bit layout
        constexpr uint32_t attach_surface=1U;
        constexpr uint32_t attach_thing_face=2U;
    }

    inline bool read_thing_attachment_normal(
        thing_node_reader const &read,uintptr_t thing,
        thing_node_vector &output)
    {
        using namespace thing_attachment_layout;
        if(!read || !thing_node_valid_address(thing,0xc4))
            return false;
        uint32_t kind=0,flags=0,face=0;
        if(!read(thing+thing_kind,&kind,4) ||
           kind==0 || kind>15 ||
           !read(thing+thing_attach_flags,&flags,4) ||
           (flags&(attach_surface|attach_thing_face))==0 ||
           !read(thing+thing_attached_face,&face,4) ||
           !thing_node_valid_address(face+face_normal,12))
            return false;
        thing_node_vector normal{};
        if(!read(uintptr_t(face)+face_normal,normal.data(),12))
            return false;
        for(float v:normal)if(!std::isfinite(v))return false;

        if(flags&attach_thing_face) {
            // The engine stores a face-local normal. For a thing-face
            // attachment, rotate it through that Thing's current basis.
            uint32_t parent=0,parent_kind=0;
            if(!read(thing+thing_attached_parent,&parent,4) ||
               !thing_node_valid_address(parent,0xac) ||
               !read(uintptr_t(parent)+thing_kind,&parent_kind,4) ||
               parent_kind==0 || parent_kind>15)
                return false;
            float basis[9]{};
            if(!read(uintptr_t(parent)+thing_orientation,basis,sizeof(basis)))
                return false;
            for(float v:basis)if(!std::isfinite(v))return false;
            thing_node_vector rotated{};
            for(int component=0;component<3;++component)
                rotated[component]=
                    normal[0]*basis[component]+
                    normal[1]*basis[3+component]+
                    normal[2]*basis[6+component];
            normal=rotated;
        }
        double length2=0;
        for(float v:normal)length2+=double(v)*v;
        if(!std::isfinite(length2) || length2<1.0e-20)return false;
        double const inv=1.0/std::sqrt(length2);
        for(float &v:normal)v=float(v*inv);
        output=normal;
        return true;
    }
}
