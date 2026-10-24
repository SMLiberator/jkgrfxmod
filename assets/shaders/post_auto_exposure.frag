#version 330 core
// A single 1x1 draw meters global and highlighted HDR luminance, then adapts
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
    float square_sum = 0.0;
    for(int y=0;y<grid;++y) {
        for(int x=0;x<grid;++x) {
            // Meter the central 80% to minimize static HUD/edge overlays.
            vec2 uv = 0.1 + 0.8*(vec2(float(x),float(y))+0.5)/float(grid);
            vec3 color = max(texture(scene_image,uv).rgb,vec3(0.0));
            float lum = dot(color,vec3(0.2126,0.7152,0.0722));
            float sample_lum = clamp(lum,0.001,32.0);
            log_sum += log(sample_lum);
            linear_sum += sample_lum;
            square_sum += sample_lum * sample_lum;
        }
    }
    // A small flashlight cone barely changes the average in a nearly
    // black room. RMS captures illuminated patches without requiring a
    // fullscreen histogram or making one blazing pixel dominate exposure.
    float log_average = exp(log_sum*inv_count);
    float linear_average = linear_sum*inv_count;
    float rms_average = sqrt(square_sum*inv_count);
    // Limit sensitivity to isolated emissive hotspots; preserve the effect
    // of a sustained patch of illuminated wall/floor in a dark environment.
    float highlight_average = min(rms_average,3.0*linear_average+0.05);
    float average_meter = mix(log_average,linear_average,0.4);
    float highlight_meter = mix(linear_average,highlight_average,0.85);
    float meter_lum = max(average_meter,highlight_meter);
    // Asymmetric eye adaptation: bright scenes can lower exposure quickly,
    // but pitch-black JK interiors must not be turned into daylight at high
    // sensitivity. Keep a little dark adaptation without lifting black itself.
    float scene_ev = clamp(log2(0.18/max(meter_lum,0.00001)),-5.0,5.0);
    float sensitivity = clamp(exposure_params.x,0.0,2.0);
    float target_ev = scene_ev < 0.0
        ? scene_ev * min(1.0,sensitivity*2.0)
        : min(scene_ev*sensitivity*0.5,1.25);
    float previous = target_ev;
    if(exposure_params.z>0.5)
        previous = texelFetch(previous_exposure,ivec2(0),0).r;

    // Adapt more quickly when entering bright light than when entering
    // darkness. Exponential response is independent of framerate.
    float rate = target_ev<previous ? 3.0 : 1.2;
    float weight = 1.0-exp(-rate*clamp(exposure_params.y,0.0,0.25));
    float adapted = mix(previous,target_ev,weight);
    // R controls exposure; G/B/A carry opt-in metering diagnostics.
    out_color = vec4(adapted,target_ev,meter_lum,highlight_average);
}
