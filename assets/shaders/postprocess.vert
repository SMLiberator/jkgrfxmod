#version 330 core

layout(location = 0) in vec2 vertex_position;

out vec2 vp_texcoords;
#ifdef JKGM_BLOOM_FAST_SCALE
uniform int fused_bloom_first_pass;
uniform sampler2D exposure_image;
uniform int exposure_enabled;
uniform float hdr_exposure_compensation;
flat out float vp_bloom_exposure;
#endif

#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
// Identical for each vertex of the fullscreen quad. Flat interpolation
// transports the frame constants without evaluating them per fragment.
uniform sampler2D exposure_image;
uniform int exposure_enabled;
uniform int hdr_enabled;
uniform float hdr_exposure_compensation;
uniform vec4 vignette_params;
uniform vec4 grain_params;
flat out float vp_exposure_gain;
flat out vec3 vp_vignette_color;
flat out float vp_grain_amount;
flat out uint vp_grain_seed_hash;

// Same PCG hash as post_to_srgb.frag; only the seed is frame-invariant.
uint postfx_seed_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
#endif

void main()
{
    gl_Position = vec4(vertex_position, 0.0, 1.0);
    vp_texcoords = (vertex_position + 1.0) / 2.0;
#ifdef JKGM_BLOOM_FAST_SCALE
    vp_bloom_exposure=1.0;
    if(fused_bloom_first_pass!=0) {
        float ev=hdr_exposure_compensation;
        if(exposure_enabled!=0)
            ev+=texelFetch(exposure_image,ivec2(0),0).r;
        vp_bloom_exposure=exp2(clamp(ev,-5.0,5.0));
    }
#endif
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
    vp_exposure_gain=1.0;
    if(hdr_enabled!=0) {
        float ev=hdr_exposure_compensation;
        if(exposure_enabled!=0)
            ev+=texelFetch(exposure_image,ivec2(0),0).r;
        vp_exposure_gain=exp2(clamp(ev,-5.0,5.0));
    }
    vp_vignette_color=vec3(0.0);
    if(vignette_params.w>0.0)
        vp_vignette_color=pow(clamp(vignette_params.rgb,0.0,1.0),vec3(2.2));
    vp_grain_amount=grain_params.x*
        (1.0-clamp(vignette_params.w-1.0,0.0,1.0));
    vp_grain_seed_hash=postfx_seed_hash(uint(int(grain_params.z)));
#endif
}
