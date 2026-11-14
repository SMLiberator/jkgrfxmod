#version 330 core
// During occlusion queries color writes are disabled. The same exact
// geometry shader can be reused for an optional on-screen debug overlay.
uniform vec4 debug_color;
layout(location=0) out vec4 out_color;
void main() { out_color=debug_color; }
