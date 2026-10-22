#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
uniform sampler2D depth_image;
uniform vec4 fog_params; // start, end, opacity, unused
uniform vec3 fog_color;

// Independent orthographic decal: depth-reconstructed receiving surface,
// not a new MAT blend/alpha mode. Unit 4 is sRGB image, unit 5 normal G-buffer.
uniform int decal_active;
uniform sampler2D decal_image;
uniform sampler2D normal_image;
uniform vec4 decal_origin;       // world XYZ
uniform vec4 decal_right;        // XYZ unit axis; W inverse width
uniform vec4 decal_up;           // XYZ unit axis; W inverse height
uniform vec4 decal_forward;      // XYZ pointing into surface; W inverse depth
uniform vec4 decal_options;      // opacity, normal threshold, blend mode, unused
uniform vec4 camera_origin;
uniform vec4 camera_right;       // XYZ world axis, W tan(horizontal_fov/2)
uniform vec4 camera_forward;
uniform vec4 camera_up;          // XYZ world axis, W tan(vertical_fov/2)

// Four independent COG MAT spotlights. Textures occupy units 6-9.
// Avoid dynamic sampler-array indexing, which GLSL 330 cannot guarantee.
uniform int spotlight_count;
uniform sampler2D spotlight_image_0;
uniform sampler2D spotlight_image_1;
uniform sampler2D spotlight_image_2;
uniform sampler2D spotlight_image_3;
uniform vec4 spotlight_origins[4];  // world origin
uniform vec4 spotlight_rights[4];   // XYZ axis, W fixed range / width
uniform vec4 spotlight_ups[4];      // XYZ axis, W fixed range / height
uniform vec4 spotlight_forwards[4]; // XYZ axis, W inverse range

layout(location = 0) out vec4 out_color;

vec4 sample_spotlight(int i,vec2 uv)
{
    if(i==0) return texture(spotlight_image_0,uv);
    if(i==1) return texture(spotlight_image_1,uv);
    if(i==2) return texture(spotlight_image_2,uv);
    return texture(spotlight_image_3,uv);
}

vec3 reconstruct_scene_world(float depth)
{
    vec2 pixel_uv=gl_FragCoord.xy/vec2(textureSize(depth_image,0));
    vec2 ndc=pixel_uv*2.0-1.0;
    float forward_distance=depth/32.0; // JK's linear scene-depth scale
    return camera_origin.xyz+forward_distance*(
        camera_forward.xyz +
        ndc.x*camera_right.w*camera_right.xyz +
        ndc.y*camera_up.w*camera_up.xyz);
}

// Project a perspective light-cookie texture onto opaque G-buffer receivers.
// No shadow casting or per-material BRDF yet; texture RGB+alpha are additive.
vec3 project_spotlights(float depth,ivec2 pixel)
{
    if(spotlight_count<=0 || depth<=0.0 || depth>=65500.0)
        return vec3(0.0);
    vec3 position=reconstruct_scene_world(depth);
    vec3 vn=texelFetch(normal_image,pixel,0).rgb;
    if(dot(vn,vn)<0.0001)return vec3(0.0);
    vec3 normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                          camera_up.xyz*(vn.y/camera_up.w)+
                          camera_forward.xyz*vn.z);
    vec3 lighting=vec3(0.0);
    for(int i=0;i<4;i++) {
        if(i>=spotlight_count)break;
        vec3 relative=position-spotlight_origins[i].xyz;
        float distance=dot(relative,spotlight_forwards[i].xyz);
        float t=distance*spotlight_forwards[i].w;
        if(t<=0.01 || t>=1.0)continue;
        vec2 uv=vec2(dot(relative,spotlight_rights[i].xyz)*
                          spotlight_rights[i].w/distance+0.5,
                     dot(relative,spotlight_ups[i].xyz)*
                          spotlight_ups[i].w/distance+0.5);
        if(any(lessThan(uv,vec2(0.0))) ||
           any(greaterThan(uv,vec2(1.0))))continue;
        vec4 cookie=sample_spotlight(i,uv);
        if(cookie.a<=0.001)continue;
        float border=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));
        float edge=smoothstep(0.0,0.02,border);
        float fade=smoothstep(0.01,0.04,t)*
                   (1.0-smoothstep(0.85,1.0,t));
        float facing=max(dot(normal,normalize(-relative)),0.0);
        // Mild floor illumination even with near-horizontal beams.
        float incidence=0.25+0.75*facing;
        lighting+=cookie.rgb*cookie.a*(edge*fade*incidence);
    }
    return lighting;
}

