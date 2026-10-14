#version 330 core

uniform sampler2D normal_image;
uniform sampler2D depth_image;
uniform sampler2D noise_image;

const int num_kernel_samples = 16;
uniform vec3[num_kernel_samples] samples;

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

const float ssao_power = 2.2;

const float kernel_size = 3.0;
const float kernel_thickness = 1.0;
const float kernel_cutoff = 1.0;
const float kernel_bias = 0.25;

// SSAO distance fade
const float ssao_fade_start = 128.0;
const float ssao_fade_end = 256.0;

float sample_depth_direct(vec2 pos)
{
    float depth_samp = texture(depth_image, pos).r;
    return depth_samp;

    const float z_near = 0.1;
    const float z_far = 100.0;

    float z_n = 2.0 * depth_samp;
    float z_e = (2.0 * z_near * z_far) /
        (z_far + z_near - z_n * (z_far - z_near));

    return z_e;
}

// Returns 1 if the sample is occluded, 0 otherwise
float sample_depth(vec3 origin, vec3 offset)
{
    float depth_radius = kernel_size / max(32.0, origin.z);

    vec2 sample_tc = origin.xy + (offset.xy * depth_radius);
    float sample_z = origin.z + (offset.z * kernel_thickness);

    float occ_depth = sample_depth_direct(sample_tc);

    float range_check =
        smoothstep(0.0, 1.0,
                   kernel_cutoff / abs(origin.z - occ_depth));

    return float(occ_depth < (sample_z - kernel_bias)) * range_check;
}

void main()
{
    vec3 rndnrm = normalize(texture(normal_image, vp_texcoords).rgb);
    float origin_depth = texture(depth_image, vp_texcoords).r;

    // No SSAO beyond the fade distance.
    if (origin_depth >= ssao_fade_end) {
        out_color = vec4(0.0);
        return;
    }

    vec3 rndvec = texture(noise_image, gl_FragCoord.xy / 4.0).xyz;

    vec3 tangent =
        normalize(rndvec - rndnrm * dot(rndvec, rndnrm));
    vec3 bitangent = cross(rndnrm, tangent);
    mat3 basis = mat3(tangent, bitangent, rndnrm);

    vec3 origin = vec3(vp_texcoords, origin_depth);

    float num_fail_samps = 0.0;

    for (int i = 0; i < num_kernel_samples; ++i) {
        num_fail_samps += sample_depth(origin, basis * samples[i]);
    }

    float fail_frac = num_fail_samps / num_kernel_samples;
    float occlusion = pow(1.0 - fail_frac, ssao_power);

    // Fade SSAO smoothly from full strength to zero.
    float distance_fade =
        1.0 - smoothstep(ssao_fade_start,
                         ssao_fade_end,
                         origin_depth);

    float ssao_amount = (1.0 - occlusion) * distance_fade;

    out_color = vec4(ssao_amount, 0.0, 0.0, 0.0);
}
