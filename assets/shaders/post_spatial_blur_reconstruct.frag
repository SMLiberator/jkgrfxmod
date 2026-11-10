#version 330 core
// Experimental mode 2: bilateral upsampling of a half-resolution separable
// Gaussian blur. Tone mapping, grade, and HUD execute afterwards as before.
uniform sampler2D blur_image;      // output of half-res vertical pass
uniform sampler2D original_image;  // unmodified full-res linear HDR scene
uniform sampler2D depth_image;     // original full-res linear depth, nearest
uniform vec2 screen_size;
uniform vec4 distance_params;     // near, far, intensity
uniform vec4 peripheral_params;   // inner, outer, intensity
layout(location=0) out vec4 out_color;

float blur_depth(float raw)
{
    return (raw>0.0 && raw<65000.0) ? raw : distance_params.y;
}

void main()
{
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec2 uv=gl_FragCoord.xy/screen_size;
    vec4 original=texelFetch(original_image,pixel,0);
    float depth=blur_depth(texelFetch(depth_image,pixel,0).r);
    float distant=clamp(distance_params.z,0.0,1.0) *
        smoothstep(distance_params.x,distance_params.y,depth);
    float aspect=screen_size.x/screen_size.y;
    vec2 centered=(uv-vec2(0.5))*vec2(aspect,1.0);
    float corner_radius=0.5*length(vec2(aspect,1.0));
    float radius=length(centered)/max(corner_radius,0.0001);
    float peripheral=clamp(peripheral_params.z,0.0,1.0) *
        smoothstep(peripheral_params.x,peripheral_params.y,radius);
    float amount=max(distant,peripheral);
    if(amount<0.001) {
        // No filtering or upsampling at all for sharp regions.
        out_color=original;
        return;
    }

    // Reconstruct with four half-resolution samples. Bilinear weights are
    // multiplied by depth similarity only for distance-based blur. This
    // rejects foreground/background bleeding at silhouettes, while distant
    // pixels in the fully-blurred zone may mix as in the original shader.
    ivec2 dims=textureSize(blur_image,0);
    vec2 sample_pos=uv*vec2(dims)-vec2(0.5);
    ivec2 base=ivec2(floor(sample_pos));
    vec2 f=fract(sample_pos);
    float distance_fraction=distant/max(amount,0.0001);
    float tolerance=0.5+depth*0.03;
    vec4 accumulation=vec4(0.0);
    float weight_sum=0.0;
    for(int y=0;y<2;++y) {
        for(int x=0;x<2;++x) {
            ivec2 tap=clamp(base+ivec2(x,y),ivec2(0),dims-ivec2(1));
            float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);
            if(distance_fraction>0.0) {
                vec2 tap_uv=(vec2(tap)+vec2(0.5))/vec2(dims);
                float tap_depth=blur_depth(texture(depth_image,tap_uv).r);
                float similar=(depth>=distance_params.y &&
                               tap_depth>=distance_params.y)
                    ?1.0:exp(-abs(tap_depth-depth)/tolerance);
                w*=mix(1.0,similar,distance_fraction);
            }
            accumulation+=texelFetch(blur_image,tap,0)*w;
            weight_sum+=w;
        }
    }
    vec4 blurred=weight_sum>0.00001
        ?accumulation/weight_sum:original;

    // A half-res intermediate has an intrinsic low-pass effect even if the
    // requested Gaussian radius is tiny. Blend the untouched scene back in
    // for subtle effects, avoiding an abrupt loss of sharpness at transitions.
    float reconstruction_strength=smoothstep(0.0,0.25,amount);
    out_color=mix(original,blurred,reconstruction_strength);
}
