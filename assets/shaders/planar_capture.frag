#version 330 core
in vec2 pixel_uv;
in vec3 baked_light;
in float clip_side;
uniform sampler2D albedo_map;
uniform sampler2D emissive_map;
uniform vec2 uv_scale,uv_scroll,emissive_scroll;
uniform vec4 material_tint,emissive_factor;
uniform int use_albedo,has_emissive,emissive_only;
uniform float alpha_cutoff;
uniform vec4 fog_params;
uniform vec3 fog_color;
uniform vec4 clip_plane_view;
// Radius in emissive-map texels; zero restores unmodified capture sampling.
uniform float emissive_dilation_radius;
out vec4 out_color;

vec3 capture_emissive_texel(vec2 uv) {
    vec3 best=texture(emissive_map,uv).rgb;
    if(emissive_dilation_radius<=0.0)return best;
    // Work in emissive TEXTURE coordinates, before reflected rasterization.
    // Respect minified UV footprint for distant, subpixel windows.
    vec2 texel=1.0/vec2(textureSize(emissive_map,0));
    vec2 step_uv=max(texel,0.35*fwidth(uv))*emissive_dilation_radius;
    float best_luma=dot(best,vec3(0.2126,0.7152,0.0722));
    // Whole RGB sample preserves hue; independent channel maxima do not.
    for(int y=-1;y<=1;++y)
    for(int x=-1;x<=1;++x) {
        if(x==0 && y==0)continue;
        vec3 candidate=texture(emissive_map,
                         uv+vec2(float(x),float(y))*step_uv).rgb;
        float luma=dot(candidate,vec3(0.2126,0.7152,0.0722));
        if(luma>best_luma) {best=candidate;best_luma=luma;}
    }
    return best;
}

float capture_fog_fraction(float depth) {
    return clamp((depth-fog_params.x)/
                 max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
           fog_params.z;
}

void main() {
    // The reflected ray only retains the observer's side of the plane.
    if(clip_side<0.0001)discard;
    vec2 tex_uv=pixel_uv*uv_scale;
    vec4 albedo=use_albedo!=0
        ?texture(albedo_map,tex_uv+uv_scroll):vec4(1.0);
    // Emission is independent of albedo pigment/opacity, as in JK's
    // primary world and 3DO shaders. Alpha-zero emissive-only surfaces
    // will be submitted in a separate additive, depth-tested pass.
    vec3 emitted=emissive_factor.rgb;
    if(has_emissive!=0)
        emitted*=capture_emissive_texel(tex_uv+emissive_scroll);
    if(emissive_only==0 &&
       albedo.a*material_tint.a<alpha_cutoff)discard;
    if(emissive_only!=0 &&
       max(max(emitted.r,emitted.g),emitted.b)<=0.00001)discard;

    // The mirrored camera's depth contains BOTH optical legs: camera ->
    // receiver and receiver -> reflected scene. The primary water pass
    // already applies fog on the first leg. Decompose its existing fog
    // transmittance so the two contributions reconstruct the total rather
    // than fogging the near leg twice.
    float total_depth=32.0/max(gl_FragCoord.w,0.00000001);
    float plane_fraction=clamp(
        -clip_plane_view.w/max(clip_side-clip_plane_view.w,0.0001),
        0.0,1.0);
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
