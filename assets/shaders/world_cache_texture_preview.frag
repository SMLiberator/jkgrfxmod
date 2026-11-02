#version 330 core
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// Static JK lighting is evaluated at vertices and interpolated in RGB.
// In pixel mode, a shared GLSL dynamic-light contribution is composed per
// fragment without double-counting native per-vertex dynamic lights.
// MotS RGB light arrays are not yet decoded; JK global extra is sampled.
in vec2 material_uv;
in vec3 cached_view_normal;
in vec3 cached_lit_rgb;
in vec3 cached_light_state; // authored+extra, ambient floor, light mode
in vec3 cached_sector_tint; // the face's owning sector, not camera
in vec3 cached_world_position;
in vec3 cached_world_normal;
uniform int num_lights;
uniform float light_attenuation;
uniform vec4 light_position_radius[16];
uniform float light_intensity[16];
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
uniform int solid_color; // GeoMode 3: native MAT/CMP RGB, no texture sampling
uniform int alpha_cutout; // explicit cutout MATs on solid adjoins
uniform float alpha_cutoff;
uniform int transparent_pass; // 0 G-buffer, 1 sorted forward alpha
uniform int alpha_mode; // 0 solid, 1 mixed, 2 mask, 3 blended, 4 dither
uniform float face_opacity; // 90/255 when rdFace.type includes 0x2
uniform int blend_mode; // normal=0, additive=1, multiply=2
uniform vec4 fog_params; // native fog start/end/opacity
uniform vec3 fog_color;
uniform int sky_mode;
uniform vec4 sky_horizon; // x=horizonDistance*tanHalfFOV, y=pixels/rev, z=tanHalfFOV
uniform vec3 sky_angles; // pitch/yaw/roll in JK degrees
uniform vec3 sky_camera_origin,sky_camera_right;
uniform vec3 sky_camera_forward,sky_camera_up;
uniform float sky_ceiling_height;
uniform vec2 sky_offset,sky_face_offset,sky_viewport;
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;

#include "jk_point_lighting.glsl"

