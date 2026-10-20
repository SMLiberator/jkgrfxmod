#pragma once
// Isolated reference for choosing JK/MotS thing collision modes.
// No engine hooks, memory access, or JKGM-only COG verbs.
// Mirrors the mode/geometry/broad-phase selection in OpenJKDF2's
// sithIntersect_CheckSphereThingIntersection, not the narrow-phase solver.
#include <cmath>
#include <cstdint>

namespace jkgm {
    enum class mots_collision_path {
        sphere, sphere_tree, faces
    };
    enum class mots_collision_geometry {
        checked_thing, moving_thing
    };
    struct mots_collision_participant {
        int collide = 0;
        float collide_size = 0.0f;
    };
    struct mots_collision_dispatch {
        bool valid = false;
        mots_collision_path path = mots_collision_path::sphere;
        mots_collision_geometry geometry = mots_collision_geometry::checked_thing;
        float broadphase_moving_radius = 0.0f;
        float broadphase_checked_radius = 0.0f;
        float narrowphase_moving_radius = 0.0f;
        // Type 2 has no separate treeSize. A future caller can either skip
        // broad-phase entirely, or compute a conservative bound directly
        // from the current animated mesh spheres.
        bool skip_broadphase = false;
        bool check_end_position = false;
        bool reverse_cast = false;
    };

    constexpr uint32_t mots_raycast_force_sphere = 0x80U;

    // 'moving' is allowed to be null for ordinary raycasts. This function
    // does NOT assume that the original JKGM SithThing has a treeSize field:
    // no extra per-Thing broad-phase radius is required.
    // A failure preserves the caller's output, allowing safe opt-in use.
    inline bool select_mots_collision(
        mots_collision_participant const *moving,
        mots_collision_participant const &checked,
        float moving_radius, uint32_t raycast_flags,
        bool mots_enabled, mots_collision_dispatch &output)
    {
        if(checked.collide == 0 || !std::isfinite(moving_radius) ||
           !std::isfinite(checked.collide_size) || moving_radius < 0.0f ||
           checked.collide_size < 0.0f)
            return false;

        bool const forced_sphere = (raycast_flags & mots_raycast_force_sphere) != 0;
        bool const tree = !forced_sphere && mots_enabled &&
            (checked.collide == 2 || (moving && moving->collide == 2));
        bool const face = !forced_sphere && !tree &&
            (checked.collide == 3 || (moving && moving->collide == 3));

        mots_collision_dispatch selected;
        selected.valid = true;
        selected.path = tree ? mots_collision_path::sphere_tree :
                        face ? mots_collision_path::faces :
                               mots_collision_path::sphere;
        selected.check_end_position = tree || face;
        selected.broadphase_moving_radius = moving_radius;
        selected.broadphase_checked_radius = checked.collide_size;
        selected.narrowphase_moving_radius = moving_radius;

        if(tree) {
            // The original MotS implementation uses a separately authored
            // treeSize. We intentionally omit it: the narrow-phase sees all
            // active mesh spheres, even if the model's normal collideSize
            // would be too small to enclose them.
            selected.skip_broadphase = true;
        }

        if(tree || face) {
            bool const use_checked = tree ? checked.collide == 2
                                          : checked.collide == 3;
            if(!use_checked) {
                if(!moving || !std::isfinite(checked.collide_size))
                    return false;
                selected.geometry = mots_collision_geometry::moving_thing;
                selected.reverse_cast = true;
                selected.narrowphase_moving_radius = checked.collide_size;
            }
        }

        output = selected;
        return true;
    }
}
