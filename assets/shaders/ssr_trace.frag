#version 330 core
// Half/eighth-resolution SSR for currently visible, transparent world
// receivers. The source color and depth belong to the COMPLETED opaque
// camera render, never to the framebuffer we are writing now.
in vec2 vp_texcoords;
uniform sampler2D receiver_normal_depth,opaque_depth,opaque_color;
uniform vec3 camera_origin,camera_right,camera_forward,camera_up;
uniform vec2 tan_half_fov;
uniform vec4 trace_params; // max distance, thickness, number of steps, refine steps
uniform int opaque_receiver_mode; // 0=transparent receiver, 1=opaque 3DO/world
vec3 decode_oct(vec2 oct) {
    vec2 p=oct*2.0-1.0;
    vec3 v=vec3(p,1.0-abs(p.x)-abs(p.y));
    if(v.z<0.0)v.xy=(1.0-abs(p.yx))*sign(p);
    return normalize(v);
}
layout(location=0) out vec4 out_reflection;

vec3 world_point(vec2 uv,float linear_depth) {
    vec2 ndc=uv*2.0-1.0;
    return camera_origin+(linear_depth/32.0)*(
        camera_forward+ndc.x*tan_half_fov.x*camera_right+
        ndc.y*tan_half_fov.y*camera_up);
}
bool project_ray(vec3 position,out vec2 uv,out float depth) {
    vec3 d=position-camera_origin;
    float forward=dot(d,camera_forward);
    if(forward<=0.005)return false;
    uv=0.5+0.5*vec2(dot(d,camera_right),dot(d,camera_up))/
        (forward*max(tan_half_fov,vec2(0.00001)));
    depth=forward*32.0;
    return all(greaterThanEqual(uv,vec2(0.001))) &&
           all(lessThanEqual(uv,vec2(0.999)));
}
void main() {
    out_reflection=vec4(0.0);
    // Trace target and receiver target have identical dimensions. Nearest
    // texel fetch avoids filtering normals/depth across surface silhouettes.
    vec4 receiver=texelFetch(receiver_normal_depth,ivec2(gl_FragCoord.xy),0);
    vec3 normal;
    vec3 point;
    if(opaque_receiver_mode!=0) {
        if(receiver.z<=0.0)return;
        float opaque_distance=texture(opaque_depth,vp_texcoords).r;
        if(opaque_distance<=0.0||opaque_distance>=65500.0)return;
        normal=decode_oct(receiver.xy);
        point=world_point(vp_texcoords,opaque_distance);
    } else {
        if(receiver.w<=0.0||receiver.w>=65500.0 ||
           dot(receiver.xyz,receiver.xyz)<0.1)return;
        normal=normalize(receiver.xyz);
        point=world_point(vp_texcoords,receiver.w);
    }
    vec3 incident=normalize(point-camera_origin);
    // The same normal-map reflection direction the receiving material uses.
    vec3 reflected=normalize(reflect(incident,normal));
    float max_distance=max(trace_params.x,0.001);
    float thickness=max(trace_params.y,0.001)*32.0;
    int steps=clamp(int(trace_params.z+0.5),4,96);
    int refine=clamp(int(trace_params.w+0.5),0,8);
    float start=max(0.03,receiver.w/32.0*0.001);
    vec3 origin=point+reflected*start;
    float previous=0.0;
    float prev_delta=-1.0;
    for(int i=1;i<=96;++i) {
        if(i>steps)break;
        float t=max_distance*float(i)/float(steps);
        vec2 uv;float depth;
        if(!project_ray(origin+reflected*t,uv,depth))break;
        float scene=texture(opaque_depth,uv).r;
        if(scene<=0.0||scene>=65500.0) {
            previous=t;prev_delta=-1.0;continue;
        }
        float delta=depth-scene;
        if(delta>=0.0 && prev_delta<0.0) {
            float low=previous,high=t;
            vec2 found=uv;
            float final_delta=delta;
            for(int j=0;j<8;++j) {
                if(j>=refine)break;
                float middle=(low+high)*0.5;
                vec2 middle_uv;float middle_depth;
                if(!project_ray(origin+reflected*middle,middle_uv,middle_depth))
                    break;
                float near_scene=texture(opaque_depth,middle_uv).r;
                float error=middle_depth-near_scene;
                if(error>=0.0 && near_scene>0.0 && near_scene<65500.0) {
                    high=middle;found=middle_uv;final_delta=error;
                } else low=middle;
            }
            if(final_delta<=thickness) {
                float edge=min(min(found.x,1.0-found.x),
                               min(found.y,1.0-found.y));
                float confidence=smoothstep(0.0,0.035,edge);
                // Premultiplied coverage keeps roughness mip levels from
                // leaking dark/black uncovered texels into valid hits.
                out_reflection=vec4(
                    texture(opaque_color,found).rgb*confidence,confidence);
                return;
            }
        }
        previous=t;prev_delta=delta;
    }
}
