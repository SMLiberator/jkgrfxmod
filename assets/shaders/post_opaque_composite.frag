#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
uniform sampler2D normal_image;
uniform sampler2D depth_image;
uniform sampler2D raw_occlusion_image;
// 0=normal render, 1=normals, 2=depth, 3=blurred AO, 4=raw AO.
uniform int debug_mode;

layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 tc = ivec2(gl_FragCoord.xy);

    if(debug_mode != 0) {
        float depth = texelFetch(depth_image, tc, 0).r;
        // An empty depth pixel is 65504, the maximum finite R16F value.
        bool geometry = depth > 0.0 && depth < 65000.0;

        if(debug_mode == 1) {
            // RGB = XYZ of the exact view-space normal supplied to SSAO.
            vec3 n = texelFetch(normal_image, tc, 0).rgb;
            out_color = vec4(geometry && dot(n,n) > 0.0001
                                 ? normalize(n) * 0.5 + 0.5
                                 : vec3(1.0, 0.0, 1.0), 1.0);
        }
        else if(debug_mode == 2) {
            // Bright = far; magenta = missing geometry. Distances >=128 saturate.
            out_color = vec4(geometry ? vec3(clamp(depth / 128.0, 0.0, 1.0))
                                      : vec3(1.0, 0.0, 1.0), 1.0);
        }
        else {
            // White means more occlusion. Compare before vs after the box filter.
            float ao = debug_mode == 4 ? texelFetch(raw_occlusion_image, tc, 0).r
                                       : texelFetch(occlusion_image, tc, 0).r;
            out_color = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
        }
        return;
    }

    vec4 albedo = texelFetch(color_image, tc, 0);
    vec4 emissive = texelFetch(emissive_image, tc, 0);
    float occlusion = 1.0 - texelFetch(occlusion_image, tc, 0).r;

    out_color = vec4(emissive.rgb + (albedo.rgb * occlusion), albedo.a);
}