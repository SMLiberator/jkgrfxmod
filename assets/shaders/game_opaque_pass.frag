#version 330 core

uniform vec4 features;

uniform sampler2D albedo_map;
uniform vec4 albedo_factor;

uniform sampler2D emissive_map;
uniform vec3 emissive_factor;

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
// Dynamic fog: replaces the fixed FOG_START / FOG_END / color / opacity
// constants in BOTH game_opaque_pass.frag and game_trns_pass.frag.
// The values come from the renderer, set by the SetFog* COG verbs.
// ---------------------------------------------------------------------------
uniform vec4 fog_params;  // x = enabled (0 or 1), y = start, z = end, w = opacity
uniform vec3 fog_color;   // 0..1 as written in the COG script (do the same
                          // sRGB->linear conversion here that you do today)

float fog_factor(float dist)
{
    float span = max(fog_params.z - fog_params.y, 0.0001);
    float f = clamp((dist - fog_params.y) / span, 0.0, 1);
    return fog_params.x * fog_params.w * f;
}

// ---------------------------------------------------------------------------
// Reflections (environment mapping). Keep this block identical in BOTH
// game_opaque_pass.frag and game_trns_pass.frag.
//
// Materials from a material pack can reflect an equirectangular environment image. How strongly
// is set per material (reflection_strength, plus the optional surface map: R = reflectivity,
// G = roughness, B = metalness). The result is added like emissive light, so SSAO does not
// darken it and it fades into fog.
//
// The renderer reads the engine's current camera every frame, so the environment stays fixed in
// the world as you turn, and the field of view follows zoom and other changes made by scripts.
// If the camera cannot be read, the environment turns with the view and the field of view is
// assumed to be REFLECTION_HFOV_DEGREES.
// ---------------------------------------------------------------------------
uniform sampler2D surface_map;   // R = reflectivity, G = roughness, B = metalness (linear)
uniform sampler2D env_map;       // equirectangular environment image
uniform vec4 reflection_params;  // x = strength, y = roughness, z = metalness, w = has surface map
uniform vec4 env_params;         // x = has an environment image
uniform vec4 camera_right;       // xyz = camera right axis in world space, w = 1 if the camera was read
uniform vec4 camera_forward;     // xyz = camera forward axis in world space
uniform vec4 camera_up;          // xyz = camera up axis in world space
uniform vec4 view_params;        // x = tan(half the horizontal field of view)
uniform vec2 screen_resolution;

// Only used if the engine camera could not be read: the horizontal field of view to assume
const float REFLECTION_HFOV_DEGREES = 90.0;

// Turns the environment image around the vertical axis (degrees), to line up the sun, a skyline...
const float ENV_YAW_DEGREES = 0.0;

// Brightness of the environment image as seen in reflections
const float ENV_INTENSITY = 1.0;

// Fresnel look: how much of the reflection is visible looking straight at a surface (0.25 = a
// quarter), rising to full strength at glancing angles. FRESNEL_POWER = how fast it ramps up.
const float FRESNEL_BIAS = 0.25;
const float FRESNEL_POWER = 4.0;

// Tuning aid for reflective materials (those with a strength above 0):
//   0  off
//   1  show the reflection direction (relative to the view) as a color
//   2  show the surface normal (relative to the view) as a color
//   3  show the environment at full strength, ignoring strength, fresnel and tint
//   4  show whether the engine camera was read: green = yes, red = no (fallback in use)
//   5  show the surface normal in world space as a color. On any given wall this should NOT
//      change as you turn or move; if it does, the camera orientation is being applied wrongly.
const int REFLECTION_DEBUG = 0;

const float REFLECTION_PI = 3.14159265359;

