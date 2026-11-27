#version 330 core

uniform sampler2D fbuf_image;
// Only the first downsample needs source-image bloom extraction. Default off.
uniform int fused_bloom_first_pass;
uniform sampler2D exposure_image;
uniform int exposure_enabled;
uniform float hdr_exposure_compensation;
layout(location = 0) out vec4 out_color;
#ifdef JKGM_BLOOM_FAST_SCALE
flat in float vp_bloom_exposure;
#endif

// These are the original post_low_pass.frag threshold constants and curve.
// Threshold each texel before averaging: threshold(mean) is NOT equivalent.
vec4 bloom_threshold(vec4 samp,float ev_factor)
{
    vec3 col=samp.rgb;
    float lum=dot(vec3(0.2125,0.7154,0.0721),col);
    lum=max(0.0,lum*ev_factor*0.5);
    #ifdef JKGM_BLOOM_FAST_SCALE
    float x=lum*lum*lum;
    #else
    float x=pow(lum,3.0);
    #endif
    float fac=smoothstep(0.0,1.0,x);
    return vec4(col*fac,samp.a);
}

void main()
{
#ifdef JKGM_BLOOM_FAST_SCALE
    if(fused_bloom_first_pass==0) {
        // Center of the exact 2x2 source footprint, not interpolated
        // destination UV: preserves the old crop on odd dimensions.
        vec2 uv=(gl_FragCoord.xy*2.0)/vec2(textureSize(fbuf_image,0));
        out_color=texture(fbuf_image,uv);
        return;
    }
#endif
    ivec2 base_tc=ivec2(gl_FragCoord.xy)*2;
    vec4 a=texelFetchOffset(fbuf_image,base_tc,0,ivec2(0,0));
    vec4 b=texelFetchOffset(fbuf_image,base_tc,0,ivec2(1,0));
    vec4 c=texelFetchOffset(fbuf_image,base_tc,0,ivec2(0,1));
    vec4 d=texelFetchOffset(fbuf_image,base_tc,0,ivec2(1,1));
    if(fused_bloom_first_pass!=0) {
        #ifdef JKGM_BLOOM_FAST_SCALE
        float exposure_factor=vp_bloom_exposure;
        #else
        float ev=hdr_exposure_compensation;
        if(exposure_enabled!=0)
            ev+=texelFetch(exposure_image,ivec2(0),0).r;
        float exposure_factor=exp2(clamp(ev,-5.0,5.0));
        #endif
        a=bloom_threshold(a,exposure_factor);
        b=bloom_threshold(b,exposure_factor);
        c=bloom_threshold(c,exposure_factor);
        d=bloom_threshold(d,exposure_factor);
    }
    out_color=(a+b+c+d)*0.25;
}
