#version 330 core

uniform sampler2D fbuf_image;

const int num_bloom_layers = 5;
uniform sampler2D bloom_fbuf[num_bloom_layers];
uniform float bloom_weight[num_bloom_layers];

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

// =======================================================================================
// Exposure, tonemapping and bloom strength. Edit the values here and restart the game.
// With the defaults below the image is exactly what it was before these controls existed.
// =======================================================================================

// Scales the whole scene (bloom included) before tonemapping. 1.0 = unchanged.
const float EXPOSURE = 1.0;

// Bloom strength. 1.0 = unchanged, 0.0 = no bloom. (Which pixels bloom is set by the
// threshold values at the top of post_low_pass.frag.)
const float BLOOM_INTENSITY = 1.0;

// Tonemapping squeezes over-bright values into the displayable range instead of letting
// them clip to flat white.
//   0  off (what you had before)
//   1  soft shoulder: colors below TONEMAP_KNEE are left exactly as they are, brighter ones are
//      compressed towards white. Keeps hues and leaves normal scenes alone. Good first choice.
//   2  extended Reinhard on luminance: smooth rolloff everywhere, darkens midtones noticeably
//      (raise EXPOSURE to compensate)
//   3  ACES filmic curve: contrasty, desaturates bright colors, and brightens midtones unless
//      EXPOSURE is lowered (about 0.6 - 0.8 keeps midtones close to normal)
const int TONEMAP_MODE = 0;

// 0.0 - 1.0: how much of the tonemapped result to use (blends with the untouched color)
const float TONEMAP_AMOUNT = 1.0;

// Mode 1: brightness below which nothing changes. Lower = gentler, wider rolloff but affects
// more of the image; higher = affects less. Note UI and menus pass through this shader too, so
// pure white (1.0) comes out a little dimmer than the knee implies: 0.8 maps 1.0 to about 0.93.
const float TONEMAP_KNEE = 0.8;

// Mode 2: the luminance that maps to pure white.
const float TONEMAP_WHITE = 4.0;

// Tuning aids:
//   0  off
//   1  split screen: left half tonemapped, right half untouched (with a magenta divider)
//   2  show over-bright pixels: magenta = brighter than 1.0 (would clip), yellow = above the
//      knee (affected by mode 1), grey = unaffected. Shows at a glance where tonemapping matters.
const int TONEMAP_DEBUG = 0;

float tonemap_peak(vec3 c)
{
    return max(c.r, max(c.g, c.b));
}

// Linear below `knee`, then eases towards 1.0 with a matching slope at the knee (no visible kink)
float shoulder(float x, float knee)
{
    if(x <= knee) {
        return x;
    }

    float headroom = 1.0 - knee;
    return knee + headroom * (1.0 - exp(-(x - knee) / headroom));
}

vec3 aces_filmic(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 tonemap(vec3 c)
{
    if(TONEMAP_MODE == 1) {
        // Compress by the brightest channel so the ratio between channels (the hue) is kept
        float peak = tonemap_peak(c);
        return peak > 0.0 ? c * (shoulder(peak, TONEMAP_KNEE) / peak) : c;
    }
    else if(TONEMAP_MODE == 2) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float mapped = l * (1.0 + l / (TONEMAP_WHITE * TONEMAP_WHITE)) / (1.0 + l);
        return l > 0.0 ? c * (mapped / l) : c;
    }
    else if(TONEMAP_MODE == 3) {
        return aces_filmic(c);
    }

    return c;
}

// Color filter, set by the renderer from the SetColorFilter COG verb (the values below are only
// used if the renderer does not set them; the real defaults live in postfx.hpp).
uniform float brightness = 0.07; // [-1.0 to 1.0]
uniform float contrast = 1.16;   // [0.0 to 4.0]
uniform float saturation = 0.9;  // [0.0 to 4.0]

// Vignette, set by the renderer from the SetVignette COG verb.
// rgb = color the edges are pulled towards, w = strength (0 turns the vignette off).
// Black at full strength is the classic darkening vignette.
uniform vec4 vignette_params = vec4(0.0, 0.0, 0.0, 1.0);

