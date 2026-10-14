#version 330 core

uniform sampler2D fbuf_image;

const int num_bloom_layers = 5;
uniform sampler2D bloom_fbuf[num_bloom_layers];
uniform float bloom_weight[num_bloom_layers];

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

uniform float brightness = 0.02; // [-1.0 to 1.0]
uniform float contrast = 1.04;   // [0.0 to 2.0]
uniform float saturation = 1.1; // [0.0 to 2.0]

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
    float vignette = smoothstep(0.8, 0.4, length(uv));

    out_color = vec4(color*vignette, samp.a);
}
