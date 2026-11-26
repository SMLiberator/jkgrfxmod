#version 330 core
#ifdef JKGM_SIMPLE_OPAQUE
// Draw eligibility excludes every mapped material and non-opaque pass.
// Retain the shared G-buffer, alpha coverage, direct light and tint math.
#undef JKGM_MATCAP_LAYER
#undef JKGM_CLEARCOAT_LAYER
#undef JKGM_REFLECTION_PROBE
#endif
#include "gbuffer_normal_packed.glsl"
// Static GPU geometry with per-fragment opaque/translucent separation.
// MIXED surfaces draw fully opaque pixels into the G-buffer and translucent
// pixels in the sorted alpha pass, without double shading any pixel.
uniform sampler2D albedo_map;
uniform sampler2D albedo_map2;
uniform vec4 albedo_transition; // has second albedo, progress, enabled, unused
uniform sampler2D emissive_map;
uniform vec2 uv_scale;
uniform vec2 albedo_scroll;
uniform vec2 emissive_scroll;
uniform vec4 albedo_factor;
uniform vec3 emissive_factor;
uniform vec4 fog_params;   // start, end, opacity, 0 planar linear / 1 radial linear / 2 radial exp
uniform vec3 fog_color;
uniform sampler2D scene_depth; // completed opaque depth, native units 32*Y
#ifdef JKGM_SIMPLE_OPAQUE
const int smoke_half_resolution=0;
#else
uniform int smoke_half_resolution; // 1 during half-size fluid SPR rendering
#endif
uniform vec2 advanced_material_params; // depth_clip, fluid thickness
uniform sampler2D displacement_map;
#ifdef JKGM_SIMPLE_OPAQUE
const float displacement_factor=0.0;
#else
uniform float displacement_factor;
#endif
uniform float alpha_cutoff;
uniform float native_face_opacity;
uniform float thing_opacity; // 3DO or SPR per-instance emissive fade multiplier
uniform int has_texture; // 0=unresolved diagnostic, 1=albedo map, 2=intentionally no albedo
uniform int solid_fullbright; // exact 8-bit CMP light-table invariant for GeoMode 3
uniform sampler2D palette_fullbright_mask;
uniform int has_palette_fullbright;
uniform int has_emissive;
#ifdef JKGM_SIMPLE_OPAQUE
const int transparent_pass=0;
#else
uniform int transparent_pass;
#endif
#ifdef JKGM_SIMPLE_OPAQUE
const int post_exempt_pass=0;
#else
uniform int post_exempt_pass; // Camera Thing pass after post-processing
#endif
#ifdef JKGM_SIMPLE_OPAQUE
const int camera_hud_thing=0;
#else
uniform int camera_hud_thing; // 0=world, 1=overlay HUD, 2=world-depth HUD
#endif
// Flag 0x20 on a GPU-cached Thing suppresses added sunlight/specular and
// projected spotlights; a zero G-buffer normal also omits SSAO on this Thing.
uniform int world_lighting_exempt;
uniform int sprite_receiver; // 1=visible SPR; distinguish billboard receivers in G-buffer
#ifdef JKGM_SIMPLE_OPAQUE
const float sprite_depth_bias=0.0;
#else
uniform float sprite_depth_bias; // SPR render-only Y, in JK units
#endif
#ifdef JKGM_SIMPLE_OPAQUE
const vec4 sprite_flare_center=vec4(0.0);
#else
uniform vec4 sprite_flare_center; // pixel X/Y, native depth, W=flare enabled
#endif
// Projected decal only: opaque G-buffer emissive.A stores diffuse JK light,
// leaving emission RGB and all existing render targets unchanged.
#ifdef JKGM_SIMPLE_OPAQUE
const int decal_receiver_lighting=0;
#else
uniform int decal_receiver_lighting;
#endif
uniform sampler2D decal_light_texture;
uniform sampler2D decal_occlusion;
#ifdef JKGM_SIMPLE_OPAQUE
const int blend_mode=0;
#else
uniform int blend_mode; // 0 normal, 1 additive, 2 multiply, 3 fluid
#endif
uniform int emissive_only; // zero-alpha base, independently blended emission
uniform int alpha_mode; // 0 solid, 1 mixed, 2 mask, 3 blend only, 4 dithered
in vec2 pixel_uv;
in vec3 view_normal;
in vec3 jk_lit_rgb;
in vec4 decal_coverage;
in vec3 pixel_light_face_anchor;
in vec3 pixel_light_face_normal;
in vec3 pixel_light_vertex_rgb;
in vec3 pixel_light_state; // original static light, face extra light, mode // projector U,V,depth,normal-based opacity

uniform sampler2D surface_map; // linear RGB: reflectivity, roughness, metalness
uniform sampler2D normal_map;  // tangent-space RGB
uniform sampler2D normal_map2; // second slope layer
uniform sampler2D env_map;     // equirectangular fallback
uniform sampler2D env_map2;
#ifdef JKGM_MATCAP_LAYER
uniform sampler2D matcap_map,matcap_surface_map;
uniform vec4 matcap_params; // artistic RGB factor and roughness, no BRDF
uniform float matcap_lighting_influence;
uniform int matcap_enabled,matcap_has_surface;
#endif
#ifdef JKGM_CLEARCOAT_LAYER
uniform sampler2D clearcoat_surface_map,clearcoat_normal_map;
uniform vec4 clearcoat_params; // strength, roughness, transmission
uniform int clearcoat_enabled,clearcoat_has_surface,clearcoat_has_normal;
#endif
// Hemi: layers 0/1 are fixed up/down paraboloid disks with horizon overlap.
// Cube: layers 0..5 are fixed-axis 90-degree perspective views.
// Every layer has independent mipmaps; coverage alpha is sampled at LOD 0.
#ifdef JKGM_REFLECTION_PROBE
uniform sampler2DArray hemi_reflection_map;
#endif
uniform vec3 hemi_right_axis,hemi_forward_axis,hemi_up_axis;
uniform vec4 hemi_capture_info; // legacy x/y, ready, reflection mode
// Do not reconstruct fragment position or normals from draw-wide view_*
// uniforms: those belong to only the first member of an instanced draw.
flat in vec3 fragment_basis_right;
flat in vec3 fragment_basis_forward;
flat in vec3 fragment_basis_up;
in vec3 fragment_native_view_position;
uniform vec4 projection; // FOV scale for the deferred spotlight/sun normal
uniform vec3 camera_right_axis,camera_forward_axis,camera_up_axis;
uniform vec4 environment_info; // from enabled, reflection_mode, to enabled, progress
uniform vec4 sun_direction; // view-space direction TOWARD sun
uniform vec4 sun_color_intensity; // rgb, intensity
uniform vec4 specular_params; // strength, roughness, metalness, has surface map
#ifdef JKGM_SIMPLE_OPAQUE
const vec4 normal_specular_params=vec4(0.0,1.0,0.0,0.0);
#else
uniform vec4 normal_specular_params; // has normal map, strength, unused, unused
#endif
uniform vec2 normal_specular_scroll;
uniform vec2 normal_specular_scroll2;
uniform vec3 local_eye;
uniform int num_lights;
uniform int pixel_dynamic_lighting;
#ifdef JKGM_SIMPLE_OPAQUE
const int normal_mapped_diffuse=0;
#else
uniform int normal_mapped_diffuse; // opt-in: added real-time diffuse only
#endif
uniform float ambient_light;
uniform float effective_light_mode;
uniform vec3 colormap_tint;
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;
uniform vec3 thing_sector_tint_delta; // own SithSector - camera SithSector
uniform int nonrigid_light_transform;
uniform int dynamic_light_specular_enabled;
uniform vec4 light_position_radius[16];
uniform float light_intensity[16];
uniform vec3 light_color[16]; // white for all non-saber native rdLights
uniform float attenuation_min;
in vec3 local_fragment_position;
in vec3 local_fragment_normal;
// Available for cached 3DOs only. Sector plane tests use a GL 3.3
// RGBA32F texture buffer rather than consuming huge GLSL uniform arrays.
uniform int sector_crossing_count;
uniform int sector_crossing_per_vertex; // selects and blends color in vertex stage
uniform samplerBuffer sector_crossing_planes;
uniform vec3 sector_crossing_camera_origin;
uniform vec4 sector_crossing_info[4]; // ambient, plane offset, plane count, interior flag
uniform vec4 sector_crossing_bounds_min[4];
uniform vec4 sector_crossing_bounds_max[4];
uniform vec4 sector_crossing_inner_min[4];
uniform vec4 sector_crossing_inner_max[4];
uniform vec4 sector_crossing_cmp[4];
uniform vec4 sector_crossing_tint[4];
in vec3 sector_crossing_lit[4];
in vec3 sector_vertex_cmp;
in vec3 sector_vertex_tint;
int sector_crossing_slot=0;

