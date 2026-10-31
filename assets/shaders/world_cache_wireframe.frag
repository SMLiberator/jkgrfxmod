#version 330 core
// Premultiplied translucent green, visually distinct from native world.
// Debug ONLY: geometry/visibility/depth parity, not MAT or light parity.
layout(location=0) out vec4 out_color;
void main() {
    out_color=vec4(0.05,0.68,0.12,0.72);
}
