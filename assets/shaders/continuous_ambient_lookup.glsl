// GL 3.3 sparse voxel hash lookup, included by the opt-in cached-3DO
// vertex shader on devices with enough texture units. Keys: GL_RGBA32UI;
// values: GL_RG32F. CPU hash and signed coordinate bits must match.
uniform usamplerBuffer continuous_voxel_keys;
uniform samplerBuffer continuous_voxel_values;
uniform int continuous_voxel_mask;
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
    if(continuous_voxel_mask<=0 || sector==0xffffffffu)
        return vec2(0.0,-1.0);
    uvec4 id=uvec4(sector,uvec3(grid));
    uint slot=continuous_voxel_hash(id)&uint(continuous_voxel_mask);
    for(int attempt=0;attempt<32;++attempt) {
        uvec4 stored=texelFetch(continuous_voxel_keys,int(slot));
        if(all(equal(stored,id))) {
            found=true;
            return texelFetch(continuous_voxel_values,int(slot)).xy;
        }
        if(stored.x==0xffffffffu)break;
        slot=(slot+1u)&uint(continuous_voxel_mask);
    }
    return vec2(0.0,-1.0);
}
