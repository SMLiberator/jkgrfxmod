#version 330 core
// Preserve the center AO sample's representative depth and normal when
// blurring. All inputs are in the same half-resolution packed texture.
uniform sampler2D source_image;
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
    ivec2 size_px=textureSize(source_image,0);
    ivec2 center=ivec2(gl_FragCoord.xy);
    vec4 origin=texelFetch(source_image,center,0);
    if(origin.g<=0.0 || origin.g>=256.0) {
        out_color=vec4(0.0,origin.gba);
        return;
    }
    vec3 N=decode_oct(origin.ba);
    float tolerance=max(0.2,origin.g*0.0125);
    float total=0.0,weight_sum=0.0;
    // Identical 4x4 extent as baseline box4, evaluated on low-res texels.
    for(int y=-2;y<2;++y)for(int x=-2;x<2;++x) {
        ivec2 p=center+ivec2(x,y);
        if(any(lessThan(p,ivec2(0))) ||
           any(greaterThanEqual(p,size_px)))continue;
        vec4 s=texelFetch(source_image,p,0);
        if(s.g<=0.0 || s.g>=256.0 ||
           abs(s.g-origin.g)>=tolerance ||
           dot(N,decode_oct(s.ba))<=0.75)continue;
        total+=s.r;
        weight_sum+=1.0;
    }
    out_color=vec4(total/max(weight_sum,1.0),origin.gba);
}
