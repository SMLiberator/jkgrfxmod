#version 330 core
// Static GPU geometry with per-fragment opaque/translucent separation.
// MIXED surfaces draw fully opaque pixels into the G-buffer and translucent
// pixels in the sorted alpha pass, without double shading any pixel.
uniform sampler2D albedo_map;
uniform sampler2D albedo_map2;
uniform vec4 albedo_transition; // has second albedo, progress, enabled, unused
uniform sampler2D emissive_map;
uniform vec2 uv_scale;
uniform vec2 albedo_scroll;
uniform vec2 emissive_scroll;
uniform vec4 albedo_factor;
uniform vec3 emissive_factor;
uniform vec4 fog_params;   // start, end, opacity, unused
uniform vec3 fog_color;
uniform sampler2D scene_depth; // completed opaque depth, native units 32*Y
uniform vec2 advanced_material_params; // depth_clip, fluid thickness
uniform float alpha_cutoff;
uniform float native_face_opacity;
uniform int has_texture; // 0=unresolved diagnostic, 1=albedo map, 2=intentionally no albedo
uniform int has_emissive;
uniform int transparent_pass;
uniform int blend_mode; // 0 normal, 1 additive, 2 multiply, 3 fluid
uniform int emissive_only; // zero-alpha base, independently blended emission
uniform int alpha_mode; // 0 solid, 1 mixed, 2 mask, 3 blend only, 4 dithered
in vec2 pixel_uv;
in vec3 view_normal;
in vec3 jk_lit_rgb;

uniform sampler2D surface_map; // linear RGB: reflectivity, roughness, metalness
uniform sampler2D normal_map;  // tangent-space RGB
uniform sampler2D normal_map2; // second slope layer
uniform sampler2D env_map;     // equirectangular / matcap
uniform sampler2D env_map2;
uniform vec4 view_right, view_forward, view_up, view_origin;
uniform vec3 camera_right_axis,camera_forward_axis,camera_up_axis;
uniform vec4 environment_info; // from enabled, reflection_mode, to enabled, progress
uniform vec4 sun_direction; // view-space direction TOWARD sun
uniform vec4 sun_color_intensity; // rgb, intensity
uniform vec4 specular_params; // strength, roughness, metalness, has surface map
uniform vec4 normal_specular_params; // has normal map, strength, unused, unused
uniform vec2 normal_specular_scroll;
uniform vec2 normal_specular_scroll2;
uniform vec3 local_eye;
uniform int num_lights;
uniform vec4 light_position_radius[16];
uniform float light_intensity[16];
uniform float attenuation_min;
in vec3 local_fragment_position;
in vec3 local_fragment_normal;

vec3 mapped_local_normal(vec2 uv)
{
    vec3 n=normalize(local_fragment_normal);
    if(normal_specular_params.x<0.5 && normal_specular_params.z<0.5)
        return n;
    vec2 slope=vec2(0.0);
    if(normal_specular_params.x>0.5) {
        vec3 texn=texture(normal_map,uv+normal_specular_scroll).xyz*2.0-1.0;
        slope+=texn.xy/max(texn.z,0.05)*normal_specular_params.y;
    }
    if(normal_specular_params.z>0.5) {
        vec3 texn2=texture(normal_map2,uv+normal_specular_scroll2).xyz*2.0-1.0;
        slope+=texn2.xy/max(texn2.z,0.05)*normal_specular_params.w;
    }
    vec3 dp1=dFdx(local_fragment_position);
    vec3 dp2=dFdy(local_fragment_position);
    vec2 du1=dFdx(pixel_uv*uv_scale);
    vec2 du2=dFdy(pixel_uv*uv_scale);
    vec3 t=cross(dp2,n)*du1.x+cross(n,dp1)*du2.x;
    vec3 b=cross(dp2,n)*du1.y+cross(n,dp1)*du2.y;
    float basis=max(dot(t,t),dot(b,b));
    if(basis<1e-16) return n;
    float inv_basis=inversesqrt(basis);
    return normalize(mat3(t*inv_basis,b*inv_basis,n)*
                     normalize(vec3(slope,1.0)));
}

