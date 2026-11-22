// G-buffer normal/class ABI: default RGB10_A2 Cartesian normals, or
// octahedral XY + JK decal receiver light in B10 when packing is enabled.
// Alpha classification: 0=uncovered, 1=ordinary, 2=cutout sprite,
// 3=covered but exempt from normal-dependent lighting (packed mode only).
#ifndef JKGM_GBUFFER_NORMAL_PACKED
#define JKGM_GBUFFER_NORMAL_PACKED
uniform int gbuffer_packing;

vec2 gbuffer_oct_encode(vec3 n)
{
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),0.000001);
    vec2 p=n.xy;
    if(n.z<0.0) {
        vec2 s=vec2(p.x>=0.0?1.0:-1.0,p.y>=0.0?1.0:-1.0);
        p=(vec2(1.0)-abs(p.yx))*s;
    }
    return p*0.5+0.5;
}
vec3 gbuffer_oct_decode(vec2 uv)
{
    vec2 p=uv*2.0-1.0;
    vec3 n=vec3(p,1.0-abs(p.x)-abs(p.y));
    if(n.z<0.0) {
        vec2 s=vec2(n.x>=0.0?1.0:-1.0,n.y>=0.0?1.0:-1.0);
        n.xy=(vec2(1.0)-abs(n.yx))*s;
    }
    return normalize(n);
}
vec4 gbuffer_pack_normal(vec3 n,float receiver_class,float receiver_light)
{
    if(gbuffer_packing==0) {
        float c=clamp(receiver_class,0.0,3.0)*(1.0/3.0);
        if(dot(n,n)<0.0001)return vec4(0.0,0.0,0.0,c);
        return vec4(clamp(n*0.5+0.5,vec3(0.0),vec3(1.0)),c);
    }
    float cls=dot(n,n)<0.0001?3.0:
        (receiver_class>1.5?2.0:1.0);
    vec2 oct=cls>2.5?vec2(0.5):gbuffer_oct_encode(normalize(n));
    // Projected decals clamp their receiver light to [0,1.25], so
    // an unsigned B10 stores the entire useful range without HDR overflow.
    return vec4(oct,clamp(receiver_light/1.25,0.0,1.0),cls/3.0);
}
vec4 gbuffer_pack_normal(vec3 n,float receiver_class)
{
    return gbuffer_pack_normal(n,receiver_class,1.0);
}
vec4 gbuffer_unpack_normal(vec4 encoded)
{
    float receiver_class=floor(encoded.a*3.0+0.5);
    if(receiver_class<0.5)return vec4(0.0);
    if(gbuffer_packing!=0) {
        if(receiver_class>2.5)return vec4(0.0,0.0,0.0,receiver_class);
        return vec4(gbuffer_oct_decode(encoded.rg),receiver_class);
    }
    vec3 n=all(equal(encoded.rgb,vec3(0.0)))
        ?vec3(0.0):encoded.rgb*2.0-1.0;
    return vec4(n,receiver_class);
}
float gbuffer_receiver_light(vec4 encoded)
{
    return clamp(encoded.b,0.0,1.0)*1.25;
}
bool gbuffer_is_sprite(vec4 decoded)
{
    return decoded.a>1.5 && (gbuffer_packing==0 || decoded.a<2.5);
}
#endif