int select_sector_crossing_slot()
{
    if(sector_crossing_count<=1)return 0;
    // Local vertex coordinates -> native view -> world. The existing
    // camera axes already contain the inverse of JK's rigid view basis.
    // Only models actually crossing sectors run this per-fragment work.
    vec3 vp=fragment_native_view_position;
    vec3 world=sector_crossing_camera_origin+
        camera_right_axis*vp.x+camera_forward_axis*vp.y+
        camera_up_axis*vp.z;
    for(int i=0;i<4;i++) {
        if(i>=sector_crossing_count)break;
        vec3 lo=sector_crossing_bounds_min[i].xyz;
        vec3 hi=sector_crossing_bounds_max[i].xyz;
        if(any(lessThan(world,lo-vec3(0.0001))) ||
           any(greaterThan(world,hi+vec3(0.0001))))continue;
        if(sector_crossing_info[i].w>0.5 &&
           all(greaterThan(world,sector_crossing_inner_min[i].xyz)) &&
           all(lessThan(world,sector_crossing_inner_max[i].xyz)))
            return i;
        int begin_plane=int(sector_crossing_info[i].y+0.5);
        int count=int(sector_crossing_info[i].z+0.5);
        if(count<1 || count>96)continue;
        bool inside=true;
        // Shader bound matches CPU's validated per-sector plane limit.
        for(int j=0;j<96;j++) {
            if(j>=count)break;
            vec4 plane=texelFetch(sector_crossing_planes,begin_plane+j);
            if(dot(plane.xyz,world)+plane.w < -0.0001) {
                inside=false;break;
            }
        }
        if(inside)return i; // owner is slot 0: stable boundary tie
    }
    return 0; // invalid/hole: preserve native owning Thing sector
}


#include "jk_point_lighting.glsl"
#include "gpu_material_common.glsl"
#include "fluid_absorption.glsl"

// Evaluate a receiver's untinted-to-tinted response at a given brightness.
// Called at white and black to obtain a light color independently of baked
// brightness. Gouraud/static shading itself stays in the vertex shader.
vec3 pixel_jk_color_at_brightness(float bright)
{
    vec3 effective_cmp=sector_crossing_count>1
        ? (sector_crossing_per_vertex!=0
            ? sector_vertex_cmp
            : sector_crossing_cmp[sector_crossing_slot].xyz)
        : colormap_tint;
    vec3 result=jk_light_color_cap(vec3(pow(max(bright,0.0),1.8)) *
                pixel_light_vertex_rgb * effective_cmp);
    vec3 sector_tint=sector_crossing_count>1
        ? (sector_crossing_per_vertex!=0
            ? sector_vertex_tint
            : sector_crossing_tint[sector_crossing_slot].xyz)
        : thing_sector_tint_delta;
    if(jk_color_filter.w>0.5 ||
       any(greaterThan(abs(sector_tint),vec3(0.00001)))) {
        vec3 v=pow(jk_light_color_cap(result),vec3(1.0/1.8));
        if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
            v*=step(vec3(0.5),jk_color_filter.xyz);
        vec3 t=clamp(jk_color_tint.xyz+sector_tint,
                     vec3(-4.0),vec3(4.0));
        vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
        v=jk_light_color_cap(v*scale);
        v=jk_light_color_cap(v+jk_color_add_fade.xyz);
        v*=jk_color_add_fade.w;
        result=pow(jk_light_color_cap(v),vec3(1.8));
    }
    return result;
}

// The baked vertex color stays authoritative. Dynamic lighting is evaluated
// with precisely the same sample and composition formulas as world geometry.
// 3DO-specific logic is limited to local-space transformations and light mode.
vec3 pixel_jk_diffuse_lighting(vec3 mapped_diffuse_normal)
{
    float mode=pixel_light_state.z>3.5 ? 1.0 :
               min(pixel_light_state.z,effective_light_mode);
    vec3 baked=sector_crossing_count>1 && sector_crossing_per_vertex==0
        ? sector_crossing_lit[sector_crossing_slot] : jk_lit_rgb;
    if(mode<1.5 || num_lights==0) return baked;

    vec3 anchor=mode>=2.5 ? local_fragment_position
                          : pixel_light_face_anchor;
    // Flat LightMode 2 still uses its authored face normal/attenuation.
    // Only Gouraud/LightMode 3 point diffuse gains mapped detail.
    vec3 normal=normalize(mode>=2.5 ? mapped_diffuse_normal
                                    : pixel_light_face_normal);
    if(nonrigid_light_transform!=0) {
        vec3 r=fragment_basis_right,f=fragment_basis_forward,u=fragment_basis_up;
        vec3 cr=cross(f,u),cf=cross(u,r),cu=cross(r,f);
        float determinant=dot(r,cr);
        normal=normalize(sign(determinant)*
            (cr*normal.x+cf*normal.y+cu*normal.z));
    }
    vec3 added=vec3(0.0);
    for(int i=0;i<num_lights;i++) {
        vec3 delta=light_position_radius[i].xyz-anchor;
        if(nonrigid_light_transform!=0)
            delta=fragment_basis_right*delta.x+
                  fragment_basis_forward*delta.y+fragment_basis_up*delta.z;
        added+=light_color[i]*jk_point_sample(
            delta,normal,light_position_radius[i].w,
            light_intensity[i],attenuation_min,mode<2.5);
    }
    if(all(lessThanEqual(added,vec3(0.0)))) return baked;
    vec3 light_color=max(
        pixel_jk_color_at_brightness(1.0)-
        pixel_jk_color_at_brightness(0.0),vec3(0.0));
    return jk_point_compose(baked,added,light_color);
}

// Transform shaded normals with the node's inverse transpose. Raw
// R/L/U columns are valid for vertex positions but not for normals on a
// nonuniformly scaled 3DO node. Mirrored nodes retain their signed parity.
// Per-fragment surface state prepared once after alpha/HUD rejection.
mat3 surface_normal_transform;
mat3 surface_tangent_frame;
bool surface_tangent_valid;
vec3 surface_geometric_normal;
vec3 surface_base_local, surface_base_view, surface_coat_view;
vec3 surface_to_eye, surface_map_value;

vec3 gpu_view_normal_from_local(vec3 local_n)
{
    vec3 n=surface_normal_transform[0]*local_n.x+
           surface_normal_transform[1]*local_n.y+
           surface_normal_transform[2]*local_n.z;
    vec3 projected=vec3(n.x,n.z,n.y);
    return dot(projected,projected)>1e-16
        ? normalize(projected) : vec3(0.0,0.0,1.0);
}

vec3 mapped_local_normal(vec2 uv)
{
    vec3 n=surface_geometric_normal;
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
    if(!surface_tangent_valid)return n;
    return normalize(surface_tangent_frame*normalize(vec3(slope,1.0)));
}

