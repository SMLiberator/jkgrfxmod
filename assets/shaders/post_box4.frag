#version 330 core

uniform sampler2D fbuf_image;
uniform sampler2D normal_image;
uniform sampler2D depth_image;

in vec2 vp_texcoords;

layout(location = 0) out vec4 out_color;

// ---------------------------------------------------------------------------------------
// SSAO blur tuning
//
// A neighbouring pixel is averaged in only if it looks like part of the same surface.
// edge_depth_threshold:  neighbours whose depth differs by less than this count as the same surface
// edge_normal_threshold: neighbours whose normal points the same way (dot product above this,
//                        1.0 = identical) count as the same surface
// Both depth and normal must agree to keep AO from bleeding over silhouettes.
// A relative depth tolerance also helps gradual slopes remain smooth.
// ---------------------------------------------------------------------------------------
const float edge_depth_threshold = 0.2;
const float edge_normal_threshold = 0.75;
const float ssao_fade_end = 256.0;

vec3 safe_normal(vec3 v)
{
    float len = length(v);
    return len > 0.0001 ? v / len : vec3(0.0);
}

void main()
{
    float samps[16];
    float dsamps[16];
    vec3 nsamps[16];

    samps[0]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-2, -2)).r;
    samps[1]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-1, -2)).r;
    samps[2]  = textureOffset(fbuf_image, vp_texcoords, ivec2( 0, -2)).r;
    samps[3]  = textureOffset(fbuf_image, vp_texcoords, ivec2( 1, -2)).r;
    samps[4]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-2, -1)).r;
    samps[5]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-1, -1)).r;
    samps[6]  = textureOffset(fbuf_image, vp_texcoords, ivec2( 0, -1)).r;
    samps[7]  = textureOffset(fbuf_image, vp_texcoords, ivec2( 1, -1)).r;
    samps[8]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-2,  0)).r;
    samps[9]  = textureOffset(fbuf_image, vp_texcoords, ivec2(-1,  0)).r;
    samps[10] = textureOffset(fbuf_image, vp_texcoords, ivec2( 0,  0)).r;
    samps[11] = textureOffset(fbuf_image, vp_texcoords, ivec2( 1,  0)).r;
    samps[12] = textureOffset(fbuf_image, vp_texcoords, ivec2(-2,  1)).r;
    samps[13] = textureOffset(fbuf_image, vp_texcoords, ivec2(-1,  1)).r;
    samps[14] = textureOffset(fbuf_image, vp_texcoords, ivec2( 0,  1)).r;
    samps[15] = textureOffset(fbuf_image, vp_texcoords, ivec2( 1,  1)).r;

    dsamps[0]  = textureOffset(depth_image, vp_texcoords, ivec2(-2, -2)).r;
    dsamps[1]  = textureOffset(depth_image, vp_texcoords, ivec2(-1, -2)).r;
    dsamps[2]  = textureOffset(depth_image, vp_texcoords, ivec2( 0, -2)).r;
    dsamps[3]  = textureOffset(depth_image, vp_texcoords, ivec2( 1, -2)).r;
    dsamps[4]  = textureOffset(depth_image, vp_texcoords, ivec2(-2, -1)).r;
    dsamps[5]  = textureOffset(depth_image, vp_texcoords, ivec2(-1, -1)).r;
    dsamps[6]  = textureOffset(depth_image, vp_texcoords, ivec2( 0, -1)).r;
    dsamps[7]  = textureOffset(depth_image, vp_texcoords, ivec2( 1, -1)).r;
    dsamps[8]  = textureOffset(depth_image, vp_texcoords, ivec2(-2,  0)).r;
    dsamps[9]  = textureOffset(depth_image, vp_texcoords, ivec2(-1,  0)).r;
    dsamps[10] = textureOffset(depth_image, vp_texcoords, ivec2( 0,  0)).r;
    dsamps[11] = textureOffset(depth_image, vp_texcoords, ivec2( 1,  0)).r;
    dsamps[12] = textureOffset(depth_image, vp_texcoords, ivec2(-2,  1)).r;
    dsamps[13] = textureOffset(depth_image, vp_texcoords, ivec2(-1,  1)).r;
    dsamps[14] = textureOffset(depth_image, vp_texcoords, ivec2( 0,  1)).r;
    dsamps[15] = textureOffset(depth_image, vp_texcoords, ivec2( 1,  1)).r;

    nsamps[0]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-2, -2)).rgb);
    nsamps[1]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-1, -2)).rgb);
    nsamps[2]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 0, -2)).rgb);
    nsamps[3]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 1, -2)).rgb);
    nsamps[4]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-2, -1)).rgb);
    nsamps[5]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-1, -1)).rgb);
    nsamps[6]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 0, -1)).rgb);
    nsamps[7]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 1, -1)).rgb);
    nsamps[8]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-2,  0)).rgb);
    nsamps[9]  = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-1,  0)).rgb);
    nsamps[10] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 0,  0)).rgb);
    nsamps[11] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 1,  0)).rgb);
    nsamps[12] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-2,  1)).rgb);
    nsamps[13] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2(-1,  1)).rgb);
    nsamps[14] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 0,  1)).rgb);
    nsamps[15] = safe_normal(textureOffset(normal_image, vp_texcoords, ivec2( 1,  1)).rgb);

    float base_dsamp = dsamps[10];
    vec3 base_nsamp = nsamps[10];
    if(base_dsamp <= 0.0 || base_dsamp >= ssao_fade_end ||
       length(base_nsamp) < 0.0001) {
        out_color = vec4(0.0);
        return;
    }

    float depth_tolerance = max(edge_depth_threshold, base_dsamp * 0.0125);
    float occlusion = 0.0;
    float occ_weights = 0.0;
    for(int i = 0; i < 16; ++i) {
        bool depth_ok = dsamps[i] > 0.0 && dsamps[i] < ssao_fade_end &&
                        abs(dsamps[i] - base_dsamp) < depth_tolerance;
        bool normal_ok = dot(nsamps[i], base_nsamp) > edge_normal_threshold;
        float weight = float(depth_ok && normal_ok);
        occlusion += weight * samps[i];
        occ_weights += weight;
    }

    occlusion /= max(occ_weights, 1.0);
    out_color = vec4(occlusion, 0.0, 0.0, 0.0);
}