// Applied before distance fog, after AO shading, and under existing
// emissive/specular output. All three modes share image alpha/opacity.
vec3 project_decal(vec3 surface_rgb, float depth, ivec2 pixel)
{
    if(decal_active == 0 || depth <= 0.0 || depth >= 65500.0)
        return surface_rgb;

    vec3 vn = texelFetch(normal_image,pixel,0).rgb;
    if(dot(vn,vn)<0.0001) return surface_rgb;
    // G-buffer uses perspective-scaled x/y normal components, matching
    // GPU 3DO and native JK's smooth-normal sideband.
    vec3 n = normalize(camera_right.xyz*(vn.x/camera_right.w) +
                       camera_up.xyz*(vn.y/camera_up.w) +
                       camera_forward.xyz*vn.z);
    if(dot(n,-decal_forward.xyz)<decal_options.y)
        return surface_rgb;

    vec2 pixel_uv=gl_FragCoord.xy/vec2(textureSize(depth_image,0));
    vec2 ndc=pixel_uv*2.0-1.0;
    float forward_distance=depth/32.0; // JK's linear scene-depth scale
    vec3 position=camera_origin.xyz + forward_distance*(
        camera_forward.xyz +
        ndc.x*camera_right.w*camera_right.xyz +
        ndc.y*camera_up.w*camera_up.xyz);
    vec3 relative=position-decal_origin.xyz;
    float projector_z=dot(relative,decal_forward.xyz)*decal_forward.w;
    if(projector_z<0.0 || projector_z>1.0) return surface_rgb;
    vec2 uv=vec2(dot(relative,decal_right.xyz)*decal_right.w+0.5,
                 dot(relative,decal_up.xyz)*decal_up.w+0.5);
    if(any(lessThan(uv,vec2(0.0))) ||
       any(greaterThan(uv,vec2(1.0)))) return surface_rgb;

    vec4 decal=texture(decal_image,uv);
    // Soften the projector volume edges; the image supplies its own alpha.
    float edge=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));
    float fade=smoothstep(0.0,0.01,edge) *
               smoothstep(0.0,0.04,projector_z) *
               (1.0-smoothstep(0.96,1.0,projector_z));
    float a=clamp(decal.a*decal_options.x*fade,0.0,1.0);
    int blend=int(decal_options.z+0.5);
    if(blend==1) return surface_rgb*mix(vec3(1.0),decal.rgb,a);
    if(blend==2) return surface_rgb+decal.rgb*a;
    return mix(surface_rgb,decal.rgb,a);
}

void main()
{
    ivec2 tc = ivec2(gl_FragCoord.xy);

    vec4 albedo = texelFetch(color_image, tc, 0);
    vec4 emissive = texelFetch(emissive_image, tc, 0);
    float occlusion = 1.0 - texelFetch(occlusion_image, tc, 0).r;
    float depth = texelFetch(depth_image, tc, 0).r;

    // The G-buffer holds un-fogged pigment. AO applies only to that
    // pigment, never to the intervening fog. Depth=65504 is empty sky.
    float fog = 0.0;
    if(depth > 0.0 && depth < 65500.0) {
        float span = max(fog_params.y - fog_params.x, 0.0001);
        fog = clamp((depth - fog_params.x) / span, 0.0, 1.0) *
              fog_params.z;
    }
    vec3 surface_rgb=albedo.rgb*occlusion;
    surface_rgb=project_decal(surface_rgb,depth,tc);
    // Add projected MAT illumination before distance fog. Decals remain
    // pigment; headlights are light, independent of the receiver albedo.
    vec3 light_rgb=project_spotlights(depth,tc);
    vec3 lit=mix(surface_rgb+light_rgb,fog_color,fog);
    out_color=vec4(emissive.rgb+lit,albedo.a);
}
