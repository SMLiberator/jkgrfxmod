#include "renderer/mots_collision_dispatch.hpp"
#include <cassert>
#include <limits>

using namespace jkgm;

int main()
{
    mots_collision_participant const sphere{1, 2.5f, 0.0f};
    mots_collision_participant const face{3, 4.0f, 0.0f};
    mots_collision_participant const tree{2, 5.0f, 9.0f};
    mots_collision_dispatch d{};

    // Original JK sphere-only behavior and ordinary raycasts with no mover.
    assert(select_mots_collision(nullptr, sphere, 0.25f, 0, true, d));
    assert(d.valid && d.path == mots_collision_path::sphere);
    assert(d.geometry == mots_collision_geometry::checked_thing);
    assert(!d.reverse_cast && !d.check_end_position);
    assert(d.broadphase_moving_radius == 0.25f);
    assert(d.broadphase_checked_radius == 2.5f);

    // Face collision can be requested by either Thing, with the latter
    // requiring the ray and contact normal to be reversed.
    assert(select_mots_collision(nullptr, face, 0.25f, 0, true, d));
    assert(d.path == mots_collision_path::faces && d.check_end_position);
    assert(!d.reverse_cast);
    assert(select_mots_collision(&face, sphere, 0.25f, 0, false, d));
    assert(d.path == mots_collision_path::faces);
    assert(d.geometry == mots_collision_geometry::moving_thing);
    assert(d.reverse_cast && d.narrowphase_moving_radius == 2.5f);

    // MotS: target owns the sphere tree; use its treeSize for broad phase.
    assert(select_mots_collision(nullptr, tree, 0.25f, 0, true, d));
    assert(d.path == mots_collision_path::sphere_tree && !d.reverse_cast);
    assert(d.broadphase_checked_radius == 9.0f);
    assert(d.broadphase_moving_radius == 0.25f);
    assert(d.narrowphase_moving_radius == 0.25f);

    // MotS: mover owns the tree; preserve the asymmetric radius exchange.
    assert(select_mots_collision(&tree, sphere, 0.25f, 0, true, d));
    assert(d.path == mots_collision_path::sphere_tree);
    assert(d.geometry == mots_collision_geometry::moving_thing);
    assert(d.reverse_cast && d.check_end_position);
    assert(d.broadphase_moving_radius == 9.0f);
    assert(d.broadphase_checked_radius == 0.25f);
    assert(d.narrowphase_moving_radius == 2.5f);

    // Type 2 takes priority over face collision while MotS is enabled,
    // regardless of which participant uses type 2.
    assert(select_mots_collision(&face, tree, 0.1f, 0, true, d));
    assert(d.path == mots_collision_path::sphere_tree && !d.reverse_cast);
    assert(select_mots_collision(&tree, face, 0.1f, 0, true, d));
    assert(d.path == mots_collision_path::sphere_tree && d.reverse_cast);

    // The original JK engine has no type-2 special path. Also, the
    // RAYCAST_80 flag forces sphere-only handling in either engine.
    assert(select_mots_collision(nullptr, tree, 0.25f, 0, false, d));
    assert(d.path == mots_collision_path::sphere);
    assert(d.broadphase_checked_radius == 5.0f);
    assert(select_mots_collision(&tree, face, 0.25f,
                                 mots_raycast_force_sphere, true, d));
    assert(d.path == mots_collision_path::sphere);
    assert(!d.check_end_position && !d.reverse_cast);

    // Unknown/invalid radii or absent collision must not corrupt output.
    auto const previous = d;
    assert(!select_mots_collision(nullptr, {0, 1.0f, 0}, 0.1f, 0, true, d));
    assert(!select_mots_collision(nullptr, tree,
                                  std::numeric_limits<float>::quiet_NaN(),
                                  0, true, d));
    assert(!select_mots_collision(nullptr, {2, 3.0f, -1.0f},
                                  0.1f, 0, true, d));
    assert(!select_mots_collision(nullptr, {1, -1.0f, 0},
                                  0.1f, 0, true, d));
    assert(d.path == previous.path && d.reverse_cast == previous.reverse_cast &&
           d.broadphase_checked_radius == previous.broadphase_checked_radius);
}
