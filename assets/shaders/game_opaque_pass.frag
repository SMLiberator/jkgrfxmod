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

vec3 surface_view_normal(vec2 uv)
{
    float h_scale = camera_right.w > 0.5
                        ? view_params.x
                        : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
    float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
    vec2 xy_scale = vec2(h_scale, h_scale / aspect);
    vec3 n = normalize(vec3(vp_normal.xy / xy_scale, vp_normal.z));

    // No replacement map: bit-for-bit same geometric view normal used
    // by the previous reflection implementation.
    if(normal_params.z < 0.5 && normal_params.w < 0.5) {
        return n;
    }

    vec2 slope = vec2(0.0);
    if(normal_params.z > 0.5 && normal_params.x > 0.0) {
        vec3 layer = texture(normal_map, uv + normal_uv_offset).xyz * 2.0 - 1.0;
        slope += normal_params.x * layer.xy / max(layer.z, 0.05);
    }
    if(normal_params.w > 0.5 && normal_params.y > 0.0) {
        vec3 layer = texture(normal_map2, uv + normal_uv_offset2).xyz * 2.0 - 1.0;
        slope += normal_params.y * layer.xy / max(layer.z, 0.05);
    }
    // Cotangent frame from geometric position and original UV derivatives.
    // All quantities are in metric view space, avoiding the projection
    // scaling distortion present in raw clip-space vertex coordinates.
    vec3 pos = vec3(vp_pos.xy * xy_scale, vp_pos.z);
    vec3 dp1 = dFdx(pos);
    vec3 dp2 = dFdy(pos);
    vec2 duv1 = dFdx(vp_texcoords);
    vec2 duv2 = dFdy(vp_texcoords);
    vec3 t = cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x;
    vec3 b = cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y;
    float basis_len = max(dot(t, t), dot(b, b));
    if(basis_len < 1e-16) {
        return n;
    }
    float inv_basis = inversesqrt(basis_len);
    mat3 tbn = mat3(t * inv_basis, b * inv_basis, n);
    return normalize(tbn * normalize(vec3(slope, 1.0)));
}

