#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
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
uniform float spotlight_shadow_angle_bias_scale; // 0=legacy, 1=+0.035 JKU at grazing

uniform int sun_lighting_active; // independent of depth-map readiness
uniform int sun_shadow_active;
#ifdef JKGM_SUN_VSM
uniform sampler2D sun_shadow_image;
#else
uniform sampler2DShadow sun_shadow_image;
#endif
uniform vec3 sun_shadow_direction; // world-space direction toward the sun
uniform vec4 sun_shadow_color_intensity;
uniform vec3 sun_shadow_origin;
uniform vec3 sun_shadow_right;
uniform vec3 sun_shadow_up;
uniform vec3 sun_shadow_forward;
uniform vec3 sun_shadow_limits; // orthographic width, height, depth range
#ifdef JKGM_SUN_VSM
uniform sampler2D sun_shadow_far_image;
#else
uniform sampler2DShadow sun_shadow_far_image;
#endif
uniform vec3 sun_shadow_far_origin,sun_shadow_far_right;
uniform vec3 sun_shadow_far_up,sun_shadow_far_forward,sun_shadow_far_limits;
uniform float sun_shadow_split; // JK forward distance from camera
uniform float sun_shadow_receiver_bias_scale;
uniform vec2 sun_shadow_pcf_radius; // 0=legacy HW comparison; positive=4 taps
uniform vec2 sun_vsm_params; // x=min world-space stddev (JKU), y=bleed reduction
uniform int sun_sky_aperture_active;
#ifdef JKGM_SUN_SKY_APERTURE
uniform sampler2DArray sun_sky_aperture_map;
uniform vec4 sun_sky_aperture_depth_transform; // near scale,bias; far scale,bias
#endif
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
#ifdef JKGM_SUN_VSM
float sun_shadow_filter(sampler2D map,vec3 coordinate,float range,
                        float incidence,float radius)
#else
float sun_shadow_filter(sampler2DShadow map,vec3 coordinate,float range,
                        float incidence,float radius)
#endif
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
#ifdef JKGM_SUN_VSM
    // VSM: Chebyshev bound from prefiltered first and second moments.
    // Preserve the receiver bias and reduce light bleeding conservatively.
    float receiver=coordinate.z-bias;
    vec2 moments=texture(map,coordinate.xy).rg;
    if(receiver<=moments.x)return 1.0;
    // The legacy 2e-7 floor prevents loss-of-significance noise in
    // E[z*z]-E[z]^2. A world-space floor is ADDITIVE, never a replacement.
    // With sun_vsm_params.x=0 this exactly matches the original VSM floor.
    float min_stddev=sun_vsm_params.x/max(range,0.001);
    float variance=max(moments.y-moments.x*moments.x,
                       max(2.0e-7,min_stddev*min_stddev));
    float delta=receiver-moments.x;
    float probability=variance/(variance+delta*delta);
    float bleed=sun_vsm_params.y;
    return clamp((probability-bleed)/(1.0-bleed),0.0,1.0);
#else
    // Keep the original one-comparison path when the opt-in radius is 0.
    // Four bilinear hardware PCF comparisons broaden edges for radius > 0.
    vec3 lookup=vec3(coordinate.xy,coordinate.z-bias);
    if(radius<=0.0)return texture(map,lookup);
    vec2 spread=(radius/vec2(textureSize(map,0)))*0.5;
    float sum=0.0;
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=vec2(x==0?-spread.x:spread.x,
                        y==0?-spread.y:spread.y);
        sum+=texture(map,vec3(lookup.xy+delta,lookup.z));
    }
    return sum*0.25;
