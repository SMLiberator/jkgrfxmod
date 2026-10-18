#version 330 core
// Intermediate textured GPU mesh validation, not full JK lighting parity.
// Alpha-tested/transparent face groups use the renderer's two pass structure.
uniform sampler2D albedo_map;
uniform vec2 uv_scale;
uniform int has_texture;
uniform int transparent_pass;
in float view_depth;
in vec2 pixel_uv;
in vec3 view_normal;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    vec4 texel=has_texture!=0
        ? texture(albedo_map,pixel_uv*uv_scale)
        : vec4(1.0,0.05,0.65,1.0);
    if(transparent_pass==0 && texel.a<0.999) discard;
    if(transparent_pass!=0 && texel.a<0.002) discard;
    // Transparency framebuffer blends premultiplied color.
    out_color=transparent_pass!=0
        ? vec4(texel.rgb*texel.a,texel.a)
        : vec4(texel.rgb,1.0);
    out_emissive=vec4(0.0,0.0,0.0,texel.a);
    out_normal=vec4(normalize(view_normal),1.0);
    out_depth=vec4(view_depth,0.0,0.0,1.0);
}
