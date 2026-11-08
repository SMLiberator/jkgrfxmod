#version 330 core
// EXPERIMENTAL: convert scalar depth to RG32F moments (horizontal),
// then filter the moments vertically. Only on a cascade depth-map redraw.
uniform sampler2D source_image;
uniform int axis;
uniform vec2 texel_size;
uniform float radius;
layout(location=0) out vec2 out_moments;
vec2 input_moments(vec2 uv)
{
    if(axis==0) {
        float d=texture(source_image,clamp(uv,vec2(0.0),vec2(1.0))).r;
        return vec2(d,d*d);
    }
    return texture(source_image,clamp(uv,vec2(0.0),vec2(1.0))).rg;
}
void main()
{
    vec2 uv=gl_FragCoord.xy*texel_size;
    vec2 step_uv=(axis==0?vec2(texel_size.x,0.0):
                           vec2(0.0,texel_size.y))*radius;
    const float w0=0.40261995;
    const float w1=0.24420134;
    const float w2=0.05448869;
    vec2 sum=input_moments(uv)*w0;
    sum+=(input_moments(uv+step_uv)+input_moments(uv-step_uv))*w1;
    sum+=(input_moments(uv+2.0*step_uv)+
          input_moments(uv-2.0*step_uv))*w2;
    out_moments=sum;
}
