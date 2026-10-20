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
uniform float fluid_thickness; // Beer-Lambert absorption per game unit
uniform int depth_clip;
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

// 4x4 Bayer ordered screen-door coverage. Kept samples become fully
// opaque and write ordinary depth/SSAO; no blend or per-face sort needed.
float dither_threshold()
{
    ivec2 p=ivec2(gl_FragCoord.xy) & ivec2(3);
    int lo=(((p.x^p.y)&1)<<1)|(p.y&1);
    int hi=((((p.x>>1)^(p.y>>1))&1)<<1)|((p.y>>1)&1);
    return (float(4*lo+hi)+0.5)/16.0;
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

void main()
{
    float flare_visibility=1.0;
    if(flare_mode!=0) {
        flare_visibility=flare_center_visibility();
        if(flare_visibility<=0.0) discard;
    }
    if(depth_clip != 0) {
        // Compare perpendicular separation from the DECAL PLANE, not raw
        // camera-forward depth: the latter grows at grazing angles even when
        // a polygon remains a fixed distance above its receiving surface.
        // The resolved R16F opaque depth uses 65504 to mark empty pixels.
        const float DECAL_MAX_PLANE_GAP = 0.15;
        float surface_depth = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
        if(surface_depth >= 65500.0) {
            discard;
        }

        // Reconstruct metric view-space positions from the same coordinates
        // used by the reflection shader. The supplied vertex_position.xy
        // contains clip XY, while vertex_position.w contains linear view Z.
        float h_scale = camera_right.w > 0.5
                            ? view_params.x
                            : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
        float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
        vec3 decal_pos = vec3(vp_pos.xy * vec2(h_scale, h_scale / aspect), vp_z);

        // Use the actual triangle plane, not interpolated smooth normals,
        // which can differ substantially from the polygon orientation.
        vec3 plane_normal = cross(dFdx(decal_pos), dFdy(decal_pos));
        float plane_len = length(plane_normal);
        if(plane_len < 1e-10 || vp_z <= 0.0) {
            discard;
        }
        plane_normal /= plane_len;

        // Both depth values describe points along the same screen-space ray.
        // Their displacement along that ray, projected onto the decal plane
        // normal, is the true perpendicular gap in view-space game units.
        vec3 ray_per_view_z = decal_pos / vp_z;
        float plane_gap = abs((surface_depth - vp_z) *
                              dot(plane_normal, ray_per_view_z));
        if(plane_gap > DECAL_MAX_PLANE_GAP) {
            discard;
        }
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
        }
    }

    // Preserve the original transparency blending convention:
    // (ONE, ONE_MINUS_SRC_ALPHA), not premultiplied color twice.
    const float TRANSLUCENT_COLOR_ALPHA_WEIGHT = 0.0;
    vec3 surface_color = albedo.rgb * (1.0 - metalness) *
                         mix(1.0, albedo.a, TRANSLUCENT_COLOR_ALPHA_WEIGHT);
    vec3 lit = surface_color * (1.0 - fog_amount) + fog_color * (fog_amount * albedo.a);

    vec3 color = emissive + (reflection + sun_specular) * (1.0 - fog_amount) + lit;

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
        // Zero density means an optically clear fluid, even when no
        // opaque receiving surface exists behind it.
        float absorption = fluid_thickness > 0.0 ? 1.0 : 0.0;
        if(opaque_depth < 65500.0) {
            float h_scale = camera_right.w > 0.5
                                ? view_params.x
                                : tan(radians(REFLECTION_HFOV_DEGREES) * 0.5);
            float aspect = screen_resolution.x / max(screen_resolution.y, 1.0);
            vec3 view_ray = vec3(vp_pos.xy *
                                 vec2(h_scale, h_scale / aspect), vp_z);
            float ray_ratio = length(view_ray) / max(vp_z, 0.0001);
            float thickness = max(opaque_depth - vp_z, 0.0) * ray_ratio;
            absorption = 1.0 - exp(-fluid_thickness * thickness);
        }
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
        vec3 specular = emissive + (reflection + sun_specular) * (1.0 - fog_amount);
        out_color = vec4(diffuse + specular, opacity);
    }
    else {
        // Preserve the existing JK convention for regular geometry.
        // Depth-clipped decals also premultiply texture/material coverage
        // to avoid overbright fringes at translucent PNG edges.
        float coverage = depth_clip != 0
                             ? clamp(albedo_map_sample.a * albedo_factor.a, 0.0, 1.0)
                             : 1.0;
        out_color = vec4(color * coverage, albedo.a);
    }
    // RGB is already premultiplied in the transparent blending path.
    // Attenuate both RGB and alpha to preserve that convention.
    if(flare_mode!=0) out_color*=flare_visibility;
}
