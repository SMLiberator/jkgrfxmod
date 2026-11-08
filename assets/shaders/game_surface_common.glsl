// Identical native game-pass reflection, tangent-space and dither helpers.
// Included after all material/camera uniforms in both game fragment passes.
// Keep the two shaders' intentionally different composition paths separate.

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

// 4x4 Bayer ordered screen-door coverage. Kept samples become fully
// opaque and write ordinary depth/SSAO; no blend or per-face sort needed.
float dither_threshold()
{
    ivec2 p=ivec2(gl_FragCoord.xy) & ivec2(3);
    int lo=(((p.x^p.y)&1)<<1)|(p.y&1);
    int hi=((((p.x>>1)^(p.y>>1))&1)<<1)|((p.y>>1)&1);
    return (float(4*lo+hi)+0.5)/16.0;
}
