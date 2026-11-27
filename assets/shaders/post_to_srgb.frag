#version 330 core

uniform sampler2D fbuf_image;
uniform sampler2D exposure_image; // 1x1 adapted EV (linear scene metering)
uniform int exposure_enabled;
uniform int hdr_enabled; // HDR display transform independent of adaptation
uniform float hdr_exposure_compensation; // fixed EV offset
uniform float hdr_shadow_softness; // 0 original ACES toe, 0.6 current softened toe
uniform float hdr_color_crispness; // 0 hue-preserving, 1 old per-channel ACES

const int num_bloom_layers = 5;
uniform sampler2D bloom_fbuf[num_bloom_layers];
uniform float bloom_weight[num_bloom_layers];

in vec2 vp_texcoords;
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
flat in float vp_exposure_gain;
flat in vec3 vp_vignette_color;
flat in float vp_grain_amount;
flat in uint vp_grain_seed_hash;
#endif

layout(location = 0) out vec4 out_color;

// Color filter, set by the renderer from the SetColorFilter COG verb (the values below are only
// used if the renderer does not set them; the real defaults live in postfx.hpp).
uniform float brightness = 0.0; // [-1.0 to 1.0], 0 = no change
uniform float contrast = 1.0;   // [0.0 to 4.0], 1 = no change
uniform float saturation = 1.0; // [0.0 to 4.0], 1 = no change

// SetRGBSplit(factor, fadeTime): radial red/blue displacement in pixels
// at the viewport corners, clamped to 0..32. Zero is completely free:
// no extra scene samples, and exactly the same center pixel as before.
uniform float rgb_split_pixels = 0.0;

// Vignette, set by the renderer from the SetVignette COG verb.
// rgb = color the edges are pulled towards, w = strength:
//   0       off
//   0 - 1   fades in the classic vignette (black rgb = the usual darkening vignette)
//   1 - 2   closes the vignette in like an iris until the whole screen is the vignette color
uniform vec4 vignette_params = vec4(0.0, 0.0, 0.0, 0.0);

// Film grain, set by the renderer from the SetFilmGrain COG verb.
// x = amount (0 turns grain off), y = grain size in pixels, z = frame seed (new noise per value)
uniform vec4 grain_params;
#ifdef JKGM_GRAIN_ATLAS
uniform sampler2D grain_noise_image;
#endif

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
#ifdef JKGM_GRAIN_ATLAS
    // The existing 10-bit seed selects one of 16 tiles and 64 offsets.
    // Wrap WITHIN the selected tile to avoid sampling adjacent patterns.
    int tile=seed&15;
    int phase=seed>>4;
    ivec2 offset=ivec2(phase*37,phase*101);
    ivec2 local_cell=(cell+offset)&ivec2(255);
    ivec2 origin=ivec2(tile&3,tile>>2)*256;
    return texelFetch(grain_noise_image,origin+local_cell,0).r;
#else
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
    uint h = pcg_hash(uint(cell.x) + pcg_hash(uint(cell.y) + vp_grain_seed_hash));
#else
    uint h = pcg_hash(uint(cell.x) + pcg_hash(uint(cell.y) + pcg_hash(uint(seed))));
#endif
    return float(h) * (1.0 / 4294967295.0);
#endif
}


float filmic_luminance(float x)
{
    // Use the existing ACES fit for midtones/highlights but soften its
    // low-light toe. Zero always remains zero (no artificial ambient light).
    float aces = (x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14);
    float soft_toe = x/(1.0+x);
    // Adjustable low-light toe: 0 restores the original sharper ACES
    // shadow curve, while 0.6 retains the current softened appearance.
    // Keep hue-preserving luminance mapping and highlight gamut handling.
    float toe_weight = clamp(hdr_shadow_softness,0.0,1.0) *
                       (1.0-smoothstep(0.015,0.10,x));
    return clamp(mix(aces,soft_toe,toe_weight),0.0,1.0);
}

#ifdef JKGM_HDR_OPTIMIZED
// Same filmic curve and toe as the scalar reference. Uniform endpoints
// can skip an entire mapping style or the soft toe without pixel divergence.
vec4 filmic_luminance4(vec4 x)
{
    vec4 aces=(x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14);
    float softness=clamp(hdr_shadow_softness,0.0,1.0);
    if(softness>0.0) {
        vec4 soft_toe=x/(1.0+x);
        vec4 toe_weight=softness*(1.0-smoothstep(vec4(0.015),vec4(0.10),x));
        aces=mix(aces,soft_toe,toe_weight);
    }
    return clamp(aces,0.0,1.0);
}

