#version 330 core

uniform vec4 features;

uniform sampler2D albedo_map;
uniform sampler2D albedo_map2;
uniform vec4 albedo_transition; // x=second map present, y=blend, z=active, w=unused
uniform vec4 albedo_factor;

uniform sampler2D emissive_map;
uniform vec3 emissive_factor;

// Independent per-material UV animation offsets, in texture repetitions.
uniform vec2 albedo_uv_offset;
uniform vec2 emissive_uv_offset;

uniform float alpha_cutoff;

uniform sampler2D displacement_map;
uniform float displacement_factor;

in vec3 vp_pos;
in vec2 vp_texcoords;
in vec4 vp_color;
in vec3 vp_normal;
in float vp_z;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_emissive;
layout(location = 2) out vec4 out_normal;
layout(location = 3) out vec4 out_depth;

// ---------------------------------------------------------------------------
// Dynamic fog: values are supplied by the runtime SetFog* COG verbs.
// Keep this definition identical in opaque and transparent game passes.
// ---------------------------------------------------------------------------
uniform vec4 fog_params;  // x=start, y=end, z=opacity, w=reserved
uniform vec3 fog_color;

float fog_factor(float dist)
{
    float span = max(fog_params.y - fog_params.x, 0.0001);
    float f = clamp((dist - fog_params.x) / span, 0.0, 1.0);
    return fog_params.z * f;
}

// ---------------------------------------------------------------------------
// Reflections (environment mapping). Keep this block identical in the opaque
// and transparent passes. Material factors x R/G/B surface map remain intact.
// Metalness now uses a Schlick Fresnel F0, and is colored by the UNLIT material
// base color instead of the already vertex-lit diffuse color.
// ---------------------------------------------------------------------------
uniform sampler2D surface_map;   // R=reflectivity, G=roughness, B=metalness (linear)
uniform sampler2D env_map;       // equirectangular environment image
uniform sampler2D env_map2;      // replacement target for crossfade
uniform vec4 env_transition;     // x=from present, y=to present, z=blend, w=unused
uniform vec4 reflection_params; // x=strength, y=roughness, z=metalness, w=has surface map
uniform vec4 env_params;        // x=has environment image, y=reflection mode (0 envmap, 1 screen fallback, 2 fixed, 3 matcap)
uniform vec4 camera_right;      // xyz=camera right in world space, w=1 if available
uniform vec4 camera_forward;    // xyz=camera forward in world space
uniform vec4 camera_up;         // xyz=camera up in world space
uniform vec4 view_params;       // x=tan(horizontal FOV / 2)
uniform vec2 screen_resolution;

const float REFLECTION_HFOV_DEGREES = 90.0;
const float ENV_YAW_DEGREES = 0.0;
const float ENV_INTENSITY = 1.0;

// Ordinary nonmetals reflect about 4% at normal incidence in a PBR model.
// Increase this artistically (e.g. 0.08) if glass/clear-coat looks too weak.
const float DIELECTRIC_F0 = 0.04;

// Debug 0=off; 1=view reflection direction; 2=view normal; 3=raw environment;
// 4=camera validity; 5=world-space normal.
const int REFLECTION_DEBUG = 0;
const float REFLECTION_PI = 3.14159265359;

// Optional linear normal maps (OpenGL tangent +Y).
// Each layer contributes a tangent-space surface slope; summing the slopes
// preserves the neutral (0,0,1) normal and avoids averaging away ripples.
uniform sampler2D normal_map;
uniform sampler2D normal_map2;
uniform vec2 normal_uv_offset;
uniform vec2 normal_uv_offset2;
uniform vec4 normal_params; // x/y strength, z/w layer enabled

#include "game_surface_common.glsl"





// Shared sun shadow samplers; no additional G-buffer attachment or pass.
uniform vec2 translucent_spotlight_resolution;
uniform vec4 translucent_spotlight_camera_origin;
uniform vec4 translucent_spotlight_camera_right;
uniform vec4 translucent_spotlight_camera_forward;
uniform vec4 translucent_spotlight_camera_up;
vec3 translucent_spotlight_world_pos(float depth)
{
    vec2 ndc = gl_FragCoord.xy / translucent_spotlight_resolution * 2.0 - 1.0;
    return translucent_spotlight_camera_origin.xyz + (depth / 32.0) * (
        translucent_spotlight_camera_forward.xyz +
        ndc.x * translucent_spotlight_camera_right.w *
                translucent_spotlight_camera_right.xyz +
        ndc.y * translucent_spotlight_camera_up.w *
                translucent_spotlight_camera_up.xyz);
}


