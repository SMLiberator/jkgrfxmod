// Shared GPU material evaluation for cached SithWorld and 3DO geometry.
// Included verbatim into both GLSL 3.30 fragment shaders at load time.
// Keep geometry-specific lighting, sky projection and transforms outside.
// Texture inputs are normalized UV coordinates, independent of their source.
vec4 gpu_material_sample_albedo(
    sampler2D base_image,sampler2D next_image,vec2 uv,vec2 scroll,
    int base_source,int next_source,float transition_weight)
{
    vec4 base=base_source==1 ? texture(base_image,uv+scroll) :
              (base_source==2 ? vec4(1.0) : vec4(1.0,0.05,0.65,1.0));
    if(transition_weight>0.0) {
        vec4 next=next_source==1 ? texture(next_image,uv+scroll) : vec4(1.0);
        base=mix(base,next,clamp(transition_weight,0.0,1.0));
    }
    return base;
}
// Optional source-indexed fullbright mask shares normal albedo UVs.
// No extra fragment lookup for materials without a valid source mask.
float gpu_material_palette_fullbright(
    sampler2D mask_image,vec2 albedo_uv,int enabled)
{
    return enabled!=0 ? texture(mask_image,albedo_uv).r : 0.0;
}
// Emission is independent of albedo opacity: essential for light sources
// with albedo_factor 0 0 0 0, and for emissive-only glass/GUI materials.
vec3 gpu_material_emission(
    sampler2D image,vec2 uv,vec2 scroll,vec3 intensity,int has_image)
{
    vec3 result=intensity;
    if(has_image!=0)result*=texture(image,uv+scroll).rgb;
    return result;
}
float gpu_material_dither_threshold()
{
    ivec2 p=ivec2(gl_FragCoord.xy)&ivec2(3);
    int lo=(((p.x^p.y)&1)<<1)|(p.y&1);
    int hi=((((p.x>>1)^(p.y>>1))&1)<<1)|((p.y>>1)&1);
    return (float(4*lo+hi)+0.5)/16.0;
}
// Both caches use the same partition: solid coverage and masked/dithered
// pixels fill G-buffer; only fractional alpha or forced translucency
// enters forward alpha. Post-exempt Camera Things render all coverage once.
bool gpu_material_discard(
    float alpha,int policy,float cutoff,int emission_only,
    bool transparent,bool post_exempt)
{
    if(post_exempt) {
        if(policy==2 && alpha<cutoff)return true;
        if(policy==4 && alpha<=gpu_material_dither_threshold())return true;
        // The Camera HUD pass promotes solid/masked/dithered coverage
        // to opaque *before* its zero-alpha rejection. Preserve native
        // behavior, including zero-alpha solid HUD elements.
        if(policy==0 || policy==2 || policy==4)alpha=1.0;
        return alpha<=0.001 && emission_only==0;
    }
    if(!transparent) {
        if(policy==3)return true;
        if(policy==1 && alpha<0.999)return true;
        if(policy==2 && alpha<cutoff)return true;
        if(policy==4 && alpha<=gpu_material_dither_threshold())return true;
        return false;
    }
    if(policy==0 || policy==2 || policy==4)return true;
    if(alpha<=0.001 && emission_only==0)return true;
    if(policy==1 && alpha>=0.999)return true;
    return false;
}
