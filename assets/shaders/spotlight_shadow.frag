#version 330 core
in vec2 shadow_material_uv;
uniform int shadow_use_alpha;
uniform sampler2D shadow_alpha_texture;
uniform sampler2D shadow_alpha_texture_next;
uniform vec2 shadow_alpha_uv_scale;
uniform vec2 shadow_alpha_uv_scroll;
uniform vec2 shadow_alpha_transition; // second texture, interpolation
uniform float shadow_alpha_cutoff;
uniform float shadow_alpha_factor;
void main()
{
    if(shadow_use_alpha!=0) {
        vec2 uv=shadow_material_uv*shadow_alpha_uv_scale+
                shadow_alpha_uv_scroll;
        float alpha=texture(shadow_alpha_texture,uv).a;
        if(shadow_alpha_transition.x>0.5)
            alpha=mix(alpha,texture(shadow_alpha_texture_next,uv).a,
                      shadow_alpha_transition.y);
        if(alpha*shadow_alpha_factor<shadow_alpha_cutoff)discard;
    }
}