vec3 hue_preserving_hdr(vec3 hdr,float lum,float mapped_lum)
{
    // Gamut compression and highlight desaturation are both lerps
    // toward the SAME luminance. Combine their scales, not their curves.
    vec3 mapped=hdr*(mapped_lum/max(lum,0.00001));
    float peak=max(max(mapped.r,mapped.g),mapped.b);
    float chroma_scale=min(1.0,
        (1.0-mapped_lum)/max(peak-mapped_lum,0.00001));
    float highlight_fade=0.08*smoothstep(0.7,2.5,lum);
    return vec3(mapped_lum)+(mapped-vec3(mapped_lum))*
        (chroma_scale*(1.0-highlight_fade));
}

vec3 optimized_hdr_tonemap(vec3 hdr)
{
    float crispness=clamp(hdr_color_crispness,0.0,1.0);
    if(crispness>=1.0)
        return filmic_luminance4(vec4(hdr,0.0)).rgb;

    float lum=dot(hdr,vec3(0.2126,0.7152,0.0722));
    if(crispness<=0.0)
        return hue_preserving_hdr(hdr,lum,filmic_luminance4(vec4(lum)).r);

    vec4 mapped=filmic_luminance4(vec4(hdr,lum));
    return mix(hue_preserving_hdr(hdr,lum,mapped.a),mapped.rgb,crispness);
}
#endif

void main()
{
    ivec2 sc = ivec2(gl_FragCoord.xy);
    vec4 samp = texelFetch(fbuf_image, sc, 0);

    if(rgb_split_pixels > 0.0) {
        // In native screen pixels rather than UV units, so horizontal and
        // vertical channel offsets have the same length on wide screens.
        // The center is stationary, with the requested displacement at
        // each corner. Clamp sampling to avoid black/colored edge borders.
        vec2 dims=vec2(textureSize(fbuf_image,0));
        vec2 half_dims=0.5*dims;
        vec2 from_center=(vec2(sc)+vec2(0.5))-half_dims;
        vec2 shift_px=from_center/max(length(half_dims),1.0)*
                      min(rgb_split_pixels,32.0);
        vec2 center_uv=(vec2(sc)+vec2(0.5))/dims;
        vec2 shift_uv=shift_px/dims;
        samp.r=texture(fbuf_image,
                       clamp(center_uv+shift_uv,vec2(0.0),vec2(1.0))).r;
        samp.b=texture(fbuf_image,
                       clamp(center_uv-shift_uv,vec2(0.0),vec2(1.0))).b;
        // The existing center sample supplies green and alpha. Bloom stays
        // on its existing single-sample path (no additional fullscreen pass
        // or ten extra bloom reads when this effect is enabled).
    }

    vec4 bloom_samp = vec4(0.0);
    // Each weight is uniform across the frame. Do not issue expensive
    // texture fetches for disabled bloom layers (especially the basic
    // compositor, which has no bloom images at all).
    if(bloom_weight[0] > 0.0) bloom_samp += texture(bloom_fbuf[0],vp_texcoords)*bloom_weight[0];
    if(bloom_weight[1] > 0.0) bloom_samp += texture(bloom_fbuf[1],vp_texcoords)*bloom_weight[1];
    if(bloom_weight[2] > 0.0) bloom_samp += texture(bloom_fbuf[2],vp_texcoords)*bloom_weight[2];
    if(bloom_weight[3] > 0.0) bloom_samp += texture(bloom_fbuf[3],vp_texcoords)*bloom_weight[3];
    if(bloom_weight[4] > 0.0) bloom_samp += texture(bloom_fbuf[4],vp_texcoords)*bloom_weight[4];

    vec3 color = samp.rgb + bloom_samp.rgb;
    if(hdr_enabled != 0) {
        // Fixed EV compensation works even when eye adaptation is disabled.
        // All exposure happens in linear HDR before tone mapping.
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
        vec3 hdr = max(color*vp_exposure_gain,vec3(0.0));
#else
        float ev = hdr_exposure_compensation;
        if(exposure_enabled != 0)
            ev += texelFetch(exposure_image,ivec2(0),0).r;
        vec3 hdr = max(color*exp2(clamp(ev,-5.0,5.0)),vec3(0.0));
#endif

#ifdef JKGM_HDR_OPTIMIZED
        color=optimized_hdr_tonemap(hdr);
#else
        // Map luminance rather than R/G/B independently to retain hue.
        // Compress any out-of-gamut chroma toward mapped luminance instead
        // of clipping the strongest RGB channel into an oversaturated color.
        float lum = dot(hdr,vec3(0.2126,0.7152,0.0722));
        float mapped_lum = filmic_luminance(lum);
        vec3 mapped = hdr*(mapped_lum/max(lum,0.00001));
        float peak = max(max(mapped.r,mapped.g),mapped.b);
        float chroma_scale = min(1.0,
            (1.0-mapped_lum)/max(peak-mapped_lum,0.00001));
        mapped = vec3(mapped_lum)+(mapped-vec3(mapped_lum))*chroma_scale;

        // Modest additional highlight desaturation; leave dim materials
        // untouched, and preserve actual black.
        float highlight_fade = 0.08*smoothstep(0.7,2.5,lum);
        vec3 consistent_color=mix(mapped,vec3(mapped_lum),highlight_fade);
        color=consistent_color;
        if(hdr_color_crispness>0.0) {
            // Older per-RGB-channel ACES was punchier but could shift hues.
            // Keep SHADOW toe adjustment identical in both styles, so
            // hdr_shadow_softness and hdr_color_crispness stay independent.
            vec3 crisp_color=vec3(filmic_luminance(hdr.r),
                                 filmic_luminance(hdr.g),
                                 filmic_luminance(hdr.b));
            color=mix(consistent_color,crisp_color,
                      clamp(hdr_color_crispness,0.0,1.0));
        }
#endif
    }

    // Apply color balancing
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
    // Uniform conditions: no per-pixel divergence. Preserve the original
    // arithmetic whenever a grade is actually requested.
    if(brightness!=0.0 || contrast!=1.0 || saturation!=1.0) {
#endif
        color += brightness;
        color = (color - 0.5) * contrast + 0.5;
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luma), color, saturation);
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
    }
