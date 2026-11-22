// Shared G-buffer normal/receiver ABI for all GPU writers and readers.
// GL_RGB10_A2 stores biased [-1,1] XYZ in 10-bit UNORM and receiver 0/1/2
// in 2-bit UNORM alpha. The framebuffer occupies 4 bytes per pixel instead
// of 8 with RGBA16F; there is NO extra alpha attachment.
// A zero normal is special: sky/fullbright/HUD geometry still occupies a
// pixel even though AO and direct-light receivers reject its normal.
#ifndef JKGM_GBUFFER_NORMAL_PACKED
#define JKGM_GBUFFER_NORMAL_PACKED
vec4 gbuffer_pack_normal(vec3 n,float receiver_class)
{
    float c=clamp(receiver_class,0.0,3.0)*(1.0/3.0);
    if(dot(n,n)<0.0001)return vec4(0.0,0.0,0.0,c);
    return vec4(clamp(n*0.5+0.5,vec3(0.0),vec3(1.0)),c);
}
vec4 gbuffer_unpack_normal(vec4 encoded)
{
    float receiver_class=floor(encoded.a*3.0+0.5);
    if(receiver_class<0.5)return vec4(0.0);
    vec3 n=all(equal(encoded.rgb,vec3(0.0)))
        ?vec3(0.0):encoded.rgb*2.0-1.0;
    return vec4(n,receiver_class);
}
#endif