#ifdef JKGM_MATCAP_LAYER
vec3 gpu_matcap_effect(vec2 uv)
{
    if(matcap_enabled==0)return vec3(0.0);
    vec2 mask=matcap_has_surface!=0
        ?texture(matcap_surface_map,uv).rg:vec2(1.0);
    if(mask.r<=0.0)return vec3(0.0);
    float roughness=clamp(matcap_params.w*mask.g,0.0,1.0);
    // Surface normals include the existing tangent-space normal layers,
    // but bypass reflection-direction, Fresnel and metalness calculations.
    vec3 n=surface_base_view;
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
vec3 gpu_clearcoat_local_normal(vec2 uv)
{
    vec3 n=surface_geometric_normal;
    if(clearcoat_has_normal==0)return n;
    vec3 texn=texture(clearcoat_normal_map,uv).xyz*2.0-1.0;
    vec2 slope=texn.xy/max(texn.z,0.05);
    if(!surface_tangent_valid)return n;
    return normalize(surface_tangent_frame*normalize(vec3(slope,1.0)));
}
vec3 gpu_clearcoat_values(vec2 uv)
{
    if(clearcoat_enabled==0)return vec3(0.0,0.1,1.0);
    vec3 pixel=clearcoat_has_surface!=0
        ?texture(clearcoat_surface_map,uv).rgb:vec3(1.0);
    return vec3(
        clamp(clearcoat_params.x*pixel.r,0.0,1.0),
        clamp(clearcoat_params.y*pixel.g,0.0,1.0),
        clamp(clearcoat_params.z*pixel.b,0.0,1.0));
}
float gpu_clearcoat_fresnel(vec3 N)
{
    float nv=clamp(abs(dot(normalize(N),surface_to_eye)),0.0,1.0);
    return 0.04+0.96*pow(1.0-nv,5.0);
}
#endif

// Local-space analytic point-light specular, with optional normal/surface
// maps. Uses the same GGX microfacet form as the native sector sun shader.
// Does not modify JK's diffuse Gouraud lighting.
vec3 point_light_specular(vec3 base_color,vec2 uv,
                          bool coating,vec3 coat_normal,
                          float coat_roughness,float coat_strength)
{
    if((coating?coat_strength<=0.0:specular_params.x<=0.0) ||
       num_lights==0)return vec3(0.0);
    vec3 N=coating?coat_normal:surface_base_local;
    vec3 V=local_eye-local_fragment_position;
    if(nonrigid_light_transform!=0) {
        // Match scaled-mesh diffuse light attenuation: an affine local
        // distance is not a world distance. Normal and directions must
        // share a rigid camera-space basis for correct specular response.
        N=coating?surface_coat_view:surface_base_view;
        vec3 v=fragment_basis_right*V.x+
               fragment_basis_forward*V.y+fragment_basis_up*V.z;
        V=vec3(v.x,v.z,v.y);
    }
    V=normalize(V);
    if(dot(N,V)<0.0) N=-N;
    float NdotV=max(dot(N,V),0.0001);
    vec3 map_value=coating?vec3(1.0):
        surface_map_value;
    float strength=coating?coat_strength:
        max(specular_params.x*map_value.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=coating?clamp(coat_roughness,0.08,1.0):
        clamp(specular_params.y*map_value.g,0.08,1.0);
    float metalness=coating?0.0:clamp(specular_params.z*map_value.b,0.0,1.0);
    vec3 F0=coating?vec3(0.04):
        mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float a=roughness*roughness;
    float a2=a*a;
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float Gv=NdotV/(NdotV*(1.0-k)+k);
    vec3 sum=vec3(0.0);
    for(int i=0;i<num_lights;i++) {
        vec3 delta=light_position_radius[i].xyz-local_fragment_position;
        if(nonrigid_light_transform!=0) {
            vec3 d=fragment_basis_right*delta.x+
                   fragment_basis_forward*delta.y+fragment_basis_up*delta.z;
            delta=vec3(d.x,d.z,d.y);
        }
        float dist=length(delta);
        if(dist>=light_position_radius[i].w || dist<0.00001) continue;
        vec3 L=delta/dist;
        float NdotL=max(dot(N,L),0.0);
        if(NdotL<=0.0) continue;
        float radiance=max(light_intensity[i]-dist*attenuation_min,0.0);
        if(radiance<=0.0) continue;
        vec3 H=normalize(L+V);
        float Nh=max(dot(N,H),0.0);
        float Vh=max(dot(V,H),0.0);
        float d=Nh*Nh*(a2-1.0)+1.0;
        float D=a2/(3.14159265*d*d);
        float Gl=NdotL/(NdotL*(1.0-k)+k);
        vec3 F=F0+(vec3(1.0)-F0)*pow(1.0-Vh,5.0);
        sum+=F*(D*Gv*Gl/max(4.0*NdotV,0.0001))*
             radiance*light_color[i];
    }
    return min(sum*strength,vec3(32.0));
}
vec3 point_light_specular(vec3 base_color,vec2 uv)
{
    return point_light_specular(
        base_color,uv,false,vec3(0.0),0.0,0.0);
}

// PS2-era style environment mapping, sharing the native material's
// reflectivity/roughness/metalness and optional dual-layer mapped normal.
// The sun/dynamic-COG EnvMap crossfade is a separate parity feature.
vec3 gpu_environment_reflection(vec3 base_color,vec2 uv,out float metalness,
                                bool coating,vec3 coat_normal,
                                float coat_roughness,float coat_strength)
{
    metalness=0.0;
    int mode=int(environment_info.y+0.5);
    bool has_hemi=false;
#ifdef JKGM_REFLECTION_PROBE
    has_hemi=(mode==5 || mode==6 || mode==7) && hemi_capture_info.z>0.5 &&
             abs(hemi_capture_info.w-float(mode))<0.5;
#endif
    if((environment_info.x<0.5 && environment_info.z<0.5 && !has_hemi) ||
       (coating?coat_strength<=0.0:specular_params.x<=0.0))
        return vec3(0.0);
    vec3 surf=coating?vec3(1.0):
        surface_map_value;
    float strength=coating?coat_strength:max(specular_params.x*surf.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=coating?coat_roughness:
        clamp(specular_params.y*surf.g,0.0,1.0);
    metalness=coating?0.0:
        clamp(specular_params.z*surf.b,0.0,1.0)*
        max(mix(environment_info.x,environment_info.z,
                environment_info.w),has_hemi?1.0:0.0);
    vec3 n=coating?surface_coat_view:surface_base_view;
    vec3 v=-surface_to_eye; // camera -> surface
    if(dot(n,v)>0.0) n=-n;
    vec3 reflection=reflect(v,n);
    vec3 dir=reflection;
    if(mode==0 || mode==1 || mode>=4) {
        vec3 world=camera_right_axis*reflection.x+
                   camera_forward_axis*reflection.z+
                   camera_up_axis*reflection.y;
        dir=vec3(world.x,world.z,world.y);
    }
    vec2 env_uv=vec2(fract(atan(dir.x,dir.z)/6.28318530718+0.5),
                     0.5-asin(clamp(dir.y,-1.0,1.0))/3.14159265359);
    ivec2 dims=environment_info.x>0.5 ?
        textureSize(env_map,0) : textureSize(env_map2,0);
    float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
    vec3 env_from=vec3(0.0),env_to=vec3(0.0);
    if(environment_info.x>0.5 && environment_info.w<1.0)
        env_from=textureLod(env_map,env_uv,lod).rgb;
    if(environment_info.z>0.5 && environment_info.w>0.0)
        env_to=textureLod(env_map2,env_uv,lod).rgb;
    vec3 env=mix(env_from,env_to,environment_info.w);
#ifdef JKGM_REFLECTION_PROBE
    if(has_hemi) {
        // The existing world reflection direction, including mapped normals,
        // drives both probes. Face order: +X, -X, +Y, -Y, +Z, -Z.
        vec3 world_dir=camera_right_axis*reflection.x+
                       camera_forward_axis*reflection.z+
                       camera_up_axis*reflection.y;
        vec2 capture_uv=vec2(0.5);
        float softness=1.0;
        int layer=0;
        bool covered=false;
        if(mode==5) {
            // Fixed +Z/-Z dual paraboloid. Each disk covers 95 degrees from
            // its pole; smoothly blend in the +/-5 degree horizon overlap.
            vec3 d=normalize(world_dir);
            float up_weight=smoothstep(
                -0.087155743,0.087155743,d.z);
            ivec3 dims=textureSize(hemi_reflection_map,0);
            vec2 inset=0.5/vec2(dims.xy);
            float capture_lod=roughness*log2(
                max(float(max(dims.x,dims.y)),1.0));
            vec3 weighted_color=vec3(0.0);
            float weighted_coverage=0.0;
            for(int hemisphere=0;hemisphere<2;++hemisphere) {
                float weight=hemisphere==0?up_weight:1.0-up_weight;
                if(weight<=0.00001)continue;
                float axis_sign=hemisphere==0?1.0:-1.0;
                float denom=1.0+axis_sign*d.z;
                if(denom<=0.00001)continue;
                // Keep lower hemisphere texel orientation consistent with
                // the converter: local +Y is reversed on the bottom disk.
                vec2 q=vec2(d.x,axis_sign*d.y)/denom;
                vec2 uv_hemi=q*(0.5/1.091308501)+vec2(0.5);
                if(any(lessThan(uv_hemi,vec2(0.0))) ||
                   any(greaterThan(uv_hemi,vec2(1.0))))continue;
                uv_hemi=clamp(uv_hemi,inset,vec2(1.0)-inset);
                float alpha=textureLod(hemi_reflection_map,
                    vec3(uv_hemi,float(hemisphere)),0.0).a;
                if(alpha<=0.0001)continue;
                vec4 color=textureLod(hemi_reflection_map,
                    vec3(uv_hemi,float(hemisphere)),capture_lod);
                if(color.a<=0.0001)continue;
                float contribution=weight*alpha;
                weighted_color+=(color.rgb/color.a)*contribution;
                weighted_coverage+=contribution;
            }
            if(weighted_coverage>0.0001)
                env=mix(env,weighted_color/weighted_coverage,
                    clamp(weighted_coverage,0.0,1.0));
        } else if(mode==6 || mode==7) {
            vec3 a=abs(world_dir);
            float major=0.0;
            vec3 face_right=vec3(0.0),face_up=vec3(0.0);
            if(a.x>=a.y && a.x>=a.z) {
                major=a.x;
                if(world_dir.x>=0.0) {
                    layer=0;face_right=vec3(0.0,-1.0,0.0);
                } else {
                    layer=1;face_right=vec3(0.0,1.0,0.0);
                }
                face_up=vec3(0.0,0.0,1.0);
            } else if(a.y>=a.z) {
                major=a.y;
                if(world_dir.y>=0.0) {
                    layer=2;face_right=vec3(1.0,0.0,0.0);
                } else {
                    layer=3;face_right=vec3(-1.0,0.0,0.0);
                }
                face_up=vec3(0.0,0.0,1.0);
            } else {
                major=a.z;
                if(world_dir.z>=0.0) {
                    layer=4;face_right=vec3(0.0,1.0,0.0);
                } else {
                    layer=5;face_right=vec3(0.0,-1.0,0.0);
                }
                face_up=vec3(1.0,0.0,0.0);
            }
            if(major>0.000001) {
                vec2 ndc=vec2(dot(world_dir,face_right),
                              dot(world_dir,face_up))/major;
                capture_uv=ndc*0.5+0.5;
                covered=true; // every nonzero reflection ray has a cube face
            }
        }
        if(covered) {
            ivec3 size=textureSize(hemi_reflection_map,0);
            // Avoid bilinear fetches past a face boundary at LOD 0.
            vec2 inset=0.5/vec2(size.xy);
            capture_uv=clamp(capture_uv,inset,vec2(1.0)-inset);
            float capture_lod=roughness*log2(
                max(float(max(size.x,size.y)),1.0));
            // Coverage is a geometric visibility decision. Never use a
            // roughness-filtered mip alpha to decide whether this direction
            // was captured: lower mip levels average sky/holes into the
            // result and made valid reflected geometry fade to EnvMap.
            float coverage=textureLod(hemi_reflection_map,
                vec3(capture_uv,float(layer)),0.0).a;
            if(coverage>0.0001) {
                vec4 capture=textureLod(hemi_reflection_map,
                    vec3(capture_uv,float(layer)),capture_lod);
                // Captured RGB is premultiplied by coverage. Unpremultiply
                // only when its filtered mip actually contains scene color.
                // Base-level alpha remains authoritative for the fallback.
                if(capture.a>0.0001) {
                    capture.rgb/=capture.a;
                    env=mix(env,capture.rgb,
                        clamp(coverage*softness,0.0,1.0));
                }
            }
        }
    }
#endif
    vec3 f0=coating?vec3(0.04):
        mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float cos_theta=clamp(dot(n,-v),0.0,1.0);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-cos_theta,5.0);
    return env*fresnel*strength;
}
vec3 gpu_environment_reflection(vec3 base_color,vec2 uv,out float metalness)
{
    return gpu_environment_reflection(
        base_color,uv,metalness,false,vec3(0.0),0.0,0.0);
}

// The same view-space GGX/Schlick/Smith sun term used by the native
// opaque material shader; independent of the existing Gouraud diffuse.
float sun_specular_shadow_factor(float depth,float incidence);

vec3 gpu_sun_specular(vec3 base_color,vec2 uv,
                      bool coating,vec3 coat_normal,
                      float coat_roughness,float coat_strength)
{
    if(world_lighting_exempt!=0 ||
       (coating?coat_strength<=0.0:specular_params.x<=0.0) ||
       sun_color_intensity.w<=0.0 || sun_direction.w<0.5)
        return vec3(0.0);
    vec3 surface=coating?vec3(1.0):
        surface_map_value;
    float strength=coating?coat_strength:
        max(specular_params.x*surface.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=coating?clamp(coat_roughness,0.08,1.0):
        clamp(specular_params.y*surface.g,0.08,1.0);
    float metalness=coating?0.0:clamp(specular_params.z*surface.b,0.0,1.0);
    vec3 N=coating?surface_coat_view:surface_base_view;
    vec3 vp=fragment_native_view_position;
    vec3 V=surface_to_eye;
    if(dot(N,V)<0.0) N=-N;
    vec3 L=normalize(sun_direction.xyz);
    float NdotL=max(dot(N,L),0.0),NdotV=max(dot(N,V),0.0);
    if(NdotL<=0.0 || NdotV<=0.0) return vec3(0.0);
    vec3 H=normalize(L+V);
    float Nh=max(dot(N,H),0.0),Vh=max(dot(V,H),0.0);
    float a=roughness*roughness,a2=a*a;
    float d=Nh*Nh*(a2-1.0)+1.0;
    float D=a2/(3.14159265359*d*d);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float Gv=NdotV/(NdotV*(1.0-k)+k);
    float Gl=NdotL/(NdotL*(1.0-k)+k);
    vec3 F0=coating?vec3(0.04):
        mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    vec3 F=F0+(vec3(1.0)-F0)*pow(1.0-Vh,5.0);
    return min(F*(D*Gv*Gl/max(4.0*NdotV,0.0001))*
               sun_color_intensity.rgb*sun_color_intensity.w*strength,
               vec3(32.0)) * sun_specular_shadow_factor(vp.y*32.0,NdotL);
}
vec3 gpu_sun_specular(vec3 base_color,vec2 uv)
{
    return gpu_sun_specular(
        base_color,uv,false,vec3(0.0),0.0,0.0);
}

// View position in JK units: X right, Y up, Z forward.
vec3 gpu_view_position()
{
    vec3 p=fragment_native_view_position;
    return vec3(p.x,p.z,p.y);
}
// The opaque scene depth remains full resolution at all times.
ivec2 opaque_scene_pixel()
{
    ivec2 p=ivec2(gl_FragCoord.xy)*(smoke_half_resolution!=0?2:1);
    return min(p,textureSize(scene_depth,0)-ivec2(1));
}
bool depth_clip_rejected(float fragment_depth)
{
    float opaque_depth=texelFetch(scene_depth,opaque_scene_pixel(),0).r;
    if(opaque_depth>=65500.0) return true;
    vec3 pos=gpu_view_position();
    vec3 plane=cross(dFdx(pos),dFdy(pos));
    float magnitude=length(plane);
    if(magnitude<1e-10 || pos.z<=0.0) return true;
    // Perpendicular plane separation is independent of camera angle.
    float gap=abs(((opaque_depth-fragment_depth)/32.0)*
                  dot(plane/magnitude,pos/max(pos.z,0.0001)));
    return gap>0.15;
}
float fluid_absorption(float fragment_depth)
{
    float opaque_depth=texelFetch(scene_depth,opaque_scene_pixel(),0).r;
    if(opaque_depth>=65500.0)
        return jk_fluid_absorption(opaque_depth,fragment_depth,1.0,
                                  advanced_material_params.y);
    vec3 pos=gpu_view_position();
    float ray_ratio=length(pos)/max(pos.z,0.0001);
    return jk_fluid_absorption(opaque_depth,fragment_depth,ray_ratio,
                              advanced_material_params.y);
}


// Parallax-occlusion sampling in the original mesh's tangent space.
// This is opt-in and uses the same 32-128 layer budget as native materials.
vec2 gpu_parallax_uv(vec2 tc)
{
    if(displacement_factor==0.0) return tc;
    vec3 N=normalize(local_fragment_normal);
    vec3 dp1=dFdx(local_fragment_position);
    vec3 dp2=dFdy(local_fragment_position);
    vec2 du1=dFdx(pixel_uv*uv_scale);
    vec2 du2=dFdy(pixel_uv*uv_scale);
    vec3 T=cross(dp2,N)*du1.x+cross(N,dp1)*du2.x;
    vec3 B=cross(dp2,N)*du1.y+cross(N,dp1)*du2.y;
    float basis=max(dot(T,T),dot(B,B));
    if(basis<1e-16) return tc;
    mat3 tbn=mat3(T/sqrt(basis),B/sqrt(basis),N);
    vec3 view_dir=normalize(transpose(tbn)*
                            (local_eye-local_fragment_position));
    if(view_dir.z<=0.05) return tc;
    float steps=mix(128.0,32.0,abs(view_dir.z));
    float step_depth=1.0/steps;
    vec2 uv_step=(view_dir.xy/view_dir.z)*displacement_factor/steps;
    vec2 current=tc;
    float layer=0.0;
    float h=texture(displacement_map,current).r;
    for(int i=0;i<128 && layer<h;i++) {
        current-=uv_step;
        layer+=step_depth;
        h=texture(displacement_map,current).r;
    }
    vec2 previous=current+uv_step;
    float after=h-layer;
    float before=texture(displacement_map,previous).r-layer+step_depth;
    float denom=after-before;
    if(abs(denom)<1e-6) return current;
    return mix(current,previous,clamp(after/denom,0.0,1.0));
}

#include "translucent_spotlight_common.glsl"

uniform int translucent_sun_enabled;
uniform int translucent_sun_maps_ready;
uniform int translucent_sun_dynamic_count;
uniform vec3 translucent_sun_direction;
uniform vec4 translucent_sun_color; // RGB, positive direct intensity
uniform float translucent_sun_split;
uniform float translucent_sun_bias_scale;
uniform int translucent_sun_sky_aperture_active;
#ifdef JKGM_SUN_SKY_APERTURE
uniform sampler2DArray translucent_sun_sky_aperture_map;
uniform vec4 translucent_sun_sky_aperture_depth_transform;
#endif
uniform vec2 translucent_sun_pcf_radius; // independent near/far smoothing
uniform vec4 translucent_sun_map_x[10];
uniform vec4 translucent_sun_map_y[10];
uniform vec4 translucent_sun_map_z[10];
uniform vec4 translucent_sun_tile_rect[8];
uniform float translucent_sun_max_distance; // 0 = unlimited, native-pass parity
uniform sampler2DShadow translucent_sun_near_depth;
uniform sampler2DShadow translucent_sun_far_depth;
uniform sampler2DShadow translucent_sun_dynamic_depth;

#include "translucent_sun_sampling.glsl"

float translucent_sun_visibility(vec3 world,float incidence)
{
    float visible=1.0;
    if(translucent_sun_maps_ready!=0) {
        vec3 near_uv=translucent_sun_coordinates(0,world);
        vec3 far_uv=translucent_sun_coordinates(1,world);
        float camera_distance=dot(world-translucent_spotlight_camera_origin.xyz,
                                  translucent_spotlight_camera_forward.xyz);
        bool use_near=camera_distance<translucent_sun_split &&
                      translucent_sun_inside(near_uv);
        bool use_far=translucent_sun_inside(far_uv);
        if(use_near && !use_far) {
            visible=translucent_sun_sample(0,near_uv,incidence);
        } else if(use_near && use_far) {
            float begin_blend=translucent_sun_split*0.8125;
            float end_blend=translucent_sun_split*0.9375;
            if(camera_distance<=begin_blend)
                visible=translucent_sun_sample(0,near_uv,incidence);
            else {
                float far_visibility=translucent_sun_sample(1,far_uv,incidence);
                if(camera_distance>=end_blend)visible=far_visibility;
                else
                    visible=mix(translucent_sun_sample(0,near_uv,incidence),
                                far_visibility,smoothstep(begin_blend,end_blend,camera_distance));
            }
        } else if(use_far)visible=translucent_sun_sample(1,far_uv,incidence);
    }
    if(translucent_sun_dynamic_count>0 && translucent_sun_split>0.0) {
        float distance=dot(
            world-translucent_spotlight_camera_origin.xyz,
            translucent_spotlight_camera_forward.xyz);
        if(distance<translucent_sun_split) {
            // Match the opaque composite's forward-depth near-only cutoff and
            // fade the last 15 percent to avoid a hard shadow ring.
            float near_fade=1.0-smoothstep(
                translucent_sun_split*0.85,translucent_sun_split,distance);
            for(int i=0;i<8;++i) {
                if(i>=translucent_sun_dynamic_count)break;
                vec3 uv=translucent_sun_coordinates(i+2,world);
                if(!translucent_sun_inside(uv))continue;
                float shadow=translucent_sun_sample(i+2,uv,incidence);
                visible=min(visible,mix(1.0,shadow,near_fade));
                if(visible<=0.001)break;
            }
        }
    }
    return visible;
}
// Share the cascade/dynamic atlas visibility of transparent sun receivers
// with directional GGX. Environment reflections and emissive light are not
// direct sunlight and must remain visible beneath a sun shadow.
float sun_specular_shadow_factor(float depth,float incidence)
{
    if(translucent_sun_enabled==0)return 1.0;
    float fade=1.0;
    if(translucent_sun_max_distance>0.0) {
        fade=1.0-smoothstep(translucent_sun_max_distance*0.85,
                            translucent_sun_max_distance,depth/32.0);
        if(fade<=0.0)return 0.0;
    }
    if(translucent_sun_maps_ready==0 && translucent_sun_dynamic_count==0)
        return fade;
    vec3 world=translucent_spotlight_world_pos(depth);
    return fade*translucent_sun_sky_aperture(world)*
           translucent_sun_visibility(world,max(incidence,0.35));
}

// Normal is evaluated on the TRANSLUCENT fragment, not from the opaque
// G-buffer. Glass and billboard sprites are illuminated from both sides.
vec3 illuminate_translucent_sun(vec3 baked,vec3 pigment,float opacity,
                                float depth,vec3 view_normal)
{
    if(translucent_sun_enabled==0 || opacity<=0.001 ||
       depth<=0.0 || depth>=65500.0)return baked;
    vec3 normal=translucent_spotlight_world_normal(view_normal);
    float incidence=abs(dot(normal,translucent_sun_direction));
    // Billboard sprites face the camera, NOT the sun. Their arbitrary quad
    // normal must not make them immune to shadows. Evaluate the light-space
    // visibility even when the quad's dot(N,sun) is close to zero, and
    // remove the shadowed component of the baked/native vertex brightness
    // independently of that incidence. Direct added sunlight still respects
    // the geometric incidence for ordinary translucent surfaces.
    vec3 world=translucent_spotlight_world_pos(depth);
    // Match the native alpha pass and opaque composite: smoothly fade
    // sunlight over the final 15 percent of its configured distance.
    float range_fade=1.0;
    if(translucent_sun_max_distance>0.0) {
        float forward=dot(world-translucent_spotlight_camera_origin.xyz,
                          translucent_spotlight_camera_forward.xyz);
        range_fade=1.0-smoothstep(translucent_sun_max_distance*0.85,
                                  translucent_sun_max_distance,forward);
        if(range_fade<=0.0)return baked;
    }
    // A closed sunward ray is a shadowed receiver, not an instruction to
    // retain all baked light. The default-off aperture returns 1.0.
    float sky_access=translucent_sun_sky_aperture(world);
    float visibility=translucent_sun_visibility(world,max(incidence,0.35));
    float direct_visibility=visibility*sky_access;
    // Match native translucent rendering and the opaque composite:
    // shadow contrast tracks sun power rather than darkening baked ambient.
    float sun_shadow_strength=clamp(translucent_sun_color.a,0.0,1.0);
    vec3 dimmed=baked*(1.0-0.75*range_fade*sun_shadow_strength*
                      (1.0-direct_visibility));
    vec3 radiance=pigment*translucent_sun_color.rgb*
                  translucent_sun_color.a*incidence*direct_visibility*opacity*range_fade;
    return dimmed+radiance*(vec3(1.0)-clamp(dimmed,vec3(0.0),vec3(1.0)));
}



layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
layout(location=4) out vec4 out_pigment; // only attached for packed deferred lighting
// 4x4 Bayer ordered screen-door coverage. Kept samples become fully
// opaque and write ordinary depth/SSAO; no blend or per-face sort needed.
float dither_threshold()
{
    return gpu_material_dither_threshold();
}

void prepare_fragment_surface(vec2 uv)
{
    bool base_spec=specular_params.x>0.0;
    bool base_mapped=normal_specular_params.x>0.5 ||
                     normal_specular_params.z>0.5;
    bool base_needed=base_spec || (normal_mapped_diffuse!=0 && base_mapped);
    bool coat_needed=false,coat_mapped=false;
#ifdef JKGM_MATCAP_LAYER
    base_needed=base_needed || matcap_enabled!=0;
#endif
#ifdef JKGM_CLEARCOAT_LAYER
    coat_needed=clearcoat_enabled!=0;
    coat_mapped=coat_needed && clearcoat_has_normal!=0;
#endif
    if(!base_needed && !coat_needed)return;
    vec3 r=fragment_basis_right,f=fragment_basis_forward,u=fragment_basis_up;
    surface_normal_transform=mat3(r,f,u);
    // Ordinary materials receive the signed cofactor matrix from the
    // vertex shader. Only affine point-light distance evaluation needs
    // the original basis; retain the exact singular fallback in that case.
    if(nonrigid_light_transform!=0) {
        vec3 cr=cross(f,u),cf=cross(u,r),cu=cross(r,f);
        float det=dot(r,cr);
        if(abs(det)>1e-8)
            surface_normal_transform=mat3(sign(det)*cr,sign(det)*cf,sign(det)*cu);
    }
    surface_geometric_normal=normalize(local_fragment_normal);
    surface_tangent_valid=false;
    // Use draw-uniform conditions for derivatives, not per-pixel coat masks.
    if((base_needed && base_mapped) || coat_mapped) {
        vec3 n=surface_geometric_normal;
        vec3 dp1=dFdx(local_fragment_position);
        vec3 dp2=dFdy(local_fragment_position);
        vec2 du1=dFdx(pixel_uv*uv_scale);
        vec2 du2=dFdy(pixel_uv*uv_scale);
        vec3 t=cross(dp2,n)*du1.x+cross(n,dp1)*du2.x;
        vec3 b=cross(dp2,n)*du1.y+cross(n,dp1)*du2.y;
        float basis=max(dot(t,t),dot(b,b));
        if(basis>=1e-16) {
            float inv_basis=inversesqrt(basis);
            surface_tangent_frame=mat3(t*inv_basis,b*inv_basis,n);
            surface_tangent_valid=true;
        }
    }
    if(base_needed) {
        surface_base_local=mapped_local_normal(uv);
        surface_base_view=gpu_view_normal_from_local(surface_base_local);
    }
    if(base_spec) {
#ifdef JKGM_SIMPLE_OPAQUE
        surface_map_value=vec3(1.0);
#else
        surface_map_value=specular_params.w>0.5
            ?texture(surface_map,uv).rgb:vec3(1.0);
#endif
    }
    if(base_spec || coat_needed) {
        vec3 vp=fragment_native_view_position;
        surface_to_eye=-normalize(vec3(vp.x,vp.z,vp.y));
    }
}

void main() {
    // gl_FragCoord.w is 1/clip.w, and clip.w is camera-forward Y.
    // This is the exact native 1/rhw linear depth (32*Y) even on large
    // triangles crossing the camera near plane; no vertex-depth clamping.
    float view_depth=32.0/max(gl_FragCoord.w,0.00000001);
    // Lighting and fog retain the geometric depth. G-buffer and depth
    // clipping use the authored rendering-only bias for classic sprites.
    float effective_depth=view_depth+32.0*sprite_depth_bias;
    if(sprite_flare_center.w>0.5) {
        // Type 4 uses exactly one opaque-depth sample at its center.
        // Its edges must never undergo ordinary per-pixel occlusion.
        vec3 center=sprite_flare_center.xyz;
        ivec2 dims=textureSize(scene_depth,0);
        if(any(lessThan(center.xy,vec2(0.0))) ||
           any(greaterThanEqual(center.xy,vec2(dims))) ||
           !(center.z>0.0) || center.z>=65500.0) discard;
        float opaque=texelFetch(scene_depth,ivec2(center.xy),0).r;
        if(opaque<65500.0 &&
           opaque+max(0.01,center.z*0.0005)<center.z) discard;
    }
    if(smoke_half_resolution!=0) {
        // This FBO has stencil but not the full-resolution hardware depth.
        // A conservative 2x2 opaque footprint blocks smoke through walls.
        ivec2 p=opaque_scene_pixel();
        ivec2 last=textureSize(scene_depth,0)-ivec2(1);
        float closest=texelFetch(scene_depth,p,0).r;
        closest=min(closest,texelFetch(scene_depth,min(p+ivec2(1,0),last),0).r);
        closest=min(closest,texelFetch(scene_depth,min(p+ivec2(0,1),last),0).r);
        closest=min(closest,texelFetch(scene_depth,min(p+ivec2(1,1),last),0).r);
        if(closest<65500.0 &&
           effective_depth>closest+max(0.05,closest*0.0005)) discard;
    }
    // Camera + ThingFlag 0x20 behaves as a world-anchored GUI element:
    // remain immune to all postFX, but disappear behind opaque scene depth.
    // The final framebuffer has no attached scene depth after compositing.
    if(post_exempt_pass!=0 && camera_hud_thing==2) {
        float behind=texelFetch(scene_depth,ivec2(gl_FragCoord.xy),0).r;
        if(behind<65500.0 &&
           effective_depth>behind+max(0.05,behind*0.0005)) discard;
    }
    if(transparent_pass!=0 && advanced_material_params.x>0.5 &&
       depth_clip_rejected(effective_depth)) discard;
    vec2 uv=gpu_parallax_uv(pixel_uv*uv_scale);
    vec4 texel=gpu_material_sample_albedo(
        albedo_map,albedo_map2,uv,albedo_scroll,has_texture,
        albedo_transition.x>0.5?1:2,
        albedo_transition.z>0.5?albedo_transition.y:0.0);
    vec4 albedo=texel*albedo_factor;
    albedo.a*=native_face_opacity;
    if(decal_receiver_lighting!=0) {
        // Feather the actual box boundary per pixel, not per vertex:
        // clipped polygons often have ALL their vertices at the border.
        vec3 edge=min(decal_coverage.xyz,vec3(1.0)-decal_coverage.xyz);
        float feather=smoothstep(0.0,0.055,edge.x)*
                      smoothstep(0.0,0.055,edge.y)*
                      smoothstep(0.0,0.12,edge.z);
        albedo.a*=clamp(decal_coverage.w,0.0,1.0)*feather;
    }
    // Evaluate the actual emissive texel before alpha rejection. Native
    // normal/Fluid blending adds it even where albedo alpha is zero.
    // Opaque depth and explicit masks/dithering are still controlled solely
    // by albedo coverage in the shared policy.
    vec3 emission=gpu_material_emission(
        emissive_map,uv,emissive_scroll,emissive_factor,has_emissive);
    // Per-Thing 3DO and SetSpriteInfo SPR fades attenuate emissive RGB
    // exactly once, including emissive-only/zero-alpha albedo materials.
    // This is independent of the MAT's albedo alpha.
    emission*=thing_opacity;
    // Projected-decal feather and native sprite opacity must continue to
    // suppress pixels at zero overall coverage; this is an albedo-texture
    // exception, not permission to revive a completely faded effect.
    int preserve_emission=emissive_only;
    if(decal_receiver_lighting==0 && native_face_opacity>0.001)
        preserve_emission=gpu_material_emission_coverage(
            emissive_only,blend_mode,emission);
    if(gpu_material_discard(albedo.a,alpha_mode,alpha_cutoff,
                            preserve_emission,transparent_pass!=0,
                            post_exempt_pass!=0)) discard;
    // For opaque masks/dither the surviving fragments write full coverage.
    if(post_exempt_pass!=0 &&
       (alpha_mode==0 || alpha_mode==2 || alpha_mode==4))
        albedo.a=1.0;
    if(camera_hud_thing!=0) {
        // Flat HUD output, exempt from JK lighting, fog, sunlight, SSAO,
        // bloom, HDR and other postFX. Add emissive RGB independently of
        // albedo alpha, just as the world transparency pass does.
        float a=clamp(albedo.a,0.0,1.0);
        if(blend_mode==2) { // multiply uses (DST_COLOR, ZERO)
            // In multiply mode emission can brighten the multiplier but
            // does not become an independent additive contribution.
            out_color=vec4(mix(vec3(1.0),
                               clamp(albedo.rgb+emission,0.0,1.0),a),0.0);
        } else if(blend_mode==1) { // additive uses (ONE, ONE)
            out_color=vec4(albedo.rgb*a+emission,0.0);
        } else {
            // With (ONE, ONE_MINUS_SRC_ALPHA), alpha=0 preserves the
            // destination while adding emission from a transparent base.
            out_color=vec4(albedo.rgb*a+emission,a);
        }
        out_emissive=vec4(0.0);
        out_normal=vec4(0.0);
        out_depth=vec4(0.0);
        out_pigment=vec4(0.0);
        return;
    }
    // GPU view-position units are JK units; fog/depth use 32x JK units.
    // Scene depth, HUD exemptions, and Fluid absorption remain unchanged.
    float fog_depth=fog_params.w>0.5
        ?length(gpu_view_position())*32.0:view_depth;
    float fog_fraction=clamp((fog_depth-fog_params.x)/
                    max(fog_params.y-fog_params.x,0.0001),0.0,1.0);
    if(fog_params.w>1.5) {
        float reach=max(fog_params.y+0.5*max(fog_params.x,0.0),1.0);
        fog_fraction=1.0-exp(-2.302585093*max(fog_depth,0.0)/reach);
    }
    float fog=fog_fraction*fog_params.z;
    // Match the native vertex-by-vertex tint and lighting interpolation.
    // Keep it separate from alpha and from emissive output.
    float fullbright=solid_fullbright!=0 ? 1.0 :
        gpu_material_palette_fullbright(
            palette_fullbright_mask,uv+albedo_scroll,has_palette_fullbright);
    // The material's original baked JK RGB is untouched. The optional
    // perturbed normal is used only for ADDED sun, spotlight and pixel-point
    // light. Keep the geometric normal if this face has no normal texture.
    prepare_fragment_surface(uv);
    vec3 diffuse_local_normal=local_fragment_normal;
    vec3 diffuse_view_normal=view_normal;
    bool const_normal_mapped_diffuse=
        normal_mapped_diffuse!=0 &&
        (normal_specular_params.x>0.5 ||
         normal_specular_params.z>0.5) &&
        has_texture==1 && solid_fullbright==0 && fullbright<0.999 &&
        blend_mode!=3 && // Fluid keeps geometry normals for diffuse light
        world_lighting_exempt==0 && sprite_receiver==0 &&
        decal_receiver_lighting==0;
    if(const_normal_mapped_diffuse) {
        diffuse_local_normal=surface_base_local;
        vec3 n=surface_base_view;
        float sh=max(projection.x,0.0001);
        float sv=sh/max(projection.y,0.0001);
        diffuse_view_normal=normalize(vec3(n.x*sh,n.y*sv,n.z));
    }
    float env_metalness=0.0;
#ifdef JKGM_SIMPLE_OPAQUE
    vec3 env_reflection=vec3(0.0);
#else
    vec3 env_reflection=gpu_environment_reflection(albedo.rgb,uv,env_metalness);
#endif
    vec3 coat_specular=vec3(0.0),coat_local_normal=vec3(0.0);
    vec3 coat_values=vec3(0.0,0.1,1.0);
    float base_diffuse_trans=1.0,base_specular_trans=1.0;
#ifdef JKGM_CLEARCOAT_LAYER
    if(clearcoat_enabled!=0) {
        coat_values=gpu_clearcoat_values(uv);
        // Evaluate derivatives before any per-pixel coverage branch.
        coat_local_normal=gpu_clearcoat_local_normal(uv);
        if(coat_values.x>0.0) {
            // Unmapped base and coat use the exact same normal.
            surface_coat_view=(specular_params.x>0.0 &&
                normal_specular_params.x<0.5 &&
                normal_specular_params.z<0.5 && clearcoat_has_normal==0)
                ?surface_base_view:gpu_view_normal_from_local(coat_local_normal);
            float f=gpu_clearcoat_fresnel(surface_coat_view);
            base_specular_trans=clamp(1.0-coat_values.x*f,0.0,1.0);
            base_diffuse_trans=base_specular_trans*
                               mix(1.0,coat_values.z,coat_values.x);
            float unused_metalness=0.0;
            coat_specular=gpu_environment_reflection(
                albedo.rgb,uv,unused_metalness,true,coat_local_normal,
                coat_values.y,coat_values.x);
            coat_specular+=gpu_sun_specular(
                albedo.rgb,uv,true,coat_local_normal,
                coat_values.y,coat_values.x);
        }
    }
#endif
    // CMP fullbright is immune to lighting, not an extra HDR source.
    // Author-provided emission stays separate.
    env_metalness*=1.0-fullbright;
    env_reflection*=1.0-fullbright;
    // Metal suppresses diffuse only when its environment contribution
    // actually exists; follows the native replacement shader.
    // The vertex experiment has already selected and interpolated JK color.
    // Avoid all per-fragment sector bounds/plane traversal in that mode.
    sector_crossing_slot=sector_crossing_count>1 &&
                         sector_crossing_per_vertex==0
        ? select_sector_crossing_slot() : 0;
    vec3 jk_fragment_lit=mix(
        pixel_dynamic_lighting!=0
          ? pixel_jk_diffuse_lighting(diffuse_local_normal) :
          (sector_crossing_count>1 && sector_crossing_per_vertex==0
             ? sector_crossing_lit[sector_crossing_slot] : jk_lit_rgb),
        vec3(1.0),fullbright);
    vec3 matcap_lit=vec3(0.0),matcap_unlit=vec3(0.0);
#ifdef JKGM_MATCAP_LAYER
    vec3 matcap_effect=gpu_matcap_effect(uv);
    matcap_lit=matcap_effect*matcap_lighting_influence;
    matcap_unlit=matcap_effect*(1.0-matcap_lighting_influence);
#endif
    vec3 lit_rgb=albedo.rgb*jk_fragment_lit*
                 (1.0-env_metalness)*base_diffuse_trans;
    lit_rgb+=matcap_lit*jk_fragment_lit*base_diffuse_trans;
    if(transparent_pass!=0 && post_exempt_pass==0 &&
       world_lighting_exempt==0 && decal_receiver_lighting==0 &&
       fullbright<0.999 &&
       (blend_mode==0 || blend_mode==3) &&
       (alpha_mode==1 || alpha_mode==3) &&
       emissive_only==0) {
        // Normal and Fluid receive sunlight/occlusion on diffuse RGB.
        // Fluid absorption and alpha remain unchanged further below;
        // never multiply sunlight by Fluid opacity a second time.
        lit_rgb=illuminate_translucent_sun(
            lit_rgb,albedo.rgb+matcap_lit*base_diffuse_trans,1.0,
            view_depth,diffuse_view_normal);
    }
    // Decal pigment inherits the receiver's interpolated JK diffuse light
    // instead of appearing fullbright. That scalar is written into unused
    // opaque emissive.A. AO matches the opaque scene's compositing pass.
    // Crucially, the receiver material's own pigment color is NOT multiplied
    // into the decal (black/dark painted surfaces retain decal color).
    if(decal_receiver_lighting!=0) {
        ivec2 pixel=ivec2(gl_FragCoord.xy);
        float illumination=clamp(
            gbuffer_packing!=0
                ?gbuffer_receiver_light(texelFetch(decal_light_texture,pixel,0))
                :texelFetch(decal_light_texture,pixel,0).a,0.0,1.25);
        float occlusion=clamp(1.0-
            texelFetch(decal_occlusion,pixel,0).r,0.0,1.0);
        lit_rgb*=illumination*occlusion;
    }
    vec3 rgb=mix(lit_rgb,fog_color,fog);
    // Optional dynamic Thing point-light -> GGX highlights. JK currently
    // supplies scalar light intensity (not independent per-light RGB), so
    // these experimental highlights are neutral/white. Diffuse remains JK.
    vec3 specular=(gpu_sun_specular(albedo.rgb,uv)+env_reflection)*
                  base_specular_trans+coat_specular;
    // Only forward-blended non-decal faces receive projected light.
    // Ordinary alpha-tested coverage already receives opaque-composite light.
    vec3 projected_light=vec3(0.0);
    if(translucent_spotlight_count>0 && transparent_pass!=0 &&
       post_exempt_pass==0 && world_lighting_exempt==0 &&
       decal_receiver_lighting==0 && fullbright<0.999 &&
       (alpha_mode==1 || alpha_mode==3) &&
       blend_mode!=1 && blend_mode!=2 && emissive_only==0) {
        projected_light=light_translucent_spotlight(
            view_depth,diffuse_view_normal,albedo.rgb,clamp(albedo.a,0.0,1.0)) *
            (1.0-fog);
    }
    if(dynamic_light_specular_enabled!=0 && num_lights>0 &&
       specular_params.x>0.0)
        specular+=point_light_specular(albedo.rgb,uv)*
                  base_specular_trans;
#ifdef JKGM_CLEARCOAT_LAYER
    if(clearcoat_enabled!=0 && coat_values.x>0.0 &&
       dynamic_light_specular_enabled!=0 && num_lights>0)
        specular+=point_light_specular(
            albedo.rgb,uv,true,coat_local_normal,
            coat_values.y,coat_values.x);
#endif
    specular*=(1.0-fog)*(1.0-fullbright);
#ifdef JKGM_MATCAP_LAYER
    // The artistically unlit portion bypasses deferred surface shadows
    // and AO. It does not create physically emitted light or a GGX lobe.
    specular+=matcap_unlit*(1.0-fog)*base_diffuse_trans;
#endif
    projected_light*=1.0-fullbright;
    // The opaque pass composites this separate emissive attachment after
    // SSAO. The translucent pass has only the color target: emission must
    // therefore be added directly to premultiplied RGB, never multiplied
    // by albedo alpha. With alpha=0 this acts as emission-only additive.
    float a=transparent_pass!=0 ? albedo.a : 1.0;
    if(transparent_pass!=0 && blend_mode==3) {
        // Native Fluid only multiplies diffuse pigment by absorption;
        // MAT/texture alpha affects the blend factor, not pigment RGB.
        // JK's original per-face vertex alpha is premultiplied before
        // gamma-1.8, unlike MAT alpha. Match that distinction here.
        float absorption=fluid_absorption(effective_depth);
        float opacity=albedo.a*absorption;
        float vertex_rgb_alpha=pow(clamp(native_face_opacity,0.0,1.0),1.8);
        vec3 diffuse=(lit_rgb*(1.0-fog)*vertex_rgb_alpha+
                      fog_color*(fog*albedo.a))*absorption;
        // Fluid's specular is normally independent of its MAT alpha, but
        // an explicit per-Thing fade must attenuate the highlight too.
        out_color=vec4(diffuse+emission+specular*thing_opacity+
                       projected_light,opacity);
    } else if(transparent_pass!=0 && blend_mode==2) {
        // With (DST_COLOR, ZERO) the RGB output is a multiplier, not
        // premultiplied color. Alpha zero must multiply by WHITE (identity).
        // Match the native triangle path: emission biases the multiplier
        // toward white, but never adds light independently in Multiply mode.
        vec3 tint=clamp(albedo.rgb*jk_fragment_lit*base_diffuse_trans+
                        emission+specular*albedo.a,
                        0.0,1.0);
        out_color=vec4(mix(vec3(1.0),tint,albedo.a),0.0);
    } else {
        // Additive and normal pass use premultiplied pigment; alpha for the
        // additive framebuffer itself is preserved by glBlendFuncSeparate.
        // Opaque fog is applied after SSAO in the shared composite.
        out_color=vec4(transparent_pass!=0
                           ? rgb*a+emission+specular*a+projected_light : lit_rgb,
                       (transparent_pass!=0 && blend_mode==1) ? 0.0 : a);
    }
    // For the opaque G-buffer, emissive alpha is unused by composition.
    // Store JK's already-interpolated light scalar for projected decals;
    // normal emissive RGB is unchanged.
    out_emissive=vec4(transparent_pass!=0
                          ? vec3(0.0) : emission+specular,
                      transparent_pass!=0 ? a :
                      dot(jk_fragment_lit,vec3(0.2126,0.7152,0.0722)));
    // A zero normal is already rejected by the SSAO and sun receivers.
    // This costs no additional G-buffer attachment or fullscreen pass.
    // A skybox/ThingFlag 0x20 skips SSAO and sunlight via zero RGB
    // normals, but it STILL occupies a G-buffer pixel. Alpha is a separate
    // coverage marker used by HOM; never couple it to lighting flags.
    // Opaque/cutout sprites are thin, camera-facing billboards, not real
    // one-sided surfaces. G-buffer normal.A=2 distinguishes them without
    // another attachment; receiver lighting treats their shadow response as
    // two-sided, independent of camera-facing normals. Coverage is still >0.
    out_normal=(world_lighting_exempt!=0 || fullbright>=0.5)
                   ? gbuffer_pack_normal(vec3(0.0),1.0,
                        dot(jk_fragment_lit,vec3(0.2126,0.7152,0.0722))) :
                     gbuffer_pack_normal(normalize(diffuse_view_normal),
                                         sprite_receiver!=0 ? 2.0 : 1.0,
                                         dot(jk_fragment_lit,vec3(0.2126,0.7152,0.0722)));
    // Packed mode stores unlit pigment separately; depth remains R16F.
    out_depth=vec4(effective_depth,
                   albedo.rgb+matcap_lit*base_diffuse_trans);
    out_pigment=vec4(albedo.rgb+matcap_lit*base_diffuse_trans,1.0);
}
