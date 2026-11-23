#version 330 core
#include "gbuffer_normal_packed.glsl"
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// Static JK lighting is evaluated at vertices and interpolated in RGB.
// In pixel mode, a shared GLSL dynamic-light contribution is composed per
// fragment without double-counting native per-vertex dynamic lights.
// MotS RGB light arrays are not yet decoded; JK global extra is sampled.
in vec2 material_uv;
in vec3 cached_view_normal;
in vec3 cached_lit_rgb;
in vec3 cached_raw_light_rgb;
in vec3 cached_light_state; // authored+extra, ambient floor, light mode
in vec3 cached_sector_tint; // SithSector.tint, not colormap
in vec3 cached_sector_cmp_tint; // SithSector.colormap color multiplier
in vec3 cached_world_position;
in vec3 cached_world_normal;
uniform int num_lights;
uniform float light_attenuation;
uniform vec4 light_position_radius[16];
uniform float light_intensity[16];
uniform vec3 light_color[16]; // white for non-saber native lights
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
// Matcap color is an artist-authored sRGB image. Its per-pixel intensity
// and roughness are LINEAR mask channels, completely separate from GGX.
#ifdef JKGM_MATCAP_LAYER
uniform sampler2D matcap_map,matcap_surface_map;
uniform vec4 matcap_params; // RGB factor, roughness
uniform float matcap_lighting_influence;
uniform int matcap_enabled,matcap_has_surface;
#endif
#ifdef JKGM_CLEARCOAT_LAYER
uniform sampler2D clearcoat_surface_map,clearcoat_normal_map;
uniform vec4 clearcoat_params; // strength, roughness, transmission
uniform int clearcoat_enabled,clearcoat_has_surface,clearcoat_has_normal;
#endif
uniform vec4 projection; // same FOV factors as world_cache.vert
uniform int normal_mapped_diffuse; // opt-in added lights, not baked JK RGB
uniform vec2 normal_specular_scroll,normal_specular_scroll2;
uniform vec3 camera_world_origin,camera_right_axis;
uniform vec3 camera_forward_axis,camera_up_axis;
#ifdef JKGM_REFLECTION_PROBE
uniform sampler2DArray shared_reflection_map;
#endif
uniform int shared_reflection_ready;
// Single opt-in, same-frame planar capture, bound only during world draws.
// RGBA alpha marks actually reflected geometry; unfilled sky pixels fall
// through to the legacy EnvMap when one is configured.
uniform sampler2D planar_reflection_map;
uniform int planar_reflection_ready;
// Low-resolution SSR is available only AFTER the opaque pass; mode 1 keeps
// EnvMap fallback when a ray cannot find geometry in the current screen.
uniform sampler2D screen_reflection_map;
uniform int screen_reflection_ready;
uniform vec4 planar_receiver_plane;
uniform vec3 planar_camera_origin,planar_camera_right,planar_camera_forward;
uniform vec3 planar_camera_up;
uniform vec2 planar_tan_half_fov;
uniform vec4 material_tint;
uniform int solid_color; // GeoMode 3: native MAT/CMP RGB, no texture sampling
uniform int solid_fullbright; // CMP index unchanged across the 64 light levels
uniform sampler2D palette_fullbright_mask;
uniform int has_palette_fullbright;
uniform int alpha_cutout; // explicit cutout MATs on solid adjoins
uniform float alpha_cutoff;
uniform int transparent_pass; // 0 G-buffer, 1 sorted forward alpha
uniform int alpha_mode; // 0 solid, 1 mixed, 2 mask, 3 blended, 4 dither
uniform float face_opacity; // 90/255 when rdFace.type includes 0x2
uniform int blend_mode; // normal=0, additive=1, multiply=2, fluid=3
uniform sampler2D scene_depth; // resolved opaque R16F depth, JK units 32*Y
uniform float fluid_thickness; // Beer-Lambert absorption coefficient
uniform int fluid_debug_mode; // GPU world Fluid diagnostics; 0 preserves ordinary rendering
uniform vec4 fog_params; // start/end/opacity, w=0 planar linear,1 radial linear,2 radial exp
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
#include "fluid_absorption.glsl"

