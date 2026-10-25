#version 330 core
// Actual receiver-triangle plane, not the polygon-wide SithSurface normal.
// Flat interpolation keeps it invariant and avoids view-angle flicker.
flat in vec3 receiver_plane_normal;
flat in vec3 receiver_plane_point;
uniform vec3 camera_origin,camera_right,camera_up,camera_forward;
uniform vec2 view_tangents;
uniform sampler2D depth_image;
layout(location=0) out vec4 out_mask;
void main()
{
    float raw=texelFetch(depth_image,ivec2(gl_FragCoord.xy),0).r;
    if(raw<=0.0 || raw>=65500.0)discard;
    float scene_depth=raw/32.0;
    vec2 ndc=gl_FragCoord.xy/vec2(textureSize(depth_image,0))*2.0-1.0;
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
    // JK's opaque G-buffer depth is R16F, not a full-precision hardware
    // depth buffer. Compare perpendicular distance to the actual receiver
    // plane, accounting for quantization of scene_depth and subpixel error.
    // The plane test remains stable as the viewing angle becomes shallow.
    float plane_error=abs(scene_depth-plane_depth)*abs(incidence);
    float plane_epsilon=0.006+0.0008*scene_depth*abs(incidence);
    // Also reject different foreground/background receivers whose planes
    // happen to be close in space near a grazing ray.
    float distance_epsilon=0.02+0.002*scene_depth+
        min(0.5*fwidth(plane_depth),0.06+0.001*scene_depth);
    if(plane_error>plane_epsilon ||
       abs(scene_depth-plane_depth)>distance_epsilon)discard;
    // Overlapping triangles all write one, not cumulative darkness.
    out_mask=vec4(1.0,0.0,0.0,1.0);
}