vec3 world_jk_effect(vec3 raw,vec3 local_sector_tint)
{
    vec3 v=pow(clamp(raw,0.0,1.0),vec3(1.0/1.8));
    if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
        v*=step(vec3(0.5),jk_color_filter.xyz);
    vec3 t=clamp(local_sector_tint+jk_color_tint.xyz,
                 vec3(-4.0),vec3(4.0));
    vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
    v=clamp(v*scale,0.0,1.0);
    v=clamp(v+jk_color_add_fade.xyz,0.0,1.0);
    v*=jk_color_add_fade.w;
    return pow(clamp(v,0.0,1.0),vec3(1.8));
}
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
float dither_threshold()
{
    ivec2 pixel=ivec2(gl_FragCoord.xy)&ivec2(3);
    int lo=(((pixel.x^pixel.y)&1)<<1)|(pixel.y&1);
    int hi=((((pixel.x>>1)^(pixel.y>>1))&1)<<1)|((pixel.y>>1)&1);
    return (float(4*lo+hi)+0.5)/16.0;
}
void main() {
    // World alpha coverage is split with the same rules as cached 3DOs.
    // Sample the MAT once and discard *before* writing any attachment.
    vec2 uv=material_uv;
    if(sky_mode!=0) {
        // Match sithRenderSky's image-space horizon UV calculation:
        // canvas offsets, camera roll, minus yaw/pitch scaled by pixels/rev.
        vec2 ndc=gl_FragCoord.xy*2.0/max(sky_viewport,vec2(1.0))-1.0;
        if(sky_mode==1) {
            float h=sky_horizon.x;
            vec2 screen=vec2(ndc.x*h,-ndc.y*h*sky_viewport.y/sky_viewport.x);
            float roll=radians(sky_angles.z);
            float cr=cos(roll),sr=sin(roll);
            uv=vec2(screen.x*cr-screen.y*sr,screen.y*cr+screen.x*sr);
            uv-=sky_angles.yx*(sky_horizon.y/360.0);
        } else {
            // The camera ray intersects a fixed, horizontal sky plane.
            // Native JK falls back to 1000 units on a missed intersection.
            float hfov=sky_horizon.z;
            vec3 ray=sky_camera_forward+
                sky_camera_right*(ndc.x*hfov)+
                sky_camera_up*(ndc.y*hfov*sky_viewport.y/sky_viewport.x);
            vec3 dir=normalize(ray);
            float distance_to_sky=1000.0;
            if(abs(dir.z)>0.000001) {
                float t=(sky_ceiling_height-sky_camera_origin.z)/dir.z;
                if(t>=0.0 && t<=1000.0)distance_to_sky=t;
            }
            uv=(sky_camera_origin.xy+dir.xy*distance_to_sky)*16.0;
        }
        uv+=sky_offset+sky_face_offset;
    }
    vec4 texel=vec4(1.0);
    if(solid_color==0)
        texel=texture(albedo_map,uv*uv_scale+uv_scroll);
    float texture_material_alpha=clamp(texel.a*material_tint.a,0.0,1.0);
    float coverage=clamp(texture_material_alpha*face_opacity,0.0,1.0);
    if(alpha_cutout!=0 && coverage<alpha_cutoff)discard;
    if(transparent_pass==0) {
        if(alpha_mode==3)discard;
        if(alpha_mode==1 && coverage<0.999)discard;
        if(alpha_mode==2 && coverage<alpha_cutoff)discard;
        if(alpha_mode==4 && coverage<=dither_threshold())discard;
    } else {
        if(alpha_mode!=1 && alpha_mode!=3)discard;
        if(coverage<=0.001)discard;
        if(alpha_mode==1 && coverage>=0.999)discard;
    }
    vec3 albedo=solid_color!=0 ? clamp(material_tint.rgb,0.0,1.0) :
        clamp(texel.rgb*material_tint.rgb,0.0,1.0);
    if(sky_mode!=0) {
        // Occupancy is normal.A, not geometric normal. A sky pixel
        // covers the HOM background but receives no AO, sun or spotlights.
        out_color=vec4(albedo,1.0);
        out_emissive=vec4(0.0,0.0,0.0,1.0);
        out_normal=vec4(0.0,0.0,0.0,1.0);
        out_depth=vec4(65504.0,albedo);
        return;
    }
    // Keep authored Gouraud/sector lighting, already interpolated in RGB.
    // The same GLSL helpers calculate pixel light for both world and 3DO.
    vec3 lighting=cached_lit_rgb;
    if(cached_light_state.z>2.5 && num_lights>0) {
        float added=0.0;
        vec3 normal=normalize(cached_world_normal);
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            added+=jk_point_sample(delta,normal,light_position_radius[i].w,
                                   light_intensity[i],light_attenuation,
                                   false);
            if(added>=1.0)break;
        }
        if(added>0.0) {
            vec3 light_color=max(
                world_jk_effect(vec3(1.0),cached_sector_tint)-
                world_jk_effect(vec3(0.0),cached_sector_tint),vec3(0.0));
            lighting=jk_point_compose(lighting,added,light_color);
        }
    }
    lighting=clamp(lighting,0.0,1.0);
    if(transparent_pass!=0) {
        // Only one color attachment exists in the forward alpha target.
        // The framebuffer uses premultiplied (ONE, ONE_MINUS_SRC_ALPHA).
        float depth=32.0/max(gl_FragCoord.w,0.00000001);
        float fog=clamp((depth-fog_params.x)/
                        max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
                  fog_params.z;
        vec3 lit=mix(albedo*lighting,fog_color,fog);
        if(blend_mode==2) {
            // (DST_COLOR, ZERO) needs a neutral multiplier at alpha zero.
            out_color=vec4(mix(vec3(1.0),lit,coverage),0.0);
        } else {
            // Native D3DTLVERTEX diffuse RGB premultiplies the *vertex*
            // opacity BEFORE gamma-1.8 linearization. The native bridge
            // uses pow(vertexRGB*vertexAlpha,1.8), not
            // pow(vertexRGB,1.8)*vertexAlpha. For rdFace.type 0x2, the
            // actual alpha coverage is 90/255, but its diffuse RGB weight
            // is pow(90/255,1.8). MAT texture alpha / material opacity
            // are applied separately, AFTER that conversion.
            //
            // Fog is different: JK's forward shader contributes fogColor
            // multiplied by the actual (unmodified) coverage.
            float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);
            vec3 premultiplied=
                albedo*lighting*(1.0-fog)*
                    (texture_material_alpha*vertex_rgb_alpha)
                +fog_color*(fog*coverage);
            // Normal blending: ONE, ONE_MINUS_SRC_ALPHA.
            // Additive: ONE, ONE; alpha attachment remains unchanged.
            out_color=vec4(premultiplied,blend_mode==1?0.0:coverage);
        }
        return;
    }
    out_color=vec4(albedo*lighting,1.0);
    out_emissive=vec4(0.0,0.0,0.0,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
