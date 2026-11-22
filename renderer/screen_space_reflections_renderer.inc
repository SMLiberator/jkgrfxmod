        // Called only after opaque composition, before forward-alpha draws.
        // Both targets are downscaled; the main-camera scene color and depth
        // are never attached as the SSR destination, including under MSAA.
        void render_world_screen_reflections()
        {
            ssr_ready=false;
            if(!the_config || !the_config->experimental_screen_space_reflections ||
               !the_config->enable_reflections || !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame || !ogs ||
               !world_cache_target || !world_cache_target->ready ||
               !frame_world_cache_camera.valid ||
               !frame_world_cache_camera.perspective || !frame_screen)
                return;
            auto const &cam=frame_world_cache_camera;
            if(!cam.camera.valid || cam.far_plane<=0.1f)return;
            auto &cache=*world_cache_target;
            if(cache.frame_render_candidates.empty())return;
            struct receiver {
                world_cache_draw_candidate const *face=nullptr;
                gpu_3do_texture material{};
            };
            std::vector<receiver> receivers;
            std::map<std::tuple<uintptr_t,int32_t,uint32_t>,
                     gpu_3do_texture> materials;
            for(auto const &face:cache.frame_render_candidates) {
                if(face.count<3U)continue;
                auto const key=std::make_tuple(face.material,face.cel,
                                               face.geometry_mode);
                auto it=materials.find(key);
                if(it==materials.end()) {
                    gpu_3do_group group{};
                    group.material=face.material;
                    group.cel=face.cel;
                    it=materials.emplace(key,face.geometry_mode==3U
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group)).first;
                }
                auto const &mat=it->second;
                if(mat.reflection_mode!=1)continue;
                world_cache_material_info info{};
                info.albedo_source=mat.albedo_source;
                info.has_image=mat.has_image;info.alpha=mat.alpha;
                info.masked=mat.masked;info.dithered=mat.dithered;
                info.special=mat.special;info.emissive_only=mat.emissive_only;
                info.blend_mode=static_cast<int>(mat.blend);
                info.opacity=mat.opacity;info.transition=mat.albedo_transition;
                info.width=mat.width;info.height=mat.height;
                info.alpha_cutoff=mat.alpha_cutoff;
                if(!world_cache_draws_in_pass(
                        choose_world_cache_draw_policy(face,info),
                        world_cache_pass::transparent))
                    continue;
                receivers.push_back({&face,mat});
            }
            if(receivers.empty())return;
            auto const width=std::max(1,int(std::ceil(
                float(get<x>(conf_scr_res))*the_config->ssr_resolution_scale-1.e-4f)));
            auto const height=std::max(1,int(std::ceil(
                float(get<y>(conf_scr_res))*the_config->ssr_resolution_scale-1.e-4f)));
            if(!ssr_receivers)
                ssr_receivers=std::make_unique<offscreen_capture_target>();
            if(!ssr_image)
                ssr_image=std::make_unique<offscreen_capture_target>();
            if(!ssr_receivers->ensure(width,height) ||
               !ssr_image->ensure(width,height))return;
            if(!ssr_receiver_program)
                ssr_receiver_program=std::make_unique<ssr_world_receiver_shader>(
                    the_config->data_path);
            if(!ssr_trace_program)
                ssr_trace_program=std::make_unique<ssr_trace_shader>(
                    the_config->data_path);
            // The standard GL scope saves units 0/1 and all framebuffer,
            // program, viewport, VAO, blend and depth state. Unit 2 is used
            // by the normal-map and color samplers and is restored below.
            offscreen_capture_gl_scope restore;
            GLint previous_unit2=0;
            gl::set_active_texture_unit_raw(2);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous_unit2);
            glBindFramebuffer(GL_FRAMEBUFFER,ssr_receivers->framebuffer());
            glViewport(0,0,width,height);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            gl::set_cull_face_enabled(true);
            gl::set_face_cull_mode(gl::face_mode::back);
            gl::set_front_face_raw(GL_CCW);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(0.0f,0.0f,0.0f,0.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            auto &receiver_program=*ssr_receiver_program;
            receiver_program.use_program();
            float const aspect=float(get<x>(conf_scr_res))/
                               float(get<y>(conf_scr_res));
            receiver_program.camera(cam.native_view,cam.camera.fov,
                                    aspect,cam.far_plane,width,height);
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_depth_texture());
            gl::bind_vertex_array(cache.vao);
            for(auto const &item:receivers) {
                auto const &mat=item.material;
                receiver_program.material(mat.width,mat.height,
                    mat.has_normal,mat.normal_strength,mat.normal_scroll,
                    mat.has_normal2,mat.normal_strength2,mat.normal_scroll2);
                gl::set_active_texture_unit(1);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    mat.has_normal ? mat.normal : gl::default_texture);
                gl::set_active_texture_unit(2);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    mat.has_normal2 ? mat.normal2 : gl::default_texture);
                glDrawElements(GL_TRIANGLES,GLsizei(item.face->count),
                    GL_UNSIGNED_INT,reinterpret_cast<void const *>(
                        uintptr_t(item.face->first_index)*sizeof(uint32_t)));
            }
            glBindFramebuffer(GL_FRAMEBUFFER,ssr_image->framebuffer());
            glViewport(0,0,width,height);
            glDisable(GL_DEPTH_TEST);
            gl::set_cull_face_enabled(false);
            glDepthMask(GL_FALSE);
            glClearColor(0.0f,0.0f,0.0f,0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            auto &trace=*ssr_trace_program;
            trace.use_program();
            trace.configure(cam.camera,aspect,the_config->ssr_ray_steps,
                            the_config->ssr_max_distance,
                            the_config->ssr_ray_thickness);
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                gl::texture_view(ssr_receivers->color_texture()));
            gl::set_active_texture_unit(1);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                frame_screen->get_resolved_depth_texture());
            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                frame_screen->get_opaque_composite_texture());
            gl::bind_vertex_array(ogs->postmdl.vao);
            gl::draw_elements(gl::element_type::triangles,
                ogs->postmdl.num_indices,gl::index_type::uint32);
            // Rough MATs sample lower-resolution SSR mips, analogous to
            // planar capture roughness. Render first, then mipmap exactly
            // once per frame. The destination is no longer bound for drawing.
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            gl::set_active_texture_unit_raw(0);
            gl::bind_texture_raw(GL_TEXTURE_2D,ssr_image->color_texture());
            glGenerateMipmap(GL_TEXTURE_2D);
            gl::set_active_texture_unit_raw(2);
            gl::bind_texture_raw(GL_TEXTURE_2D,GLuint(previous_unit2));
            gl::set_active_texture_unit_raw(0);
            ssr_ready=true;
            if(the_config->profile_renderer && gpu_3do_present_serial%120U==0U)
                LOG_INFO("SSR world receivers=",receivers.size(),
                         " resolution=",width,"x",height,
                         " scale=",the_config->ssr_resolution_scale,
                         " ray_steps=",the_config->ssr_ray_steps,
                         " max_distance=",the_config->ssr_max_distance,
                         " thickness=",the_config->ssr_ray_thickness,
                         " scope=world_forward_alpha");
        }

