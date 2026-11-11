// Editable shared JK point-light model for GPU-cached 3DOs and world faces.
// Both paths include THIS file at runtime. No OpenGL extension required.
// Change these functions to tune both geometry types together.
//
// The static JK Gouraud light/color was already computed at VERTICES.
// Do not reconstruct it from interpolated raw vertex intensities:
// interpolate(pow(vertex_brightness, gamma)) !=
// pow(interpolate(vertex_brightness), gamma).

uniform float jk_light_limit; // 1=vanilla saturation, 2=overbright authored inputs
// Restore native RGB saturation when baked overbright is disabled.
// The HDR path retains the existing untethered post-tint/dynamic color.
vec3 jk_light_color_cap(vec3 value)
{
    return jk_light_limit<=1.0
        ? clamp(value,vec3(0.0),vec3(1.0)) : max(value,vec3(0.0));
}

const float JK_POINT_GAMMA = 1.8;

// Smooth Lambert point light. Radius is an independent hard cutoff;
// intensity/attenuation may reach zero sooner. Never subtract baked light.
// planar_range is for legacy 3DO non-Gouraud faces, not Gouraud world/3DO.
float jk_point_sample(vec3 delta, vec3 surface_normal,
                      float radius, float intensity, float attenuation,
                      bool planar_range)
{
    float dist=length(delta);
    float range_distance=planar_range
        ? abs(dot(surface_normal,delta)) : dist;
    if(range_distance>=radius || radius<=0.0) return 0.0;
    float incidence=max(dot(surface_normal,delta/max(dist,0.00001)),0.0);
    return max(intensity-range_distance*attenuation,0.0)*incidence;
}

// Same pixel-light composition for WORLD and 3DO.
// 'baked' is the ALREADY interpolated, gamma-corrected static RGB.
// 'light_rgb' is the full-white JK color response (minus black response)
// evaluated with the receiver's color/tint effects, NOT its baked intensity.
// Decode only the final interpolated RGB, add positive direct light,
// and re-encode without clipping HDR brightness. At zero contribution return exact baked RGB.
// This deliberately avoids the old interpolated-raw-brightness banding.
// RGB contribution is accumulated separately per channel. For white
// sources, vec3(added) is exactly the legacy scalar point-light behavior.
vec3 jk_point_compose(vec3 baked, vec3 added, vec3 light_rgb)
{
    if(all(lessThanEqual(added,vec3(0.0)))) return baked;
    vec3 base=pow(jk_light_color_cap(baked),vec3(1.0/JK_POINT_GAMMA));
    vec3 light=pow(clamp(light_rgb,0.0,1.0),vec3(1.0/JK_POINT_GAMMA));
    vec3 lit=jk_light_color_cap(base+max(added,vec3(0.0))*light);
    return pow(lit,vec3(JK_POINT_GAMMA));
}
vec3 jk_point_compose(vec3 baked,float added,vec3 light_rgb)
{
    return jk_point_compose(baked,vec3(added),light_rgb);
}
