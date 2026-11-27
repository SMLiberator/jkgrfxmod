#version 330 core

layout(location = 0) in vec2 vertex_position;

out vec2 vp_texcoords;
#ifdef JKGM_BLOOM_BILINEAR
uniform sampler2D fbuf_image;
uniform ivec2 blur_direction;
uniform vec4 bloom_offsets;
// Each vec4 contains the positive and negative UV of one paired tap.
out vec4 vp_bloom_pairs[4];
#endif
#ifdef JKGM_BLOOM_DOWNSAMPLE
uniform sampler2D fbuf_image;
out vec2 vp_downsample_uv;
#endif
#ifdef JKGM_BLOOM_FAST_SCALE
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
#ifdef JKGM_BLOOM_BILINEAR
    vec2 step_uv=vec2(blur_direction)/vec2(textureSize(fbuf_image,0));
    for(int i=0;i<4;++i) {
        vec2 delta=step_uv*bloom_offsets[i];
        vp_bloom_pairs[i]=vec4(vp_texcoords+delta,vp_texcoords-delta);
    }
#endif
#ifdef JKGM_BLOOM_DOWNSAMPLE
    ivec2 dims=textureSize(fbuf_image,0);
    // Preserve the original 2x2 footprint/crop for odd-sized levels.
    ivec2 dst_dims=max(dims/2,ivec2(1));
    vp_downsample_uv=vp_texcoords*(2.0*vec2(dst_dims)/vec2(dims));
#endif
#ifdef JKGM_BLOOM_FAST_SCALE
    float ev=hdr_exposure_compensation;
    if(exposure_enabled!=0)
        ev+=texelFetch(exposure_image,ivec2(0),0).r;
    vp_bloom_exposure=exp2(clamp(ev,-5.0,5.0));
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
