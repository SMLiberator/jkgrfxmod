#version 330 core
in vec2 pixel_uv;
in vec3 baked_light;
in float clip_side;
in float capture_forward_depth;
in vec3 paraboloid_view_position;
uniform int paraboloid_hemisphere;
uniform float paraboloid_inv_far;
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
uniform float capture_thing_opacity;
uniform int capture_thing_mode; // 0 full, 1 premultiplied alpha, 2 dither
uniform int capture_sky_mode; // 0=world, 1=JK horizon, 2=JK ceiling
uniform vec4 capture_sky_horizon; // x=distance*tanHalfFOV,y=pixelsPerRevolution,z=tanHalfFOV
uniform vec3 capture_sky_angles;
uniform vec3 capture_sky_camera_origin,capture_sky_camera_right;
uniform vec3 capture_sky_camera_forward,capture_sky_camera_up;
uniform float capture_sky_ceiling_height;
uniform vec2 capture_sky_offset,capture_sky_face_offset,capture_sky_viewport;
out vec4 out_color;

float capture_fog_fraction(float depth) {
    return clamp((depth-fog_params.x)/
                 max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
           fog_params.z;
}

void main() {
    if(capture_sky_mode!=0) {
        // Mirror the normal GPU-world sky UV mapping. Sky polygons are
        // visible apertures, not ordinary world-textured geometry, and
        // never receive object lighting, fog, or receiver clip-plane tests.
        vec2 ndc=gl_FragCoord.xy*2.0/
            max(capture_sky_viewport,vec2(1.0))-1.0;
        vec2 q=ndc*1.091308501;
        float r2=dot(q,q);
        if(r2>1.091308501*1.091308501)discard;
        float axis_sign=paraboloid_hemisphere==0?1.0:-1.0;
        vec3 dir=vec3(2.0*q.x,axis_sign*2.0*q.y,
                      axis_sign*(1.0-r2))/(1.0+r2);
        vec2 uv;
        if(capture_sky_mode==1) {
            // Perspective screen offsets cannot cover a hemisphere.
            // Use world yaw/elevation in JK pixels-per-revolution scale.
            float yaw=degrees(atan(dir.x,dir.y));
            float pitch=degrees(asin(clamp(dir.z,-1.0,1.0)));
            uv=vec2(yaw,-pitch)*(capture_sky_horizon.y/360.0);
        } else {
            float distance_to_sky=1000.0;
            if(abs(dir.z)>0.000001) {
                float t=(capture_sky_ceiling_height-
                         capture_sky_camera_origin.z)/dir.z;
                if(t>=0.0 && t<=1000.0)distance_to_sky=t;
            }
            uv=(capture_sky_camera_origin.xy+
                dir.xy*distance_to_sky)*16.0;
        }
        uv+=capture_sky_offset+capture_sky_face_offset;
        vec4 texel=use_albedo!=0 ?
            texture(albedo_map,uv*uv_scale+uv_scroll):vec4(1.0);
        if(texel.a*material_tint.a<alpha_cutoff)discard;
        out_color=vec4(texel.rgb*material_tint.rgb,1.0);
        gl_FragDepth=0.999999;
        return;
    }
    float radial_distance=length(paraboloid_view_position);
    float radius_fraction=radial_distance*paraboloid_inv_far;
    if(radial_distance<0.01 || radius_fraction>=1.0)discard;
    // Radial depth ensures correct ordering across the nonlinear projection.
    gl_FragDepth=clamp(radius_fraction,0.0,0.999998);
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
    if(capture_thing_opacity<=0.0)discard;
    if(emissive_only==0 && capture_thing_mode==2) {
        // Stable coverage in reflected-camera pixels, preserving depth.
        ivec2 p=ivec2(gl_FragCoord.xy);
        const float bayer[16]=float[16](
             0.0, 8.0, 2.0,10.0,
            12.0, 4.0,14.0, 6.0,
             3.0,11.0, 1.0, 9.0,
            15.0, 7.0,13.0, 5.0);
        if(capture_thing_opacity<=
           (bayer[(p.y&3)*4+(p.x&3)]+0.5)/16.0)discard;
    }

    // The mirrored camera's depth contains BOTH optical legs: camera ->
    // receiver and receiver -> reflected scene. The primary water pass
    // already applies fog on the first leg. Decompose its existing fog
    // transmittance so the two contributions reconstruct the total rather
    // than fogging the near leg twice.
    float total_depth=32.0*radial_distance;
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
        out_color=vec4(emitted*(1.0-fog_far)*capture_thing_opacity,1.0);
    } else {
        vec3 lit=albedo.rgb*material_tint.rgb*baked_light+emitted;
        out_color=vec4(mix(lit,fog_color,fog_far),1.0);
        // Capture RGBA is premultiplied; receiver unpremultiplies before
        // mixing against its environment fallback using capture alpha.
        if(capture_thing_mode==1)
            out_color*=capture_thing_opacity;
    }
}
