#pragma once
// COG GetThingJointNode(thing, joint): equivalent to
// GetPuppetJointNode(GetThingPuppet(thing), joint) for the audited JK.dll.
// JK.dll GetThingPuppet reads thing+0x128 -> puppet class pointer, then
// the class id at +0x1E. GetPuppetJointNode resolves that id in the
// current/static world's puppet table and reads jointNodes[0..9].
#include <cstdint>
#include <cstddef>

namespace jkgm {
    constexpr std::uintptr_t cog_joint_current_world_global=0x00833a10U;
    constexpr std::uintptr_t cog_joint_static_world_global=0x00833a14U;
    constexpr std::uintptr_t cog_joint_class_stride=0x1068U;

    template<class Read>
    int cog_get_thing_joint_node(Read&& read,std::uintptr_t thing,int joint)
    {
        if(thing<0x10000U || thing>0x7fffffffU-0x12cU ||
           joint<0 || joint>=10)return -1;
        std::uint32_t puppet=0;
        if(!read(thing+0x128U,&puppet,sizeof(puppet)) ||
           puppet<0x10000U || puppet>0x7fffffffU-0x20U)
            return -1;
        std::uint16_t puppet_id=0;
        if(!read(std::uintptr_t(puppet)+0x1eU,
                 &puppet_id,sizeof(puppet_id)))return -1;
        std::uint32_t world=0;
        std::uintptr_t const global=(puppet_id&0x8000U) ?
            cog_joint_static_world_global:cog_joint_current_world_global;
        if(!read(global,&world,sizeof(world)) ||
           world<0x10000U || world>0x7fffffffU-0x180U)
            return -1;
        std::uint32_t count=0,classes=0;
        if(!read(std::uintptr_t(world)+0x174U,&count,sizeof(count)) ||
           !read(std::uintptr_t(world)+0x17cU,&classes,sizeof(classes)) ||
           classes<0x10000U)return -1;
        std::uint32_t const index=puppet_id&0x7fffU;
        if(index>=count)return -1;
        std::uintptr_t const offset=index*cog_joint_class_stride+
                                     0x1040U+std::uintptr_t(joint)*4U;
        if(std::uintptr_t(classes)>0x7fffffffU-offset-sizeof(std::int32_t))
            return -1;
        std::int32_t node=-1;
        if(!read(std::uintptr_t(classes)+offset,&node,sizeof(node)))return -1;
        return node;
    }
}
