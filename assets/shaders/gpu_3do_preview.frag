#version 330 core
// Static GPU geometry with per-fragment opaque/translucent separation.
// MIXED surfaces draw fully opaque pixels into the G-buffer and translucent
// pixels in the sorted alpha pass, without double shading any pixel.
uniform sampler2D albedo_map;
uniform sampler2D emissive_map;
uniform vec2 uv_scale;
uniform vec2 albedo_scroll;
uniform vec2 emissive_scroll;
uniform vec4 albedo_factor;
uniform vec3 emissive_factor;
uniform vec4 fog_params;   // start, end, opacity, unused
uniform vec3 fog_color;
uniform float alpha_cutoff;
uniform float native_face_opacity;
uniform int has_texture; // 0=unresolved diagnostic, 1=albedo map, 2=intentionally no albedo
uniform int has_emissive;
uniform int transparent_pass;
uniform int blend_mode; // 0 normal, 1 additive, 2 multiply, 3 fluid
uniform int emissive_only; // zero-alpha base, independently blended emission
uniform int alpha_mode; // 0 solid, 1 mixed, 2 mask, 3 blend only
in float view_depth;
in vec2 pixel_uv;
in vec3 view_normal;
in vec3 jk_lit_rgb;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    vec2 uv=pixel_uv*uv_scale;
    vec4 texel=has_texture==1
        ? texture(albedo_map,uv+albedo_scroll)
        : (has_texture==2 ? vec4(1.0) : vec4(1.0,0.05,0.65,1.0));
    vec4 albedo=texel*albedo_factor;
    albedo.a*=native_face_opacity;
    if(transparent_pass==0) {
        if(alpha_mode==3) discard; // native translucent/additive/opacity<1
        if(alpha_mode==1 && albedo.a<0.999) discard;
        if(alpha_mode==2 && albedo.a<alpha_cutoff) discard;
    } else {
        if(alpha_mode==0 || alpha_mode==2) discard;
        if(albedo.a<=0.001 && emissive_only==0) discard;
        if(alpha_mode==1 && albedo.a>=0.999) discard;
    }
    float fog=clamp((view_depth-fog_params.x)/
                    max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*fog_params.z;
    // Match the native vertex-by-vertex tint and lighting interpolation.
    // Keep it separate from alpha and from emissive output.
    vec3 rgb=mix(albedo.rgb*jk_lit_rgb,fog_color,fog);
    vec3 emission=emissive_factor;
    if(has_emissive!=0)
        emission*=texture(emissive_map,uv+emissive_scroll).rgb;
    // The opaque pass composites this separate emissive attachment after
    // SSAO. The translucent pass has only the color target: emission must
    // therefore be added directly to premultiplied RGB, never multiplied
    // by albedo alpha. With alpha=0 this acts as emission-only additive.
    float a=transparent_pass!=0 ? albedo.a : 1.0;
    if(transparent_pass!=0 && blend_mode==2) {
        // With (DST_COLOR, ZERO) the RGB output is a multiplier, not
        // premultiplied color. Alpha zero must multiply by WHITE (identity).
        // Match the native triangle path: emission biases the multiplier
        // toward white, but never adds light independently in Multiply mode.
        vec3 tint=clamp(albedo.rgb*jk_lit_rgb+emission,0.0,1.0);
        out_color=vec4(mix(vec3(1.0),tint,albedo.a),0.0);
    } else {
        // Additive and normal pass use premultiplied pigment; alpha for the
        // additive framebuffer itself is preserved by glBlendFuncSeparate.
        out_color=vec4(transparent_pass!=0 ? rgb*a+emission : rgb,
                       (transparent_pass!=0 && blend_mode==1) ? 0.0 : a);
    }
    out_emissive=vec4(transparent_pass!=0 ? vec3(0.0) : emission,a);
    out_normal=vec4(normalize(view_normal),1.0);
    out_depth=vec4(view_depth,0.0,0.0,1.0);
}
