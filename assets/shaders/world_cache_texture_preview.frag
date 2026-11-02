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
uniform sampler2D albedo_map2;
uniform int albedo_source,albedo_source2;
uniform float albedo_transition;
uniform sampler2D emissive_map;
uniform int has_emissive,emissive_only;
uniform vec3 emissive_factor;
uniform vec2 emissive_scroll;
uniform vec2 uv_scale,uv_scroll;
// GPU 3DO-compatible EnvMap samplers, reflection modes, material parameters.
uniform sampler2D surface_map,normal_map,normal_map2,env_map,env_map2;
uniform vec4 specular_params,normal_specular_params,environment_info;
uniform vec2 normal_specular_scroll,normal_specular_scroll2;
uniform vec3 camera_world_origin,camera_right_axis;
uniform vec3 camera_forward_axis,camera_up_axis;
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
#include "gpu_material_common.glsl"

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
// Reconstruct world-space tangent basis from the native UVs and geometry.
// Reflection normals deliberately exclude the FOV scaling used for SSAO.
vec3 world_mapped_normal(vec2 uv)
{
    vec3 n=normalize(cached_world_normal);
    if(normal_specular_params.x<0.5 && normal_specular_params.z<0.5)
        return n;
    vec2 slope=vec2(0.0);
    if(normal_specular_params.x>0.5) {
        vec3 texn=texture(normal_map,uv+normal_specular_scroll).xyz*2.0-1.0;
        slope+=texn.xy/max(texn.z,0.05)*normal_specular_params.y;
    }
    if(normal_specular_params.z>0.5) {
        vec3 texn2=texture(normal_map2,uv+normal_specular_scroll2).xyz*2.0-1.0;
        slope+=texn2.xy/max(texn2.z,0.05)*normal_specular_params.w;
    }
    vec3 dp1=dFdx(cached_world_position),dp2=dFdy(cached_world_position);
    vec2 du1=dFdx(uv),du2=dFdy(uv);
    vec3 t=cross(dp2,n)*du1.x+cross(n,dp1)*du2.x;
    vec3 b=cross(dp2,n)*du1.y+cross(n,dp1)*du2.y;
    float basis=max(dot(t,t),dot(b,b));
    if(basis<1e-16)return n;
    float inv_basis=inversesqrt(basis);
    return normalize(mat3(t*inv_basis,b*inv_basis,n)*
                     normalize(vec3(slope,1.0)));
}

// Match GPU-cached 3DO Fresnel, equirectangular/matcap UV, roughness LOD,
// metalness and global/per-MAT environment transitions.
vec3 world_environment_reflection(vec3 base_color,vec2 uv,out float metalness)
{
    metalness=0.0;
    if((environment_info.x<0.5 && environment_info.z<0.5) ||
       specular_params.x<=0.0)return vec3(0.0);
    vec3 surf=specular_params.w>0.5?texture(surface_map,uv).rgb:vec3(1.0);
    float strength=max(specular_params.x*surf.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=clamp(specular_params.y*surf.g,0.0,1.0);
    metalness=clamp(specular_params.z*surf.b,0.0,1.0)*
              mix(environment_info.x,environment_info.z,environment_info.w);
    vec3 nw=world_mapped_normal(uv);
    vec3 vw=normalize(cached_world_position-camera_world_origin);
    vec3 n=normalize(vec3(dot(nw,camera_right_axis),
                          dot(nw,camera_up_axis),dot(nw,camera_forward_axis)));
    vec3 v=normalize(vec3(dot(vw,camera_right_axis),
                          dot(vw,camera_up_axis),dot(vw,camera_forward_axis)));
    if(dot(n,v)>0.0)n=-n;
    vec3 reflection=reflect(v,n);
    int mode=int(environment_info.y+0.5);
    vec3 dir=reflection;
    if(mode<=1) {
        vec3 world=camera_right_axis*reflection.x+
                   camera_forward_axis*reflection.z+
                   camera_up_axis*reflection.y;
        dir=vec3(world.x,world.z,world.y);
    }
    vec2 env_uv=mode==3
        ?clamp(vec2(0.5+0.5*n.x,0.5-0.5*n.y),vec2(0.001),vec2(0.999))
        :vec2(fract(atan(dir.x,dir.z)/6.28318530718+0.5),
              0.5-asin(clamp(dir.y,-1.0,1.0))/3.14159265359);
    ivec2 dims=environment_info.x>0.5?
        textureSize(env_map,0):textureSize(env_map2,0);
    float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
    vec3 env_from=vec3(0.0),env_to=vec3(0.0);
    if(environment_info.x>0.5 && environment_info.w<1.0)
        env_from=textureLod(env_map,env_uv,lod).rgb;
    if(environment_info.z>0.5 && environment_info.w>0.0)
        env_to=textureLod(env_map2,env_uv,lod).rgb;
    vec3 env=mix(env_from,env_to,environment_info.w);
    vec3 f0=mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float cos_theta=clamp(dot(n,-v),0.0,1.0);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-cos_theta,5.0);
    return env*fresnel*strength;
}

layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
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
    vec2 material_texcoord=uv*uv_scale;
    vec4 texel=gpu_material_sample_albedo(
        albedo_map,albedo_map2,material_texcoord,uv_scroll,
        solid_color!=0?2:albedo_source,albedo_source2,albedo_transition);
    float texture_material_alpha=clamp(texel.a*material_tint.a,0.0,1.0);
    float coverage=clamp(texture_material_alpha*face_opacity,0.0,1.0);
    if(alpha_cutout!=0 && coverage<alpha_cutoff)discard;
    if(gpu_material_discard(coverage,alpha_mode,alpha_cutoff,
                            emissive_only,transparent_pass!=0,false))discard;
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
    // Shared GPU MAT emission is independent of diffuse opacity.
    vec3 emission=gpu_material_emission(
        emissive_map,material_texcoord,emissive_scroll,
        emissive_factor,has_emissive);
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
    float env_metalness=0.0;
    vec3 env_reflection=world_environment_reflection(
        albedo,material_texcoord,env_metalness);
    vec3 lit_diffuse=albedo*lighting*(1.0-env_metalness);
    if(transparent_pass!=0) {
        // Only one color attachment exists in the forward alpha target.
        // The framebuffer uses premultiplied (ONE, ONE_MINUS_SRC_ALPHA).
        float depth=32.0/max(gl_FragCoord.w,0.00000001);
        float fog=clamp((depth-fog_params.x)/
                        max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
                  fog_params.z;
        vec3 lit=mix(lit_diffuse,fog_color,fog);
        vec3 specular=env_reflection*(1.0-fog);
        if(blend_mode==2) {
            // (DST_COLOR, ZERO) needs a neutral multiplier at alpha zero.
            out_color=vec4(mix(vec3(1.0),
                clamp(lit+emission+specular*coverage,0.0,1.0),
                coverage),0.0);
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
            out_color=vec4(premultiplied+emission+specular*coverage,
                           blend_mode==1?0.0:coverage);
        }
        return;
    }
    out_color=vec4(lit_diffuse,1.0);
    // Opaque reflections are composited after SSAO, as for GPU 3DOs.
    out_emissive=vec4(emission+env_reflection,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
