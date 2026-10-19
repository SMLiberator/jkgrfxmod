#version 330 core

// Both textures contain finished linear-light presentation frames, including
// HUD, bloom, grain, vignette, and color grading. Hardware sRGB conversion
// happens later, when the result is drawn into the default framebuffer.
uniform sampler2D current_image;
uniform sampler2D history_image;
uniform float blend_strength;
// Retro LUT: eight-by-eight 64x64 RGB slices in a 512x512 RGBA8 atlas,
// containing only the 256 RGB colors from the level's first colormap.
uniform sampler2D retro_palette_lut;
uniform int retro_palette_enabled;
vec3 to_srgb(vec3 x) {
    x=clamp(x,0.0,1.0);
    return mix(x*12.92,1.055*pow(x,vec3(1.0/2.4))-0.055,
               step(vec3(0.0031308),x));
}
vec3 to_linear(vec3 x) {
    return mix(x/12.92,pow((x+0.055)/1.055,vec3(2.4)),
               step(vec3(0.04045),x));
}
vec3 palette_quantize(vec3 c) {
    ivec3 rgb=ivec3(floor(to_srgb(c)*63.0+0.5));
    ivec2 uv=ivec2(rgb.x+64*(rgb.y&7),rgb.z*8+(rgb.y>>3));
    return to_linear(texelFetch(retro_palette_lut,uv,0).rgb);
}

layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 current = texelFetch(current_image, pixel, 0);

    if(blend_strength>0.0) {
        vec4 previous=texelFetch(history_image,pixel,0);
        current=mix(current,previous,clamp(blend_strength,0.0,1.0));
    }
    // Only the default-framebuffer pass quantizes; trails accumulate before
    // this, so they cannot generate any intermediate display colors.
    if(retro_palette_enabled!=0)
        current.rgb=palette_quantize(current.rgb);
    out_color=current;
}
