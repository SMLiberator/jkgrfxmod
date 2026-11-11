#version 330 core
// Stage B0: documented sparse GL 3.3 buffer-texture lookup primitive.
// NOT yet included by the production 3DO shader. Paired with
// renderer/continuous_ambient_gpu.hpp and its native MSVC tests.
// Keys: GL_RGBA32UI; values: GL_RG32F. Unit assignments must be
// reserved separately when the shader is integrated in Stage B1.
uniform usamplerBuffer continuous_voxel_keys;
uniform samplerBuffer continuous_voxel_values;
uniform uint continuous_voxel_mask;
uniform float continuous_voxel_spacing;
uint continuous_voxel_hash(uvec4 key)
{
    uint h=2166136261u;
    h=(h^key.x)*16777619u;
    h=(h^key.y)*16777619u;
    h=(h^key.z)*16777619u;
    h=(h^key.w)*16777619u;
    h^=h>>16u;h*=0x7feb352du;
    h^=h>>15u;h*=0x846ca68bu;
    h^=h>>16u;
    return h;
}
vec2 continuous_voxel_fetch(uint sector,ivec3 grid,out bool found)
{
    found=false;
    if(continuous_voxel_mask==0u || sector==0xffffffffu)
        return vec2(0.0,-1.0);
    uvec4 id=uvec4(sector,uvec3(grid));
    uint slot=continuous_voxel_hash(id)&continuous_voxel_mask;
    for(int attempt=0;attempt<32;++attempt) {
        uvec4 stored=texelFetch(continuous_voxel_keys,int(slot));
        if(all(equal(stored,id))) {
            found=true;
            return texelFetch(continuous_voxel_values,int(slot)).xy;
        }
        if(stored.x==0xffffffffu)break;
        slot=(slot+1u)&continuous_voxel_mask;
    }
    return vec2(0.0,-1.0);
}
