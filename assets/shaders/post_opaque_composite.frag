#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
uniform int fill_tjunction_gaps; // opt-in: depth-bracketed G-buffer cracks
uniform int tjunction_gap_fill_size; // 0..8 pixels to find geometry on EACH side
uniform int debug_void_pixels; // unresolved raw empty-depth pixels, neon checkerboard
uniform int retain_hom_pixels; // skip unwritten pixels, leaving prior scene buffer
uniform sampler2D depth_image;
uniform vec4 fog_params; // start, end, opacity, unused
uniform vec3 fog_color;

uniform sampler2D normal_image; // scene normals for MAT spotlights
uniform vec4 camera_origin;
uniform vec4 camera_right;       // XYZ world axis, W tan(horizontal_fov/2)
uniform vec4 camera_forward;
uniform vec4 camera_up;          // XYZ world axis, W tan(vertical_fov/2)

// Four independent COG MAT spotlights. Textures occupy units 6-9.
// Avoid dynamic sampler-array indexing, which GLSL 330 cannot guarantee.
uniform int spotlight_count;
uniform sampler2D spotlight_image_0;
uniform sampler2D spotlight_image_1;
uniform sampler2D spotlight_image_2;
uniform sampler2D spotlight_image_3;
uniform vec4 spotlight_origins[4];  // world origin
uniform vec4 spotlight_rights[4];   // XYZ axis, W fixed range / width
uniform vec4 spotlight_ups[4];      // XYZ axis, W fixed range / height
uniform vec4 spotlight_forwards[4]; // XYZ axis, W inverse range
uniform vec2 spotlight_intensities[4]; // light (pigment), additive (cookie)
// Conservative screen UV coverage of each pyramid, minXY/maxXY.
// Fullscreen fog/AO still renders; only projector work is footprint-gated.
uniform vec4 spotlight_screen_bounds[4];

// Optional one-light static-world shadow. Depth is sampled only inside the
// matching spotlight's cookie footprint; disabled mode has zero texture reads.
uniform int spotlight_shadow_slot; // -1 when no valid shadow map this frame
uniform sampler2DShadow spotlight_shadow_image;
uniform vec2 spotlight_shadow_near_range; // near, far in JK world units

uniform int sun_lighting_active; // independent of depth-map readiness
uniform int sun_shadow_active;
uniform sampler2DShadow sun_shadow_image;
uniform vec3 sun_shadow_direction; // world-space direction toward the sun
uniform vec4 sun_shadow_color_intensity;
uniform vec3 sun_shadow_origin;
uniform vec3 sun_shadow_right;
uniform vec3 sun_shadow_up;
uniform vec3 sun_shadow_forward;
uniform vec3 sun_shadow_limits; // orthographic width, height, depth range
uniform sampler2DShadow sun_shadow_far_image;
uniform vec3 sun_shadow_far_origin,sun_shadow_far_right;
uniform vec3 sun_shadow_far_up,sun_shadow_far_forward,sun_shadow_far_limits;
uniform float sun_shadow_split; // JK forward distance from camera
uniform float sun_shadow_receiver_bias_scale; // diagnostic multiplier, 0..2
uniform float sun_max_distance; // 0=unlimited; JK forward distance where added sun ends

// Added sunlight and its shadows are full strength to 85% of the range, fade
// smoothly to zero at the range and are absent beyond it, leaving JK's
// authored vertex lighting untouched there.
float sun_range_fade(float forward_distance)
{
    if(sun_max_distance<=0.0)return 1.0;
    return 1.0-smoothstep(sun_max_distance*0.85,sun_max_distance,
                          forward_distance);
}
// 4x2 tiles with independently configurable resolution and 2px guard.
uniform int sun_dynamic_count;
uniform sampler2DShadow sun_dynamic_image;
uniform float sun_dynamic_caster_depth[8]; // caster far bound in [0,1] light depth
uniform vec4 sun_dynamic_atlas_rects[8]; // xy=tile origin, zw=tile size in pixels
uniform vec3 sun_dynamic_origin[8],sun_dynamic_right[8];
uniform vec3 sun_dynamic_up[8],sun_dynamic_forward[8],sun_dynamic_limits[8];

