#version 330 core
in vec2 vp_texcoords;
uniform sampler2D traced_image,receiver_image;
out vec4 out_color;
void main() {
    // Use the exact low-resolution receiver pixel. Filtering a material
    // mask at an edge would put reflections onto adjacent opaque surfaces.
    ivec2 pixel=clamp(ivec2(vp_texcoords*
        vec2(textureSize(receiver_image,0))),ivec2(0),
        textureSize(receiver_image,0)-ivec2(1));
    vec4 receiver=texelFetch(receiver_image,pixel,0);
    if(receiver.z<=0.0)discard;
    float roughness=clamp(receiver.w,0.0,1.0);
    float max_lod=max(0.0,log2(float(
        max(textureSize(traced_image,0).x,textureSize(traced_image,0).y))));
    vec4 sample_color=textureLod(traced_image,vp_texcoords,
                                 roughness*max_lod);
    if(sample_color.a<0.0001)discard;
    // The SSR target stores coverage-premultiplied RGB. Keep scene color
    // already rendered, including the material's EnvMap, as the fallback.
    out_color=vec4(sample_color.rgb/sample_color.a,
                   clamp(sample_color.a*receiver.z,0.0,1.0));
}