vec3 world_jk_effect(vec3 raw,vec3 local_sector_tint)
{
    vec3 v=pow(jk_light_color_cap(raw*cached_sector_cmp_tint),
               vec3(1.0/1.8));
    if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
        v*=step(vec3(0.5),jk_color_filter.xyz);
    vec3 t=clamp(local_sector_tint+jk_color_tint.xyz,
                 vec3(-4.0),vec3(4.0));
    vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
    v=jk_light_color_cap(v*scale);
    v=jk_light_color_cap(v+jk_color_add_fade.xyz);
    v*=jk_color_add_fade.w;
    return pow(jk_light_color_cap(v),vec3(1.8));
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

#ifdef JKGM_MATCAP_LAYER
vec3 world_matcap_effect(vec2 uv)
{
    if(matcap_enabled==0)return vec3(0.0);
    vec2 mask=matcap_has_surface!=0
        ?texture(matcap_surface_map,uv).rg:vec2(1.0);
    if(mask.r<=0.0)return vec3(0.0);
    float roughness=clamp(matcap_params.w*mask.g,0.0,1.0);
    vec3 world_normal=world_mapped_normal(uv);
    // Use unprojected camera-space normals: MatCaps are view-oriented
    // artistic textures, not rays into the scene.
    vec3 n=normalize(vec3(
        dot(world_normal,camera_right_axis),
        dot(world_normal,camera_up_axis),
        dot(world_normal,camera_forward_axis)));
    if(n.z>0.0)n=-n;
    vec2 map_uv=clamp(
        vec2(0.5+0.5*n.x,0.5-0.5*n.y),vec2(0.001),vec2(0.999));
    ivec2 dims=textureSize(matcap_map,0);
    float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
    return textureLod(matcap_map,map_uv,lod).rgb *
           matcap_params.rgb*mask.r;
}
#endif

#ifdef JKGM_CLEARCOAT_LAYER
vec3 world_clearcoat_normal(vec2 uv)
{
    vec3 n=normalize(cached_world_normal);
    if(clearcoat_has_normal==0)return n;
    vec3 texn=texture(clearcoat_normal_map,uv).xyz*2.0-1.0;
    vec2 slope=texn.xy/max(texn.z,0.05);
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
vec3 world_clearcoat_values(vec2 uv)
{
    if(clearcoat_enabled==0)return vec3(0.0,0.1,1.0);
    vec3 pixel=clearcoat_has_surface!=0
        ?texture(clearcoat_surface_map,uv).rgb:vec3(1.0);
    return vec3(
        clamp(clearcoat_params.x*pixel.r,0.0,1.0),
        clamp(clearcoat_params.y*pixel.g,0.0,1.0),
        clamp(clearcoat_params.z*pixel.b,0.0,1.0));
}
float world_clearcoat_fresnel(vec3 N)
{
    float NV=clamp(abs(dot(normalize(N),
        normalize(camera_world_origin-cached_world_position))),0.0,1.0);
    return 0.04+0.96*pow(1.0-NV,5.0);
}
#endif

// A pixel can use this capture only if it lies on the selected world
// reflection plane. One captured MAT may be shared by non-coplanar faces.
// Pixel-space reflected camera projection is independent of texture UVs.
vec4 world_planar_reflection(vec3 mapped_normal,float roughness)
{
    if(planar_reflection_ready==0)return vec4(0.0);
    vec3 const_geom=normalize(cached_world_normal);
    if(abs(dot(const_geom,planar_receiver_plane.xyz))<0.999 ||
       abs(dot(cached_world_position,planar_receiver_plane.xyz)-
           planar_receiver_plane.w)>
               max(0.02,abs(planar_receiver_plane.w)*0.000002))
        return vec4(0.0);
    // Project a point along the normal-mapped reflected ray through the
    // mirrored camera. With the geometric normal this is exactly the
    // original planar projection; unlike a fixed UV offset, changes in
    // view angle, normal slope and distance now affect the distortion.
    vec3 incident=normalize(cached_world_position-camera_world_origin);
    vec3 ray=reflect(incident,normalize(mapped_normal));
    float distance_to_surface=length(cached_world_position-camera_world_origin);
    vec3 point_on_ray=cached_world_position+ray*distance_to_surface;
    vec3 d=point_on_ray-planar_camera_origin;
    float z=dot(d,planar_camera_forward);
    if(z<=0.001)return vec4(0.0);
    vec2 uv=0.5+0.5*vec2(
        dot(d,planar_camera_right),
        dot(d,planar_camera_up))/
        (z*max(planar_tan_half_fov,vec2(0.0001)));
    if(any(lessThan(uv,vec2(0.0))) ||
       any(greaterThan(uv,vec2(1.0))))return vec4(0.0);
    ivec2 dimensions=textureSize(planar_reflection_map,0);
    float lod=roughness*log2(
        max(float(max(dimensions.x,dimensions.y)),1.0));
    vec4 sample_color=textureLod(planar_reflection_map,uv,lod);
    // Captured geometry writes opaque RGB against a transparent black clear.
    // Bilinear filtering premultiplies edge colors; unpremultiply before
    // mixing coverage against the optional sky/environment fallback.
    if(sample_color.a>0.0001)
        sample_color.rgb/=sample_color.a;
    return sample_color;
}

// Reuse the existing strength/roughness/metalness/Fresnel law for captured
// geometry. Sky/uncaptured pixels retain optional equirectangular EnvMap.
vec3 world_environment_reflection(vec3 base_color,vec2 uv,out float metalness,
                                  bool coating,vec3 coat_normal,
                                  float coat_roughness,float coat_strength)
{
    metalness=0.0;
    int mode=int(environment_info.y+0.5);
    bool has_env=environment_info.x>0.5 || environment_info.z>0.5;
    bool has_planar=mode==4 && planar_reflection_ready!=0;
    bool has_screen=mode==1 && screen_reflection_ready!=0;
    bool has_shared=false;
#ifdef JKGM_REFLECTION_PROBE
    has_shared=mode==7 && shared_reflection_ready!=0;
#endif
    if((!has_env && !has_planar && !has_screen && !has_shared) ||
       (coating?coat_strength<=0.0:specular_params.x<=0.0))
        return vec3(0.0);
    vec3 surf=coating?vec3(1.0):
        (specular_params.w>0.5?texture(surface_map,uv).rgb:vec3(1.0));
    float strength=coating?coat_strength:max(specular_params.x*surf.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=coating?coat_roughness:
        clamp(specular_params.y*surf.g,0.0,1.0);
    vec3 nw=coating?coat_normal:world_mapped_normal(uv);
    vec3 vw=normalize(cached_world_position-camera_world_origin);
    vec3 n=normalize(vec3(dot(nw,camera_right_axis),
                          dot(nw,camera_up_axis),dot(nw,camera_forward_axis)));
    vec3 v=normalize(vec3(dot(vw,camera_right_axis),
                          dot(vw,camera_up_axis),dot(vw,camera_forward_axis)));
    if(dot(n,v)>0.0)n=-n;
    vec3 reflection=reflect(v,n);
    vec4 planar=has_planar?world_planar_reflection(nw,roughness):vec4(0.0);
    vec3 env=vec3(0.0);
    if(has_env && planar.a<0.999) {
        vec3 dir=reflection;
        if(mode==0 || mode==1 || mode>=4) {
            vec3 world=camera_right_axis*reflection.x+
                       camera_forward_axis*reflection.z+
                       camera_up_axis*reflection.y;
            dir=vec3(world.x,world.z,world.y);
        }
        vec2 env_uv=vec2(fract(atan(dir.x,dir.z)/6.28318530718+0.5),
                         0.5-asin(clamp(dir.y,-1.0,1.0))/3.14159265359);
        ivec2 dims=environment_info.x>0.5?
            textureSize(env_map,0):textureSize(env_map2,0);
        float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
        vec3 env_from=vec3(0.0),env_to=vec3(0.0);
        if(environment_info.x>0.5 && environment_info.w<1.0)
            env_from=textureLod(env_map,env_uv,lod).rgb;
        if(environment_info.z>0.5 && environment_info.w>0.0)
            env_to=textureLod(env_map2,env_uv,lod).rgb;
        env=mix(env_from,env_to,environment_info.w);
    }
#ifdef JKGM_REFLECTION_PROBE
    if(has_shared) {
        vec3 world_dir=camera_right_axis*reflection.x+
                       camera_forward_axis*reflection.z+
                       camera_up_axis*reflection.y;
        if(shared_reflection_ready==2) {
            vec3 d=normalize(world_dir);
            float up_weight=smoothstep(-0.087155743,0.087155743,d.z);
            ivec3 dims=textureSize(shared_reflection_map,0);
            vec2 inset=0.5/vec2(dims.xy);
            float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
            vec3 weighted_color=vec3(0.0);
            float weighted_coverage=0.0;
            for(int hemisphere=0;hemisphere<2;++hemisphere) {
                float weight=hemisphere==0?up_weight:1.0-up_weight;
                if(weight<=0.00001)continue;
                float axis_sign=hemisphere==0?1.0:-1.0;
                float denom=1.0+axis_sign*d.z;
                if(denom<=0.00001)continue;
                vec2 q=vec2(d.x,axis_sign*d.y)/denom;
                vec2 uv_hemi=q*(0.5/1.091308501)+vec2(0.5);
                if(any(lessThan(uv_hemi,vec2(0.0))) ||
                   any(greaterThan(uv_hemi,vec2(1.0))))continue;
                uv_hemi=clamp(uv_hemi,inset,vec2(1.0)-inset);
                float coverage=textureLod(shared_reflection_map,
                    vec3(uv_hemi,float(hemisphere)),0.0).a;
                if(coverage<=0.0001)continue;
                vec4 captured=textureLod(shared_reflection_map,
                    vec3(uv_hemi,float(hemisphere)),lod);
                if(captured.a<=0.0001)continue;
                float contribution=weight*coverage;
                weighted_color+=(captured.rgb/captured.a)*contribution;
                weighted_coverage+=contribution;
            }
            if(weighted_coverage>0.0001)
                env=mix(env,weighted_color/weighted_coverage,
                    clamp(weighted_coverage,0.0,1.0));
        } else {
            vec3 a=abs(world_dir);
            float major=max(a.x,max(a.y,a.z));
            if(major>0.000001) {
                int layer=0;
                vec3 right_axis=vec3(0.0),up_axis=vec3(0.0);
                if(a.x>=a.y && a.x>=a.z) {
                    layer=world_dir.x>=0.0?0:1;
                    right_axis=vec3(0.0,world_dir.x>=0.0?-1.0:1.0,0.0);
                    up_axis=vec3(0.0,0.0,1.0);
                } else if(a.y>=a.z) {
                    layer=world_dir.y>=0.0?2:3;
                    right_axis=vec3(world_dir.y>=0.0?1.0:-1.0,0.0,0.0);
                    up_axis=vec3(0.0,0.0,1.0);
                } else {
                    layer=world_dir.z>=0.0?4:5;
                    right_axis=vec3(0.0,world_dir.z>=0.0?1.0:-1.0,0.0);
                    up_axis=vec3(1.0,0.0,0.0);
                }
                ivec3 dims=textureSize(shared_reflection_map,0);
                vec2 inset=0.5/vec2(dims.xy);
                vec2 uv_cube=clamp(
                    0.5+0.5*vec2(dot(world_dir,right_axis),
                                 dot(world_dir,up_axis))/major,
                    inset,vec2(1.0)-inset);
                float coverage=textureLod(shared_reflection_map,
                    vec3(uv_cube,float(layer)),0.0).a;
                if(coverage>0.0001) {
                    float lod=roughness*log2(
                        max(float(max(dims.x,dims.y)),1.0));
                    vec4 captured=textureLod(shared_reflection_map,
                        vec3(uv_cube,float(layer)),lod);
                    if(captured.a>0.0001)
                        env=mix(env,captured.rgb/captured.a,
                                clamp(coverage,0.0,1.0));
                }
            }
        }
    }
#endif
    env=mix(env,planar.rgb,planar.a);
    // Receiver rays are evaluated on a separate low-resolution texture;
    // this material still applies its own strength, Fresnel and roughness.
    ivec2 screen_dimensions=has_screen
        ?textureSize(screen_reflection_map,0):ivec2(1);
    float screen_lod=roughness*log2(
        max(float(max(screen_dimensions.x,screen_dimensions.y)),1.0));
    vec4 screen=has_screen
        ?textureLod(screen_reflection_map,
            gl_FragCoord.xy/vec2(textureSize(scene_depth,0)),screen_lod)
        :vec4(0.0);
    if(screen.a>0.0001)screen.rgb/=screen.a;
    env=mix(env,screen.rgb,clamp(screen.a,0.0,1.0));
    float source_coverage=max(
        mix(environment_info.x,environment_info.z,environment_info.w),
        max(max(planar.a,screen.a),has_shared?1.0:0.0));
    metalness=coating?0.0:
        clamp(specular_params.z*surf.b,0.0,1.0)*source_coverage;
    vec3 f0=coating?vec3(0.04):
        mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float cos_theta=clamp(dot(n,-v),0.0,1.0);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-cos_theta,5.0);
    return env*fresnel*strength;
}
vec3 world_environment_reflection(vec3 base_color,vec2 uv,out float metalness)
{
    return world_environment_reflection(
        base_color,uv,metalness,false,vec3(0.0),0.0,0.0);
}

#include "world_cache_direct_specular.glsl"

// Follow the original JKGM Fluid opacity: use raw native depth differences
// (both depths are 32*camera-forward units), not normalized JK world units.
// This is the same shared law used for native triangles and GPU 3DOs.
float world_fluid_absorption(float fragment_depth)
{
    float opaque_depth=texelFetch(scene_depth,ivec2(gl_FragCoord.xy),0).r;
    if(opaque_depth>=65500.0)
        return jk_fluid_absorption(opaque_depth,fragment_depth,1.0,
                                  fluid_thickness);
    vec3 delta=cached_world_position-camera_world_origin;
    float ray_ratio=length(delta)/
                    max(dot(delta,camera_forward_axis),0.0001);
    return jk_fluid_absorption(opaque_depth,fragment_depth,ray_ratio,
                              fluid_thickness);
}

layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
layout(location=4) out vec4 out_pigment; // only attached for packed deferred lighting
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
    // Match native normal/Fluid emission: base alpha does not mask actual
    // emissive RGB. Sky remains nonemissive and is handled as before.
    vec3 emission=sky_mode==0
        ?gpu_material_emission(emissive_map,material_texcoord,
                               emissive_scroll,emissive_factor,has_emissive)
        :vec3(0.0);
    int preserve_emission=gpu_material_emission_coverage(
        emissive_only,blend_mode,emission);
    if(gpu_material_discard(coverage,alpha_mode,alpha_cutoff,
                            preserve_emission,transparent_pass!=0,false))discard;
    // Native D3D multiplies the vertex RGB by albedo_factor WITHOUT first
    // clamping the material pigment to 1.0. For textureless replacements
    // (albedo_source==2), a factor of 2 with 0.3 baked light must yield
    // 0.6, not 0.3. Clamping before lighting darkens brightened MATs and
    // makes GPU-cached world geometry disagree with the native 3DO path.
    // Downstream presentation still owns the final display-range clamp.
    vec3 albedo=solid_color!=0 ? material_tint.rgb :
        texel.rgb*material_tint.rgb;
    if(sky_mode!=0) {
        // Occupancy is normal.A, not geometric normal. A sky pixel
        // covers the HOM background but receives no AO, sun or spotlights.
        out_color=vec4(albedo,1.0);
        out_emissive=vec4(0.0,0.0,0.0,1.0);
        out_normal=gbuffer_pack_normal(vec3(0.0),1.0,1.0);
        out_depth=vec4(65504.0,albedo);
        out_pigment=vec4(albedo,1.0);
        return;
    }
    float fullbright=solid_fullbright!=0 ? 1.0 :
        gpu_material_palette_fullbright(
            palette_fullbright_mask,material_texcoord+uv_scroll,
            has_palette_fullbright);
    // Keep authored Gouraud/sector lighting, already interpolated in RGB.
    // The same GLSL helpers calculate pixel light for both world and 3DO.
    // Reuse the normal-map slope reconstruction already used by GGX, but
    // never use it for GeoMode 3 or CMP fullbright faces.
    vec3 diffuse_world_normal=normalize(cached_world_normal);
    // Water normal maps shape reflections/specular, not diffuse incidence:
    // bumpy diffuse headlights make thin liquid look opaque/thick.
    bool mapped_diffuse=normal_mapped_diffuse!=0 && solid_color==0 &&
        blend_mode!=3 &&
        (normal_specular_params.x>0.5 ||
         normal_specular_params.z>0.5) && fullbright<0.999;
    if(mapped_diffuse)
        diffuse_world_normal=world_mapped_normal(material_texcoord);
    vec3 lighting=cached_lit_rgb;
    if(world_pixel_dynamic_lighting!=0 && fullbright<0.999 &&
       cached_light_state.z>2.5 && num_lights>0) {
        vec3 added=vec3(0.0);
        vec3 normal=diffuse_world_normal;
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            added+=light_color[i]*jk_point_sample(
                delta,normal,light_position_radius[i].w,
                light_intensity[i],light_attenuation,false);
        }
        if(any(greaterThan(added,vec3(0.0)))) {
            vec3 light_color=max(
                world_jk_effect(vec3(1.0),cached_sector_tint)-
                world_jk_effect(vec3(0.0),cached_sector_tint),vec3(0.0));
            lighting=jk_point_compose(lighting,added,light_color);
        }
    }
    // Do not clip combined authored and dynamic HDR lighting.
    lighting=mix(max(lighting,vec3(0.0)),vec3(1.0),fullbright);
    float env_metalness=0.0;
    vec3 env_reflection=world_environment_reflection(
        albedo,material_texcoord,env_metalness);
    vec3 direct_specular=world_direct_specular(albedo,material_texcoord);
    vec3 coat_specular=vec3(0.0);
    float base_diffuse_trans=1.0,base_specular_trans=1.0;
#ifdef JKGM_CLEARCOAT_LAYER
    if(clearcoat_enabled!=0) {
        vec3 coat=world_clearcoat_values(material_texcoord);
        // Derivatives for the optional normal map must run uniformly
        // across the quad, even where the coverage mask is zero.
        vec3 N=world_clearcoat_normal(material_texcoord);
        if(coat.x>0.0) {
            float f=world_clearcoat_fresnel(N);
            base_specular_trans=clamp(1.0-coat.x*f,0.0,1.0);
            base_diffuse_trans=base_specular_trans*
                               mix(1.0,coat.z,coat.x);
            float unused_coat_metalness=0.0;
            coat_specular=world_environment_reflection(
                albedo,material_texcoord,unused_coat_metalness,
                true,N,coat.y,coat.x);
            coat_specular+=world_direct_specular(
                albedo,material_texcoord,true,N,coat.y,coat.x);
        }
    }
#endif
    vec3 matcap_lit=vec3(0.0),matcap_unlit=vec3(0.0);
#ifdef JKGM_MATCAP_LAYER
    vec3 matcap_effect=world_matcap_effect(material_texcoord);
    matcap_lit=matcap_effect*matcap_lighting_influence;
    matcap_unlit=matcap_effect*(1.0-matcap_lighting_influence);
#endif
    env_metalness*=1.0-fullbright;
    env_reflection*=1.0-fullbright;
    direct_specular*=1.0-fullbright;
    vec3 lit_diffuse=albedo*lighting*(1.0-env_metalness)*
                     base_diffuse_trans;
    // MatCap already contains its view-dependent pattern. Apply the
    // EXISTING illumination, then let the opaque composite handle AO,
    // sun visibility and shadowing without a second diffuse BRDF.
    lit_diffuse+=matcap_lit*lighting*base_diffuse_trans;
    // Coating attenuates the lower base lobe, not its own specular term.
    vec3 layered_specular=(env_reflection+direct_specular)*
                          base_specular_trans+coat_specular+
                          matcap_unlit*base_diffuse_trans;
    if(transparent_pass!=0) {
        // Only one color attachment exists in the forward alpha target.
        // The framebuffer uses premultiplied (ONE, ONE_MINUS_SRC_ALPHA).
        float depth=32.0/max(gl_FragCoord.w,0.00000001);
        // Radial fog affects ONLY forward shading: don't change scene depth
        // or the camera-ray Fluid absorption against opaque depth.
        float fog_depth=fog_params.w>0.5
            ?length(cached_world_position-camera_world_origin)*32.0:depth;
        float fog_fraction=clamp((fog_depth-fog_params.x)/
                            max(fog_params.y-fog_params.x,0.0001),0.0,1.0);
        if(fog_params.w>1.5) {
            float reach=max(fog_params.y+0.5*max(fog_params.x,0.0),1.0);
            fog_fraction=1.0-exp(-2.302585093*max(fog_depth,0.0)/reach);
        }
        float fog=fog_fraction*fog_params.z;
        vec3 lit=mix(lit_diffuse,fog_color,fog);
        // MatCap adds authored color but no dynamically evaluated
        // specular highlight; forward fog attenuates the artistic layer.
        vec3 specular=layered_specular*(1.0-fog);
        if(blend_mode==3) {
            // Match game_trns_pass.frag's *native* Fluid composition.
            // Native material/texture alpha changes destination blending,
            // but does not multiply the already-lit liquid RGB a second
            // time. Only the original D3D vertex alpha was premultiplied
            // before the gamma-1.8 conversion. Reusing lit*opacity here
            // darkens alpha<1 fluids, including tint-only replacements.
            float absorption=world_fluid_absorption(depth);
            float opacity=coverage*absorption;
            float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);
            vec3 diffuse=(lit_diffuse*(1.0-fog)*vertex_rgb_alpha+
                          fog_color*(fog*coverage))*absorption;
            // Opt-in GPU Fluid debugging. Full-alpha outputs expose the
            // source calculation before the transparency blend can obscure it.
            // Normal mode (0) does not alter absorption, pigment, or reflections.
            if(fluid_debug_mode==1) {
                out_color=vec4(vec3(absorption),1.0);
                return;
            }
            if(fluid_debug_mode==2) {
                out_color=vec4(diffuse,1.0);
                return;
            }
            if(fluid_debug_mode==3) {
                bool invalid=any(isnan(env_reflection)) ||
                             any(isinf(env_reflection)) ||
                             any(isnan(direct_specular)) ||
                             any(isinf(direct_specular)) ||
                             any(isnan(specular)) || any(isinf(specular));
                out_color=invalid ? vec4(1.0,0.0,1.0,1.0)
                                  : vec4(0.0,1.0,0.0,1.0);
                return;
            }
            // Inspect the inputs to the Fluid-only diffuse term separately.
            // These outputs deliberately bypass regular alpha blending.
            if(fluid_debug_mode==4) {
                out_color=vec4(albedo,1.0);
                return;
            }
            if(fluid_debug_mode==5) {
                out_color=vec4(lighting,1.0);
                return;
            }
            if(fluid_debug_mode==6) {
                out_color=vec4(vec3(env_metalness),1.0);
                return;
            }
            if(fluid_debug_mode==7) {
                out_color=vec4(lit_diffuse,1.0);
                return;
            }
            if(fluid_debug_mode==8) {
                out_color=vec4(vec3((1.0-fog)*vertex_rgb_alpha*absorption),1.0);
                return;
            }
            if(fluid_debug_mode==9) {
                // Is the live per-vertex light already black before the
                // sector CMP/JK color effect stage in the vertex shader?
                out_color=vec4(cached_raw_light_rgb,1.0);
                return;
            }
            if(fluid_debug_mode==10) {
                out_color=vec4(cached_sector_cmp_tint,1.0);
                return;
            }
            if(fluid_debug_mode==11) {
                out_color=vec4(vec3(jk_color_add_fade.w),1.0);
                return;
            }
            if(fluid_debug_mode==12) {
                // A/B against mode 2: preserve Fluid fog/coverage/depth
                // math but replace ONLY the cached sector-light factor.
                // No specular, no emission, no persistent render change.
                vec3 force_lit=albedo*(1.0-env_metalness);
                vec3 forced_diffuse=(
                    force_lit*(1.0-fog)*vertex_rgb_alpha+
                    fog_color*(fog*coverage))*absorption;
                out_color=vec4(forced_diffuse,1.0);
                return;
            }
            // Just as in native Fluid, emission and reflection are
            // independent of pigment absorption and alpha.
            out_color=vec4(diffuse+emission+specular,opacity);
        } else if(blend_mode==2) {
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
                albedo*lighting*base_diffuse_trans*(1.0-fog)*
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
    out_emissive=vec4(emission+layered_specular,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    // Fullbright palette entries do not receive AO, sun or projected light.
    // G-buffer normal is consumed by both opaque projected spotlights
    // and sunlight. Retain JK's projected geometry normal without a map;
    // match the vertex shader's FOV-scaled normal encoding when mapped.
    vec3 diffuse_view_normal=normalize(cached_view_normal);
    if(mapped_diffuse) {
        vec3 view_n=vec3(
            dot(diffuse_world_normal,camera_right_axis),
            dot(diffuse_world_normal,camera_up_axis),
            dot(diffuse_world_normal,camera_forward_axis));
        float sh=max(projection.x,0.0001);
        float sv=sh/max(projection.y,0.0001);
        diffuse_view_normal=normalize(vec3(
            view_n.x*sh,view_n.y*sv,view_n.z));
    }
    out_normal=gbuffer_pack_normal(
        fullbright>=0.5 ? vec3(0.0) : diffuse_view_normal,1.0,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    // Deferred sun and spotlights need the lighting-influenced MatCap
    // in unlit pigment, but must not illuminate the artistic unlit part.
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),
                   albedo+matcap_lit*base_diffuse_trans);
    out_pigment=vec4(albedo+matcap_lit*base_diffuse_trans,1.0);
}