// Returns the reflection to add, and the material's metalness (used to remove diffuse color)
vec3 compute_reflection(vec3 albedo_rgb, vec2 texcoords, out float metalness)
{
    // Sampled before any early-out so the texture lookup stays in uniform control flow
    vec3 surface = texture(surface_map, texcoords).rgb;

    metalness = 0.0;

    float strength = reflection_params.x;
    if(env_params.x < 0.5 || strength <= 0.0) {
        return vec3(0.0);
    }

    float roughness = reflection_params.y;
    metalness = reflection_params.z;
    if(reflection_params.w > 0.5) {
        strength *= surface.r;
        roughness *= surface.g;
        metalness *= surface.b;
    }

    metalness = clamp(metalness, 0.0, 1.0);

    bool have_camera = camera_right.w > 0.5;

    // Undo the projection scale: positions and normals arrive as (clip x, clip y, depth)
    float h_scale = have_camera ? view_params.x : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
    float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
    vec2 view_scale = vec2(h_scale, h_scale / aspect);

    // View space: x right, y up, z forward
    vec3 v = normalize(vec3(vp_pos.xy * view_scale, vp_pos.z));      // eye -> surface
    vec3 n = normalize(vec3(vp_normal.xy / view_scale, vp_normal.z));
    if(dot(n, v) > 0.0) {
        n = -n; // face the viewer
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

    // The direction the environment is looked up in: x right, y up, z forward, where "forward"
    // is a fixed direction in the world once the engine camera is known
    vec3 env_dir = r;
    if(have_camera) {
        // The camera axes are world-space directions; JK's world has x and y horizontal, z up
        vec3 world_r = camera_right.xyz * r.x + camera_forward.xyz * r.z + camera_up.xyz * r.y;
        env_dir = vec3(world_r.x, world_r.z, world_r.y);

        if(REFLECTION_DEBUG == 5) {
            vec3 world_n = camera_right.xyz * n.x + camera_forward.xyz * n.z + camera_up.xyz * n.y;
            return world_n * 0.5 + 0.5;
        }
    }
    else if(REFLECTION_DEBUG == 5) {
        return vec3(0.6, 0.0, 0.0);
    }

    // Direction -> equirectangular image coordinates (top row of the image = straight up)
    vec2 uv = vec2(fract(atan(env_dir.x, env_dir.z) / (2.0 * REFLECTION_PI) + 0.5 +
                         ENV_YAW_DEGREES / 360.0),
                   0.5 - asin(clamp(env_dir.y, -1.0, 1.0)) / REFLECTION_PI);

    // Rough surfaces look at blurrier (smaller) versions of the image
    vec2 env_size = vec2(textureSize(env_map, 0));
    float lod = clamp(roughness, 0.0, 1.0) * log2(max(env_size.x, env_size.y));
    vec3 env = textureLod(env_map, uv, lod).rgb * ENV_INTENSITY;

    if(REFLECTION_DEBUG == 3) {
        return env;
    }

    float cos_theta = clamp(dot(n, -v), 0.0, 1.0);
    float fresnel = mix(FRESNEL_BIAS, 1.0, pow(1.0 - cos_theta, FRESNEL_POWER));

    // Metals tint their reflections with their own color
    vec3 tint = mix(vec3(1.0), albedo_rgb, metalness);

    return env * tint * (strength * fresnel);
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

void main()
{
    vec2 adj_texcoords = vp_texcoords;
    float adj_vp_z = vp_z;

    if(displacement_factor != 0.0) {
        vec3 pmapv = parallax_mapping(vp_texcoords);
        adj_texcoords = pmapv.xy;
        adj_vp_z += pmapv.z;
    }

    vec4 albedo_map_sample = texture(albedo_map, adj_texcoords);
    albedo_map_sample = mix(vec4(1.0), albedo_map_sample, features.x);

    if(features.z > 0.5) {
        if(albedo_map_sample.a < alpha_cutoff) {
            discard;
        }

        albedo_map_sample.a = 1.0;
    }

    vec4 posterized_vertex_color = vec4(ceil(vp_color * 64.0)) / 64.0;
    vec4 vertex_color = mix(vp_color, posterized_vertex_color, features.w);

    vec4 albedo = albedo_map_sample * vertex_color * albedo_factor;

    if(albedo.a < 0.99999f) {
        discard;
    }

    vec3 emissive_map_sample = texture(emissive_map, adj_texcoords).rgb;
    emissive_map_sample = mix(vec3(1.0), emissive_map_sample, features.y);
    vec3 emissive = emissive_map_sample * emissive_factor;

    float fog_amount = fog_factor(vp_z);

    float metalness;
    vec3 reflection = compute_reflection(albedo.rgb, adj_texcoords, metalness);

    // Metals have no diffuse color of their own; it all comes from the reflection
    albedo.rgb *= (1.0 - metalness);

    if(REFLECTION_DEBUG != 0 && reflection_params.x > 0.0) {
        albedo.rgb = vec3(0.0);
    }

    vec3 fogged_albedo = mix(albedo.rgb, fog_color, fog_amount);
    //vec3 fogged_emissive = emissive * (1.0 - fog_amount);

    out_color = vec4(fogged_albedo, albedo.a);
    //out_emissive = vec4(fogged_emissive, albedo.a);
    out_emissive = vec4(emissive + reflection * (1.0 - fog_amount), albedo.a);
    out_normal = vec4(normalize(vp_normal), 1.0);
    out_depth = vec4(adj_vp_z, 0.0, 0.0, 1.0);
}
