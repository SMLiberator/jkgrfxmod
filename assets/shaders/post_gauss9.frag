#version 330 core

uniform sampler2D fbuf_image;
uniform ivec2 blur_direction;

layout(location = 0) out vec4 out_color;
#ifdef JKGM_BLOOM_HIERARCHY
uniform sampler2D coarse_image;
uniform int coarse_enabled;
uniform float layer_weight;
in vec2 vp_texcoords;
#endif

vec4 fetch_sym(ivec2 tc, int radius, float fac)
{
    vec4 rv = texelFetch(fbuf_image, tc + radius * blur_direction, 0);
    rv += texelFetch(fbuf_image, tc - radius * blur_direction, 0);
    return rv * fac;
}

void main()
{
#ifdef JKGM_BLOOM_BILINEAR
    // Adjacent weighted taps combined by linear texture filtering.
    // Texture state clamps edge samples rather than fetching out of bounds.
    vec2 inv_dims=1.0/vec2(textureSize(fbuf_image,0));
    vec2 uv=gl_FragCoord.xy*inv_dims;
    vec2 step_uv=vec2(blur_direction)*inv_dims;
    out_color=texture(fbuf_image,uv)*0.289141483254;
    out_color+=(texture(fbuf_image,uv+step_uv*1.31291339212)+
                texture(fbuf_image,uv-step_uv*1.31291339212))*0.323807747195;
    out_color+=(texture(fbuf_image,uv+step_uv*3.13716660760)+
                texture(fbuf_image,uv-step_uv*3.13716660760))*0.0316215111776;
#else
    ivec2 tc = ivec2(gl_FragCoord.xy);

    out_color = texelFetch(fbuf_image, tc + blur_direction, 0);
    out_color += texelFetch(fbuf_image, tc - blur_direction, 0);
    out_color *= 0.22248396662566527;

    out_color += texelFetch(fbuf_image, tc, 0) * 0.289141483254238;

    out_color += fetch_sym(tc, 2, 0.10132378056964125);
    out_color += fetch_sym(tc, 3, 0.02728409576222039);
    out_color += fetch_sym(tc, 4, 0.004337415415354041);
#endif
#ifdef JKGM_BLOOM_HIERARCHY
    out_color.rgb*=layer_weight;
    if(coarse_enabled!=0)
        out_color.rgb+=texture(coarse_image,vp_texcoords).rgb;
#endif
}