// Returns added EnvMap radiance and effective metalness (used to remove diffuse).
// The base_color is BEFORE JK's vertex light intensity / vertex RGB multiplier:
// painted metal should not lose environmental reflections in dark lighting.
vec3 compute_reflection(vec3 base_color, vec2 texcoords, vec3 view_normal,
                        out float metalness)
{
    // Without an active envmap, do not touch the surface/specular texture.
    // In particular this makes enable_reflections=false a zero-sample path.
    metalness = 0.0;
    float strength = reflection_params.x;
    if(env_params.x < 0.5 || strength <= 0.0) {
        return vec3(0.0);
    }

    float roughness = reflection_params.y;
    metalness = reflection_params.z;
    if(reflection_params.w > 0.5) {
        vec3 surface = texture(surface_map, texcoords).rgb;
        strength *= surface.r;
        roughness *= surface.g;
        metalness *= surface.b;
    }
    // Avoid suppressing diffuse instantly when an EnvMap is faded in from black,
    // or keeping metal suppression after its EnvMap has faded out.
    metalness = clamp(metalness, 0.0, 1.0) *
                mix(env_transition.x, env_transition.y, env_transition.z);

    bool have_camera = camera_right.w > 0.5;
    float h_scale = have_camera ? view_params.x : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
    float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
    vec2 view_scale = vec2(h_scale, h_scale / aspect);

    // View space: x right, y up, z forward. Normals are passed with a projection
    // scale, which we remove here so the reflection uses genuine geometry.
    vec3 v = normalize(vec3(vp_pos.xy * view_scale, vp_pos.z)); // eye -> surface
    vec3 n = view_normal;
    if(dot(n, v) > 0.0) {
        n = -n;
    }
    vec3 r = reflect(v, n);

    if(REFLECTION_DEBUG == 1) {
        return r * 0.5 + 0.5;
    }
    else if(REFLECTION_DEBUG == 2) {
        return n * 0.5 + 0.5;
    }
    else if(REFLECTION_DEBUG == 4) {
        return have_camera ? vec3(0.0, 0.6, 0.0) : vec3(0.6, 0.0, 0.0);
    }

    // envmap = world-aligned, screen = world-aligned fallback until SSR is ready.
    // fixed = camera-aligned equirectangular, matcap = view-normal UV on a square image.
    int mode = int(env_params.y + 0.5);
    vec3 env_dir = r;
    if(mode <= 1 && have_camera) {
        vec3 world_r = camera_right.xyz * r.x + camera_forward.xyz * r.z + camera_up.xyz * r.y;
        env_dir = vec3(world_r.x, world_r.z, world_r.y);
    }

    if(REFLECTION_DEBUG == 5) {
        if(have_camera) {
            vec3 world_n = camera_right.xyz * n.x + camera_forward.xyz * n.z + camera_up.xyz * n.y;
            return world_n * 0.5 + 0.5;
        }
        return vec3(0.6, 0.0, 0.0);
    }

    vec2 uv;
    if(mode == 3) {
        // Standard sphere-normal MatCap projection, fixed relative to the screen.
        // Positive view-space Y is up; top of texture is v=0.
        uv = vec2(0.5 + 0.5 * n.x, 0.5 - 0.5 * n.y);
        uv = clamp(uv, vec2(0.001), vec2(0.999));
    }
    else {
        uv = vec2(fract(atan(env_dir.x, env_dir.z) / (2.0 * REFLECTION_PI) + 0.5 +
                            ENV_YAW_DEGREES / 360.0),
                  0.5 - asin(clamp(env_dir.y, -1.0, 1.0)) / REFLECTION_PI);
    }

    // Preserve existing roughness->mip blur approximation.
    vec2 env_size = env_transition.x > 0.5
        ? vec2(textureSize(env_map, 0)) : vec2(textureSize(env_map2, 0));
    float lod = clamp(roughness, 0.0, 1.0) * log2(max(max(env_size.x, env_size.y), 1.0));
    vec3 env_from = vec3(0.0);
    vec3 env_to = vec3(0.0);
    if(env_transition.z < 1.0 && env_transition.x > 0.5) {
        env_from = textureLod(env_map, uv, lod).rgb;
    }
    if(env_transition.z > 0.0 && env_transition.y > 0.5) {
        env_to = textureLod(env_map2, uv, lod).rgb;
    }
    vec3 env = mix(env_from, env_to, env_transition.z) * ENV_INTENSITY;

    if(REFLECTION_DEBUG == 3) {
        return env;
    }

    // Schlick Fresnel. Dielectrics use a neutral F0; metals use their UNLIT
    // base-color spectrum as specular reflectance, not vertex-lighted albedo.
    vec3 f0 = mix(vec3(DIELECTRIC_F0), clamp(base_color, 0.0, 1.0), metalness);
    float cos_theta = clamp(dot(n, -v), 0.0, 1.0);
    vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - cos_theta, 5.0);

    return env * fresnel * strength;
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
               sun_color_intensity.w * strength, vec3(32.0));
}

mat3 construct_tbn()
{
    vec3 n = normalize(vp_normal);

    vec3 dp1 = dFdx(vp_pos);
    vec3 dp2 = dFdy(vp_pos);
    vec2 duv1 = dFdx(vp_texcoords);
    vec2 duv2 = dFdy(vp_texcoords);

    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);

    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(t, t), dot(b, b)));
    return mat3(t * invmax, b * invmax, n);
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

// 4x4 Bayer ordered screen-door coverage. Kept samples become fully
// opaque and write ordinary depth/SSAO; no blend or per-face sort needed.
float dither_threshold()
{
    const int bayer[16]=int[16](
         0, 8, 2,10,
        12, 4,14, 6,
         3,11, 1, 9,
        15, 7,13, 5);
    ivec2 p=ivec2(gl_FragCoord.xy)&3;
    return (float(bayer[p.x+4*p.y])+0.5)/16.0;
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
    out_emissive = vec4(emissive + (reflection + sun_specular) * (1.0 - fog_amount),
                        albedo.a);
    out_normal = vec4(normalize(vp_normal), 1.0);
    out_depth = vec4(adj_vp_z, 0.0, 0.0, 1.0);
}
