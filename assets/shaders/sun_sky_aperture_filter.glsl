// Filter SKY ACCESS, not aperture depth. The aperture is a depth texture
// with 1.0 meaning "no sky polygon"; bilinear interpolation of raw depth
// would invent sky in sealed regions and distort the receiver comparison.
//
// Compare the four neighboring depth texels independently, then interpolate
// their Boolean visibility. The conservative ramp suppresses small partial
// coverage around sky-polygon edges so it does not revive wall light leaks.
// This is independent of the sun-shadow PCF/VSM filter and works with both.
float sun_sky_aperture_filtered_coverage(
    sampler2DArray aperture_map,vec2 uv,float receiver_z,int layer)
{
    ivec2 size=textureSize(aperture_map,0).xy;
    vec2 pixel=uv*vec2(size)-vec2(0.5);
    ivec2 base=ivec2(floor(pixel));
    vec2 fraction=fract(pixel);
    float coverage=0.0;
    for(int y=0;y<2;++y) {
        for(int x=0;x<2;++x) {
            ivec2 at=base+ivec2(x,y);
            // Texels outside the aperture atlas never expose the sun.
            if(any(lessThan(at,ivec2(0))) ||
               any(greaterThanEqual(at,size)))continue;
            float sky_z=texelFetch(aperture_map,ivec3(at,layer),0).r;
            float exposed=(sky_z<0.999999 &&
                           sky_z+0.00001<receiver_z)?1.0:0.0;
            float weight_x=x==0?1.0-fraction.x:fraction.x;
            float weight_y=y==0?1.0-fraction.y:fraction.y;
            coverage+=exposed*weight_x*weight_y;
        }
    }
    // No sky pixels = exactly zero light. Ramp inward over a fraction of
    // one texel to soften silhouette aliasing without blurring depth itself.
    return smoothstep(0.25,1.0,coverage);
}
