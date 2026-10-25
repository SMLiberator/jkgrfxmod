#version 330 core
// Actual receiver-triangle plane, not the polygon-wide SithSurface normal.
// Flat interpolation keeps it invariant and avoids view-angle flicker.
flat in vec3 receiver_plane_normal;
flat in vec3 receiver_plane_point;
uniform vec3 camera_origin,camera_right,camera_up,camera_forward;
uniform vec2 view_tangents;
uniform vec2 mask_resolution;
uniform sampler2D depth_image;
layout(location=0) out vec4 out_mask;
void main()
{
    // Depth G-buffer stays full-resolution while this shadow mask can be
    // rasterized at 1/2 or 1/4 screen resolution. Sample the G-buffer at
    // the center of the corresponding full-resolution footprint.
    vec2 uv=gl_FragCoord.xy/mask_resolution;
    ivec2 depth_dims=textureSize(depth_image,0);
    ivec2 pixel=clamp(ivec2(uv*vec2(depth_dims)),
                      ivec2(0),depth_dims-ivec2(1));
    float raw=texelFetch(depth_image,pixel,0).r;
    if(raw<=0.0 || raw>=65500.0)discard;
    float scene_depth=raw/32.0;
    vec2 ndc=uv*2.0-1.0;
    vec3 ray=camera_forward+
        ndc.x*view_tangents.x*camera_right+
        ndc.y*view_tangents.y*camera_up;
    vec3 n=normalize(receiver_plane_normal);
    float incidence=dot(ray,n);
    if(abs(incidence)<0.0001)discard;
    // Intersect the CURRENT pixel's camera ray with the analytically flat
    // receiver triangle. This is more stable than reading gl_FragCoord.w
    // from the separately rasterized shadow polygon at grazing angles.
    float plane_depth=dot(receiver_plane_point-camera_origin,n)/incidence;
    if(plane_depth<=0.0 || (isnan(plane_depth) || isinf(plane_depth)))discard;
    // This is a visibility / occlusion test, NOT a test for whether the
    // projected polygon and the receiver depth are exactly equal. Both
    // geometric surfaces lie on the same plane, but their interpolation and
    // R16F depth quantization differ at distance and grazing angles.
    // Requiring equality punched unstable holes (apparent z-fighting).
    //
    // Only reject when the opaque pixel is clearly CLOSER than the projected
    // receiver: a real wall/step in front of the receiver must occlude it.
    // R16F stores 10 fractional mantissa bits. Compute the exact half-ULP
    // tolerance in world distance to avoid the quantized-depth stripes.
    float half_ulp=exp2(floor(log2(raw))-11.0)/32.0;
    float guard=half_ulp+0.004+0.0005*scene_depth;
    if(scene_depth+guard<plane_depth)discard;
    // Overlapping triangles all write one, not cumulative darkness.
    out_mask=vec4(1.0,0.0,0.0,1.0);
}