uniform int translucent_sun_enabled;
uniform int translucent_sun_maps_ready;
uniform int translucent_sun_dynamic_count;
uniform vec3 translucent_sun_direction;
uniform vec4 translucent_sun_color; // RGB, positive direct intensity
uniform float translucent_sun_split;
uniform float translucent_sun_bias_scale;
uniform vec2 translucent_sun_vsm_params; // world-space stddev, bleed reduction
uniform vec2 translucent_sun_pcf_radius; // independent near/far smoothing
uniform vec4 translucent_sun_map_x[10];
uniform vec4 translucent_sun_map_y[10];
uniform vec4 translucent_sun_map_z[10];
uniform vec4 translucent_sun_tile_rect[8];
uniform float translucent_sun_max_distance; // 0 = unlimited, native-pass parity
#ifdef JKGM_SUN_VSM
uniform sampler2D translucent_sun_near_depth;
#else
uniform sampler2DShadow translucent_sun_near_depth;
#endif
#ifdef JKGM_SUN_VSM
uniform sampler2D translucent_sun_far_depth;
#else
uniform sampler2DShadow translucent_sun_far_depth;
#endif
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
    return fade*translucent_sun_visibility(world,max(incidence,0.35));
}

// One global COG-controlled directional sun. Only the specular term is
// affected: the original Jedi Knight vertex lighting stays unchanged.
uniform vec4 sun_direction;       // xyz unit view-space direction, w camera valid
uniform vec4 sun_color_intensity; // RGB linear color, w intensity

vec3 compute_sun_specular(vec3 base_color, vec2 texcoords, vec3 mapped_normal)
{
    if(sun_direction.w < 0.5 || sun_color_intensity.w <= 0.0 ||
       reflection_params.x <= 0.0) {
        return vec3(0.0);
    }

    float strength = reflection_params.x;
    float roughness = reflection_params.y;
    float metalness = reflection_params.z;
    if(reflection_params.w > 0.5) {
        vec3 surface = texture(surface_map, texcoords).rgb;
        strength *= surface.r;
        roughness *= surface.g;
        metalness *= surface.b;
    }
    if(strength <= 0.0) {
        return vec3(0.0);
    }
    metalness = clamp(metalness, 0.0, 1.0);

    // Engine vertex XY is scaled by projection; restore metric view axes.
    float hscale = view_params.x;
    float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
    vec3 view_pos = vec3(vp_pos.xy * vec2(hscale, hscale / aspect), vp_pos.z);
    vec3 V = -normalize(view_pos);
    vec3 N = normalize(mapped_normal);
    if(dot(N, V) < 0.0) N = -N;
    vec3 L = normalize(sun_direction.xyz);
    float NdotL = max(dot(N,L), 0.0);
    float NdotV = max(dot(N,V), 0.0);
    if(NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N,H), 0.0);
    float VdotH = max(dot(V,H), 0.0);

    // GGX normal distribution + Schlick/Smith geometry.
    float r = clamp(roughness, 0.08, 1.0);
    float a = r*r;
    float a2 = a*a;
    float d = NdotH*NdotH*(a2 - 1.0) + 1.0;
    float D = a2 / (REFLECTION_PI * d * d);
    float k = (r+1.0)*(r+1.0) / 8.0;
    float Gv = NdotV / (NdotV*(1.0-k) + k);
    float Gl = NdotL / (NdotL*(1.0-k) + k);

    vec3 F0 = mix(vec3(DIELECTRIC_F0), clamp(base_color,0.0,1.0), metalness);
    vec3 F = F0 + (vec3(1.0)-F0)*pow(1.0-VdotH, 5.0);
    vec3 spec = F * (D*Gv*Gl / max(4.0*NdotV,0.0001));
    // NdotL is already cancelled out from BRDF*incoming radiance.
    return min(spec * sun_color_intensity.rgb *
               sun_color_intensity.w * strength, vec3(32.0)) *
           sun_specular_shadow_factor(vp_z,NdotL);
}



vec3 parallax_mapping(vec2 tc)
{
    // The injector world space view position is always considered (0, 0, 0):
    vec3 view_dir = -normalize(transpose(construct_tbn()) * vp_pos);

    const float min_layers = 32.0;
    const float max_layers = 128.0;
    float num_layers = mix(max_layers, min_layers, abs(dot(vec3(0.0, 0.0, 1.0), view_dir)));

    float layer_depth = 1.0 / num_layers;
    float current_layer_depth = 0.0;
    vec2 shift_per_layer = (view_dir.xy / view_dir.z) * displacement_factor;
    vec2 d_tc = shift_per_layer / num_layers;

    vec2 current_tc = tc;
    float current_sample = texture(displacement_map, current_tc).r;

    while(current_layer_depth < current_sample) {
        current_tc -= d_tc;
        current_sample = texture(displacement_map, current_tc).r;
        current_layer_depth += layer_depth;
    }

    vec2 prev_tc = current_tc + d_tc;

    float after_col_depth = current_sample - current_layer_depth;
    float before_col_depth = texture(displacement_map, prev_tc).r - current_layer_depth + layer_depth;

    float a = after_col_depth / (after_col_depth - before_col_depth);
    vec2 adj_tc = mix(current_tc, prev_tc, a);

    // Calculate depth bias for SSAO
    vec3 tbn_fragdelta = vec3(adj_tc - tc, mix(after_col_depth, before_col_depth, a));
    float adj_vp_z = length(tbn_fragdelta);

    return vec3(adj_tc, adj_vp_z);
}



