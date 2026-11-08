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
// Keep the dynamic Thing atlas on its original one-comparison path. Its
// packed guard regions are not designed for extended PCF footprints.
float translucent_sun_cascade_filter(sampler2DShadow map,vec3 uv,float radius)
{
    if(radius<=0.0)return texture(map,uv);
    vec2 spread=(radius/vec2(textureSize(map,0)))*0.5;
    float sum=0.0;
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=vec2(x==0?-spread.x:spread.x,
                        y==0?-spread.y:spread.y);
        sum+=texture(map,vec3(uv.xy+delta,uv.z));
    }
    return sum*0.25;
}

float translucent_sun_sample(int slot,vec3 uv,float incidence)
{
    float grazing=1.0-clamp(incidence,0.0,1.0);
    float offset=(0.008+0.027*grazing*grazing)*
                 translucent_sun_bias_scale*
                 length(translucent_sun_map_z[slot].xyz);
    float comparison=uv.z-max(4.0/16777215.0,offset);
    if(slot==0)
        return translucent_sun_cascade_filter(
            translucent_sun_near_depth,vec3(uv.xy,comparison),
            translucent_sun_pcf_radius.x);
    if(slot==1)
        return translucent_sun_cascade_filter(
            translucent_sun_far_depth,vec3(uv.xy,comparison),
            translucent_sun_pcf_radius.y);
    vec4 tile=translucent_sun_tile_rect[slot-2];
    vec2 tex_size=vec2(textureSize(translucent_sun_dynamic_depth,0));
    vec2 center=tile.xy+vec2(2.0)+uv.xy*(tile.zw-vec2(4.0));
    // Fixed one-sample hardware PCF for atlas tiles, without
    // receiver-distance-dependent kernel expansion.
    return texture(translucent_sun_dynamic_depth,
                   vec3(center/tex_size,comparison));
}