// Local-space analytic point-light specular, with optional normal/surface
// maps. Uses the same GGX microfacet form as the native sector sun shader.
// Does not modify JK's diffuse Gouraud lighting.
vec3 point_light_specular(vec3 base_color,vec2 uv)
{
    if(specular_params.x<=0.0 || num_lights==0) return vec3(0.0);
    vec3 N=mapped_local_normal(uv);
    vec3 V=normalize(local_eye-local_fragment_position);
    if(dot(N,V)<0.0) N=-N;
    float NdotV=max(dot(N,V),0.0001);
    vec3 map_value=specular_params.w>0.5
        ? texture(surface_map,uv).rgb : vec3(1.0);
    float strength=max(specular_params.x*map_value.r,0.0);
    if(strength<=0.0) return vec3(0.0);
    float roughness=clamp(specular_params.y*map_value.g,0.08,1.0);
    float metalness=clamp(specular_params.z*map_value.b,0.0,1.0);
    vec3 F0=mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float a=roughness*roughness;
    float a2=a*a;
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float Gv=NdotV/(NdotV*(1.0-k)+k);
    vec3 sum=vec3(0.0);
    for(int i=0;i<num_lights;i++) {
        vec3 delta=light_position_radius[i].xyz-local_fragment_position;
        float dist=length(delta);
        if(dist>=light_position_radius[i].w || dist<0.00001) continue;
        vec3 L=delta/dist;
        float NdotL=max(dot(N,L),0.0);
        if(NdotL<=0.0) continue;
        float radiance=max(light_intensity[i]-dist*attenuation_min,0.0);
        if(radiance<=0.0) continue;
        vec3 H=normalize(L+V);
        float Nh=max(dot(N,H),0.0);
        float Vh=max(dot(V,H),0.0);
        float d=Nh*Nh*(a2-1.0)+1.0;
        float D=a2/(3.14159265*d*d);
        float Gl=NdotL/(NdotL*(1.0-k)+k);
        vec3 F=F0+(vec3(1.0)-F0)*pow(1.0-Vh,5.0);
        sum+=F*(D*Gv*Gl/max(4.0*NdotV,0.0001))*radiance;
    }
    return min(sum*strength,vec3(32.0));
}


// PS2-era style environment mapping, sharing the native material's
// reflectivity/roughness/metalness and optional dual-layer mapped normal.
// The sun/dynamic-COG EnvMap crossfade is a separate parity feature.
vec3 gpu_environment_reflection(vec3 base_color,vec2 uv,out float metalness)
{
    metalness=0.0;
    if((environment_info.x<0.5 && environment_info.z<0.5) ||
       specular_params.x<=0.0)
        return vec3(0.0);
    vec3 surf=specular_params.w>0.5 ? texture(surface_map,uv).rgb:vec3(1.0);
    float strength=max(specular_params.x*surf.r,0.0);
    if(strength<=0.0) return vec3(0.0);
    float roughness=clamp(specular_params.y*surf.g,0.0,1.0);
    metalness=clamp(specular_params.z*surf.b,0.0,1.0)*
              mix(environment_info.x,environment_info.z,environment_info.w);
    vec3 n_local=mapped_local_normal(uv);
    vec3 nv=view_right.xyz*n_local.x+
            view_forward.xyz*n_local.y+view_up.xyz*n_local.z;
    vec3 n=normalize(vec3(nv.x,nv.z,nv.y));
    vec3 vp=view_origin.xyz+
            view_right.xyz*local_fragment_position.x+
            view_forward.xyz*local_fragment_position.y+
            view_up.xyz*local_fragment_position.z;
    vec3 v=normalize(vec3(vp.x,vp.z,vp.y)); // camera -> surface
    if(dot(n,v)>0.0) n=-n;
    vec3 reflection=reflect(v,n);
    int mode=int(environment_info.y+0.5);
    vec3 dir=reflection;
    if(mode<=1) {
        vec3 world=camera_right_axis*reflection.x+
                   camera_forward_axis*reflection.z+
                   camera_up_axis*reflection.y;
        dir=vec3(world.x,world.z,world.y);
    }
    vec2 env_uv=mode==3
        ? clamp(vec2(0.5+0.5*n.x,0.5-0.5*n.y),vec2(0.001),vec2(0.999))
        : vec2(fract(atan(dir.x,dir.z)/6.28318530718+0.5),
               0.5-asin(clamp(dir.y,-1.0,1.0))/3.14159265359);
    ivec2 dims=environment_info.x>0.5 ?
        textureSize(env_map,0) : textureSize(env_map2,0);
    float lod=roughness*log2(max(float(max(dims.x,dims.y)),1.0));
    vec3 env_from=vec3(0.0),env_to=vec3(0.0);
    if(environment_info.x>0.5 && environment_info.w<1.0)
        env_from=textureLod(env_map,env_uv,lod).rgb;
    if(environment_info.z>0.5 && environment_info.w>0.0)
        env_to=textureLod(env_map2,env_uv,lod).rgb;
    vec3 env=mix(env_from,env_to,environment_info.w);
    vec3 f0=mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    float cos_theta=clamp(dot(n,-v),0.0,1.0);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-cos_theta,5.0);
    return env*fresnel*strength;
}


