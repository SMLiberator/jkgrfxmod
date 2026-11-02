// Fluid absorption in the ORIGINAL native JKGM renderer's depth units.
// The R16F opaque depth and the fragment depths used by all three shaders
// are camera-forward distance multiplied by 32. Existing MAT packs were
// tuned to native's raw depth separation. Do NOT divide that separation by
// 32 here: it changes a material's opacity by an enormous factor.
//
// The viewing-ray ratio converts camera-forward thickness to the distance
// actually travelled through the liquid, preserving grazing-angle behavior.
// No opaque receiver behind the fluid means effectively infinite depth.
float jk_fluid_absorption(float opaque_depth,float fragment_depth,
                          float ray_ratio,float coefficient)
{
    if(opaque_depth>=65500.0)
        return coefficient>0.0 ? 1.0 : 0.0;
    float thickness=max(opaque_depth-fragment_depth,0.0)*ray_ratio;
    return clamp(1.0-exp(-coefficient*thickness),0.0,1.0);
}
