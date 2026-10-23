#version 330 core

uniform sampler2D color_image;
uniform sampler2D emissive_image;
uniform sampler2D occlusion_image;
uniform sampler2D depth_image;
uniform vec4 fog_params; // start, end, opacity, unused
uniform vec3 fog_color;

uniform sampler2D normal_image; // scene normals for MAT spotlights
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
uniform vec2 spotlight_intensities[4]; // light (pigment), additive (cookie)

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

// Evaluate cookie radiance once per light and route it into two
// independently controlled channels. Diffuse is later multiplied by the
// UNLIT receiver pigment, so dark framebuffer colors retain surface detail.
void project_spotlights(float depth,ivec2 pixel,
                        out vec3 diffuse,out vec3 additive)
{
    diffuse=vec3(0.0);
    additive=vec3(0.0);
    if(spotlight_count<=0 || depth<=0.0 || depth>=65500.0)
        return;
    vec3 position=reconstruct_scene_world(depth);
    vec3 vn=texelFetch(normal_image,pixel,0).rgb;
    if(dot(vn,vn)<0.0001)return;
    vec3 normal=normalize(camera_right.xyz*(vn.x/camera_right.w)+
                          camera_up.xyz*(vn.y/camera_up.w)+
                          camera_forward.xyz*vn.z);
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
        vec3 radiance=cookie.rgb*cookie.a*(edge*fade*incidence);
        diffuse+=radiance*spotlight_intensities[i].x;
        additive+=radiance*spotlight_intensities[i].y;
    }
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
    // Blend the new light against the material's UNLIT pigment, not its
    // already-darkened framebuffer result. Screen/soft-light applied directly
    // to surface_rgb cannot recover texture detail from a pitch-black pixel.
    // Depth.GBA stores base pigment when spotlights are enabled; depth.R
    // remains unchanged for SSAO and reconstruction.
    vec3 light_rgb,add_rgb;
    project_spotlights(depth,tc,light_rgb,add_rgb);
    vec3 pigment=spotlight_count>0
        ? max(texelFetch(depth_image,tc,0).gba,vec3(0.0))
        : vec3(0.0);
    vec3 diffuse_light=pigment*light_rgb;
    vec3 screened=surface_rgb+diffuse_light*
                  (vec3(1.0)-clamp(surface_rgb,0.0,1.0));
    // Artist-controlled additive spill retains cookie color without albedo
    // multiplication. Both contributions sit beneath distance fog.
    vec3 lit=mix(screened+add_rgb,fog_color,fog);
    out_color=vec4(emissive.rgb+lit,albedo.a);
}