vec3 sun_shadow_coordinates(vec3 position,vec3 origin,vec3 right,
                            vec3 up,vec3 forward,vec3 limits)
{
    vec3 p=position-origin;
    return vec3(dot(p,right)/limits.x,
                dot(p,up)/limits.y,
                dot(p,forward)/limits.z);
}
bool sun_shadow_inside(vec3 coord)
{
    return all(greaterThanEqual(coord,vec3(0.0))) &&
           all(lessThanEqual(coord,vec3(1.0)));
}
float sun_shadow_filter(sampler2DShadow map,vec3 coordinate,float range,
                        float incidence)
{
    // A 24-bit normalized orthographic depth map avoids quantizing the
    // world's full light-axis range into only 65536 possible distances.
    // Keep a small world-space offset for geometric precision/roundoff.
    // Receiver bias grows toward grazing sunlight, where quantization
    // otherwise causes the surface to repeatedly shadow itself.
    float grazing=1.0-clamp(incidence,0.0,1.0);
    float bias_world=(0.008+0.027*grazing*grazing)*
                     sun_shadow_receiver_bias_scale;
    float bias=max(4.0/16777215.0,bias_world/max(range,0.001));
    // Use one hardware bilinear comparison PCF sample in both cascades.
    return texture(map,vec3(coordinate.xy,coordinate.z-bias));
}
float sun_shadow_visibility(vec3 world_position,float incidence)
{
    if(sun_shadow_active==0)return 1.0;
    float camera_depth=dot(world_position-camera_origin.xyz,
                           camera_forward.xyz);
    vec3 near_uv=sun_shadow_coordinates(world_position,sun_shadow_origin,
        sun_shadow_right,sun_shadow_up,sun_shadow_forward,sun_shadow_limits);
    // Far cascade overlaps near coverage; never discard a receiver merely
    // because its near projection fell outside the map.
    bool use_near=camera_depth<sun_shadow_split && sun_shadow_inside(near_uv);
    // Keep the sharper near cascade until close to the split. Blending for
    // six units would waste most of a 16-unit character-focused cascade.
    float transition_start=sun_shadow_split*0.8125;
    float transition_end=sun_shadow_split*0.9375;
    if(use_near && camera_depth<transition_start)
        return sun_shadow_filter(sun_shadow_image,near_uv,
                                 sun_shadow_limits.z,incidence);
    vec3 far_uv=sun_shadow_coordinates(world_position,sun_shadow_far_origin,
        sun_shadow_far_right,sun_shadow_far_up,
        sun_shadow_far_forward,sun_shadow_far_limits);
    if(!sun_shadow_inside(far_uv))
        return use_near
            ? sun_shadow_filter(sun_shadow_image,near_uv,sun_shadow_limits.z,incidence)
            : 1.0;
    float far_visibility=sun_shadow_filter(sun_shadow_far_image,
        far_uv,sun_shadow_far_limits.z,incidence);
    if(!use_near)return far_visibility;
    float near_visibility=sun_shadow_filter(sun_shadow_image,
        near_uv,sun_shadow_limits.z,incidence);
    float blend=smoothstep(transition_start,transition_end,camera_depth);
    return mix(near_visibility,far_visibility,blend);
}

float sun_dynamic_shadow_visibility(vec3 position,float incidence)
{
    if(sun_dynamic_count==0 || sun_shadow_split<=0.0)return 1.0;
    // The dynamic atlas is near-only even when a Thing's projected shadow
    // footprint extends much farther than its caster bounds.
    float distance=dot(position-camera_origin.xyz,camera_forward.xyz);
    if(distance>=sun_shadow_split)return 1.0;
    float near_fade=1.0-smoothstep(
        sun_shadow_split*0.85,sun_shadow_split,distance);
    float visibility=1.0;
    for(int i=0;i<8;++i) {
        if(i>=sun_dynamic_count)break;
        vec3 uv=sun_shadow_coordinates(position,sun_dynamic_origin[i],
            sun_dynamic_right[i],sun_dynamic_up[i],sun_dynamic_forward[i],
            sun_dynamic_limits[i]);
        if(!sun_shadow_inside(uv))continue;
        vec4 rect=sun_dynamic_atlas_rects[i];
        vec2 atlas_uv=(rect.xy+vec2(2.0)+
                       uv.xy*(rect.zw-vec2(4.0))) /
                      vec2(textureSize(sun_dynamic_image,0));
        float grazing=1.0-clamp(incidence,0.0,1.0);
        float bias_world=(0.006+0.032*grazing*grazing)*
                         sun_shadow_receiver_bias_scale;
        float bias=max(4.0/16777215.0,
                       bias_world/max(sun_dynamic_limits[i].z,0.001));
        float comparison=uv.z-bias;
        // Fixed single-sample hardware PCF; no separation-dependent taps.
        float object_visibility=texture(sun_dynamic_image,
            vec3(atlas_uv,comparison));
        visibility=min(visibility,object_visibility);
        if(visibility<=0.001)break;
    }
    return mix(1.0,visibility,near_fade);
}

