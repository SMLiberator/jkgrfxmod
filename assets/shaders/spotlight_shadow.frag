#version 330 core
in vec2 shadow_material_uv;
uniform int shadow_use_alpha;
uniform sampler2D shadow_alpha_texture;
uniform sampler2D shadow_alpha_texture_next;
uniform vec2 shadow_alpha_uv_scale;
uniform vec2 shadow_alpha_uv_scroll;
uniform vec2 shadow_alpha_transition; // second texture, interpolation
uniform float shadow_alpha_cutoff;
uniform float shadow_alpha_factor;
uniform int shadow_dither_enabled;
uniform float shadow_instance_opacity;
uniform int shadow_alpha_masked;
// Only the caustics FBO has a color attachment; depth-only passes ignore it.
uniform float shadow_caustic_density;
uniform int shadow_caustic_from_texture,shadow_caustic_to_texture;
uniform vec3 shadow_caustic_color_factor;
uniform float shadow_caustic_color_blend;
layout(location=0) out vec4 caustic_density_output;
void main()
{
    float alpha=1.0;
    if(shadow_use_alpha!=0) {
        vec2 uv=shadow_material_uv*shadow_alpha_uv_scale+
                shadow_alpha_uv_scroll;
        alpha=texture(shadow_alpha_texture,uv).a;
        if(shadow_alpha_transition.x>0.5)
            alpha=mix(alpha,texture(shadow_alpha_texture_next,uv).a,
                      shadow_alpha_transition.y);
    }
    if(shadow_dither_enabled!=0) {
        float material_alpha=alpha*(shadow_use_alpha!=0 ?
                                   shadow_alpha_factor : 1.0);
        if(shadow_alpha_masked!=0 &&
           material_alpha<shadow_alpha_cutoff)discard;
        // Alpha masks are binary cutouts: a surviving mask texel casts a
        // full shadow at 100% Thing opacity. Only the Thing/MAT fade
        // reduces its coverage, not the mask's antialiased edge alpha.
        // Mixed-alpha and dither MATs retain their continuous source alpha.
        float coverage=clamp(
            (shadow_alpha_masked!=0 ? shadow_alpha_factor :
               material_alpha)*shadow_instance_opacity,0.0,1.0);
        ivec2 p=ivec2(gl_FragCoord.xy);
        const float bayer[16]=float[16](
             0.0, 8.0, 2.0,10.0,
            12.0, 4.0,14.0, 6.0,
             3.0,11.0, 1.0, 9.0,
            15.0, 7.0,13.0, 5.0);
        float threshold=(bayer[(p.y&3)*4+(p.x&3)]+0.5)/16.0;
        if(coverage<=threshold)discard;
    } else if(shadow_use_alpha!=0 &&
              alpha*shadow_alpha_factor<shadow_alpha_cutoff)discard;
    // Only the Fluid capture has the color attachment; depth-only passes
    // are unaffected by the output. Reuse the shadow-alpha samplers.
    // The texture cache builds a full mip chain, ending at one pixel.
    // Use the representative material color, NOT its spatial pattern or
    // scroll offset. Untextured materials fall back to their RGB factor.
    vec3 water_rgb=vec3(1.0);
    if(shadow_caustic_from_texture!=0) {
        ivec2 dims=textureSize(shadow_alpha_texture,0);
        float last_mip=floor(log2(float(max(max(dims.x,dims.y),1))));
        water_rgb=textureLod(shadow_alpha_texture,vec2(0.5),last_mip).rgb;
    }
    if(shadow_caustic_color_blend>0.0) {
        vec3 next_rgb=vec3(1.0);
        if(shadow_caustic_to_texture!=0) {
            ivec2 next_dims=textureSize(shadow_alpha_texture_next,0);
            float last_mip=floor(log2(float(max(max(next_dims.x,next_dims.y),1))));
            next_rgb=textureLod(shadow_alpha_texture_next,
                                vec2(0.5),last_mip).rgb;
        }
        water_rgb=mix(water_rgb,next_rgb,
                      clamp(shadow_caustic_color_blend,0.0,1.0));
    }
    caustic_density_output=vec4(shadow_caustic_density,
        clamp(water_rgb*shadow_caustic_color_factor,vec3(0.0),vec3(1.0)));
}
