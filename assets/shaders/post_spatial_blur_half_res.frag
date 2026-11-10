#version 330 core
// EXPERIMENTAL HALF-RES HORIZONTAL BLUR. Default shader is unchanged.
// Full-res color grading, tone mapping, and HUD never use this shader.

uniform sampler2D source_image;
uniform sampler2D depth_image; // Linear depth in JK world units; empty = 65504 (R16F).
uniform int blur_axis;         // 0 = horizontal, 1 = vertical.
uniform vec2 screen_size; // Full-resolution scene size for blur radius/aspect.
uniform vec2 output_size; // Current destination: half-res horizontal, full-res vertical.
uniform sampler2D original_image; // Full-resolution unblurred scene, never modified.
uniform int restore_original; // 1 only on mode-1 full-resolution vertical pass.

// xyz = near, far, intensity in [0,1].
// xyz = inner radius, outer radius, intensity in [0,1].
uniform vec4 distance_params;
uniform vec4 peripheral_params;

layout(location = 0) out vec4 out_color;

const float weights[5] = float[5](
    0.2270270270, 0.1945945946, 0.1216216216,
    0.0540540541, 0.0162162162
);

bool valid_depth(float depth)
{
    return depth > 0.0 && depth < 65000.0;
}

// R16F depth clears to 65504. Treat untouched background/sky as the
// farthest blur zone, not as a sharp strip beside distant geometry.
// This never makes a nearby foreground pixel blur: its own depth remains
// below the blur thresholds and distant neighbors fail the depth test.
float blur_depth(float raw_depth)
{
    return valid_depth(raw_depth) ? raw_depth : distance_params.y;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / output_size;
    // The horizontal pass filters into a half-size target. The vertical
    // pass samples that target BUT restores untouched pixels from the
    // original full-resolution image, preserving sharp no-blur regions.
    vec4 source = restore_original!=0
        ?texelFetch(original_image,pixel,0)
        :texture(source_image,uv);

    // Always sample the original full-resolution depth at the same UV.
    // The depth texture uses nearest filtering to retain object boundaries.
    float depth = blur_depth(texture(depth_image,uv).r);
    float distant = clamp(distance_params.z, 0.0, 1.0) *
        smoothstep(distance_params.x, distance_params.y, depth);

    // Radius is normalized to the physical screen corner, not UV distance,
    // so the blur region stays approximately circular across aspect ratios.
    float aspect = screen_size.x / screen_size.y;
    vec2 centered = (uv - vec2(0.5)) * vec2(aspect, 1.0);
    float corner_radius = 0.5 * length(vec2(aspect, 1.0));
    float radius = length(centered) / max(corner_radius, 0.0001);
    float peripheral = clamp(peripheral_params.z, 0.0, 1.0) *
        smoothstep(peripheral_params.x, peripheral_params.y, radius);

    float amount = max(distant, peripheral);
    if(amount < 0.001) {
        out_color = source;
        return;
    }

    // Separable 9-tap Gaussian at full-resolution UV radii; horizontal
    // runs at 1/4 pixel count and vertical still covers the original image.
    // Each axis reaches up to 12 screen pixels at
    // strength 1. Gradually scale kernel radius, rather than crossfade an
    // always-strong blur, to avoid a jump when fading the effect in.
    vec2 direction = blur_axis == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 step_uv = direction * (3.0 * amount) / screen_size;

    vec4 sum = source * weights[0];
    float sum_weights = weights[0];

    // Preserve foreground silhouettes when blur differs, but do not
    // constrain *fully blurred* distant surfaces by their original depth.
    // Beyond distance_params.y, 700 and 1000 must blur into each other
    // just as two surfaces both at depth 700 would. Peripheral blur can
    // still intentionally cross foreground edges.
    float distance_fraction = distant / max(amount, 0.0001);
    for(int i = 1; i <= 4; ++i) {
        vec2 offset = step_uv * float(i);
        for(int sign_idx = 0; sign_idx < 2; ++sign_idx) {
            vec2 sample_uv = clamp(uv + (sign_idx == 0 ? offset : -offset),
                                   vec2(0.0), vec2(1.0));
            float depth_weight = 1.0;
            if(distance_fraction > 0.0) {
                float sampled_depth = blur_depth(texture(depth_image, sample_uv).r);
                float similar;
                if(depth >= distance_params.y &&
                   sampled_depth >= distance_params.y) {
                    // Both pixels have reached the same maximum blur zone.
                    // Do not preserve an artificial sharp distant edge.
                    similar = 1.0;
                }
                else {
                    // At near/far transitions keep the existing bilateral
                    // rejection to prevent sharp foreground objects from
                    // bleeding into the background (and vice versa).
                    float tolerance = 0.5 + depth * 0.03;
                    similar = exp(-abs(sampled_depth - depth) / tolerance);
                }
                depth_weight = mix(1.0, similar, distance_fraction);
            }
            float w = weights[i] * depth_weight;
            sum += texture(source_image, sample_uv) * w;
            sum_weights += w;
        }
    }

    out_color = sum / max(sum_weights, 0.00001);
}
