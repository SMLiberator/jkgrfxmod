#version 330 core
uniform sampler2D depth_image;
layout(location=0) out vec4 out_mask;
void main()
{
    float scene=texelFetch(depth_image,ivec2(gl_FragCoord.xy),0).r;
    // gl_FragCoord.w tracks the original receiver triangle's perspective-
    // interpolated view distance. Skip occluded/mismatched receiver pixels.
    float receiver=1.0/max(gl_FragCoord.w,0.00001);
    if(scene>=65500.0 || receiver<=0.0 ||
       abs(scene/32.0-receiver)>0.02+0.003*receiver)discard;
    // Repeated/overlapping caster polygons write the SAME 1.0 mask value;
    // there is no multiply-overdraw darkening.
    out_mask=vec4(1.0,0.0,0.0,1.0);
}
