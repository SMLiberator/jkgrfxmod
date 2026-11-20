#version 330 core
// Capture-only adaptive subdivision of triangles before nonlinear projection.
// The two clip distances reject geometry behind this hemisphere and near the
// probe; a third rejects geometry beyond the capture range.
layout(triangles) in;
layout(triangle_strip,max_vertices=48) out;
in vec2 vs_pixel_uv[];
in vec3 vs_baked_light[];
in float vs_clip_side[];
in float vs_capture_forward_depth[];
out vec2 pixel_uv;
out vec3 baked_light;
out float clip_side;
out float capture_forward_depth;
out vec3 paraboloid_view_position;
uniform vec4 projection;
uniform int capture_sky_mode; // sky apertures bypass local probe depth limits
const float disk_radius=1.091308501;
const float overlap_sin=0.087155743;
const float near_radius=0.01;

vec3 bary(int i,int j,int divisions) {
    float x=float(i)/float(divisions), y=float(j)/float(divisions);
    return vec3(1.0-x-y,x,y);
}
void projected_vertex(vec3 weights) {
    vec3 p=gl_in[0].gl_Position.xyz*weights.x+
           gl_in[1].gl_Position.xyz*weights.y+
           gl_in[2].gl_Position.xyz*weights.z;
    float dist=length(p);
    float denom=max(dist+p.y,0.00001);
    vec2 paraboloid=vec2(p.x,-p.z)/denom;
    float depth=clamp(dist*projection.z,0.0,1.0);
    gl_Position=vec4(paraboloid/disk_radius,2.0*depth-1.0,1.0);
    gl_ClipDistance[0]=p.y+dist*overlap_sin;
    gl_ClipDistance[1]=dist-near_radius;
    // Sky-flagged polygons are windows to the environment, not local
    // capture geometry. Keep their hemisphere/near clipping, but never
    // cut them away because the dedicated probe's radial limit is short.
    gl_ClipDistance[2]=capture_sky_mode!=0 ? 1.0 : 1.0-dist*projection.z;
    pixel_uv=vs_pixel_uv[0]*weights.x+
             vs_pixel_uv[1]*weights.y+
             vs_pixel_uv[2]*weights.z;
    baked_light=vs_baked_light[0]*weights.x+
                vs_baked_light[1]*weights.y+
                vs_baked_light[2]*weights.z;
    clip_side=vs_clip_side[0]*weights.x+
              vs_clip_side[1]*weights.y+
              vs_clip_side[2]*weights.z;
    capture_forward_depth=dist;
    paraboloid_view_position=p;
    EmitVertex();
}
void triangle(vec3 a,vec3 b,vec3 c) {
    projected_vertex(a);
    projected_vertex(b);
    projected_vertex(c);
    EndPrimitive();
}
void main() {
    vec3 p0=gl_in[0].gl_Position.xyz;
    vec3 p1=gl_in[1].gl_Position.xyz;
    vec3 p2=gl_in[2].gl_Position.xyz;
    float longest=max(length(p0-p1),max(length(p1-p2),length(p2-p0)));
    float nearest=max(min(length(p0),min(length(p1),length(p2))),0.05);
    float ratio=longest/nearest;
    int divisions=ratio>1.5?4:ratio>0.75?3:ratio>0.25?2:1;
    for(int i=0;i<4;++i) {
        if(i>=divisions)break;
        for(int j=0;j<4;++j) {
            if(i+j>=divisions)break;
            vec3 a=bary(i,j,divisions);
            vec3 b=bary(i+1,j,divisions);
            vec3 c=bary(i,j+1,divisions);
            triangle(a,b,c);
            if(i+j+1<divisions)
                triangle(b,bary(i+1,j+1,divisions),c);
        }
    }
}
