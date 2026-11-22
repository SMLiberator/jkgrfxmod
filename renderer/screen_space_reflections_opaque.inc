        // Additional SSR receivers for depth-writing cached-world polygons
        // and GPU-resident 3DOs (vehicles, etc). Separate from transparent
        // receiver capture: opaque positions come from the finished G-buffer.
        void render_opaque_screen_reflections()
        {
            ssr_opaque_ready=false;
            if(!the_config || !the_config->experimental_screen_space_reflections ||
               !the_config->enable_reflections || !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame || !ogs || !frame_screen)
                return;
            auto const *frame=&frame_gpu_3do_camera;
            if(!frame->valid || !frame->perspective)
                frame=&frame_world_cache_camera;
            if(!frame->valid || !frame->perspective ||
               !frame->camera.valid || frame->far_plane<=0.1f)return;
            auto const &cam=frame->camera;
            float const aspect=float(get<x>(conf_scr_res))/
                               float(get<y>(conf_scr_res));
            int const width=std::max(1,int(std::ceil(
                float(get<x>(conf_scr_res))*the_config->ssr_resolution_scale-1.e-4f)));
            int const height=std::max(1,int(std::ceil(
                float(get<y>(conf_scr_res))*the_config->ssr_resolution_scale-1.e-4f)));

            struct opaque_world_item {
                world_cache_draw_candidate const *face=nullptr;
                gpu_3do_texture material{};
                int alpha_mode=0;
            };
            std::vector<opaque_world_item> world_items;
            if(world_cache_target && world_cache_target->ready &&
               frame_world_cache_camera.valid) {
                auto &cache=*world_cache_target;
                std::map<std::tuple<uintptr_t,int32_t,uint32_t>,
                         gpu_3do_texture> materials;
                for(auto const &face:cache.frame_render_candidates) {
                    if(face.count<3U)continue;
                    auto const key=std::make_tuple(face.material,face.cel,
                                                   face.geometry_mode);
                    auto it=materials.find(key);
                    if(it==materials.end()) {
                        gpu_3do_group group{};
                        group.material=face.material;group.cel=face.cel;
                        it=materials.emplace(key,face.geometry_mode==3U
                            ?resolve_gpu_3do_solid_material(group,false)
                            :resolve_gpu_3do_material(group)).first;
                    }
                    auto const &mat=it->second;
                    if(mat.reflection_mode!=1 ||
                       mat.reflection_strength<=0.0f)continue;
                    world_cache_material_info info{};
                    info.albedo_source=mat.albedo_source;
                    info.has_image=mat.has_image;info.alpha=mat.alpha;
                    info.masked=mat.masked;info.dithered=mat.dithered;
                    info.special=mat.special;
                    info.emissive_only=mat.emissive_only;
                    info.blend_mode=static_cast<int>(mat.blend);
                    info.opacity=mat.opacity;info.transition=mat.albedo_transition;
                    info.width=mat.width;info.height=mat.height;
                    info.alpha_cutoff=mat.alpha_cutoff;
                    auto const policy=choose_world_cache_draw_policy(face,info);
                    if(!world_cache_draws_in_pass(
                        policy,world_cache_pass::opaque))continue;
                    world_items.push_back({&face,mat,world_cache_alpha_mode(policy)});
                }
            }

            struct opaque_model_item {
                gpu_3do_instance const *instance=nullptr;
                gpu_3do_group const *group=nullptr;
                gpu_3do_texture material{};
                int alpha_mode=0;
            };
            std::vector<opaque_model_item> model_items;
            std::map<std::tuple<uintptr_t,int32_t,bool>,gpu_3do_texture> materials;
            if(frame_gpu_3do_camera.valid)for(auto const &inst:frame_gpu_3do_instances) {
                if(!inst.geometry || inst.shadow_only || inst.camera_hud ||
                   inst.world_lighting_exempt || inst.is_sprite_quad ||
                   inst.is_projected_decal || inst.thing_opacity<=0.0f ||
                   inst.saber_trail_active)continue;
                if(gpu_3do_gl_cache.find(inst.id)==gpu_3do_gl_cache.end())
                    continue; // reuse meshes uploaded by main opaque pass
                for(auto const &group:inst.geometry->groups) {
                    if(group.count<3U)continue;
                    bool const solid=inst.effective_geometry_mode==3 ||
                                     group.solid_color;
                    auto const key=std::make_tuple(group.material,group.cel,solid);
                    auto it=materials.find(key);
                    if(it==materials.end())
                        it=materials.emplace(key,solid
                            ?resolve_gpu_3do_solid_material(group,false)
                            :resolve_gpu_3do_material(group)).first;
                    auto const &mat=it->second;
                    if(mat.reflection_mode!=1 ||
                       mat.reflection_strength<=0.0f)continue;
                    bool const fading=inst.thing_opacity<0.999f;
                    auto const policy=choose_gpu_3do_alpha_mode(
                        (group.face_type&0x2U)!=0U ||
                            (fading&&!inst.thing_dither),
                        mat.alpha,mat.masked,mat.special||mat.emissive_only,
                        mat.opacity,mat.dithered || (fading&&inst.thing_dither));
                    if(!gpu_3do_draws_in_pass(policy,false))continue;
                    model_items.push_back({&inst,&group,mat,int(policy)});
                }
            }
            if(world_items.empty()&&model_items.empty())return;
            if(!ssr_opaque_receivers)
                ssr_opaque_receivers=std::make_unique<offscreen_capture_target>();
            if(!ssr_opaque_image)
                ssr_opaque_image=std::make_unique<offscreen_capture_target>();
            if(!ssr_opaque_receivers->ensure(width,height)||
               !ssr_opaque_image->ensure(width,height))return;
            if(!ssr_opaque_world_program)
                ssr_opaque_world_program=std::make_unique<
                    ssr_opaque_receiver_shader>(the_config->data_path,false);
            if(!ssr_opaque_model_program)
                ssr_opaque_model_program=std::make_unique<
                    ssr_opaque_receiver_shader>(the_config->data_path,true);
            if(!ssr_trace_program)
                ssr_trace_program=std::make_unique<ssr_trace_shader>(
                    the_config->data_path);
            if(!ssr_opaque_composite_program)
                ssr_opaque_composite_program=std::make_unique<
                    ssr_opaque_composite_shader>(the_config->data_path);
            offscreen_capture_gl_scope restore;
            GLint old_tex2=0,old_tex3=0;
            gl::set_active_texture_unit_raw(2);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_tex2);
            gl::set_active_texture_unit_raw(3);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_tex3);
            glBindFramebuffer(GL_FRAMEBUFFER,ssr_opaque_receivers->framebuffer());
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

            auto bind_material=[&](ssr_opaque_receiver_shader &program,
                                   gpu_3do_texture const &mat,int alpha_mode,
                                   float opacity) {
                program.material(mat.width,mat.height,mat.reflection_strength,
                    mat.roughness,alpha_mode,mat.alpha_cutoff,
                    opacity,mat.has_normal,mat.normal_strength,mat.normal_scroll,
                    mat.has_normal2,mat.normal_strength2,mat.normal_scroll2);
                gl::set_active_texture_unit(1);
                gl::bind_texture(gl::texture_bind_target::texture_2d,mat.texture);
                gl::set_active_texture_unit(2);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    mat.has_normal?mat.normal:gl::default_texture);
                gl::set_active_texture_unit(3);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    mat.has_normal2?mat.normal2:gl::default_texture);
            };
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             frame_screen->get_resolved_depth_texture());
            if(!world_items.empty() && world_cache_target) {
                auto &program=*ssr_opaque_world_program;
                program.use_program();
                program.camera(frame_world_cache_camera.native_view,cam,aspect,
                               frame_world_cache_camera.far_plane,width,height);
                gl::bind_vertex_array(world_cache_target->vao);
                for(auto const &item:world_items) {
                    bind_material(program,item.material,item.alpha_mode,
                                  item.material.opacity);
                    glDrawElements(GL_TRIANGLES,GLsizei(item.face->count),
                        GL_UNSIGNED_INT,reinterpret_cast<void const *>(
                            uintptr_t(item.face->first_index)*sizeof(uint32_t)));
                }
            }
            if(!model_items.empty()) {
                auto &program=*ssr_opaque_model_program;
                program.use_program();
                for(auto const &item:model_items) {
                    auto const &inst=*item.instance;
                    auto const it=gpu_3do_gl_cache.find(inst.id);
                    if(it==gpu_3do_gl_cache.end())continue;
                    auto const view=gpu_3do_native_modelview(
                        frame_gpu_3do_camera.native_view,inst.transform);
                    program.camera(view,cam,aspect,frame_gpu_3do_camera.far_plane,
                        width,height,it->second->packed_vertex_format?1:0);
                    gl::set_front_face_raw(gpu_3do_reversed_winding(inst.transform)
                                ?GL_CW:GL_CCW);
                    if((item.group->face_type&0x1U)!=0U)
                        gl::set_cull_face_enabled(false);
                    else gl::set_cull_face_enabled(true);
                    bind_material(program,item.material,item.alpha_mode,
                        item.material.opacity*inst.thing_opacity);
                    gl::bind_vertex_array(it->second->vao);
                    glDrawElements(GL_TRIANGLES,GLsizei(item.group->count),
                        GL_UNSIGNED_INT,reinterpret_cast<void const *>(
                            uintptr_t(item.group->offset)*sizeof(uint32_t)));
                }
            }
            gl::set_front_face_raw(GL_CCW);

            glBindFramebuffer(GL_FRAMEBUFFER,ssr_opaque_image->framebuffer());
            glViewport(0,0,width,height);
            glDisable(GL_DEPTH_TEST);
            gl::set_cull_face_enabled(false);
            glDepthMask(GL_FALSE);
            glClearColor(0.0f,0.0f,0.0f,0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            auto &trace=*ssr_trace_program;
            trace.use_program();
            trace.configure(cam,aspect,the_config->ssr_ray_steps,
                            the_config->ssr_max_distance,
                            the_config->ssr_ray_thickness,true);
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                gl::texture_view(ssr_opaque_receivers->color_texture()));
            gl::set_active_texture_unit(1);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                frame_screen->get_resolved_depth_texture());
            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                frame_screen->get_opaque_composite_texture());
            gl::bind_vertex_array(ogs->postmdl.vao);
            gl::draw_elements(gl::element_type::triangles,
                ogs->postmdl.num_indices,gl::index_type::uint32);

            // Prepare roughness mips before blending to the main target.
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            gl::set_active_texture_unit_raw(0);
            gl::bind_texture_raw(GL_TEXTURE_2D,ssr_opaque_image->color_texture());
            glGenerateMipmap(GL_TEXTURE_2D);
            gl::set_active_texture_unit_raw(2);
            gl::bind_texture_raw(GL_TEXTURE_2D,0); // no main-color feedback

            frame_screen->begin_compose_opaque_pass(true); // NO CLEAR
            glViewport(0,0,get<x>(conf_scr_res),get<y>(conf_scr_res));
            glDisable(GL_DEPTH_TEST);
            gl::set_cull_face_enabled(false);
            glEnable(GL_BLEND);
            // Preserve the opaque compositor's alpha/HOM coverage exactly.
            glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,
                                GL_ZERO,GL_ONE);
            auto &composite=*ssr_opaque_composite_program;
            composite.use_program();
            composite.configure();
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                gl::texture_view(ssr_opaque_image->color_texture()));
            gl::set_active_texture_unit(1);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                gl::texture_view(ssr_opaque_receivers->color_texture()));
            gl::bind_vertex_array(ogs->postmdl.vao);
            gl::draw_elements(gl::element_type::triangles,
                ogs->postmdl.num_indices,gl::index_type::uint32);

            gl::set_active_texture_unit_raw(3);
            gl::bind_texture_raw(GL_TEXTURE_2D,GLuint(old_tex3));
            gl::set_active_texture_unit_raw(2);
            gl::bind_texture_raw(GL_TEXTURE_2D,GLuint(old_tex2));
            gl::set_active_texture_unit_raw(0);
            ssr_opaque_ready=true;
            if(the_config->profile_renderer && gpu_3do_present_serial%120U==0U)
                LOG_INFO("SSR opaque world_faces=",world_items.size(),
                         " 3do_groups=",model_items.size(),
                         " resolution=",width,"x",height,
                         " scale=",the_config->ssr_resolution_scale,
                         " steps=",the_config->ssr_ray_steps,
                         " mode=world_and_cached_3do");
        }

