#include "renderer/sprite_decal_mesh_hit.hpp"
#include "renderer/sprite_decal_attachment.hpp"
#include <cassert>
#include <cmath>

using namespace jkgm;
static bool near(float a,float b,float eps=0.0005f) {
    return std::fabs(a-b)<eps;
}
static decal_world_surface_geometry flat_receiver(
    float y,float min_x=-0.1f,float max_x=0.1f)
{
    decal_world_surface_geometry f{};
    // Counterclockwise from the shot's viewpoint (+Y travel): normal -Y.
    f.normal={{0,-1,0}};
    f.polygon={{{min_x,y,-0.1f}},{{max_x,y,-0.1f}},
               {{max_x,y,0.1f}},{{min_x,y,0.1f}}};
    f.bounds_min={{min_x,y,-0.1f}};
    f.bounds_max={{max_x,y,0.1f}};
    return f;
}
int main()
{
    decal_world_geometry actor{};
    actor.valid_world=true;
    actor.receivers.push_back(flat_receiver(0.06f));
    actor.receivers.push_back(flat_receiver(0.10f)); // rear of model
    auto const sphere_hit=decal_vec3{{0,-0.05f,0}};
    decal_vec3 const look{{0,1,0}};
    auto const hit=find_decal_mesh_contact(actor,sphere_hit,look);
    assert(hit.valid && hit.ray_hit);
    assert(near(hit.position[1],0.06f));
    assert(near(hit.travel,0.36f));
    assert(near(hit.miss,0.0f));
    assert(near(hit.normal[1],-1.0f));
    // Normalizing incoming Look should not affect the hit.
    auto const scaled=find_decal_mesh_contact(
        actor,sphere_hit,{{0,3,0}});
    assert(near(scaled.position[1],hit.position[1]));
    // A grazing collider hit misses the visible mesh by 0.03 world
    // units. Find its closest point to the projectile PATH (not just the
    // collision sphere position) without increasing decal thickness.
    auto const grazing=find_decal_mesh_contact(
        actor,{{0.13f,-0.05f,0}},look);
    assert(grazing.valid && !grazing.ray_hit);
    assert(near(grazing.position[0],0.10f));
    assert(near(grazing.position[1],0.06f));
    assert(near(grazing.miss,0.03f));
    assert(!find_decal_mesh_contact(
        actor,{{0.25f,-0.05f,0}},look).valid);
    // Preserve one-sided impact checks: a ray travelling from behind must
    // not snap to the front face as if it were an entrance hit.
    assert(!find_decal_mesh_contact(
        actor,{{0,0.05f,0}},{{0,-1,0}}).valid);
    // Parallel paths can still find the nearest triangle edge, while a
    // far-away projectile cannot be attached to an unrelated actor.
    assert(!find_decal_mesh_contact(
        actor,{{10.0f,0,0}},look).valid);
    // Ray contact always wins over a merely nearby triangle.
    decal_mesh_contact fallback{};
    fallback.valid=true; fallback.miss=0.001f;
    assert(decal_mesh_contact_better(hit,fallback));
    assert(!decal_mesh_contact_better(fallback,hit));
    // The SPR orientation still supplies texture roll. Only the GPU
    // projector is placed on the mesh, never the native Thing.
    sprite_axes axes{};
    axes.look=look;
    sprite_axes aligned{};
    assert(sprite_decal_attachment_axes(axes,hit.normal,aligned));
    decal_projector projector{};
    assert(make_sprite_decal_projector(
        aligned,hit.position,{{0,0,0}},0.08f,0.08f,1,1,0,projector));
    auto const center=decal_mesh_add(
        projector.origin,projector.look,projector.depth*0.5f);
    assert(near(center[1],0.06f));
    assert(near(projector.depth,0.08f)); // not increased by ray reach
    assert(near(projector.look[1],1.0f));
    return 0;
}