#endif
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
                                 sun_shadow_limits.z,incidence,
                                 sun_shadow_pcf_radius.x);
    vec3 far_uv=sun_shadow_coordinates(world_position,sun_shadow_far_origin,
        sun_shadow_far_right,sun_shadow_far_up,
        sun_shadow_far_forward,sun_shadow_far_limits);
    if(!sun_shadow_inside(far_uv))
        return use_near
            ? sun_shadow_filter(sun_shadow_image,near_uv,sun_shadow_limits.z,
                                incidence,sun_shadow_pcf_radius.x)
            : 1.0;
    float far_visibility=sun_shadow_filter(sun_shadow_far_image,
        far_uv,sun_shadow_far_limits.z,incidence,sun_shadow_pcf_radius.y);
    if(!use_near)return far_visibility;
    float near_visibility=sun_shadow_filter(sun_shadow_image,
        near_uv,sun_shadow_limits.z,incidence,sun_shadow_pcf_radius.x);
    float blend=smoothstep(transition_start,transition_end,camera_depth);
    return mix(near_visibility,far_visibility,blend);
}

// Separate sky aperture from normal occluder shadowing: a sky polygon
// facing the sun is necessary for added directional sunlight to enter.
#ifdef JKGM_SUN_SKY_APERTURE
float sun_sky_aperture_sample(vec3 uv,int layer)
{
    if(!sun_shadow_inside(uv))return 0.0;
    vec2 tr=layer==0
        ?sun_sky_aperture_depth_transform.xy
        :sun_sky_aperture_depth_transform.zw;
    float receiver_z=uv.z*tr.x+tr.y;
    if(receiver_z<0.0 || receiver_z>1.0)return 0.0;
    float sky_z=texture(sun_sky_aperture_map,vec3(uv.xy,float(layer))).r;
    // Missing aperture pixels contain the clear depth 1.0.
    // Slight physical tolerance avoids rejecting a receiver exactly at
    // a sky polygon because of depth quantization.
    return (sky_z<0.999999 && sky_z+0.00001<receiver_z)?1.0:0.0;
}
float sun_sky_aperture(vec3 position)
{
    if(sun_sky_aperture_active==0)return 1.0;
    float depth=dot(position-camera_origin.xyz,camera_forward.xyz);
    vec3 near_uv=sun_shadow_coordinates(position,sun_shadow_origin,
        sun_shadow_right,sun_shadow_up,sun_shadow_forward,sun_shadow_limits);
    vec3 far_uv=sun_shadow_coordinates(position,sun_shadow_far_origin,
        sun_shadow_far_right,sun_shadow_far_up,
        sun_shadow_far_forward,sun_shadow_far_limits);
    bool n=depth<sun_shadow_split && sun_shadow_inside(near_uv);
    bool f=sun_shadow_inside(far_uv);
    if(n && !f)return sun_sky_aperture_sample(near_uv,0);
    if(!n && f)return sun_sky_aperture_sample(far_uv,1);
    if(!n && !f)return 0.0;
    float near_access=sun_sky_aperture_sample(near_uv,0);
    if(depth<sun_shadow_split*0.8125)return near_access;
    float far_access=sun_sky_aperture_sample(far_uv,1);
    return mix(near_access,far_access,
        smoothstep(sun_shadow_split*0.8125,
                   sun_shadow_split*0.9375,depth));
}

#else
float sun_sky_aperture(vec3 position) { return 1.0; }
#endif

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

float spotlight_shadow_visibility(vec2 uv,float receiver_distance,float facing)
{
    float n=spotlight_shadow_near_range.x;
    float f=spotlight_shadow_near_range.y;
    if(receiver_distance<=n || receiver_distance>=f)return 1.0;
    // Same perspective Z transform as spotlight_shadow.vert. Using linear
    // receiver distance directly would create spurious shadows everywhere.
    float receiver=f/(f-n)-f*n/((f-n)*receiver_distance);
    float derivative=f*n/((f-n)*receiver_distance*receiver_distance);
    // Zero scale preserves the fixed 0.015-JKU receiver bias. Grazing
    // surfaces need more offset because each texel spans more surface area.
    float grazing=1.0-clamp(abs(facing),0.0,1.0);
    float world_bias=0.015+0.035*spotlight_shadow_angle_bias_scale*
                     grazing*grazing;
    float bias=max(0.000025,derivative*world_bias);
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
        float facing=dot(normal,normalize(-relative));
        // Mild floor illumination even with near-horizontal beams.
        float incidence=max(0.05, 0.25+0.75*facing);
        float shadow=1.0;
        if(i==spotlight_shadow_slot)
            shadow=spotlight_shadow_visibility(uv,distance,facing);
        vec3 radiance=cookie.rgb*cookie.a*(edge*fade*incidence*shadow);
        diffuse+=radiance*spotlight_intensities[i].x;
        additive+=radiance*spotlight_intensities[i].y;
    }
}