// The same view-space GGX/Schlick/Smith sun term used by the native
// opaque material shader; independent of the existing Gouraud diffuse.
vec3 gpu_sun_specular(vec3 base_color,vec2 uv)
{
    if(specular_params.x<=0.0 || sun_color_intensity.w<=0.0 ||
       sun_direction.w<0.5) return vec3(0.0);
    vec3 surface=specular_params.w>0.5 ?
        texture(surface_map,uv).rgb : vec3(1.0);
    float strength=max(specular_params.x*surface.r,0.0);
    if(strength<=0.0) return vec3(0.0);
    float roughness=clamp(specular_params.y*surface.g,0.08,1.0);
    float metalness=clamp(specular_params.z*surface.b,0.0,1.0);
    vec3 n_local=mapped_local_normal(uv);
    vec3 vnorm=view_right.xyz*n_local.x+
               view_forward.xyz*n_local.y+view_up.xyz*n_local.z;
    vec3 N=normalize(vec3(vnorm.x,vnorm.z,vnorm.y));
    vec3 vp=view_origin.xyz+
            view_right.xyz*local_fragment_position.x+
            view_forward.xyz*local_fragment_position.y+
            view_up.xyz*local_fragment_position.z;
    vec3 V=-normalize(vec3(vp.x,vp.z,vp.y));
    if(dot(N,V)<0.0) N=-N;
    vec3 L=normalize(sun_direction.xyz);
    float NdotL=max(dot(N,L),0.0),NdotV=max(dot(N,V),0.0);
    if(NdotL<=0.0 || NdotV<=0.0) return vec3(0.0);
    vec3 H=normalize(L+V);
    float Nh=max(dot(N,H),0.0),Vh=max(dot(V,H),0.0);
    float a=roughness*roughness,a2=a*a;
    float d=Nh*Nh*(a2-1.0)+1.0;
    float D=a2/(3.14159265359*d*d);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float Gv=NdotV/(NdotV*(1.0-k)+k);
    float Gl=NdotL/(NdotL*(1.0-k)+k);
    vec3 F0=mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    vec3 F=F0+(vec3(1.0)-F0)*pow(1.0-Vh,5.0);
    return min(F*(D*Gv*Gl/max(4.0*NdotV,0.0001))*
               sun_color_intensity.rgb*sun_color_intensity.w*strength,
               vec3(32.0));
}


// View position in JK units: X right, Y up, Z forward.
vec3 gpu_view_position()
{
    vec3 p=view_origin.xyz+view_right.xyz*local_fragment_position.x+
           view_forward.xyz*local_fragment_position.y+
           view_up.xyz*local_fragment_position.z;
    return vec3(p.x,p.z,p.y);
}
bool depth_clip_rejected(float fragment_depth)
{
    float opaque_depth=texelFetch(scene_depth,ivec2(gl_FragCoord.xy),0).r;
    if(opaque_depth>=65500.0) return true;
    vec3 pos=gpu_view_position();
    vec3 plane=cross(dFdx(pos),dFdy(pos));
    float magnitude=length(plane);
    if(magnitude<1e-10 || pos.z<=0.0) return true;
    // Perpendicular plane separation is independent of camera angle.
    float gap=abs(((opaque_depth-fragment_depth)/32.0)*
                  dot(plane/magnitude,pos/max(pos.z,0.0001)));
    return gap>0.15;
}
float fluid_absorption(float fragment_depth)
{
    float opacity=advanced_material_params.y>0.0?1.0:0.0;
    float opaque_depth=texelFetch(scene_depth,ivec2(gl_FragCoord.xy),0).r;
    if(opaque_depth<65500.0) {
        vec3 pos=gpu_view_position();
        float ray_ratio=length(pos)/max(pos.z,0.0001);
        float thickness=max((opaque_depth-fragment_depth)/32.0,0.0)*ray_ratio;
        opacity=1.0-exp(-advanced_material_params.y*thickness);
    }
    return clamp(opacity,0.0,1.0);
}

layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
// 4x4 Bayer ordered screen-door coverage. Kept samples become fully
// opaque and write ordinary depth/SSAO; no blend or per-face sort needed.
float dither_threshold()
{
    ivec2 p=ivec2(gl_FragCoord.xy) & ivec2(3);
    int lo=(((p.x^p.y)&1)<<1)|(p.y&1);
    int hi=((((p.x>>1)^(p.y>>1))&1)<<1)|((p.y>>1)&1);
    return (float(4*lo+hi)+0.5)/16.0;
}

