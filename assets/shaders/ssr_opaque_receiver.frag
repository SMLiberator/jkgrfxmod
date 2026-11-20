#version 330 core
// G-buffer depth already identifies visible opaque geometry. Re-render only
// explicit screen-reflective materials into a low-res directional mask.
// No final scene color is sampled while it is attached for drawing.
in vec3 receiver_position,receiver_normal;
in vec2 receiver_uv;
uniform sampler2D opaque_depth,albedo_map,normal_map,normal_map2;
uniform vec2 capture_resolution,uv_scale,normal_scroll,normal_scroll2;
uniform vec4 normal_params;
uniform vec4 material_params; // strength,roughness,alpha_cutoff,alpha_mode
uniform float material_opacity;
layout(location=0) out vec4 out_receiver;
vec2 encode_oct(vec3 v) {
    v/=max(abs(v.x)+abs(v.y)+abs(v.z),0.000001);
    vec2 p=v.xy;
    if(v.z<0.0)p=(1.0-abs(p.yx))*sign(p);
    return p*0.5+0.5;
}
void main() {
    float linear_depth=32.0/max(gl_FragCoord.w,0.00000001);
    vec2 screen_uv=gl_FragCoord.xy/max(capture_resolution,vec2(1.0));
    float scene_depth=texture(opaque_depth,screen_uv).r;
    // Reject reflective surfaces hidden by opaque objects. Tolerance is
    // deliberately small, unlike the ray's adjustable thickness.
    if(scene_depth<=0.0||scene_depth>=65500.0||
       abs(linear_depth-scene_depth)>max(0.12,scene_depth*0.001))
        discard;
    vec2 uv=receiver_uv*uv_scale;
    int mode=int(material_params.w+0.5);
    if(mode!=0) {
        float alpha=texture(albedo_map,uv).a*material_opacity;
        if(mode==2 && alpha<material_params.z)discard;
        if(mode==1 && alpha<0.999)discard;
        if(mode==4) {
            // Match the 4x4 dithered opaque policy as closely as possible
            // at this lower viewport resolution.
            if(alpha<0.5)discard;
        }
    }
    vec3 n=normalize(receiver_normal);
    if(normal_params.x>0.5 || normal_params.z>0.5) {
        vec2 slope=vec2(0.0);
        if(normal_params.x>0.5) {
            vec3 t=texture(normal_map,uv+normal_scroll).xyz*2.0-1.0;
            slope+=t.xy/max(t.z,0.05)*normal_params.y;
        }
        if(normal_params.z>0.5) {
            vec3 t=texture(normal_map2,uv+normal_scroll2).xyz*2.0-1.0;
            slope+=t.xy/max(t.z,0.05)*normal_params.w;
        }
        vec3 dp1=dFdx(receiver_position),dp2=dFdy(receiver_position);
        vec2 du1=dFdx(uv),du2=dFdy(uv);
        vec3 tangent=cross(dp2,n)*du1.x+cross(n,dp1)*du2.x;
        vec3 bitangent=cross(dp2,n)*du1.y+cross(n,dp1)*du2.y;
        float basis=max(dot(tangent,tangent),dot(bitangent,bitangent));
        if(basis>1e-16) {
            float inv=inversesqrt(basis);
            n=normalize(mat3(tangent*inv,bitangent*inv,n)*
                        normalize(vec3(slope,1.0)));
        }
    }
    // Two oct-normal channels, strength, roughness. The tracer obtains
    // position from the original opaque depth, so no depth is sacrificed.
    out_receiver=vec4(encode_oct(n),
                      clamp(material_params.x,0.0,1.0),
                      clamp(material_params.y,0.0,1.0));
}
