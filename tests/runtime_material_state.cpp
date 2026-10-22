#include "../renderer/runtime_material.hpp"

#include <cassert>

int main()
{
    using namespace jkgm;
    runtime_material_controls controls;
    controls.prepare_world(0x123456);
    auto initial = controls.snapshot();
    assert(initial.empty());

    controls.set_mat_albedo(76, 1, 0.0f);
    auto first = controls.snapshot();
    assert(first.revision > initial.revision);
    assert(first.per_mat.at(76).albedo->to->image_id == 1);

    // Replace a completed first image with another from the same pack.
    controls.set_mat_albedo(76, 2, 0.0f);
    auto second = controls.snapshot();
    auto const &transition = *second.per_mat.at(76).albedo;
    assert(transition.from && transition.from->image_id == 1);
    assert(transition.to && transition.to->image_id == 2);

    // Reset must restore the ordinary compiled/vanilla albedo.
    controls.reset_mat_albedo(76, 0.0f);
    assert(controls.snapshot().empty());

    // A further set after reset must be clean (no stale previous image).
    controls.set_mat_albedo(76, 3, 0.0f);
    auto third = controls.snapshot();
    assert(!third.per_mat.at(76).albedo->from);
    assert(third.per_mat.at(76).albedo->to->image_id == 3);
    controls.reset_mat_albedo(76, 0.0f);
    assert(controls.snapshot().empty());

    // Color factor is a relative multiplier; reset returns to identity.
    controls.set_mat_color(76, {{0.2f, 0.5f, 1.0f}}, 0.0f);
    assert(controls.snapshot().per_mat.at(76).color->value()[0] == 0.2f);
    controls.reset_mat_color(76, 0.0f);
    assert(controls.snapshot().empty());

    // A global image source and a per-material override can coexist.
    controls.set_global_env(32781, 1, 0.0f);
    controls.set_mat_env(76, 32781, 2, 0.0f);
    auto env = controls.snapshot();
    assert(env.global_env && env.global_env->to->source_mat == 32781);
    assert(env.per_mat.at(76).env->to->image_id == 2);
    controls.reset_mat_env(76, 0.0f);
    controls.reset_global_env(0.0f);
    assert(controls.snapshot().empty());

    // A missing global source MAT or image ID must not erase a pack's
    // existing EnvMap: this was the all-black/cleared-env regression.
    auto resolve_image=[](runtime_image_key const &key)->std::optional<int> {
        if(key.source_mat==32781 && key.image_id==1) return 101;
        if(key.source_mat==32781 && key.image_id==2) return 102;
        return std::nullopt;
    };
    std::optional<int> from,to;
    float weight=-1.0f;
    runtime_image_transition pending{};
    pending.to=runtime_image_key{32781,9};
    assert(!resolve_runtime_transition_images(
        pending,std::optional<int>(55),resolve_image,from,to,weight));
    assert(from && *from==55 && to && *to==55 && weight==0.0f);
    pending.from=runtime_image_key{32781,1};
    assert(!resolve_runtime_transition_images(
        pending,std::optional<int>(55),resolve_image,from,to,weight));
    assert(from && *from==101 && to && *to==101 && weight==0.0f);
    // A missing old fade source must not poison a successfully resolved
    // new target, and reset to default must not hide the material.
    pending.from=runtime_image_key{32781,9};
    pending.to=runtime_image_key{32781,2};
    assert(resolve_runtime_transition_images(
        pending,std::optional<int>(55),resolve_image,from,to,weight));
    assert(from && *from==102 && to && *to==102 && weight==0.0f);
    pending.to.reset();
    assert(resolve_runtime_transition_images(
        pending,std::optional<int>(55),resolve_image,from,to,weight));
    assert(from && *from==55 && to && *to==55 && weight==0.0f);

    // Simulate a level transition; same-world COG requests must persist.
    controls.set_mat_color(76, {{0.4f, 0.5f, 0.6f}}, 0.0f);
    controls.prepare_world(0x123456);
    assert(!controls.snapshot().empty());
    controls.prepare_world(0x234567);
    assert(controls.snapshot().empty());
    return 0;
}
