#version 330 core
// One quarter as many opaque sun-shadow evaluations. The ordinary compositor
// still applies the authored per-pixel sun incidence, falloff and SKY gate.
uniform sampler2D depth_image; // resolved full-resolution linear depth
uniform sampler2D normal_image; // resolved full-resolution view normals
uniform vec4 camera_origin,camera_right,camera_forward,camera_up;
uniform int sun_shadow_active;
uniform sampler2DShadow sun_shadow_image,sun_shadow_far_image,sun_dynamic_image;
uniform vec3 sun_shadow_direction;
uniform vec3 sun_shadow_origin,sun_shadow_right,sun_shadow_up;
uniform vec3 sun_shadow_forward,sun_shadow_limits;
uniform vec3 sun_shadow_far_origin,sun_shadow_far_right;
uniform vec3 sun_shadow_far_up,sun_shadow_far_forward,sun_shadow_far_limits;
uniform float sun_shadow_split,sun_shadow_receiver_bias_scale;
uniform vec2 sun_shadow_pcf_radius;
uniform int sun_dynamic_count;
uniform float sun_dynamic_pcf_radius; // 0=original single hardware PCF comparison
uniform vec4 sun_dynamic_atlas_rects[8];
uniform vec3 sun_dynamic_origin[8],sun_dynamic_right[8];
uniform vec3 sun_dynamic_up[8],sun_dynamic_forward[8],sun_dynamic_limits[8];
in vec2 vp_texcoords;
layout(location=0) out vec4 out_color;

// Keep the same bias, cascade overlap, dynamic atlas and PCF formulas as
// post_opaque_composite.frag. This opt-in prototype duplicates only these
// functions; tests guard source equivalence until shared extraction.
#include "sun_dynamic_tile_pcf.glsl"

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
                        float incidence,float radius)
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
        // Tile-safe filter retains the original lookup for radius zero.
        float grazing=1.0-clamp(incidence,0.0,1.0);
        float bias_world=(0.006+0.032*grazing*grazing)*
                         sun_shadow_receiver_bias_scale;
        float bias=max(4.0/16777215.0,
                       bias_world/max(sun_dynamic_limits[i].z,0.001));
        float comparison=uv.z-bias;
        float object_visibility=sun_dynamic_tile_pcf(
            sun_dynamic_image,rect,uv,comparison,sun_dynamic_pcf_radius);
        visibility=min(visibility,object_visibility);
        if(visibility<=0.001)break;
    }
    return mix(1.0,visibility,near_fade);
}


vec2 encode_oct(vec3 n)
{
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),0.00001);
    vec2 e=n.xy;
    vec2 sign_nonzero=vec2(e.x>=0.0?1.0:-1.0,
                           e.y>=0.0?1.0:-1.0);
    if(n.z<0.0)e=(vec2(1.0)-abs(e.yx))*sign_nonzero;
    return e*0.5+0.5;
}
void main()
{
    ivec2 full_size=textureSize(depth_image,0);
    ivec2 base=ivec2(gl_FragCoord.xy)*2;
    ivec2 chosen=ivec2(0);
    float d=65504.0;
    vec4 chosen_normal=vec4(0.0);
    // Pick the nearest valid receiver from the underlying 2x2 G-buffer.
    // Full-resolution reconstruction falls back where it has no match.
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 p=base+ivec2(x,y);
        if(any(greaterThanEqual(p,full_size)))continue;
        float z=texelFetch(depth_image,p,0).r;
        vec4 n=texelFetch(normal_image,p,0);
        if(n.a<=0.5 || z<=0.0 || z>=65500.0 ||
           dot(n.rgb,n.rgb)<0.0001 || z>=d)continue;
        d=z;chosen_normal=n;chosen=p;
    }
    if(d>=65500.0) {
        out_color=vec4(1.0,65504.0,0.5,0.5);
        return;
    }
    vec3 view_n=normalize(chosen_normal.rgb);
    vec3 normal=normalize(camera_right.xyz*(view_n.x/camera_right.w)+
                          camera_up.xyz*(view_n.y/camera_up.w)+
                          camera_forward.xyz*view_n.z);
    bool sprite=chosen_normal.a>1.5;
    float incidence=sprite
        ?max(abs(dot(normal,sun_shadow_direction)),0.35)
        :max(dot(normal,sun_shadow_direction),0.0);
    float visibility=1.0;
    if(incidence>0.0) {
        vec2 ndc=(vec2(chosen)+vec2(0.5))/vec2(full_size)*2.0-1.0;
        vec3 world=camera_origin.xyz+(d/32.0)*(
            camera_forward.xyz+
            ndc.x*camera_right.w*camera_right.xyz+
            ndc.y*camera_up.w*camera_up.xyz);
        visibility=min(sun_shadow_visibility(world,incidence),
                       sun_dynamic_shadow_visibility(world,incidence));
    }
    out_color=vec4(visibility,d,encode_oct(view_n));
}