float spotlight_shadow_visibility(vec2 uv,float receiver_distance)
{
    float n=spotlight_shadow_near_range.x;
    float f=spotlight_shadow_near_range.y;
    if(receiver_distance<=n || receiver_distance>=f)return 1.0;
    // Same perspective Z transform as spotlight_shadow.vert. Using linear
    // receiver distance directly would create spurious shadows everywhere.
    float receiver=f/(f-n)-f*n/((f-n)*receiver_distance);
    float derivative=f*n/((f-n)*receiver_distance*receiver_distance);
    // Preserve the geometry-scale depth bias with a 24-bit comparison map.
    // Avoid detached contact shadows at long projector ranges.
    float bias=max(0.000025,derivative*0.015);
    vec2 texel=1.0/vec2(textureSize(spotlight_shadow_image,0));
    float lit=0.0;
    // Four nearby hardware-bilinear depth comparisons smooth jagged edges,
    // without adding a blur pass or altering the projected MAT cookie.
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=(vec2(x,y)-vec2(0.5))*0.7*texel;
        lit+=texture(spotlight_shadow_image,vec3(uv+delta,receiver-bias));
    }
    return lit*0.25;
}

layout(location = 0) out vec4 out_color;

vec4 sample_spotlight(int i,vec2 uv)
{
    if(i==0) return texture(spotlight_image_0,uv);
    if(i==1) return texture(spotlight_image_1,uv);
    if(i==2) return texture(spotlight_image_2,uv);
    return texture(spotlight_image_3,uv);
}

vec3 reconstruct_scene_world(float depth)
{
    vec2 pixel_uv=gl_FragCoord.xy/vec2(textureSize(depth_image,0));
    vec2 ndc=pixel_uv*2.0-1.0;
    float forward_distance=depth/32.0; // JK's linear scene-depth scale
    return camera_origin.xyz+forward_distance*(
        camera_forward.xyz +
        ndc.x*camera_right.w*camera_right.xyz +
        ndc.y*camera_up.w*camera_up.xyz);
}


