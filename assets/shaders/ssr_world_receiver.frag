#version 330 core
in vec3 receiver_position,receiver_normal;
in vec2 receiver_uv;
uniform sampler2D opaque_depth,normal_map,normal_map2;
uniform vec2 capture_resolution,uv_scale;
uniform vec2 normal_scroll,normal_scroll2;
uniform vec4 normal_params; // has_normal,strength,has_normal2,strength2
layout(location=0) out vec4 out_normal_depth;
void main() {
    float depth=32.0/max(gl_FragCoord.w,0.00000001);
    vec2 screen_uv=gl_FragCoord.xy/max(capture_resolution,vec2(1.0));
    // The current opaque scene is authoritative: a mirror/water behind a
    // solid wall should not replace an actual visible surface.
    float opaque=texture(opaque_depth,screen_uv).r;
    if(depth>opaque+max(0.15,opaque*0.001))discard;
    vec3 n=normalize(receiver_normal);
    vec2 uv=receiver_uv*uv_scale;
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
    out_normal_depth=vec4(n,depth);
}
