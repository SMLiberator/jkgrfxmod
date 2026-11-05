#version 330 core
// Same world -> rdCamera -> JK projected depth as gpu_3do_preview.vert.
// Static SithWorld positions are already world space: no model matrix.
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 native_uv;
layout(location=2) in vec3 native_normal;
layout(location=3) in vec3 native_light_rgb;
layout(location=4) in vec3 native_light_state;
layout(location=5) in vec3 native_sector_tint;
layout(location=6) in vec2 native_uv_offset; // JK SlideSurface
layout(location=7) in vec3 native_sector_cmp_tint; // owning SithSector.colormap, NOT tint
out vec2 material_uv;
out vec3 cached_view_normal;
out vec3 cached_lit_rgb;
out vec3 cached_raw_light_rgb; // uncooked GPU world VBO lighting for Fluid debug
out vec3 cached_light_state;
out vec3 cached_sector_tint;
out vec3 cached_sector_cmp_tint;
out vec3 cached_world_position;
out vec3 cached_world_normal;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection; // tan(half horizontal FOV), aspect, inverse far
uniform int sky_mode; // 0=ordinary world, 1=horizon, 2=ceiling
// Already neutralized native global palette: only COG/player effects remain.
// The per-face sector tint is applied locally in the same color operation.
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;
vec3 world_jk_effect(vec3 raw,vec3 local_sector_tint,vec3 cmp_tint)
{
    // Match GPU 3DO's colormap multiplication before native palette effects.
    vec3 v=pow(clamp(raw*cmp_tint,0.0,1.0),vec3(1.0/1.8));
    if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
        v*=step(vec3(0.5),jk_color_filter.xyz);
    vec3 t=clamp(local_sector_tint+jk_color_tint.xyz,
                 vec3(-4.0),vec3(4.0));
    vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
    v=clamp(v*scale,0.0,1.0);
    v=clamp(v+jk_color_add_fade.xyz,0.0,1.0);
    v*=jk_color_add_fade.w;
    return pow(clamp(v,0.0,1.0),vec3(1.8));
}
void main() {
    // Native texVertOffset adds in MAT pixels, before dimension scaling.
    material_uv=native_uv+native_uv_offset;
    // Gamma-correct per-vertex JK lighting must interpolate before texels.
    // RGB attribute will also support future MotS colored world lighting.
    // JK's palette tint changes vertex illumination, not the albedo.
    // Apply it before Gouraud interpolation, matching the existing 3DO
    // GPU path's gamma conversion and per-channel tint scaling.
    cached_raw_light_rgb=native_light_rgb;
    cached_lit_rgb=world_jk_effect(native_light_rgb,
                                   native_sector_tint,native_sector_cmp_tint);
    cached_light_state=native_light_state;
    cached_sector_tint=native_sector_tint;
    cached_sector_cmp_tint=native_sector_cmp_tint;
    cached_world_position=world_position;
    cached_world_normal=native_normal;
    vec3 vn=view_right.xyz*native_normal.x+
            view_forward.xyz*native_normal.y+
            view_up.xyz*native_normal.z;
    float sh=max(projection.x,0.0001);
    float sv=sh/max(projection.y,0.0001);
    cached_view_normal=normalize(vec3(vn.x*sh,vn.z*sv,vn.y));
    float x=(view_forward.x*world_position.y+
             view_up.x*world_position.z+
             view_right.x*world_position.x)+view_origin.x;
    float y=(view_forward.y*world_position.y+
             view_up.y*world_position.z+
             view_right.y*world_position.x)+view_origin.y;
    float z=(view_up.z*world_position.z+
             view_forward.z*world_position.y+
             view_right.z*world_position.x)+view_origin.z;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    // Sky uses the source SithSurface as the portal aperture, but sits
    // at far depth regardless of its authored plane's distance.
    gl_Position=vec4(x/h,z/v,
        sky_mode!=0 ? y*(1.0-0.000002) : -projection.z,y);
}
