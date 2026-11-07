// Optional forward spotlights on genuinely blended receivers. The opaque
// composite already lights masked/dithered surfaces; NEVER relight them here.
// No geometry pass, framebuffer copy, shadow map or normal texture is needed.
uniform int translucent_spotlight_count;
uniform vec2 translucent_spotlight_resolution;
uniform vec4 translucent_spotlight_camera_origin;
uniform vec4 translucent_spotlight_camera_right;   // w = horizontal tan(FOV/2)
uniform vec4 translucent_spotlight_camera_forward;
uniform vec4 translucent_spotlight_camera_up;      // w = vertical tan(FOV/2)
uniform sampler2D translucent_spotlight_image_0;
uniform sampler2D translucent_spotlight_image_1;
uniform sampler2D translucent_spotlight_image_2;
uniform sampler2D translucent_spotlight_image_3;
uniform vec4 translucent_spotlight_origins[4];
uniform vec4 translucent_spotlight_rights[4];
uniform vec4 translucent_spotlight_ups[4];
uniform vec4 translucent_spotlight_forwards[4];
uniform vec2 translucent_spotlight_intensities[4];
uniform vec4 translucent_spotlight_bounds[4];

uniform int translucent_shadow_slot; // -1 disables shadow sampling
uniform sampler2DShadow translucent_shadow_image;
uniform vec2 translucent_shadow_near_range;
uniform float translucent_shadow_angle_bias_scale; // opt-in; 0=legacy

float translucent_shadow_visibility(vec2 uv,float distance,float facing)
{
    float n=translucent_shadow_near_range.x;
    float f=translucent_shadow_near_range.y;
    if(distance<=n || distance>=f)return 1.0;
    float receiver=f/(f-n)-f*n/((f-n)*distance);
    float derivative=f*n/((f-n)*distance*distance);
    // The same geometric angle-dependent receiver bias as opaque spotlights.
    // No additional shadow-map samples or caster offset.
    float grazing=1.0-clamp(abs(facing),0.0,1.0);
    float world_bias=0.015+0.035*translucent_shadow_angle_bias_scale*
                     grazing*grazing;
    float bias=max(0.000025,derivative*world_bias);
    vec2 texel=1.0/vec2(textureSize(translucent_shadow_image,0));
    float visible=0.0;
    // Match the opaque spotlight's filtered depth comparison.
    for(int y=0;y<2;++y)
    for(int x=0;x<2;++x) {
        vec2 delta=(vec2(x,y)-vec2(0.5))*0.7*texel;
        visible+=texture(translucent_shadow_image,
                         vec3(uv+delta,receiver-bias));
    }
    return visible*0.25;
}

vec4 sample_translucent_spotlight(int i, vec2 uv)
{
    if(i == 0) return texture(translucent_spotlight_image_0, uv);
    if(i == 1) return texture(translucent_spotlight_image_1, uv);
    if(i == 2) return texture(translucent_spotlight_image_2, uv);
    return texture(translucent_spotlight_image_3, uv);
}

// Input depth is JK's view-space forward distance multiplied by 32.
vec3 translucent_spotlight_world_pos(float depth)
{
    vec2 ndc = gl_FragCoord.xy / translucent_spotlight_resolution * 2.0 - 1.0;
    return translucent_spotlight_camera_origin.xyz + (depth / 32.0) * (
        translucent_spotlight_camera_forward.xyz +
        ndc.x * translucent_spotlight_camera_right.w *
                translucent_spotlight_camera_right.xyz +
        ndc.y * translucent_spotlight_camera_up.w *
                translucent_spotlight_camera_up.xyz);
}

// View normals in JK's original projected-normal convention are corrected
// for the camera FOV before transforming into world axes.
vec3 translucent_spotlight_world_normal(vec3 normal)
{
    vec3 n = normalize(normal);
    return normalize(
        translucent_spotlight_camera_right.xyz *
            (n.x / max(translucent_spotlight_camera_right.w, 0.0001)) +
        translucent_spotlight_camera_up.xyz *
            (n.y / max(translucent_spotlight_camera_up.w, 0.0001)) +
        translucent_spotlight_camera_forward.xyz * n.z);
}


