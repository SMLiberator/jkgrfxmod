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
               texture(fbuf_image,vp_bloom_pairs[0].zw))*0.227925206528;
    out_color+=(texture(fbuf_image,vp_bloom_pairs[1].xy)+
               texture(fbuf_image,vp_bloom_pairs[1].zw))*0.194047250518;
    out_color+=(texture(fbuf_image,vp_bloom_pairs[2].xy)+
               texture(fbuf_image,vp_bloom_pairs[2].zw))*0.0659578338974;
    out_color+=(texture(fbuf_image,vp_bloom_pairs[3].xy)+
               texture(fbuf_image,vp_bloom_pairs[3].zw))*0.0120697090564;
#else
    ivec2 tc = ivec2(gl_FragCoord.xy);

    out_color = texelFetch(fbuf_image, tc + blur_direction, 0);
    out_color += texelFetch(fbuf_image, tc - blur_direction, 0);
    out_color *= 0.1478352381802946;

    out_color += texelFetch(fbuf_image, tc, 0) * 0.1601799366950943;

    out_color += fetch_sym(tc, 2, 0.11622102852187252);
    out_color += fetch_sym(tc, 3, 0.07782622199643041);
    out_color += fetch_sym(tc, 4, 0.04439114149471707);
    out_color += fetch_sym(tc, 5, 0.021566692402713306);
    out_color += fetch_sym(tc, 6, 0.008924355643617059);
    out_color += fetch_sym(tc, 7, 0.003145353412807818);
#endif
#ifdef JKGM_BLOOM_HIERARCHY
    out_color.rgb*=layer_weight;
    if(coarse_enabled!=0)
        out_color.rgb+=texture(coarse_image,vp_texcoords).rgb;
#endif
}