void main()
{
    vec2 adj_texcoords = vp_texcoords;
    float adj_vp_z = vp_z;

    if(displacement_factor != 0.0) {
        vec3 pmapv = parallax_mapping(vp_texcoords);
        adj_texcoords = pmapv.xy;
        adj_vp_z += pmapv.z;
    }

    vec4 albedo_map_sample = features.x > 0.5
        ? texture(albedo_map, adj_texcoords + albedo_uv_offset) : vec4(1.0);
    if(albedo_transition.z > 0.5) {
        vec4 target = albedo_transition.x > 0.5
            ? texture(albedo_map2, adj_texcoords + albedo_uv_offset) : vec4(1.0);
        albedo_map_sample = mix(albedo_map_sample, target, albedo_transition.y);
    }

    // features.z: 0=blend/default, 1=mask, 2=screen-door dither.
    // Mask has historically tested texture alpha only; preserve that.
    if(features.z > 0.5 && features.z < 1.5) {
        if(albedo_map_sample.a < alpha_cutoff) discard;
        albedo_map_sample.a = 1.0;
    }

    vec4 posterized_vertex_color = vec4(ceil(vp_color * 64.0)) / 64.0;
    vec4 vertex_color = mix(vp_color, posterized_vertex_color, features.w);
    vec4 albedo = albedo_map_sample * vertex_color * albedo_factor;
    if(features.z > 1.5) {
        // Texture, material opacity and native JK vertex alpha all
        // contribute to coverage, including mesh/sector fading.
        if(albedo.a <= dither_threshold()) discard;
        albedo.a = 1.0;
    }
    if(albedo.a < 0.99999f) {
        discard;
    }

    vec3 emissive_map_sample = texture(emissive_map, adj_texcoords + emissive_uv_offset).rgb;
    emissive_map_sample = mix(vec3(1.0), emissive_map_sample, features.y);
    vec3 emissive = emissive_map_sample * emissive_factor;

    float fog_amount = fog_factor(vp_z);

    // Material color without baked lighting: separate metallic pigment from
    // JK's grayscale and RGB per-vertex illumination.
    // GeoMode 3 (solid color) has no texture pigment. In JK's native D3D
    // execute buffer the face color is already baked into vp_color, while
    // default albedo_factor is white. Using white as the deferred pigment
    // made direct sunlight / diffuse spotlights bleach colored faces.
    // For untextured surfaces use the visible native RGB as a conservative
    // pigment proxy (it can include authored vertex lighting). Textured
    // faces keep the true unlit MAT pigment, preserving dark-scene recovery.
    vec3 unlit_base_color = features.x > 0.5
        ? albedo_map_sample.rgb * albedo_factor.rgb
        : clamp(vertex_color.rgb * albedo_factor.rgb, 0.0, 1.0);
    // Uniform branch: skip normal-map sampling and derivative-based TBN
    // construction unless a material actually has envmap or sun specularity.
    // The reflection master toggle sends reflection_params.x = 0.
    float metalness = 0.0;
    vec3 reflection = vec3(0.0);
    vec3 sun_specular = vec3(0.0);
    bool has_env_reflection = env_params.x > 0.5;
    bool has_sun_specular =
        sun_direction.w > 0.5 && sun_color_intensity.w > 0.0;
    if(reflection_params.x > 0.0 && (has_env_reflection || has_sun_specular)) {
        vec3 mapped_normal = surface_view_normal(adj_texcoords);
        if(has_env_reflection) {
            reflection = compute_reflection(unlit_base_color, adj_texcoords,
                                            mapped_normal, metalness);
        }
        if(has_sun_specular) {
            sun_specular = compute_sun_specular(unlit_base_color, adj_texcoords,
                                               mapped_normal);
        }
    }

    // Metal suppresses the diffuse term (as in the original implementation).
    // The unlit base color only changes its SPECULAR response.
    albedo.rgb *= (1.0 - metalness);

    if(REFLECTION_DEBUG != 0 && reflection_params.x > 0.0) {
        albedo.rgb = vec3(0.0);
    }

    // Defer fog to the opaque composite, AFTER SSAO has shaded the
    // surface. Otherwise SSAO multiplies/darkens the fog color itself.
    out_color = vec4(albedo.rgb, albedo.a);
    // This G-buffer's emissive alpha is not used by the opaque compositor.
    // Preserve native JK's vertex-interpolated diffuse intensity here for
    // projected decals; no extra texture or render target is required.
    float receiver_light = dot(vertex_color.rgb, vec3(0.2126, 0.7152, 0.0722));
    out_emissive = vec4(emissive + (reflection + sun_specular) * (1.0 - fog_amount),
                        receiver_light);
    out_normal = vec4(normalize(vp_normal), 1.0);
    // Depth R retains the existing linear-depth ABI. With spotlights
    // enabled the target is RGBA16F and GBA preserves unlit pigment.
    // In R16F mode OpenGL discards unused GBA without extra cost.
    out_depth = vec4(adj_vp_z, unlit_base_color.rgb);
}
