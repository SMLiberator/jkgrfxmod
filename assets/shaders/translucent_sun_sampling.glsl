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
float translucent_sun_cascade_filter(
    sampler2DShadow map,vec3 uv,float radius,float inv_range)
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
    float inv_range=length(translucent_sun_map_z[slot].xyz);
    float offset=(0.008+0.027*grazing*grazing)*
                 translucent_sun_bias_scale*inv_range;
    float comparison=uv.z-max(4.0/16777215.0,offset);
    if(slot==0)
        return translucent_sun_cascade_filter(
            translucent_sun_near_depth,vec3(uv.xy,comparison),
            translucent_sun_pcf_radius.x,inv_range);
    if(slot==1)
        return translucent_sun_cascade_filter(
            translucent_sun_far_depth,vec3(uv.xy,comparison),
            translucent_sun_pcf_radius.y,inv_range);
    vec4 tile=translucent_sun_tile_rect[slot-2];
    vec2 tex_size=vec2(textureSize(translucent_sun_dynamic_depth,0));
    vec2 center=tile.xy+vec2(2.0)+uv.xy*(tile.zw-vec2(4.0));
    // Fixed one-sample hardware PCF for atlas tiles, without
    // receiver-distance-dependent kernel expansion.
    return texture(translucent_sun_dynamic_depth,
                   vec3(center/tex_size,comparison));
}

// Sky apertures are a SEPARATE gate on added directional lighting, not a
// replacement shadow comparison (darkening the baked JK ambient is wrong).
#ifdef JKGM_SUN_SKY_APERTURE
float translucent_sun_sky_sample(vec3 uv,int layer)
{
    if(!translucent_sun_inside(uv))return 0.0;
    vec2 tr=layer==0
        ?translucent_sun_sky_aperture_depth_transform.xy
        :translucent_sun_sky_aperture_depth_transform.zw;
    float receiver_z=uv.z*tr.x+tr.y;
    if(receiver_z<0.0 || receiver_z>1.0)return 0.0;
    float sky_z=texture(translucent_sun_sky_aperture_map,
                        vec3(uv.xy,float(layer))).r;
    return (sky_z<0.999999 && sky_z+0.00001<receiver_z)?1.0:0.0;
}
float translucent_sun_sky_aperture(vec3 world)
{
    if(translucent_sun_sky_aperture_active==0)return 1.0;
    vec3 near_uv=translucent_sun_coordinates(0,world);
    vec3 far_uv=translucent_sun_coordinates(1,world);
    float depth=dot(world-translucent_spotlight_camera_origin.xyz,
                    translucent_spotlight_camera_forward.xyz);
    bool n=depth<translucent_sun_split && translucent_sun_inside(near_uv);
    bool f=translucent_sun_inside(far_uv);
    if(n && !f)return translucent_sun_sky_sample(near_uv,0);
    if(!n && f)return translucent_sun_sky_sample(far_uv,1);
    if(!n && !f)return 0.0;
    float near_access=translucent_sun_sky_sample(near_uv,0);
    if(depth<translucent_sun_split*0.8125)return near_access;
    float far_access=translucent_sun_sky_sample(far_uv,1);
    return mix(near_access,far_access,
        smoothstep(translucent_sun_split*0.8125,
                   translucent_sun_split*0.9375,depth));
}

#else
float translucent_sun_sky_aperture(vec3 world) { return 1.0; }
#endif
