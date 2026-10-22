#version 330 core

uniform sampler2D fbuf_image;
uniform sampler2D exposure_image; // 1x1 adapted EV (linear scene metering)
uniform int exposure_enabled;

const int num_bloom_layers = 5;
uniform sampler2D bloom_fbuf[num_bloom_layers];
uniform float bloom_weight[num_bloom_layers];

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

// Color filter, set by the renderer from the SetColorFilter COG verb (the values below are only
// used if the renderer does not set them; the real defaults live in postfx.hpp).
uniform float brightness = 0.07; // [-1.0 to 1.0]
uniform float contrast = 1.16;   // [0.0 to 4.0]
uniform float saturation = 0.9;  // [0.0 to 4.0]

// Vignette, set by the renderer from the SetVignette COG verb.
// rgb = color the edges are pulled towards, w = strength:
//   0       off
//   0 - 1   fades in the classic vignette (black rgb = the usual darkening vignette)
//   1 - 2   closes the vignette in like an iris until the whole screen is the vignette color
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

    vec3 color = samp.rgb + bloom_samp.rgb;
    if(exposure_enabled != 0) {
        // Exposure applies to HDR scene+bloom BEFORE the display transform.
        // ACES approximation rolls over highlights instead of hard clipping.
        float ev = texelFetch(exposure_image,ivec2(0),0).r;
        vec3 hdr = max(color * exp2(clamp(ev,-4.0,4.0)), vec3(0.0));
        color = (hdr * (2.51 * hdr + 0.03)) /
                (hdr * (2.43 * hdr + 0.59) + 0.14);
    }

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
    // How far the vignette has closed in: 1 = classic size, 0 = fully closed (strength 2)
    float vignette_strength = vignette_params.w;
    float iris = 1.0 - clamp(vignette_strength - 1.0, 0.0, 1.0);

    // 0 in the centre, rising towards the edges. At iris = 1 this is the same curve as the
    // original 1.0 - smoothstep(0.8, 0.4, length(uv)), written with the edges in valid order.
    float edge = smoothstep(0.4 * iris, max(0.8 * iris, 0.0001), length(uv));

    // Pull the edges towards the vignette color. The color is given as the value you would see
    // on screen, so convert it to linear like the rest of the pipeline.
    if(vignette_strength > 0.0) {
        vec3 vignette_color = pow(clamp(vignette_params.rgb, 0.0, 1.0), vec3(2.2));

        float cover = clamp(edge * min(vignette_strength, 1.0), 0.0, 1.0);
        cover = mix(cover, 1.0, step(1.0, vignette_strength - 1.0)); // strength 2: everything

        color = mix(color, vignette_color, cover);
    }

    // Apply film grain, fading it out as the vignette takes over the whole screen so a
    // fade to black ends in real black
    float grain_amount = grain_params.x * (1.0 - clamp(vignette_strength - 1.0, 0.0, 1.0));
    if(grain_amount > 0.0) {
        ivec2 cell = ivec2(floor(gl_FragCoord.xy / max(grain_params.y, 1.0)));
        float n = grain_noise(cell, int(grain_params.z)) - 0.5;

        // Work in gamma space so the grain looks equally strong in dark and bright areas, and
        // ease it off towards pure black and white where it would just clip.
        vec3 g = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
        float g_luma = clamp(dot(g, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
        float response = mix(0.6, 1.0, 4.0 * g_luma * (1.0 - g_luma));

        g += n * grain_amount * 0.5 * response;
        color = pow(max(g, vec3(0.0)), vec3(2.2));
    }

    out_color = vec4(color, samp.a);
}
