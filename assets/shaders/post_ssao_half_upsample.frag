#version 330 core
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
    vec3 raw_normal=texelFetch(normal_image,p,0).rgb;
    if(d<=0.0 || d>=256.0 || dot(raw_normal,raw_normal)<0.0001) {
        out_color=vec4(0.0);
        return;
    }
    vec3 N=normalize(raw_normal);
    ivec2 dims=textureSize(source_image,0);
    ivec2 full_dims=textureSize(depth_image,0);
    vec2 low_position=(vec2(p)+0.5)*vec2(dims)/vec2(full_dims)-0.5;
    ivec2 begin=ivec2(floor(low_position));
    vec2 f=fract(low_position);
    float total=0.0,weights=0.0;
    float tolerance=max(0.2,d*0.0125);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 loc=clamp(begin+ivec2(x,y),ivec2(0),dims-1);
        vec4 s=texelFetch(source_image,loc,0);
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
