#version 330 core
#include "gbuffer_normal_packed.glsl"
// Reconstruct the original full-resolution R16F AO texture without allowing
// a different surface's AO across depth or normal discontinuities.
uniform sampler2D source_image; // packed half-resolution AO/depth/oct normal
uniform sampler2D normal_image; // unmodified full-resolution G-buffer
uniform sampler2D depth_image;
in vec2 vp_texcoords;
layout(location=0) out vec4 out_color;

vec3 decode_oct(vec2 e)
{
    vec2 f=e*2.0-1.0;
    vec3 n=vec3(f,1.0-abs(f.x)-abs(f.y));
    if(n.z<0.0)n.xy=(vec2(1.0)-abs(n.yx))*sign(n.xy);
    return normalize(n);
}
void main()
{
    ivec2 p=ivec2(gl_FragCoord.xy);
    float d=texelFetch(depth_image,p,0).r;
    if(d<=0.0 || d>=256.0) {
        out_color=vec4(0.0);
        return;
    }
    ivec2 dims=textureSize(source_image,0);
    ivec2 full_dims=textureSize(depth_image,0);
    vec2 low_position=(vec2(p)+0.5)*vec2(dims)/vec2(full_dims)-0.5;
    ivec2 begin=ivec2(floor(low_position));
    vec2 f=fract(low_position);
    // Four zero AO taps reconstruct to zero regardless of edge weights.
    // Load each once and avoid G-buffer normal access and all bilateral
    // arithmetic on unoccluded regions.
    ivec2 hi=dims-1;
    vec4 taps[4];
    taps[0]=texelFetch(source_image,clamp(begin,ivec2(0),hi),0);
    taps[1]=texelFetch(source_image,clamp(begin+ivec2(1,0),ivec2(0),hi),0);
    taps[2]=texelFetch(source_image,clamp(begin+ivec2(0,1),ivec2(0),hi),0);
    taps[3]=texelFetch(source_image,clamp(begin+ivec2(1,1),ivec2(0),hi),0);
    if(all(equal(vec4(taps[0].r,taps[1].r,taps[2].r,taps[3].r),vec4(0.0)))) {
        out_color=vec4(0.0);
        return;
    }
    vec3 raw_normal=gbuffer_unpack_normal(texelFetch(normal_image,p,0)).rgb;
    if(dot(raw_normal,raw_normal)<0.0001) {
        out_color=vec4(0.0);
        return;
    }
    vec3 N=gbuffer_packing!=0?raw_normal:normalize(raw_normal);
    float total=0.0,weights=0.0;
    float tolerance=max(0.2,d*0.0125);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec4 s=taps[y*2+x];
        if(s.g<=0.0 || s.g>=256.0)continue;
        float difference=abs(d-s.g);
        if(difference>=tolerance)continue;
        float ndot=dot(N,decode_oct(s.ba));
        if(ndot<=0.75)continue;
        float geometric=(1.0-smoothstep(0.0,tolerance,difference))*
                        smoothstep(0.75,1.0,ndot);
        float bilinear=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);
        float w=geometric*bilinear;
        total+=s.r*w;
        weights+=w;
    }
    // No matched low-res surface: avoid unrelated AO rather than staining
    // silhouettes. Thin geometry may lose AO, but never inherits a wall's.
    out_color=vec4(weights>0.00001?total/weights:0.0,0.0,0.0,0.0);
}
