#version 330 core

uniform sampler2D fbuf_image;
uniform ivec2 blur_direction;

layout(location = 0) out vec4 out_color;
#ifdef JKGM_BLOOM_HIERARCHY
uniform sampler2D coarse_image;
uniform int coarse_enabled;
uniform float layer_weight;
#endif
#if defined(JKGM_BLOOM_HIERARCHY) || defined(JKGM_BLOOM_BILINEAR)
in vec2 vp_texcoords;
#endif
#ifdef JKGM_BLOOM_BILINEAR
in vec4 vp_bloom_pairs[4];
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
    out_color=(texture(fbuf_image,vp_bloom_pairs[0].xy)+
               texture(fbuf_image,vp_bloom_pairs[0].zw))*0.403041000000;
    out_color+=(texture(fbuf_image,vp_bloom_pairs[1].xy)+
               texture(fbuf_image,vp_bloom_pairs[1].zw))*0.0969590000000;
#else
    ivec2 tc = ivec2(gl_FragCoord.xy);

    out_color = texelFetch(fbuf_image, tc + blur_direction, 0);
    out_color += texelFetch(fbuf_image, tc - blur_direction, 0);
    out_color *= 0.235836;

    out_color += texelFetch(fbuf_image, tc, 0) * 0.33441;

    out_color += fetch_sym(tc, 2, 0.082624);
    out_color += fetch_sym(tc, 3, 0.014335);
#endif
#ifdef JKGM_BLOOM_HIERARCHY
    out_color.rgb*=layer_weight;
    if(coarse_enabled!=0)
        out_color.rgb+=texture(coarse_image,vp_texcoords).rgb;
#endif
}