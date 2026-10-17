#version 330 core

uniform sampler2D source_image;
uniform sampler2D depth_image; // Linear depth in JK world units; empty = 65504 (R16F).
uniform int blur_axis;         // 0 = horizontal, 1 = vertical.
uniform vec2 screen_size;

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

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / screen_size;
    vec4 source = texelFetch(source_image, pixel, 0);

    float depth = texelFetch(depth_image, pixel, 0).r;
    float distant = valid_depth(depth)
        ? clamp(distance_params.z, 0.0, 1.0) *
          smoothstep(distance_params.x, distance_params.y, depth)
        : 0.0;

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

    // Separable 9-tap Gaussian: each axis reaches up to 12 screen pixels at
    // strength 1. Gradually scale kernel radius, rather than crossfade an
    // always-strong blur, to avoid a jump when fading the effect in.
    vec2 direction = blur_axis == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 step_uv = direction * (3.0 * amount) / screen_size;

    vec4 sum = source * weights[0];
    float sum_weights = weights[0];

    // At hard depth boundaries, favor pixels near the same surface when the
    // distance effect is responsible. Peripheral blur may intentionally blur
    // across silhouettes, so it is not fully depth-constrained.
    float distance_fraction = distant / max(amount, 0.0001);
    for(int i = 1; i <= 4; ++i) {
        vec2 offset = step_uv * float(i);
        for(int sign_idx = 0; sign_idx < 2; ++sign_idx) {
            vec2 sample_uv = clamp(uv + (sign_idx == 0 ? offset : -offset),
                                   vec2(0.0), vec2(1.0));
            float depth_weight = 1.0;
            if(distance_fraction > 0.0) {
                float sampled_depth = texture(depth_image, sample_uv).r;
                float tolerance = 0.5 + depth * 0.03;
                float similar = valid_depth(sampled_depth)
                    ? exp(-abs(sampled_depth - depth) / tolerance) : 0.0;
                depth_weight = mix(1.0, similar, distance_fraction);
            }
            float w = weights[i] * depth_weight;
            sum += texture(source_image, sample_uv) * w;
            sum_weights += w;
        }
    }

    out_color = sum / max(sum_weights, 0.00001);
}
