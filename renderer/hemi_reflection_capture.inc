        // Experimental single-camera probe. The 160-degree square
        // perspective capture is deliberately an APPROXIMATION of a
        // hemisphere; unsupported directions fade to the original EnvMap.
        // One nearest eligible GPU 3DO owner gets one capture per frame.
        void render_hemi_capture()
        {
            hemi_capture_ready=false;
            hemi_capture_owner=0U;
            if(!the_config->experimental_hemi_reflection_capture ||
               !the_config->enable_reflections || !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame || !ogs ||
               !world_cache_target || !world_cache_target->ready ||
               !frame_world_cache_camera.valid ||
               !frame_world_cache_camera.perspective)return;
            // Extended fragment units are needed only with the new mode.
            // Fail open to the ordinary EnvMap on older GL 3.3 drivers.
            static GLint fragment_units=0;
            if(fragment_units==0)
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
            if(fragment_units<=23)return;
            auto const &viewer=frame_world_cache_camera.camera;
            std::array<float,3> focus{};
            uint64_t selected_owner=0;
            float best=std::numeric_limits<float>::infinity();
            std::unordered_set<uint64_t> visited;
            std::map<std::pair<uintptr_t,int32_t>,gpu_3do_texture> mats;
            for(auto const &instance:frame_gpu_3do_instances) {
                if(!instance.shadow_owner ||
                   visited.count(instance.shadow_owner)!=0U ||
                   !instance.owning_thing_world_position_valid ||
                   !instance.geometry || instance.shadow_only ||
                   instance.is_sprite_quad || instance.is_projected_decal ||
                   instance.camera_hud || instance.thing_opacity<=0.0f)
                    continue;
                bool reflective=false;
                for(auto const &group:instance.geometry->groups) {
                    auto const key=std::make_pair(group.material,group.cel);
                    auto it=mats.find(key);
                    if(it==mats.end())
                        it=mats.emplace(key,resolve_gpu_3do_material(group)).first;
                    if(it->second.reflection_mode==5 &&
                       it->second.reflection_strength>0.0f) {
                        reflective=true;break;
                    }
                }
                if(!reflective)continue;
                visited.insert(instance.shadow_owner);
                float d2=0.0f;
                for(unsigned a=0;a<3;++a) {
                    float const d=instance.owning_thing_world_position[a]-
                                  viewer.position[a];
                    d2+=d*d;
                }
                if(d2<best) {
                    best=d2;
                    focus=instance.owning_thing_world_position;
                    selected_owner=instance.shadow_owner;
                }
            }
            if(!selected_owner)return;
            auto const camera=make_hemi_reflection_camera(focus,viewer,160.0f);
            if(!camera.valid)return;
            auto const view=planar_camera_native_view(camera);
            float const far_plane=std::min(
                frame_world_cache_camera.far_plane,96.0f);
            if(!(std::isfinite(far_plane)&&far_plane>0.1f))return;
            // Square capture: horizontal and vertical FOV are both 160
            // degrees. Neither ordinary perspective nor this approximation
            // can reach an exact 180-degree horizon.
            int const shorter=std::min(get<x>(internal_scr_res),
                                       get<y>(internal_scr_res));
            int const side=std::max(32,int(std::ceil(
                float(shorter)*the_config->hemi_reflection_resolution_scale)));
            auto const projection=make_offscreen_capture_projection(
                side,side,camera.fov,0.0f,far_plane);
            if(!projection.valid)return;
            gpu_3do_native_frustum frustum=frame_world_cache_camera.frustum;
            frustum.clip_far=1;
            frustum.near_plane=0.01f;
            frustum.far_plane=far_plane;
            frustum.far_left=-projection.half_horizontal;
            frustum.right=projection.half_horizontal;
            frustum.far_top=projection.half_horizontal;
            frustum.bottom=-projection.half_horizontal;
            if(!hemi_capture_target)
                hemi_capture_target=std::make_unique<offscreen_capture_target>();
            if(!hemi_capture_target->ensure(side,side))return;
            if(!hemi_capture_program)
                hemi_capture_program=
                    std::make_unique<planar_reflection_capture_shader>(
                        the_config->data_path);
            auto &shader=*hemi_capture_program;
            // Independent model discovery allows the probe to see animated
            // Things outside the primary camera. The reflecting Thing itself
            // must not be captured from the inside.
            planar_reflection_offscreen_gpu offscreen{};
            if(engine_material_debug::matches_tested_executable()) {
                uint32_t world=0;
                if(engine_material_debug::read_object(
                       engine_material_debug::current_world_global,world)) {
                    auto read=[](uintptr_t at,void *dst,size_t bytes) {
                        return engine_material_debug::read_bytes(at,dst,bytes);
                    };
                    planar_reflection_thing_scan scan{};
                    if(collect_planar_reflection_things(read,world,scan)) {
                        auto &things=scan.models;
                        things.erase(std::remove_if(things.begin(),things.end(),
                            [&](auto const &thing) {
                                return planar_reflection_thing_owner_key(thing)==
                                           selected_owner ||
                                    planar_reflection_thing_outside_frustum(
                                        read,thing,frustum,view);
                            }),things.end());
                        std::sort(things.begin(),things.end(),
                            [&](auto const &a,auto const &b) {
                                auto distance2=[&](auto const &t) {
                                    float sum=0;
                                    for(unsigned j=0;j<3;++j) {
                                        float const delta=t.position[j]-focus[j];
                                        sum+=delta*delta;
                                    }
                                    return sum;
                                };
                                return distance2(a)<distance2(b);
                            });
                        offscreen=acquire_planar_reflection_offscreen_gpu(
                            things,64U,&frustum,&view);
                    }
                }
            }
            std::unordered_set<uint64_t> acquired;
            for(auto const &inst:offscreen.nodes)
                if(inst.shadow_owner)acquired.insert(inst.shadow_owner);
            offscreen_capture_gl_scope restore;
            glBindFramebuffer(GL_FRAMEBUFFER,hemi_capture_target->framebuffer());
            glViewport(0,0,side,side);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(0.0f,0.0f,0.0f,0.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            shader.use_program();
            shader.set_projection(projection);
            shader.set_clip({{0.0f,0.0f,0.0f,0.0f}},false);
            auto const fog=get_fog_settings();
            auto const fi=fog.info.value(),fc=fog.color.value();
            shader.set_fog(make_point(fi[0],fi[1],
                the_config->enable_fog?fi[2]:0.0f,0.0f),
                make_point(fc[0],fc[1],fc[2]));
            jk_palette_effect effect{};
            if(native_sector_palette_mode().load(
                std::memory_order_relaxed)!=0)
                effect=read_native_jk_hardware_color_effects();
            shader.set_color_effects(
                make_point(float(effect.filter[0]),float(effect.filter[1]),
                    float(effect.filter[2]),effect.active?1.0f:0.0f),
                make_point(effect.tint[0],effect.tint[1],effect.tint[2],0.0f),
                make_point(effect.add[0]/255.0f,effect.add[1]/255.0f,
                    effect.add[2]/255.0f,effect.fade),
                authored_light_limit(the_config->enable_baked_overbright));
            auto submit_material=[&](gpu_3do_texture const &mat,bool solid) {
                // Captured shaders draw only base pigment, not nested
                // reflections. Other shiny cars remain opaque geometry.
                if((mat.special&&!mat.emissive_only) ||
                   mat.blend==material_blend_mode::fluid ||
                   mat.blend==material_blend_mode::multiply ||
                   mat.emissive_only ||
                   !std::isfinite(mat.opacity) || mat.opacity<0.999f)
                    return false;
                bool const textured=!solid && mat.albedo_source==1 &&
                                     mat.has_image;
                if(!textured && !(solid || mat.albedo_source==2))return false;
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    textured?mat.texture:gl::default_texture);
                gl::set_active_texture_unit(1);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    mat.has_emission?mat.emission:gl::default_texture);
                shader.set_material(mat.width,mat.height,mat.tint,
                    mat.albedo_scroll,textured,
                    (mat.masked||mat.alpha||mat.dithered)?
                       mat.alpha_cutoff:0.0f);
                shader.set_emission(mat.has_emission,
                                    mat.emission_factor,
                                    mat.emissive_scroll,false);
                gl::set_active_texture_unit(0);
                return true;
            };
            size_t world_drawn=0,model_drawn=0;
            auto &cache=*world_cache_target;
            shader.set_modelview(view,false);
            shader.set_thing_fade(1.0f,0);
            gl::bind_vertex_array(cache.vao);
            // The full cached world is considered, not the main camera's
            // portal-visible subset. Frustum rejection keeps huge levels
            // from submitting distant non-reflected geometry.
            std::map<std::tuple<uintptr_t,int32_t,bool>,
                     std::vector<world_cache_surface_range const *>> groups;
            for(auto const &surface:cache.mesh.surfaces) {
                if(surface.count<3U || !surface.material ||
                   surface.passable_adjoin ||
                   (surface.surface_flags&0x600U)!=0U ||
                   gpu_3do_batch_outside_frustum(
                       frustum,view,surface.bounds_min,surface.bounds_max))
                    continue;
                groups[std::make_tuple(surface.material,surface.cel,
                                      !surface.has_uv)].push_back(&surface);
            }
            std::vector<GLsizei> counts;
            std::vector<void const *> offsets;
            for(auto const &entry:groups) {
                gpu_3do_group group{};
                group.material=std::get<0>(entry.first);
                group.cel=std::get<1>(entry.first);
                bool const solid=std::get<2>(entry.first);
                auto const mat=solid
                    ?resolve_gpu_3do_solid_material(group,false)
                    :resolve_gpu_3do_material(group);
                if(!submit_material(mat,solid))continue;
                counts.clear();offsets.clear();
                for(auto const *surface:entry.second) {
                    counts.push_back(GLsizei(surface->count));
                    offsets.push_back(reinterpret_cast<void const *>(
                        uintptr_t(surface->first_index)*sizeof(uint32_t)));
                }
                glMultiDrawElements(GL_TRIANGLES,counts.data(),
                    GL_UNSIGNED_INT,offsets.data(),GLsizei(counts.size()));
                world_drawn+=entry.second.size();
            }
            auto draw_model=[&](gpu_3do_instance const &inst) {
                if(inst.shadow_owner==selected_owner ||
                   inst.shadow_only || inst.camera_hud ||
                   inst.is_sprite_quad || inst.is_projected_decal ||
                   inst.thing_opacity<0.999f ||
                   inst.effective_geometry_mode<3U ||
                   !inst.geometry || !gpu_3do_matrix_finite(inst.transform))
                    return;
                auto it=gpu_3do_gl_cache.find(inst.id);
                if(it==gpu_3do_gl_cache.end()) {
                    if(gpu_3do_gl_cache.size()>=8192U)return;
                    it=gpu_3do_gl_cache.emplace(inst.id,
                        std::make_unique<gpu_3do_gl_mesh>(
                            the_config->experimental_packed_3do_vertices,
                            *inst.geometry,
                            the_config->profile_renderer
                                ?&packed_3do_upload_profile:nullptr)).first;
                }
                auto const modelview=gpu_3do_native_modelview(
                    view,inst.transform);
                if(!gpu_3do_matrix_finite(modelview))return;
                shader.set_modelview(modelview,true,
                                     it->second->packed_vertex_format);
                shader.set_model_lighting(inst.ambient,inst.effective_light_mode,
                    make_point(inst.colormap_tint[0],inst.colormap_tint[1],
                               inst.colormap_tint[2]),
                    make_point(inst.sector_tint[0],inst.sector_tint[1],
                               inst.sector_tint[2]));
                shader.set_thing_fade(1.0f,0);
                gl::bind_vertex_array(it->second->vao);
                glFrontFace(gpu_3do_reversed_winding(inst.transform)
                            ?GL_CW:GL_CCW);
                for(auto const &group:inst.geometry->groups) {
                    if(gpu_3do_batch_outside_frustum(
                        frustum,modelview,group.bounds_min,group.bounds_max))
                        continue;
                    bool const solid=group.solid_color ||
                                      inst.effective_geometry_mode==3U;
                    auto const mat=solid
                        ?resolve_gpu_3do_solid_material(group,false)
                        :resolve_gpu_3do_material(group);
                    if(!submit_material(mat,solid))continue;
                    if(group.nonplanar||(group.face_type&1U))
                        glDisable(GL_CULL_FACE);
                    else glEnable(GL_CULL_FACE);
                    gl::draw_elements(gl::element_type::triangles,
                        GLsizei(group.count),gl::index_type::uint32,
                        static_cast<ptrdiff_t>(
                            group.offset*sizeof(uint32_t)));
                    ++model_drawn;
                }
            };
            for(auto const &inst:offscreen.nodes)draw_model(inst);
            for(auto const &inst:frame_gpu_3do_instances)
                if(!inst.shadow_owner||!acquired.count(inst.shadow_owner))
                    draw_model(inst);
            gl::set_active_texture_unit(0);
            glBindTexture(GL_TEXTURE_2D,hemi_capture_target->color_texture());
            glGenerateMipmap(GL_TEXTURE_2D);
            hemi_capture_camera=camera;
            hemi_capture_owner=selected_owner;
            hemi_capture_ready=true;
            if(the_config->profile_renderer && gpu_3do_present_serial%120U==0U)
                LOG_INFO("HEMI capture owner=",selected_owner,
                         " side=",side," fov=",camera.fov,
                         " world_faces=",world_drawn,
                         " model_groups=",model_drawn,
                         " acquired_nodes=",offscreen.stats.ready_nodes,
                         " NOTE=one_wide_angle_view_not_true_fisheye");
        }

