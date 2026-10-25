#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
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
uniform sampler2D spotlight_shadow_image;
uniform vec2 spotlight_shadow_near_range; // near, far in JK world units

uniform int sun_shadow_active;
uniform sampler2D sun_shadow_image;
uniform vec3 sun_shadow_direction; // world-space direction toward the sun
uniform vec4 sun_shadow_color_intensity;
uniform vec3 sun_shadow_origin;
uniform vec3 sun_shadow_right;
uniform vec3 sun_shadow_up;
uniform vec3 sun_shadow_forward;
uniform vec3 sun_shadow_limits; // orthographic width, height, depth range

float sun_shadow_visibility(vec3 world_position)
{
    vec3 relative=world_position-sun_shadow_origin;
    vec3 coordinate=vec3(
        dot(relative,sun_shadow_right)/sun_shadow_limits.x,
        dot(relative,sun_shadow_up)/sun_shadow_limits.y,
        dot(relative,sun_shadow_forward)/sun_shadow_limits.z);
    if(any(lessThan(coordinate,vec3(0.0))) ||
       any(greaterThan(coordinate,vec3(1.0))))return 1.0;
    float receiver=coordinate.z;
    float bias=max(0.0001,1.0/float(textureSize(sun_shadow_image,0).x));
    vec2 texel=1.0/vec2(textureSize(sun_shadow_image,0));
    float visible=0.0;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
        float caster=texture(sun_shadow_image,
            coordinate.xy+vec2(x,y)*texel).r;
        visible+=receiver-bias<=caster ? 1.0 : 0.0;
    }
    return visible/9.0;
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
    // Geometry-scale bias, bounded by one 16-bit depth quantum. Avoid the
    // huge depth-space bias that would detach contact shadows at long range.
    float bias=max(0.000025,derivative*0.015);
    vec2 texel=1.0/vec2(textureSize(spotlight_shadow_image,0));
    float lit=0.0;
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=(vec2(x,y)-vec2(0.5))*texel;
        float caster=texture(spotlight_shadow_image,uv+delta).r;
        lit+=(receiver-bias<=caster)?0.25:0.0;
    }
    return lit;
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

void main()
{
    ivec2 tc = ivec2(gl_FragCoord.xy);

    vec4 albedo = texelFetch(color_image, tc, 0);
    vec4 emissive = texelFetch(emissive_image, tc, 0);
    float occlusion = 1.0 - texelFetch(occlusion_image, tc, 0).r;
    float depth = texelFetch(depth_image, tc, 0).r;

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
    vec3 pigment=(spotlight_count>0 || sun_shadow_active!=0)
        ? max(texelFetch(depth_image,tc,0).gba,vec3(0.0))
        : vec3(0.0);
    if(sun_shadow_active!=0 && depth>0.0 && depth<65500.0) {
        vec3 vn=texelFetch(normal_image,tc,0).rgb;
        if(dot(vn,vn)>0.0001) {
            vec3 normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                                  camera_up.xyz*(vn.y/camera_up.w)+
                                  camera_forward.xyz*vn.z);
            float incidence=max(dot(normal,sun_shadow_direction),0.0);
            if(incidence>0.0) {
                vec3 world_position=reconstruct_scene_world(depth);
                float visibility=sun_shadow_visibility(world_position);
                // JK's vertex colors already contain authored lighting. If
                // only the newly added sun term is shadowed, a bright baked
                // floor barely changes under a caster. Keep a generous
                // ambient floor while allowing the occluder to shade that
                // baseline as well.
                surface_rgb*=1.0-0.6*incidence*(1.0-visibility);
                vec3 sun_light=pigment*sun_shadow_color_intensity.rgb*
                    sun_shadow_color_intensity.a*incidence*visibility;
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
