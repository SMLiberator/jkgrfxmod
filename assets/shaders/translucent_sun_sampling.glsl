vec3 translucent_sun_coordinates(int slot,vec3 world)
{
    vec4 p=vec4(world,1.0);
    return vec3(dot(translucent_sun_map_x[slot],p),
                dot(translucent_sun_map_y[slot],p),
                dot(translucent_sun_map_z[slot],p));
}
bool translucent_sun_inside(vec3 uv)
{
    return all(greaterThanEqual(uv,vec3(0.0))) &&
           all(lessThanEqual(uv,vec3(1.0)));
}
float translucent_sun_sample(int slot,vec3 uv,float incidence)
{
    float grazing=1.0-clamp(incidence,0.0,1.0);
    float offset=(0.008+0.027*grazing*grazing)*
                 translucent_sun_bias_scale*
                 length(translucent_sun_map_z[slot].xyz);
    float comparison=uv.z-max(4.0/16777215.0,offset);
    if(slot==0)
        return texture(translucent_sun_near_depth,vec3(uv.xy,comparison));
    if(slot==1)
        return texture(translucent_sun_far_depth,vec3(uv.xy,comparison));
    vec4 tile=translucent_sun_tile_rect[slot-2];
    vec2 tex_size=vec2(textureSize(translucent_sun_dynamic_depth,0));
    vec2 center=tile.xy+vec2(2.0)+uv.xy*(tile.zw-vec2(4.0));
    // Fixed one-sample hardware PCF for atlas tiles, without
    // receiver-distance-dependent kernel expansion.
    return texture(translucent_sun_dynamic_depth,
                   vec3(center/tex_size,comparison));
}
