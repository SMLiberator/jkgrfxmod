#version 330 core
in vec2 pixel_uv;
in vec3 baked_light;
in float clip_side;
in float capture_forward_depth;
uniform sampler2D albedo_map;
uniform sampler2D emissive_map;
uniform vec2 uv_scale,uv_scroll,emissive_scroll;
uniform vec4 material_tint,emissive_factor;
uniform int use_albedo,has_emissive,emissive_only;
uniform float alpha_cutoff;
uniform vec4 fog_params;
uniform vec3 fog_color;
uniform vec4 clip_plane_view;
uniform int clip_enabled,orthographic_mode;
out vec4 out_color;

float capture_fog_fraction(float depth) {
    return clamp((depth-fog_params.x)/
                 max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
           fog_params.z;
}

void main() {
    // The reflected ray only retains the observer's side of the plane.
    if(clip_enabled!=0 && clip_side<0.0001)discard;
    vec2 tex_uv=pixel_uv*uv_scale;
    vec4 albedo=use_albedo!=0
        ?texture(albedo_map,tex_uv+uv_scroll):vec4(1.0);
    // Emission is independent of albedo pigment/opacity, as in JK's
    // primary world and 3DO shaders. Alpha-zero emissive-only surfaces
    // will be submitted in a separate additive, depth-tested pass.
    vec3 emitted=emissive_factor.rgb;
    if(has_emissive!=0)
        emitted*=texture(emissive_map,tex_uv+emissive_scroll).rgb;
    if(emissive_only==0 &&
       albedo.a*material_tint.a<alpha_cutoff)discard;
    if(emissive_only!=0 &&
       max(max(emitted.r,emitted.g),emitted.b)<=0.00001)discard;

    // The mirrored camera's depth contains BOTH optical legs: camera ->
    // receiver and receiver -> reflected scene. The primary water pass
    // already applies fog on the first leg. Decompose its existing fog
    // transmittance so the two contributions reconstruct the total rather
    // than fogging the near leg twice.
    float total_depth=orthographic_mode!=0
        ?32.0*max(capture_forward_depth,0.0)
        :32.0/max(gl_FragCoord.w,0.00000001);
    // No planar receiver for ordinary camera-to-MAT: all fog belongs to
    // the secondary view. Planar reflections still subtract the near leg.
    float plane_fraction=clip_enabled!=0 ? clamp(
        -clip_plane_view.w/max(clip_side-clip_plane_view.w,0.0001),
        0.0,1.0):0.0;
    float fog_total=capture_fog_fraction(total_depth);
    float fog_near=capture_fog_fraction(total_depth*plane_fraction);
    float fog_far=clamp((fog_total-fog_near)/
                        max(1.0-fog_near,0.00001),0.0,1.0);

    if(emissive_only!=0) {
        // Add only emitted energy over captured opaque geometry; do not add
        // fog-colored translucent quads in the emission-only overlay.
        out_color=vec4(emitted*(1.0-fog_far),1.0);
    } else {
        vec3 lit=albedo.rgb*material_tint.rgb*baked_light+emitted;
        out_color=vec4(mix(lit,fog_color,fog_far),1.0);
    }
}
