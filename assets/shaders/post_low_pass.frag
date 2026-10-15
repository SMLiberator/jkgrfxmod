#version 330 core

uniform sampler2D fbuf_image;

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

// ---------------------------------------------------------------------------------------
// Bloom threshold tuning: decides which pixels are bright enough to glow.
//
// Each pixel contributes smoothstep(0, 1, (luminance * bloom_brightness_scale) ^ bloom_curve_power)
// of its color to the bloom. With the defaults (0.5 and 3.0):
//   luminance 1.0 -> ~4% of its color,  1.6 -> ~50%,  2.0 and above -> 100%
//
// bloom_brightness_scale: higher = more of the image blooms (0.75 gives half strength at ~1.06)
// bloom_curve_power:      higher = sharper cutoff, only the brightest pixels bloom (2.0 is gentler)
//
// Overall bloom strength is BLOOM_INTENSITY in post_to_srgb.frag.
// ---------------------------------------------------------------------------------------
const float bloom_brightness_scale = 0.5;
const float bloom_curve_power = 3.0;

void main()
{
    ivec2 sc = ivec2(gl_FragCoord.xy);
    vec4 samp = texelFetch(fbuf_image, sc, 0);

    vec3 col = samp.rgb;

    float lum = dot(vec3(0.2125, 0.7154, 0.0721), col);
    lum = max(0.0, lum * bloom_brightness_scale);

    float x = pow(lum, bloom_curve_power);
    float fac = smoothstep(0.0, 1.0, x);

    out_color = vec4(col * fac, samp.a);
}
