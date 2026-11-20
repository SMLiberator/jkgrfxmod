#version 330 core
// Depth-guided reconstruction of premultiplied half-resolution fluid SPRs.
// The full-resolution scene is the blend destination, not a source sampler.
in vec2 vp_texcoords;
uniform sampler2D smoke_image;
uniform sampler2D opaque_depth;
layout(location=0) out vec4 out_color;
void main()
{
    ivec2 half_size=textureSize(smoke_image,0);
    ivec2 full_size=textureSize(opaque_depth,0);
    vec2 coord=vp_texcoords*vec2(half_size)-vec2(0.5);
    ivec2 base=ivec2(floor(coord));
    vec2 fraction=fract(coord);
    ivec2 pixel=min(ivec2(gl_FragCoord.xy),full_size-ivec2(1));
    float receiver=texelFetch(opaque_depth,pixel,0).r;
    vec4 accumulated=vec4(0.0);
    float weight_sum=0.0;
    // Bilateral 2x2 upscale: avoid spreading smoke across opaque depth
    // discontinuities. Premultiplied RGB and alpha share interpolation.
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        ivec2 h=clamp(base+ivec2(x,y),ivec2(0),half_size-1);
        vec2 d=abs(vec2(x,y)-fraction);
        float w=(1.0-d.x)*(1.0-d.y);
        ivec2 nearest_full=min(h*2,full_size-ivec2(1));
        float z=texelFetch(opaque_depth,nearest_full,0).r;
        float tolerance=max(1.0,min(receiver,z)*0.01);
        if(abs(z-receiver)>tolerance)w*=0.01;
        accumulated+=texelFetch(smoke_image,h,0)*w;
        weight_sum+=w;
    }
    out_color=accumulated/max(weight_sum,0.000001);
}
