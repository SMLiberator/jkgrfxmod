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

void main()
{
#ifdef JKGM_BLOOM_BILINEAR
    // Adjacent weighted taps combined by linear texture filtering.
    // Texture state clamps edge samples rather than fetching out of bounds.
    out_color=(texture(fbuf_image,vp_bloom_pairs[0].xy)+
               texture(fbuf_image,vp_bloom_pairs[0].zw))*0.500000000000;
#else
    ivec2 tc = ivec2(gl_FragCoord.xy);

    out_color = texelFetch(fbuf_image, tc + blur_direction, 0);
    out_color += texelFetch(fbuf_image, tc - blur_direction, 0);
    out_color *= 0.157731;

    out_color += texelFetch(fbuf_image, tc, 0) * 0.684538;
#endif
#ifdef JKGM_BLOOM_HIERARCHY
    out_color.rgb*=layer_weight;
    if(coarse_enabled!=0)
        out_color.rgb+=texture(coarse_image,vp_texcoords).rgb;
#endif
}