// Evaluate cookie radiance once per light and route it into two
// independently controlled channels. Diffuse is later multiplied by the
// UNLIT receiver pigment, so dark framebuffer colors retain surface detail.
void project_spotlights(float depth,ivec2 pixel,
                        out vec3 diffuse,out vec3 additive)
{
    diffuse=vec3(0.0);
    additive=vec3(0.0);
    if(spotlight_count<=0 || depth<=0.0 || depth>=65500.0)
        return;
    vec2 pixel_uv=(vec2(pixel)+vec2(0.5))/
                   vec2(textureSize(depth_image,0));
    // Avoid depth->world reconstruction and normal-buffer sampling on all
    // pixels outside every active spotlight's projected screen footprint.
    bool potentially_lit=false;
    for(int i=0;i<4;i++) {
        if(i>=spotlight_count)break;
        vec4 bounds=spotlight_screen_bounds[i];
        if(all(greaterThanEqual(pixel_uv,bounds.xy)) &&
           all(lessThanEqual(pixel_uv,bounds.zw))) {
            potentially_lit=true;
            break;
        }
    }
    if(!potentially_lit)return;
    vec3 position=reconstruct_scene_world(depth);
    vec3 normal=vec3(0.0);
    bool have_normal=false;
    for(int i=0;i<4;i++) {
        if(i>=spotlight_count)break;
        vec4 bounds=spotlight_screen_bounds[i];
        if(any(lessThan(pixel_uv,bounds.xy)) ||
           any(greaterThan(pixel_uv,bounds.zw)))continue;
        vec3 relative=position-spotlight_origins[i].xyz;
        float distance=dot(relative,spotlight_forwards[i].xyz);
        float t=distance*spotlight_forwards[i].w;
        if(t<=0.01 || t>=1.0)continue;
        vec2 uv=vec2(dot(relative,spotlight_rights[i].xyz)*
                          spotlight_rights[i].w/distance+0.5,
                     dot(relative,spotlight_ups[i].xyz)*
                          spotlight_ups[i].w/distance+0.5);
        if(any(lessThan(uv,vec2(0.0))) ||
           any(greaterThan(uv,vec2(1.0))))continue;
        vec4 cookie=sample_spotlight(i,uv);
        if(cookie.a<=0.001)continue;
        // Only sample/reconstruct normals when at least one spotlight actually
        // reaches the receiver, not once per fullscreen compositor pixel.
        if(!have_normal) {
            vec3 vn=texelFetch(normal_image,pixel,0).rgb;
            if(dot(vn,vn)<0.0001)return;
            normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                             camera_up.xyz*(vn.y/camera_up.w)+
                             camera_forward.xyz*vn.z);
            have_normal=true;
        }
        float border=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));
        float edge=smoothstep(0.0,0.02,border);
        // Begin attenuation much earlier than the old 85%-to-100% cutoff.
        // A longer smooth fade looks more natural on car-headlight receivers,
        // retaining near-field brightness and reaching zero at the same range.
        float fade=smoothstep(0.01,0.04,t)*
                   (1.0-smoothstep(0.35,1.0,t));
        float facing=max(dot(normal,normalize(-relative)),0.0);
        // Mild floor illumination even with near-horizontal beams.
        float incidence=0.05+0.95*facing;
        float shadow=1.0;
        if(i==spotlight_shadow_slot)
            shadow=spotlight_shadow_visibility(uv,distance);
        vec3 radiance=cookie.rgb*cookie.a*(edge*fade*incidence*shadow);
        diffuse+=radiance*spotlight_intensities[i].x;
        additive+=radiance*spotlight_intensities[i].y;
    }
}

// Reuse the scene depth once per candidate; do not re-fetch depth values
// after finding bracketed neighbors. A sky pixel with no nearby opaque
// geometry still needs tests when repair is enabled, so favor an early exit
// and a small configured radius over additional full-screen passes.
float gap_depth_or_empty(ivec2 p,ivec2 dims)
{
    if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,dims)))
        return 65504.0;
    return texelFetch(depth_image,p,0).r;
}

