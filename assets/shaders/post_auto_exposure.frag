#version 330 core
// A single 1x1 draw estimates log-average HDR scene luminance, then adapts
// exposure in EV on the GPU. No glReadPixels/CPU synchronisation and no
// full-resolution shader pass beyond the existing presentation pass.
uniform sampler2D scene_image;
uniform sampler2D previous_exposure;
uniform vec4 exposure_params; // x=sensitivity 0..2, y=frame dt, z=history valid
layout(location=0) out vec4 out_color;

void main()
{
    const int grid = 12;
    const float inv_count = 1.0 / float(grid*grid);
    float log_sum = 0.0;
    float linear_sum = 0.0;
    for(int y=0;y<grid;++y) {
        for(int x=0;x<grid;++x) {
            // Meter the central 80% to minimize static HUD/edge overlays.
            vec2 uv = 0.1 + 0.8*(vec2(float(x),float(y))+0.5)/float(grid);
            vec3 color = max(texture(scene_image,uv).rgb,vec3(0.0));
            float lum = dot(color,vec3(0.2126,0.7152,0.0722));
            float sample_lum = clamp(lum,0.001,32.0);
            log_sum += log(sample_lum);
            linear_sum += sample_lum;
        }
    }
    // Log averaging alone makes small light sources almost invisible to
    // metering (especially in JK's pitch-black interiors). Include some
    // linear luminance so brightly lit objects can drive adaptation too.
    float log_average = exp(log_sum*inv_count);
    float linear_average = linear_sum*inv_count;
    float meter_lum = mix(log_average,linear_average,0.4);
    // Bound only the displayed EV, rather than saturating the metering
    // before applying sensitivity. This preserves changes within dark rooms.
    float target_ev = clamp(
        log2(0.18/max(meter_lum,0.00001))*exposure_params.x,-5.0,5.0);
    float previous = target_ev;
    if(exposure_params.z>0.5)
        previous = texelFetch(previous_exposure,ivec2(0),0).r;

    // Adapt more quickly when entering bright light than when entering
    // darkness. Exponential response is independent of framerate.
    float rate = target_ev<previous ? 3.0 : 1.2;
    float weight = 1.0-exp(-rate*clamp(exposure_params.y,0.0,0.25));
    float adapted = mix(previous,target_ev,weight);
    // G/B are profiling diagnostics; R alone controls scene exposure.
    out_color = vec4(adapted,target_ev,meter_lum,1.0);
}
