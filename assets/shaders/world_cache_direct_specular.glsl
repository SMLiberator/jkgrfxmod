// Direct GGX highlights for GPU-cached SithWorld geometry.
// Reuse the SAME live sun cascade/dynamic-shadow uniforms as cached 3DOs.
// Native Gouraud diffuse and environment reflections are intentionally unchanged.
uniform int world_pixel_dynamic_lighting;
uniform int world_dynamic_light_specular;
uniform int translucent_sun_enabled;
uniform int translucent_sun_maps_ready;
uniform int translucent_sun_dynamic_count;
uniform vec3 translucent_sun_direction;
uniform vec4 translucent_sun_color;
uniform float translucent_sun_split;
uniform float translucent_sun_bias_scale;
uniform float translucent_sun_max_distance;
uniform vec4 translucent_sun_map_x[10];
uniform vec4 translucent_sun_map_y[10];
uniform vec4 translucent_sun_map_z[10];
uniform vec4 translucent_sun_tile_rect[8];
uniform sampler2DShadow translucent_sun_near_depth;
uniform sampler2DShadow translucent_sun_far_depth;
uniform sampler2DShadow translucent_sun_dynamic_depth;

vec3 world_sun_coordinates(int slot,vec3 p)
{
    vec4 world=vec4(p,1.0);
    return vec3(dot(translucent_sun_map_x[slot],world),
                dot(translucent_sun_map_y[slot],world),
                dot(translucent_sun_map_z[slot],world));
}
bool world_sun_inside(vec3 uv)
{
    return all(greaterThanEqual(uv,vec3(0.0))) &&
           all(lessThanEqual(uv,vec3(1.0)));
}
float world_sun_sample(int slot,vec3 uv,float incidence)
{
    float grazing=1.0-clamp(incidence,0.0,1.0);
    float offset=(0.008+0.027*grazing*grazing)*
                 translucent_sun_bias_scale*
                 length(translucent_sun_map_z[slot].xyz);
    float comparison=uv.z-max(4.0/16777215.0,offset);
    if(slot==0)
        return texture(translucent_sun_near_depth,vec3(uv.xy,comparison));
    if(slot==1)
        return texture(translucent_sun_far_depth,vec3(uv.xy,comparison));
    vec4 tile=translucent_sun_tile_rect[slot-2];
    vec2 tex_size=vec2(textureSize(translucent_sun_dynamic_depth,0));
    vec2 center=tile.xy+vec2(2.0)+uv.xy*(tile.zw-vec2(4.0));
    return texture(translucent_sun_dynamic_depth,
                   vec3(center/tex_size,comparison));
}
float world_sun_visibility(vec3 world,float incidence)
{
    float camera_distance=dot(world-camera_world_origin,camera_forward_axis);
    float fade=1.0;
    if(translucent_sun_max_distance>0.0) {
        fade=1.0-smoothstep(translucent_sun_max_distance*0.85,
                            translucent_sun_max_distance,camera_distance);
        if(fade<=0.0)return 0.0;
    }
    float visibility=1.0;
    if(translucent_sun_maps_ready!=0) {
        vec3 near_uv=world_sun_coordinates(0,world);
        vec3 far_uv=world_sun_coordinates(1,world);
        bool use_near=camera_distance<translucent_sun_split &&
                      world_sun_inside(near_uv);
        bool use_far=world_sun_inside(far_uv);
        if(use_near && !use_far)visibility=world_sun_sample(0,near_uv,incidence);
        else if(use_near && use_far) {
            float begin_blend=translucent_sun_split*0.8125;
            float end_blend=translucent_sun_split*0.9375;
            if(camera_distance<=begin_blend)
                visibility=world_sun_sample(0,near_uv,incidence);
            else {
                float far_visibility=world_sun_sample(1,far_uv,incidence);
                if(camera_distance>=end_blend)visibility=far_visibility;
                else visibility=mix(world_sun_sample(0,near_uv,incidence),
                                    far_visibility,
                                    smoothstep(begin_blend,end_blend,camera_distance));
            }
        } else if(use_far)visibility=world_sun_sample(1,far_uv,incidence);
    }
    if(translucent_sun_dynamic_count>0 && translucent_sun_split>0.0 &&
       camera_distance<translucent_sun_split) {
        float near_fade=1.0-smoothstep(
            translucent_sun_split*0.85,translucent_sun_split,camera_distance);
        for(int i=0;i<8;++i) {
            if(i>=translucent_sun_dynamic_count)break;
            vec3 uv=world_sun_coordinates(i+2,world);
            if(!world_sun_inside(uv))continue;
            float sample_visibility=world_sun_sample(i+2,uv,incidence);
            visibility=min(visibility,mix(1.0,sample_visibility,near_fade));
            if(visibility<=0.001)break;
        }
    }
    return visibility*fade;
}

// Same GGX / Schlick Fresnel / Smith geometry model as GPU-cached 3DOs.
vec3 world_ggx(vec3 n,vec3 v,vec3 l,vec3 f0,float roughness)
{
    float nl=max(dot(n,l),0.0);
    float nv=max(dot(n,v),0.0);
    if(nl<=0.0 || nv<=0.0)return vec3(0.0);
    vec3 h=normalize(l+v);
    float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a=roughness*roughness,a2=a*a;
    float d=nh*nh*(a2-1.0)+1.0;
    float D=a2/(3.14159265359*d*d);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float Gv=nv/(nv*(1.0-k)+k);
    float Gl=nl/(nl*(1.0-k)+k);
    vec3 F=f0+(vec3(1.0)-f0)*pow(1.0-vh,5.0);
    return F*(D*Gv*Gl/max(4.0*nv,0.0001));
}

vec3 world_direct_specular(vec3 base_color,vec2 uv)
{
    if(specular_params.x<=0.0 || solid_fullbright!=0 ||
       cached_light_state.z<0.5 ||
       (translucent_sun_enabled==0 || translucent_sun_color.w<=0.0) &&
       (world_dynamic_light_specular==0 || num_lights==0))
        return vec3(0.0);
    vec3 surf=specular_params.w>0.5
        ? texture(surface_map,uv).rgb : vec3(1.0);
    float strength=max(specular_params.x*surf.r,0.0);
    if(strength<=0.0)return vec3(0.0);
    float roughness=clamp(specular_params.y*surf.g,0.08,1.0);
    float metalness=clamp(specular_params.z*surf.b,0.0,1.0);
    vec3 F0=mix(vec3(0.04),clamp(base_color,0.0,1.0),metalness);
    vec3 N=world_mapped_normal(uv);
    vec3 V=normalize(camera_world_origin-cached_world_position);
    if(dot(N,V)<0.0)N=-N;
    vec3 sum=vec3(0.0);
    if(translucent_sun_enabled!=0 && translucent_sun_color.w>0.0) {
        vec3 L=normalize(translucent_sun_direction);
        float nl=max(dot(N,L),0.0);
        if(nl>0.0)
            sum+=world_ggx(N,V,L,F0,roughness)*
                 translucent_sun_color.rgb*translucent_sun_color.w*
                 world_sun_visibility(cached_world_position,max(nl,0.35));
    }
    if(world_dynamic_light_specular!=0) {
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            float distance=length(delta);
            if(distance<0.00001 || distance>=light_position_radius[i].w)
                continue;
            float radiance=max(light_intensity[i]-
                               distance*light_attenuation,0.0);
            if(radiance<=0.0)continue;
            sum+=world_ggx(N,V,delta/distance,F0,roughness)*
                 radiance*light_color[i];
        }
    }
    return min(sum*strength,vec3(32.0));
}