#endif

    // Apply tint
    //color.r = dot(color, vec3(0.494, 0.769, 0.686));
    //color.g = dot(color, vec3(0.494, 0.769, 0.686));
    //color.b = dot(color, vec3(0.494, 0.769, 0.686));
    
    float vignette_strength = vignette_params.w;
    // Vignette off is the common case: bypass sqrt/smoothstep/pow entirely.
    // Keep the exact original curve when the vignette is enabled.
    if(vignette_strength > 0.0) {
        vec2 uv = vp_texcoords - 0.5;
        float iris = 1.0 - clamp(vignette_strength - 1.0, 0.0, 1.0);
        float edge = smoothstep(0.4 * iris, max(0.8 * iris, 0.0001), length(uv));
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
        vec3 vignette_color = vp_vignette_color;
#else
        vec3 vignette_color = pow(clamp(vignette_params.rgb, 0.0, 1.0), vec3(2.2));
#endif
        float cover = clamp(edge * min(vignette_strength, 1.0), 0.0, 1.0);
        cover = mix(cover, 1.0, step(1.0, vignette_strength - 1.0));
        color = mix(color, vignette_color, cover);
    }

    // Apply film grain, fading it out as the vignette takes over the whole screen so a
    // fade to black ends in real black
#ifdef JKGM_POSTFX_VERTEX_CONSTANTS
    float grain_amount = vp_grain_amount;
#else
    float grain_amount = grain_params.x * (1.0 - clamp(vignette_strength - 1.0, 0.0, 1.0));
#endif
    if(grain_amount > 0.0) {
        ivec2 cell = ivec2(floor(gl_FragCoord.xy / max(grain_params.y, 1.0)));
        float n = grain_noise(cell, int(grain_params.z)) - 0.5;

        // Work in gamma space so the grain looks equally strong in dark and bright areas, and
        // ease it off towards pure black and white where it would just clip.
#ifdef JKGM_GRAIN_GAMMA_APPROX
        // Gamma 2.0 approximates the existing 2.2 perceptual curve.
        // An exact square-root/square round trip avoids generic powers.
        vec3 g = sqrt(max(color,vec3(0.0)));
#else
        vec3 g = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
#endif
        float g_luma = clamp(dot(g, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
        float response = mix(0.6, 1.0, 4.0 * g_luma * (1.0 - g_luma));

        g += n * grain_amount * 0.5 * response;
#ifdef JKGM_GRAIN_GAMMA_APPROX
        g=max(g,vec3(0.0));
        color=g*g;
#else
        color = pow(max(g, vec3(0.0)), vec3(2.2));
#endif
    }

    out_color = vec4(color, samp.a);
}