void main()
{
    ivec2 tc=ivec2(gl_FragCoord.xy);
    float depth=texelFetch(depth_image,tc,0).r;
    if(fill_tjunction_gaps!=0 && tjunction_gap_fill_size>0 &&
       (depth<=0.0 || depth>=65500.0)) {
        ivec2 dims=textureSize(depth_image,0);
        ivec2 selected=tc;
        int best_span=100;
        float best_depth=65504.0;
        for(int axis=0;axis<2;++axis) {
            // A one-pixel gap bracketed on both sides is already optimal.
            // Avoid even starting the perpendicular search in that case.
            if(best_span==2)break;
            ivec2 step_dir=axis==0?ivec2(1,0):ivec2(0,1);
            ivec2 before=tc,after=tc;
            int dist_before=0,dist_after=0;
            float z_before=65504.0,z_after=65504.0;
            for(int d=1;d<=tjunction_gap_fill_size;++d) {
                if(dist_before==0) {
                    float z=gap_depth_or_empty(tc-step_dir*d,dims);
                    if(z>0.0 && z<65500.0) {
                        before=tc-step_dir*d;
                        dist_before=d;
                        z_before=z;
                    }
                }
                if(dist_after==0) {
                    float z=gap_depth_or_empty(tc+step_dir*d,dims);
                    if(z>0.0 && z<65500.0) {
                        after=tc+step_dir*d;
                        dist_after=d;
                        z_after=z;
                    }
                }
                if(dist_before>0 && dist_after>0)break;
            }
            // No filled geometry on both sides: leave true voids untouched.
            if(dist_before==0 || dist_after==0)continue;
            int span=dist_before+dist_after;
            float nearest=min(z_before,z_after);
            if(span<best_span || (span==best_span && nearest<best_depth)) {
                best_span=span;
                best_depth=nearest;
                selected=z_before<=z_after?before:after;
            }
        }
        if(any(notEqual(selected,tc))) {
            tc=selected;
            depth=best_depth;
        }
    }

    // Debug the ACTUAL remaining missing pixels after any gap repair,
    // regardless of HOM being enabled. Bright alternating neon colors make
    // subpixel edge cracks conspicuous even under sunlight/HDR processing.
    if(debug_void_pixels!=0 && (depth<=0.0 || depth>=65500.0)) {
        ivec2 pixel=ivec2(gl_FragCoord.xy);
        bool checker=(((pixel.x/2)+(pixel.y/2))&1)==0;
        out_color=vec4(checker?vec3(8.0,0.0,8.0):
                              vec3(0.0,8.0,0.0),1.0);
        return;
    }

    // No extra history texture or blit: the target scene color attachment
    // already contains the previous frame. Keep it only where nothing in
    // this frame wrote opaque depth. Alpha geometry then blends normally.
    if(retain_hom_pixels!=0 && (depth<=0.0 || depth>=65500.0))
        discard;

    vec4 albedo = texelFetch(color_image, tc, 0);
    vec4 emissive = texelFetch(emissive_image, tc, 0);
    float occlusion = 1.0 - texelFetch(occlusion_image, tc, 0).r;


    // The G-buffer holds un-fogged pigment. AO applies only to that
    // pigment, never to the intervening fog. Depth=65504 is empty sky.
    float fog = 0.0;
    if(depth > 0.0 && depth < 65500.0) {
        float span = max(fog_params.y - fog_params.x, 0.0001);
        fog = clamp((depth - fog_params.x) / span, 0.0, 1.0) *
              fog_params.z;
    }
    vec3 surface_rgb=albedo.rgb*occlusion;
    // Blend the new light against the material's UNLIT pigment, not its
    // already-darkened framebuffer result. Screen/soft-light applied directly
    // to surface_rgb cannot recover texture detail from a pitch-black pixel.
    // Depth.GBA stores base pigment when spotlights are enabled; depth.R
    // remains unchanged for SSAO and reconstruction.
    vec3 light_rgb,add_rgb;
    project_spotlights(depth,tc,light_rgb,add_rgb);
    vec3 pigment=(spotlight_count>0 || sun_lighting_active!=0)
        ? max(texelFetch(depth_image,tc,0).gba,vec3(0.0))
        : vec3(0.0);
    // Depth is JK's linear forward distance * 32. Far pixels skip the normal
    // fetch, world reconstruction and every shadow lookup.
    float sun_fade=(sun_lighting_active!=0 && depth>0.0 && depth<65500.0)
        ? sun_range_fade(depth/32.0) : 0.0;
    if(sun_fade>0.0) {
        vec3 vn=texelFetch(normal_image,tc,0).rgb;
        if(dot(vn,vn)>0.0001) {
            vec3 normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                                  camera_up.xyz*(vn.y/camera_up.w)+
                                  camera_forward.xyz*vn.z);
            float incidence=max(dot(normal,sun_shadow_direction),0.0);
            if(incidence>0.0) {
                vec3 world_position=reconstruct_scene_world(depth);
                float visibility=min(
                    sun_shadow_visibility(world_position,incidence),
                    sun_dynamic_shadow_visibility(world_position,incidence));
                // JK's vertex colors already contain authored lighting. If
                // only the newly added sun term is shadowed, a bright baked
                // floor barely changes under a caster. Keep a generous
                // ambient floor while allowing the occluder to shade that
                // baseline as well.
                float applied=incidence*sun_fade;
                surface_rgb*=1.0-0.6*applied*(1.0-visibility);
                vec3 sun_light=pigment*sun_shadow_color_intensity.rgb*
                    sun_shadow_color_intensity.a*applied*visibility;
                surface_rgb+=sun_light*(vec3(1.0)-clamp(surface_rgb,0.0,1.0));
            }
        }
    }
    vec3 diffuse_light=pigment*light_rgb;
    vec3 screened=surface_rgb+diffuse_light*
                  (vec3(1.0)-clamp(surface_rgb,0.0,1.0));
    // Artist-controlled additive spill retains cookie color without albedo
    // multiplication. Both contributions sit beneath distance fog.
    vec3 lit=mix(screened+add_rgb,fog_color,fog);
    out_color=vec4(emissive.rgb+lit,albedo.a);
}
