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
uniform int blend_mode; // 0 normal, 1 additive, 2 multiply, 3 fluid
uniform float fluid_thickness; // native depth-buffer units (32 per game unit)
uniform int flare_mode; // SPR type 0x40: center-only depth occlusion
uniform sampler2D scene_depth; // Resolved opaque linear view depth (R16F)

uniform sampler2D displacement_map;
uniform float displacement_factor;

in vec3 vp_pos;
in vec2 vp_texcoords;
in vec4 vp_color;
in vec3 vp_normal;
in float vp_z;

layout(location = 0) out vec4 out_color;

#include "fluid_absorption.glsl"

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
               sun_color_intensity.w * strength, vec3(32.0));
}



vec2 parallax_mapping(vec2 tc)
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

    return adj_tc;
}



// A flare is an ordinary native billboard with special depth behavior.
// Rather than per-pixel occlusion, reconstruct its center from the
// sprite's UV mapping and the screen-space derivatives of that mapping.
// All pixels of one billboard therefore query the same opaque-scene point.
float flare_center_visibility()
{
    vec2 dx=dFdx(vp_texcoords);
    vec2 dy=dFdy(vp_texcoords);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    if(abs(determinant)<1e-8) return 0.0;

    vec2 uv_to_center=vec2(0.5)-vp_texcoords;
    vec2 offset=vec2(
        (uv_to_center.x*dy.y-uv_to_center.y*dy.x)/determinant,
        (dx.x*uv_to_center.y-dx.y*uv_to_center.x)/determinant
    );
    vec2 pixel=gl_FragCoord.xy+offset;
    ivec2 dimensions=textureSize(scene_depth,0);
    if(any(lessThan(pixel,vec2(0.0))) ||
       any(greaterThanEqual(pixel,vec2(dimensions)))) return 0.0;

    float center_depth=vp_z+dot(vec2(dFdx(vp_z),dFdy(vp_z)),offset);
    if(center_depth<=0.0) return 0.0;
    ivec2 texel=ivec2(pixel);
    float sampled_depth=texelFetch(scene_depth,texel,0).r;

    // A pixel marked 65504 is unoccupied. Do not require a background
    // surface: flares also work against the sky. Reject the entire quad
    // whenever its CENTER is behind an opaque object.
    if(sampled_depth<65500.0 && sampled_depth+0.01<center_depth)
        return 0.0;

    // Spatial edge softening (not a CPU readback or temporal lag). This
    // preserves the center test while avoiding an abrupt full-bright pop.
    float visible=1.0;
    const ivec2 taps[4]=ivec2[4](
        ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    for(int i=0;i<4;i++) {
        ivec2 xy=clamp(texel+taps[i],ivec2(0),dimensions-ivec2(1));
        float d=texelFetch(scene_depth,xy,0).r;
        if(d<65500.0 && d+0.01<center_depth)
            visible-=0.125;
    }
    return visible;
}


#include "translucent_spotlight_common.glsl"

uniform int translucent_sun_enabled;
uniform int translucent_sun_maps_ready;
uniform int translucent_sun_dynamic_count;
uniform vec3 translucent_sun_direction;
uniform vec4 translucent_sun_color; // RGB, positive direct intensity
uniform float translucent_sun_split;
uniform float translucent_sun_bias_scale;
uniform vec2 translucent_sun_pcf_radius; // independent near/far smoothing
uniform float translucent_sun_max_distance; // 0=unlimited
uniform vec4 translucent_sun_map_x[10];
uniform vec4 translucent_sun_map_y[10];
uniform vec4 translucent_sun_map_z[10];
uniform vec4 translucent_sun_tile_rect[8];
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
    for(int i=0;i<8;++i) {
        if(i>=translucent_sun_dynamic_count)break;
        vec3 uv=translucent_sun_coordinates(i+2,world);
        if(!translucent_sun_inside(uv))continue;
        visible=min(visible,translucent_sun_sample(i+2,uv,incidence));
        if(visible<=0.001)break;
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
    // Same range limit as the opaque composite: no added sun or shadow past
    // the configured distance, with a smooth fade over the last 15%.
    float range_fade=1.0;
    if(translucent_sun_max_distance>0.0) {
        float forward=dot(world-translucent_spotlight_camera_origin.xyz,
                          translucent_spotlight_camera_forward.xyz);
        range_fade=1.0-smoothstep(translucent_sun_max_distance*0.85,
                                  translucent_sun_max_distance,forward);
        if(range_fade<=0.0)return baked;
    }
    float visibility=translucent_sun_visibility(world,max(incidence,0.35));
    vec3 dimmed=baked*(1.0-0.75*range_fade*clamp(translucent_sun_color.a,0.0,1.0)*(1.0-visibility));
    vec3 radiance=pigment*translucent_sun_color.rgb*
                  translucent_sun_color.a*incidence*visibility*opacity*range_fade;
    return dimmed+radiance*(vec3(1.0)-clamp(dimmed,vec3(0.0),vec3(1.0)));
}



void main()
{
    float flare_visibility=1.0;
    if(flare_mode!=0) {
        flare_visibility=flare_center_visibility();
        if(flare_visibility<=0.0) discard;
    }
    vec2 adj_texcoords = vp_texcoords;
    if(displacement_factor != 0.0) {
        adj_texcoords = parallax_mapping(vp_texcoords);
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

    vec3 emissive_map_sample = texture(emissive_map, adj_texcoords + emissive_uv_offset).rgb;
    emissive_map_sample = mix(vec3(1.0), emissive_map_sample, features.y);
    // The JK/D3D bridge premultiplies diffuse RGB by vertex alpha, but
    // emissive maps bypassed that path entirely. Consequently a
    // SetSpriteInfo opacity fade darkened the diffuse sprite while its
    // emissive contribution remained constant. Apply native vertex alpha
    // ONCE to emission here; additive blending applies texture/material
    // coverage separately further below.
    vec3 emissive = emissive_map_sample * emissive_factor *
                    clamp(vertex_color.a, 0.0, 1.0);
    float fog_amount = fog_factor(vp_z);

    vec3 unlit_base_color = albedo_map_sample.rgb * albedo_factor.rgb;
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
            float incidence=max(dot(normalize(mapped_normal),
                                    normalize(sun_direction.xyz)),0.0);
            sun_specular*=sun_specular_shadow_factor(vp_z,incidence);
        }
    }

    // Preserve the original transparency blending convention:
    // (ONE, ONE_MINUS_SRC_ALPHA), not premultiplied color twice.
    const float TRANSLUCENT_COLOR_ALPHA_WEIGHT = 0.0;
    vec3 surface_color = albedo.rgb * (1.0 - metalness) *
                         mix(1.0, albedo.a, TRANSLUCENT_COLOR_ALPHA_WEIGHT);
    // Normal and Fluid receive diffuse sunlight and existing sun shadows.
    // Fluid applies depth absorption to the lit RGB below without changing
    // its alpha law. Exclude flares, alpha-cutouts, additive and multiply.
    if((blend_mode==0 || blend_mode==3) && flare_mode==0 &&
       features.z<0.5) {
        surface_color=illuminate_translucent_sun(
            surface_color,unlit_base_color,clamp(albedo.a,0.0,1.0),
            vp_z,vp_normal);
    }
    vec3 lit = surface_color * (1.0 - fog_amount) + fog_color * (fog_amount * albedo.a);

    vec3 projected_light = vec3(0.0);
    if(translucent_spotlight_count > 0 &&
       blend_mode != 1 && blend_mode != 2 && flare_mode == 0 &&
       features.z < 0.5) {
        projected_light = light_translucent_spotlight(
            vp_z, vp_normal, unlit_base_color, clamp(albedo.a, 0.0, 1.0)) *
            (1.0 - fog_amount);
    }
    vec3 color = emissive + (reflection + sun_specular) * (1.0 - fog_amount) +
                 lit + projected_light;

    if(blend_mode == 1) {
        // Additive: RGB is the emitted contribution. The engine already
        // premultiplies its vertex colors by vertex alpha, so only the
        // replacement texture and material-factor opacity remain to apply.
        // Alpha in the color target is preserved by glBlendFuncSeparate.
        float texture_opacity = clamp(albedo_map_sample.a * albedo_factor.a, 0.0, 1.0);
        out_color = vec4(color * texture_opacity, 0.0);
    }
    else if(blend_mode == 2) {
        // Multiply with alpha: result = dst * mix(1, tint, opacity).
        // A fully transparent pixel is neutral white, NOT black.
        // Use the material-lit pigment without the fog term: a dark decal
        // should not brighten the framebuffer by mixing fog into its tint.
        // JK's vertex RGB is already premultiplied by vertex alpha.
        // Recover the actual pigment for a multiply factor, otherwise a
        // white semi-transparent texel would incorrectly darken the scene.
        vec3 unpremultiplied_vertex = vertex_color.rgb /
                                      max(vertex_color.a, 0.00001);
        vec3 pigment = albedo_map_sample.rgb * albedo_factor.rgb *
                       unpremultiplied_vertex;
        vec3 tint = clamp(pigment * (1.0 - metalness) + emissive +
                          (reflection + sun_specular) * (1.0 - fog_amount), 0.0, 1.0);
        float opacity = clamp(albedo.a, 0.0, 1.0);
        out_color = vec4(mix(vec3(1.0), tint, opacity), 0.0);
    }
    else if(blend_mode == 3) {
        // Fluid: progressively replace the underlying terrain with the
        // liquid's albedo as the opaque scene behind it gets deeper.
        // Measure the separation ALONG THE VIEW RAY, so a grazing view
        // traverses more water than a perpendicular view. The depth buffer
        // stores camera-forward linear Z, not radial distance.
        float opaque_depth = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
        // Existing native materials are calibrated to unnormalized JK
        // depth-buffer units (32 per world unit). Share the precise law with
        // GPU world and 3DO rather than independently rescaling thickness.
        float ray_ratio = 1.0;
        if(opaque_depth < 65500.0) {
            float h_scale = camera_right.w > 0.5
                                ? view_params.x
                                : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
            float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
            vec3 view_ray = vec3(vp_pos.xy *
                                 vec2(h_scale, h_scale / aspect), vp_z);
            ray_ratio = length(view_ray) / max(vp_z, 0.0001);
        }
        float absorption=jk_fluid_absorption(
            opaque_depth,vp_z,ray_ratio,fluid_thickness);
        // With no opaque surface behind the fluid, treat it as infinitely
        // deep. The shader does not discard those pixels like a decal.
        //
        // Albedo texture and JK vertex colors are already premultiplied
        // by their respective alpha values. Scale only their contribution
        // by the fluid absorption, retaining existing mask/coverage.
        float opacity = clamp(albedo.a * absorption, 0.0, 1.0);
        vec3 diffuse = (surface_color * (1.0 - fog_amount) +
                        fog_color * (fog_amount * albedo.a)) * absorption;

        // Reflection and emission deliberately do NOT fade with depth.
        // ONE / ONE_MINUS_SRC_ALPHA blending preserves them over shallow
        // liquid where the albedo itself becomes almost transparent.
        vec3 specular = emissive + (reflection + sun_specular) * (1.0 - fog_amount) + projected_light;
        out_color = vec4(diffuse + specular, opacity);
    }
    else {
        // Preserve the existing JK convention for regular geometry.
        out_color = vec4(color, albedo.a);
    }
    // RGB is already premultiplied in the transparent blending path.
    // Attenuate both RGB and alpha to preserve that convention.
    if(flare_mode!=0) out_color*=flare_visibility;
}
