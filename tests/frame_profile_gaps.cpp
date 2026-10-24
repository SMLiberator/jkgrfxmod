#include "renderer/frame_profile.hpp"
#include <cassert>
#include <cstdint>

int main()
{
    jkgm::profile_sample a{};
    a.before_first_execute_us = 110;
    a.between_executes_us = 20;
    a.after_last_execute_us = 30;
    a.execute_calls = 2;
    a.execute_vertices = 500;
    a.execute_instruction_bytes = 7000;
    a.frames_with_execute = 1;
    a.sprite_type_calls[0] = 10;
    a.sprite_native_calls = 10;
    a.sprite_native_ns = 123456;
    a.sprite_projected_gpu = 3;
    a.gpu_ready_cache_hits = 25;
    a.gpu_mat_preload_attempts = 2;
    a.gpu_mat_preload_success = 1;

    jkgm::profile_sample b{};
    b.before_first_execute_us = 200;
    b.after_last_execute_us = 50;
    b.execute_calls = 1;
    b.execute_vertices = 400;
    b.execute_instruction_bytes = 1000;
    b.frames_with_execute = 1;
    b.sprite_type_calls[0] = 12;
    b.sprite_type_calls[4] = 5;
    b.sprite_native_calls = 17;
    b.sprite_native_ns = 222222;
    b.sprite_projected_gpu = 2;
    b.gpu_ready_cache_hits = 30;
    b.gpu_mat_preload_attempts = 3;
    b.gpu_mat_preload_success = 2;

    jkgm::profile_sample combined{};
    combined.add(a);
    combined.add(b);
    assert(combined.before_first_execute_us == 310);
    assert(combined.between_executes_us == 20);
    assert(combined.after_last_execute_us == 80);
    assert(combined.execute_calls == 3);
    assert(combined.execute_vertices == 900);
    assert(combined.execute_instruction_bytes == 8000);
    assert(combined.frames_with_execute == 2);
    assert(combined.sprite_type_calls[0] == 22);
    assert(combined.sprite_type_calls[4] == 5);
    assert(combined.sprite_native_calls == 27);
    assert(combined.sprite_native_ns == 345678);
    assert(combined.sprite_projected_gpu == 5);
    assert(combined.gpu_ready_cache_hits == 55);
    assert(combined.gpu_mat_preload_attempts == 5);
    assert(combined.gpu_mat_preload_success == 3);
    return 0;
}