// Film grain, set by the renderer from the SetFilmGrain COG verb.
// x = amount (0 turns grain off), y = grain size in pixels, z = frame seed (new noise per value)
uniform vec4 grain_params;

// PCG integer hash (O'Neill; see Jarzynski & Olano, "Hash Functions for GPU Rendering")
uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Uniform noise in [0, 1], one value per grain cell and seed
float grain_noise(ivec2 cell, int seed)
{
    uint h = pcg_hash(uint(cell.x) + pcg_hash(uint(cell.y) + pcg_hash(uint(seed))));
    return float(h) * (1.0 / 4294967295.0);
}

void main()
{
    ivec2 sc = ivec2(gl_FragCoord.xy);
    vec4 samp = texelFetch(fbuf_image, sc, 0);

    vec4 bloom_samp = vec4(0.0);
    bloom_samp += texture(bloom_fbuf[0], vp_texcoords) * bloom_weight[0];
    bloom_samp += texture(bloom_fbuf[1], vp_texcoords) * bloom_weight[1];
    bloom_samp += texture(bloom_fbuf[2], vp_texcoords) * bloom_weight[2];
    bloom_samp += texture(bloom_fbuf[3], vp_texcoords) * bloom_weight[3];
    bloom_samp += texture(bloom_fbuf[4], vp_texcoords) * bloom_weight[4];

    vec3 color = (samp.rgb + bloom_samp.rgb * BLOOM_INTENSITY) * EXPOSURE;

    // Tonemapping
    if(TONEMAP_DEBUG == 2) {
        float peak = tonemap_peak(color);
        vec3 shown = vec3(0.5 * min(peak, 1.0));
        if(peak > 1.0) {
            shown = vec3(1.0, 0.0, 1.0);
        }
        else if(peak > TONEMAP_KNEE) {
            shown = vec3(1.0, 1.0, 0.0);
        }

        out_color = vec4(shown, samp.a);
        return;
    }

    vec3 tonemapped = mix(color, tonemap(color), TONEMAP_AMOUNT);
    if(TONEMAP_DEBUG == 1 && vp_texcoords.x > 0.5) {
        tonemapped = color;
    }
    color = tonemapped;

    // Apply color balancing
    color += brightness;
    color = (color - 0.5) * contrast + 0.5;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation);

    // Apply tint
    //color.r = dot(color, vec3(0.494, 0.769, 0.686));
    //color.g = dot(color, vec3(0.494, 0.769, 0.686));
    //color.b = dot(color, vec3(0.494, 0.769, 0.686));
    
    // Calculate distance from center (0.5, 0.5)
    vec2 uv = vp_texcoords -0.5;
    // 0 in the centre, rising towards the edges
    float edge = 1.0 - smoothstep(0.8, 0.4, length(uv));

    // Pull the edges towards the vignette color. The color is given as the value you would see
    // on screen, so convert it to linear like the rest of the pipeline.
    if(vignette_params.w > 0.0) {
        vec3 vignette_color = pow(clamp(vignette_params.rgb, 0.0, 1.0), vec3(2.2));
        color = mix(color, vignette_color, clamp(edge * vignette_params.w, 0.0, 1.0));
    }

    // Apply film grain
    if(grain_params.x > 0.0) {
        ivec2 cell = ivec2(floor(gl_FragCoord.xy / max(grain_params.y, 1.0)));
        float n = grain_noise(cell, int(grain_params.z)) - 0.5;

        // Work in gamma space so the grain looks equally strong in dark and bright areas, and
        // ease it off towards pure black and white where it would just clip.
        vec3 g = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
        float g_luma = clamp(dot(g, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
        float response = mix(0.6, 1.0, 4.0 * g_luma * (1.0 - g_luma));

        g += n * grain_params.x * 0.5 * response;
        color = pow(max(g, vec3(0.0)), vec3(2.2));
    }

    out_color = vec4(color, samp.a);
}
