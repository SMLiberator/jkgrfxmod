#include "renderer_compositor.hpp"
#include "common/config.hpp"
#include "renderer_compositor_basic.hpp"
#include "renderer_compositor_bloom.hpp"

std::unique_ptr<jkgm::renderer_compositor>
    jkgm::create_renderer_compositor(config const *the_config)
{
    if(the_config->enable_post && the_config->enable_bloom &&
       the_config->debug_render_mode == "none") {
        return std::make_unique<renderer_compositor_bloom>();
    }

    return std::make_unique<renderer_compositor_basic>();
}