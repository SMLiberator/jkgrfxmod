#version 330 core

uniform sampler2D fbuf_image;
// Only the first downsample needs source-image bloom extraction. Default off.
uniform int fused_bloom_first_pass;
uniform sampler2D exposure_image;
uniform int exposure_enabled;
uniform float hdr_exposure_compensation;
layout(location = 0) out vec4 out_color;

// These are the original post_low_pass.frag threshold constants and curve.
// Threshold each texel before averaging: threshold(mean) is NOT equivalent.
vec4 bloom_threshold(vec4 samp,float ev_factor)
{
    vec3 col=samp.rgb;
    float lum=dot(vec3(0.2125,0.7154,0.0721),col);
    lum=max(0.0,lum*ev_factor*0.5);
    float x=pow(lum,3.0);
    float fac=smoothstep(0.0,1.0,x);
    return vec4(col*fac,samp.a);
}

void main()
{
    ivec2 base_tc=ivec2(gl_FragCoord.xy)*2;
    vec4 a=texelFetchOffset(fbuf_image,base_tc,0,ivec2(0,0));
    vec4 b=texelFetchOffset(fbuf_image,base_tc,0,ivec2(1,0));
    vec4 c=texelFetchOffset(fbuf_image,base_tc,0,ivec2(0,1));
    vec4 d=texelFetchOffset(fbuf_image,base_tc,0,ivec2(1,1));
    if(fused_bloom_first_pass!=0) {
        float ev=hdr_exposure_compensation;
        if(exposure_enabled!=0)
            ev+=texelFetch(exposure_image,ivec2(0),0).r;
        float exposure_factor=exp2(clamp(ev,-5.0,5.0));
        a=bloom_threshold(a,exposure_factor);
        b=bloom_threshold(b,exposure_factor);
        c=bloom_threshold(c,exposure_factor);
        d=bloom_threshold(d,exposure_factor);
    }
    out_color=(a+b+c+d)*0.25;
}