// Opaque coverage is independent of the precision of the depth buffer.
// Missing geometry intentionally follows HOM/clear-color behavior.
bool has_opaque_coverage(ivec2 p)
{
    return texelFetch(normal_image,p,0).a>0.5;
}

void main()
{
    ivec2 tc=ivec2(gl_FragCoord.xy);
    float depth=texelFetch(depth_image,tc,0).r;
    bool covered=has_opaque_coverage(tc);
    // Debug the ACTUAL remaining missing pixels,
    // regardless of HOM being enabled. Bright alternating neon colors make
    // subpixel edge cracks conspicuous even under sunlight/HDR processing.
    if(debug_void_pixels!=0 && !covered) {
        ivec2 pixel=ivec2(gl_FragCoord.xy);
        bool checker=(((pixel.x/2)+(pixel.y/2))&1)==0;
        out_color=vec4(checker?vec3(8.0,0.0,8.0):
                              vec3(0.0,8.0,0.0),1.0);
        return;
    }

    // Preserve the previous scene only if NO opaque fragment was drawn.
    // In particular, a skybox beyond 65504/32 world units can overflow
    // half-float scene depth while its color and coverage are still valid.
    // Depth overflow may limit distant lighting/fog precision, but never
    // turn a real opaque fragment into a HOM pixel.
    if(retain_hom_pixels!=0 && !covered)
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
        vec4 normal_pixel=texelFetch(normal_image,tc,0);
        vec3 vn=normal_pixel.rgb;
        if(dot(vn,vn)>0.0001) {
            vec3 normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                                  camera_up.xyz*(vn.y/camera_up.w)+
                                  camera_forward.xyz*vn.z);
            // Normal.A=2 means a visible cutout SPR. Its face rotates to
            // follow the camera; a tree's shadow must not disappear merely
            // because the billboard happens to face away from the sun.
            // Ordinary 3DO/world surfaces retain their existing response.
            bool sprite=normal_pixel.a>1.5;
            float incidence=sprite
                ?max(abs(dot(normal,sun_shadow_direction)),0.35)
                :max(dot(normal,sun_shadow_direction),0.0);
            if(incidence>0.0) {
                vec3 world_position=reconstruct_scene_world(depth);
                float visibility=min(
                    sun_shadow_visibility(world_position,incidence),
                    sun_dynamic_shadow_visibility(world_position,incidence));
                // Treat an absent sky aperture exactly like a blocked sun
                // ray. It must suppress direct light AND allow the shadow
                // baseline to darken authored brightness indoors, rather
                // than silently restoring JK's original baked brightness.
                // No change occurs when the sun is disabled (sun_fade=0).
                float sky_access=sun_sky_aperture(world_position);
                float direct_visibility=visibility*sky_access;
                float sun_applied=incidence*sun_fade;
                // Preserve low-intensity compatibility: shadow contrast
                // tracks the developer's positive sun intensity.
                float sun_shadow_strength=clamp(
                    sun_shadow_color_intensity.a,0.0,1.0);
                // Sprite normals follow the camera, so their shadowed
                // baseline does not depend on geometric incidence.
                float shadowed_baseline=(sprite ? 0.75*sun_fade
                                               : 0.6*sun_applied)*
                                        sun_shadow_strength;
                surface_rgb*=1.0-shadowed_baseline*(1.0-direct_visibility);
                vec3 sun_light=pigment*sun_shadow_color_intensity.rgb*
                    sun_shadow_color_intensity.a*sun_applied*direct_visibility;
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