void main() {
    // gl_FragCoord.w is 1/clip.w, and clip.w is camera-forward Y.
    // This is the exact native 1/rhw linear depth (32*Y) even on large
    // triangles crossing the camera near plane; no vertex-depth clamping.
    float view_depth=32.0/max(gl_FragCoord.w,0.00000001);
    if(transparent_pass!=0 && advanced_material_params.x>0.5 &&
       depth_clip_rejected(view_depth)) discard;
    vec2 uv=pixel_uv*uv_scale;
    vec4 texel=has_texture==1
        ? texture(albedo_map,uv+albedo_scroll)
        : (has_texture==2 ? vec4(1.0) : vec4(1.0,0.05,0.65,1.0));
    if(albedo_transition.z>0.5) {
        vec4 next_texel=albedo_transition.x>0.5
            ? texture(albedo_map2,uv+albedo_scroll) : vec4(1.0);
        texel=mix(texel,next_texel,albedo_transition.y);
    }
    vec4 albedo=texel*albedo_factor;
    albedo.a*=native_face_opacity;
    if(transparent_pass==0) {
        if(alpha_mode==3) discard; // native translucent/additive/opacity<1
        if(alpha_mode==1 && albedo.a<0.999) discard;
        if(alpha_mode==2 && albedo.a<alpha_cutoff) discard;
        if(alpha_mode==4 && albedo.a<=dither_threshold()) discard;
    } else {
        if(alpha_mode==0 || alpha_mode==2 || alpha_mode==4) discard;
        if(albedo.a<=0.001 && emissive_only==0) discard;
        if(alpha_mode==1 && albedo.a>=0.999) discard;
    }
    float fog=clamp((view_depth-fog_params.x)/
                    max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*fog_params.z;
    // Match the native vertex-by-vertex tint and lighting interpolation.
    // Keep it separate from alpha and from emissive output.
    float env_metalness=0.0;
    vec3 env_reflection=gpu_environment_reflection(albedo.rgb,uv,env_metalness);
    // Metal suppresses diffuse only when its environment contribution
    // actually exists; follows the native replacement shader.
    vec3 lit_rgb=albedo.rgb*jk_lit_rgb*(1.0-env_metalness);
    vec3 rgb=mix(lit_rgb,fog_color,fog);
    vec3 emission=emissive_factor;
    if(has_emissive!=0)
        emission*=texture(emissive_map,uv+emissive_scroll).rgb;
    // Only reflective materials with nearby JK point-light Things enter
    // this branch. Highlights bypass SSAO, as with the existing sun term.
    vec3 specular=(point_light_specular(albedo.rgb,uv)+
                   gpu_sun_specular(albedo.rgb,uv)+env_reflection)*
                  (1.0-fog);
    // The opaque pass composites this separate emissive attachment after
    // SSAO. The translucent pass has only the color target: emission must
    // therefore be added directly to premultiplied RGB, never multiplied
    // by albedo alpha. With alpha=0 this acts as emission-only additive.
    float a=transparent_pass!=0 ? albedo.a : 1.0;
    if(transparent_pass!=0 && blend_mode==3) {
        // Beer-Lambert absorption: pigment fades in shallow water,
        // while specular highlights/emission remain independently visible.
        float absorption=fluid_absorption(view_depth);
        float opacity=albedo.a*absorption;
        out_color=vec4(rgb*opacity+emission+specular,opacity);
    } else if(transparent_pass!=0 && blend_mode==2) {
        // With (DST_COLOR, ZERO) the RGB output is a multiplier, not
        // premultiplied color. Alpha zero must multiply by WHITE (identity).
        // Match the native triangle path: emission biases the multiplier
        // toward white, but never adds light independently in Multiply mode.
        vec3 tint=clamp(albedo.rgb*jk_lit_rgb+emission+specular*albedo.a,
                        0.0,1.0);
        out_color=vec4(mix(vec3(1.0),tint,albedo.a),0.0);
    } else {
        // Additive and normal pass use premultiplied pigment; alpha for the
        // additive framebuffer itself is preserved by glBlendFuncSeparate.
        // Opaque fog is applied after SSAO in the shared composite.
        out_color=vec4(transparent_pass!=0
                           ? rgb*a+emission+specular*a : lit_rgb,
                       (transparent_pass!=0 && blend_mode==1) ? 0.0 : a);
    }
    out_emissive=vec4(transparent_pass!=0
                          ? vec3(0.0) : emission+specular,a);
    out_normal=vec4(normalize(view_normal),1.0);
    out_depth=vec4(view_depth,0.0,0.0,1.0);
}
