// Reusable tile-safe 4-tap PCF for independently packed moving Things.
// Each shadow tile reserves a 2px cleared-depth guard. Restrict all
// additional bilinear comparisons to that tile to prevent neighboring
// Things from producing false occlusion. The radius is in atlas texels.
float sun_dynamic_tile_pcf(sampler2DShadow depth_map,vec4 tile,
                           vec3 shadow_uv,float comparison,float radius)
{
    vec2 atlas_size=vec2(textureSize(depth_map,0));
    vec2 center=tile.xy+vec2(2.0)+shadow_uv.xy*(tile.zw-vec2(4.0));
    // Exact original one-lookup path when the experiment is disabled.
    if(radius<=0.0)
        return texture(depth_map,vec3(center/atlas_size,comparison));

    // A maximum two-texel radius and the two-pixel cleared guard protect
    // the neighbors even after hardware 2x2 bilinear PCF footprint.
    float offset=min(radius,2.0)*0.5;
    vec2 low=tile.xy+vec2(1.5);
    vec2 high=tile.xy+tile.zw-vec2(1.5);
    float visibility=0.0;
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=vec2(x==0?-offset:offset,
                        y==0?-offset:offset);
        vec2 sample_pixel=clamp(center+delta,low,high);
        visibility+=texture(depth_map,
            vec3(sample_pixel/atlas_size,comparison));
    }
    return visibility*0.25;
}
