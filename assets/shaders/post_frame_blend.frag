#version 330 core

// Blend operates on linear scene-only frames: bloom/grain/grading can be
// present, but Camera and DirectDraw HUDs must NOT enter temporal history.
// Normal output relies on framebuffer sRGB conversion. Retro output uses
// this shader again, as the VERY LAST draw after both HUDs have been
// composited into a separate linear staging image. Its CMP sRGB bytes
// bypass framebuffer sRGB conversion.
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
vec3 palette_quantize(vec3 c) {
    ivec3 rgb=ivec3(floor(to_srgb(c)*63.0+0.5));
    ivec2 uv=ivec2(rgb.x+64*(rgb.y&7),rgb.z*8+(rgb.y>>3));
    // The LUT stores the original CMP RGB8 values. Keep them encoded for
    // byte-exact presentation; GL_FRAMEBUFFER_SRGB is disabled for Retro.
    return texelFetch(retro_palette_lut,uv,0).rgb;
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
    // Only the final default-framebuffer Retro pass quantizes; trails and
    // both HUD layers have been composited before it runs.
    if(retro_palette_enabled!=0)
        current.rgb=palette_quantize(current.rgb);
    out_color=current;
}
