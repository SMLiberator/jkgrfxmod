#version 330 core

// Both textures contain finished linear-light presentation frames, including
// HUD, bloom, grain, vignette, and color grading. Hardware sRGB conversion
// happens later, when the result is drawn into the default framebuffer.
uniform sampler2D current_image;
uniform sampler2D history_image;
uniform float blend_strength;

layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 current = texelFetch(current_image, pixel, 0);

    // A separate branch avoids sampling uninitialized history on the first
    // frame or when temporal blending has been explicitly disabled.
    if(blend_strength <= 0.0) {
        out_color = current;
        return;
    }

    vec4 previous = texelFetch(history_image, pixel, 0);
    out_color = mix(current, previous, clamp(blend_strength, 0.0, 1.0));
}
