#version 330 core
// Camera-aligned direction rays sample the same world-oriented texture-array
// layers as GPU 3DO and world reflection materials. No Fresnel/roughness here:
// this inspects the captured radiance itself at mip zero.
in vec2 vp_texcoords;
out vec4 frag_color;
uniform sampler2DArray capture_map;
uniform vec3 camera_right,camera_forward,camera_up;
uniform float tan_half_fov,height_over_width;
uniform int hemi_projection;

vec4 capture(vec2 uv,int layer) {
    vec2 inset=0.5/vec2(textureSize(capture_map,0).xy);
    return textureLod(capture_map,
        vec3(clamp(uv,inset,vec2(1.0)-inset),float(layer)),0.0);
}
void main() {
    vec2 ndc=vp_texcoords*2.0-1.0;
    vec3 d=normalize(camera_forward+
        camera_right*(ndc.x*tan_half_fov)+
        camera_up*(ndc.y*tan_half_fov*height_over_width));
    vec3 sum=vec3(0.0);
    float coverage=0.0;
    if(hemi_projection!=0) {
        float upper=smoothstep(-0.087155743,0.087155743,d.z);
        for(int hemisphere=0;hemisphere<2;++hemisphere) {
            float weight=hemisphere==0?upper:1.0-upper;
            if(weight<=0.00001)continue;
            float sign_axis=hemisphere==0?1.0:-1.0;
            float denom=1.0+sign_axis*d.z;
            if(denom<=0.00001)continue;
            vec2 uv=vec2(d.x,sign_axis*d.y)/denom*
                (0.5/1.091308501)+vec2(0.5);
            if(any(lessThan(uv,vec2(0.0))) ||
               any(greaterThan(uv,vec2(1.0))))continue;
            vec4 color=capture(uv,hemisphere);
            if(color.a<=0.0001)continue;
            float contribution=weight*color.a;
            sum+=(color.rgb/color.a)*contribution;
            coverage+=contribution;
        }
    } else {
        vec3 a=abs(d);
        float major=max(a.x,max(a.y,a.z));
        if(major>0.000001) {
            int layer=0;
            vec3 right_axis=vec3(0.0),up_axis=vec3(0.0);
            if(a.x>=a.y && a.x>=a.z) {
                layer=d.x>=0.0?0:1;
                right_axis=vec3(0.0,d.x>=0.0?-1.0:1.0,0.0);
                up_axis=vec3(0.0,0.0,1.0);
            } else if(a.y>=a.z) {
                layer=d.y>=0.0?2:3;
                right_axis=vec3(d.y>=0.0?1.0:-1.0,0.0,0.0);
                up_axis=vec3(0.0,0.0,1.0);
            } else {
                layer=d.z>=0.0?4:5;
                right_axis=vec3(0.0,d.z>=0.0?1.0:-1.0,0.0);
                up_axis=vec3(1.0,0.0,0.0);
            }
            vec2 uv=vec2(dot(d,right_axis),dot(d,up_axis))*
                (0.5/major)+vec2(0.5);
            vec4 color=capture(uv,layer);
            if(color.a>0.0001) {
                sum=color.rgb/color.a;
                coverage=color.a;
            }
        }
    }
    // Magenta means the capture texture has NO valid scene coverage at
    // this direction (not necessarily a culling error).
    frag_color=vec4(coverage>0.0001?sum/max(coverage,0.0001):
        vec3(0.85,0.04,0.65),1.0);
}
