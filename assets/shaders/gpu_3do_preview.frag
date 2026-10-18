#version 330 core
// Debug-only G-buffer output. No attempt at material or JK lighting parity yet.
in float view_depth;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    out_color=vec4(1.0,0.05,0.65,1.0);
    out_emissive=vec4(0.0,0.0,0.0,1.0);
    out_normal=vec4(0.5,0.5,1.0,1.0);
    out_depth=vec4(view_depth,0.0,0.0,1.0);
}
