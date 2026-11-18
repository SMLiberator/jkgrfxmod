
        // Multiple independently retained GPU reflection probes. Owners are
        // selected from main-camera 3DOs, sorted nearest first, then capped to
        // limit memory and six-face capture cost. Each owner retains its
        // previous image between refreshes; no sharing of another car's map.
        void render_hemi_capture()
        {
            // The COG capture is independent of realtime per-Thing probes.
            // Retain completed EnvMaps between frames, dropping them only
            // when SetGlobalEnvMap/ResetGlobalEnvMap cancels the request.
            auto const global_request=get_global_env_capture_controls().snapshot();
            global_env_active_sequence=global_request
                ?global_request->sequence:0U;
            if(global_env_previous && global_env_current &&
               (global_env_fade_seconds<=0.0f ||
                std::chrono::duration<float>(
                    std::chrono::steady_clock::now()-
                    global_env_fade_start).count()>=global_env_fade_seconds))
                global_env_previous.reset();
            if(!global_request) {
                global_env_current.reset();
                global_env_previous.reset();
                global_env_completed_sequence=0U;
            }
            bool const capture_global=global_request &&
                global_request->sequence!=global_env_completed_sequence;
            if((!the_config->experimental_hemi_reflection_capture &&
                !the_config->experimental_cube_reflection_capture &&
                !capture_global) ||
               !the_config->enable_reflections || !the_config->use_gpu_cache ||
               mode!=renderer_mode::ingame || !ogs ||
               !world_cache_target || !world_cache_target->ready ||
               !frame_world_cache_camera.valid ||
               !frame_world_cache_camera.perspective) {
                reflection_probes.clear();
                return;
            }
            static GLint fragment_units=0;
            if(fragment_units==0)
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
            // Realtime probe sampling needs the extended sampler on unit 23,
            // but one-off COG captures use only units 0/1 and must also work
            // on ordinary GL 3.3 hardware with fewer fragment texture units.
            bool const realtime_supported=fragment_units>23;
            if(!realtime_supported && !capture_global) {
                reflection_probes.clear();
                return;
            }
            auto const &viewer=frame_world_cache_camera.camera;
            struct candidate {
                uint64_t owner=0U;
                int mode=0;
                std::array<float,3> position{};
                float distance2=0.0f;
                uint64_t exclude_owner=0U;
                bool is_global=false;
            };
            std::vector<candidate> candidates;
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
                int candidate_mode=0;
                for(auto const &group:instance.geometry->groups) {
                    auto const key=std::make_pair(group.material,group.cel);
                    auto it=mats.find(key);
                    if(it==mats.end())
                        it=mats.emplace(key,resolve_gpu_3do_material(group)).first;
                    int const reflection_mode=it->second.reflection_mode;
                    if(realtime_supported &&
                       ((reflection_mode==5 &&
                         the_config->experimental_hemi_reflection_capture) ||
                        (reflection_mode==6 &&
                         the_config->experimental_cube_reflection_capture)) &&
                        it->second.reflection_strength>0.0f) {
                        candidate_mode=reflection_mode;
                        break;
                    }
                }
                if(!candidate_mode)continue;
                visited.insert(instance.shadow_owner);
                candidate item{};
                item.owner=instance.shadow_owner;
                item.mode=candidate_mode;
                item.position=instance.owning_thing_world_position;
                for(unsigned a=0;a<3;++a) {
                    float const delta=item.position[a]-viewer.position[a];
                    item.distance2+=delta*delta;
                }
                candidates.push_back(item);
            }
            std::sort(candidates.begin(),candidates.end(),
                [](candidate const &a,candidate const &b) {
                    if(a.distance2!=b.distance2)
                        return a.distance2<b.distance2;
                    return a.owner<b.owner;
                });
            if(candidates.size()>the_config->reflection_probe_max_owners)
                candidates.resize(the_config->reflection_probe_max_owners);
            std::unordered_set<uint64_t> active_owners;
            for(auto const &c:candidates)active_owners.insert(c.owner);
            if(capture_global) {
                candidate global{};
                global.owner=0U; // never matches a real GPU 3DO owner
                global.mode=6;
                global.position=global_request->position;
                global.exclude_owner=global_request->exclude_owner;
                global.is_global=true;
                candidates.push_back(global);
                active_owners.insert(0U);
            }
            // Free targets belonging to missing/non-selected owners, including
            // destroyed Things; the cached images cannot leak across worlds.
            for(auto it=reflection_probes.begin();it!=reflection_probes.end();) {
                if(active_owners.count(it->first)==0U)
                    it=reflection_probes.erase(it);
                else ++it;
            }
            if(candidates.empty())return;
            if(!hemi_capture_program)
                hemi_capture_program=
                    std::make_unique<planar_reflection_capture_shader>(
                        the_config->data_path);
            auto &shader=*hemi_capture_program;
            auto const now=std::chrono::steady_clock::now();
            for(auto const &candidate:candidates) {
                uint64_t const selected_owner=candidate.is_global
                    ?candidate.exclude_owner:candidate.owner;
                int const selected_mode=candidate.mode;
                auto const &focus=candidate.position;
            std::unordered_map<uint64_t,bool> static_capture_owners;
            // Keep each native ownership decision stable across all six
            // directions. This must also work with sun shadows disabled.
            auto capture_static_owner=[&](uint64_t owner)->bool {
                if(!candidate.is_global)return true;
                if(!owner)return false; // Unknown geometry is not persistent scenery.
                auto found=static_capture_owners.find(owner);
                if(found!=static_capture_owners.end())return found->second;
                auto read=[](uintptr_t address,void *dst,size_t bytes) {
                    return engine_material_debug::read_bytes(
                        address,dst,bytes);
                };
                bool const eligible=global_env_capture_static_thing(
                    read,owner);
                static_capture_owners.emplace(owner,eligible);
                return eligible;
            };
            // The native sun hook already records scripted pose changes for
            // main-camera models when either shadow system is active.
            bool const native_motion_tracking_enabled=
                the_config->enable_sun_shadows ||
                (the_config->enable_spotlights &&
                 the_config->enable_spotlight_shadows);
                bool const cube_mode=selected_mode==6;
                int const face_count=cube_mode?6:1;
                auto const far_plane=std::min(
                    frame_world_cache_camera.far_plane,96.0f);
                if(!(std::isfinite(far_plane)&&far_plane>0.1f))continue;
                // Unified per-face capture size. The 160-degree hemi
                // view uses twice the linear resolution to recover some
                // peripheral detail at its extreme perspective angles.
                int const side=candidate.is_global
                    ?the_config->envmap_capture_face_resolution
                    :the_config->reflection_capture_resolution*
                        (cube_mode?1:2);
                auto &entry=reflection_probes[candidate.owner];
                entry.seen_frame=gpu_3do_present_serial;
                bool const needs_reconfigure=entry.mode!=selected_mode ||
                    !entry.target || !entry.target->ready() ||
                    entry.target->width()!=side ||
                    entry.target->layers()!=face_count;
                float const interval=candidate.is_global?0.0f:
                    the_config->reflection_capture_update_interval;
                if(entry.ready && !needs_reconfigure && interval>0.0f &&
                   std::chrono::duration<float>(
                       now-entry.last_refresh).count()<interval)
                    continue; // Reuse this Thing's complete previous probe.
                entry.ready=false;
                entry.mode=selected_mode;
                if(!entry.target)
                    entry.target=
                        std::make_unique<reflection_probe_capture_target>();
                auto *probe_target=entry.target.get();
                if(!probe_target->ensure(side,face_count))continue;
                size_t total_world_drawn=0,total_model_drawn=0;
                size_t total_acquired_nodes=0;
                engine_camera first_camera{};
                bool all_faces_ready=true;
                for(int face=0;face<face_count;++face) {
                    auto const camera=cube_mode
                        ?make_cube_reflection_camera(focus,unsigned(face))
                        :make_hemi_reflection_camera(focus,viewer,160.0f);
                    if(!camera.valid) {all_faces_ready=false;break;}
                    if(face==0)first_camera=camera;
                    auto const view=planar_camera_native_view(camera);
                    auto const projection=make_offscreen_capture_projection(
                        side,side,camera.fov,0.0f,far_plane);
                    if(!projection.valid) {all_faces_ready=false;break;}
                    gpu_3do_native_frustum frustum=
                        frame_world_cache_camera.frustum;
                    frustum.clip_far=1;
                    frustum.near_plane=0.01f;
                    frustum.far_plane=far_plane;
                    frustum.far_left=-projection.half_horizontal;
                    frustum.right=projection.half_horizontal;
                    frustum.far_top=projection.half_horizontal;
                    frustum.bottom=-projection.half_horizontal;
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
                                uint64_t const owner=
                                    planar_reflection_thing_owner_key(thing);
                                return owner==selected_owner ||
                                    (candidate.is_global &&
                                     !capture_static_owner(owner)) ||
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
            glBindFramebuffer(GL_FRAMEBUFFER,probe_target->framebuffer());
            if(!probe_target->select_layer(face)) {all_faces_ready=false;break;}
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
            // A globally captured EnvMap must remain opaque even where
            // sky faces or translucent surfaces are not rendered. Use fog
            // as an approximate environment background; never leak black.
            auto const background=get_fog_settings().color.value();
            glClearColor(candidate.is_global?background[0]:0.0f,
                         candidate.is_global?background[1]:0.0f,
                         candidate.is_global?background[2]:0.0f,
                         candidate.is_global?1.0f:0.0f);
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
            draw_reflection_capture_sky(shader,camera,view,frustum,
                projection,side,side,{{0.0f,0.0f,0.0f,0.0f}},false,false);
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
                // The same eligibility applies to primary-camera instances,
                // which otherwise bypass independent offscreen discovery.
                // Sun motion history additionally rejects scripted pose edits
                // on model Things whose movement type remains NONE.
                if(candidate.is_global &&
                   (!capture_static_owner(inst.shadow_owner) ||
                    (native_motion_tracking_enabled &&
                     inst.dynamic_sun_caster)))return;
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
            total_world_drawn+=world_drawn;
            total_model_drawn+=model_drawn;
            total_acquired_nodes+=offscreen.stats.ready_nodes;
            } // Capture each independent cube face (or one hemi view).
            // Texture-array mip levels are filtered independently per face.
            // Restore the array binding at texture unit zero so other passes
            // never inherit this optional capture target.
            gl::set_active_texture_unit(0);
            GLint previous_array=0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&previous_array);
            glBindTexture(GL_TEXTURE_2D_ARRAY,
                          probe_target->color_texture());
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glBindTexture(GL_TEXTURE_2D_ARRAY,GLuint(previous_array));
            if(!all_faces_ready)continue;
            entry.camera=first_camera;
            entry.mode=selected_mode;
            entry.ready=true;
            entry.last_refresh=now;
            if(candidate.is_global) {
                // Publish only after all six scene views and the complete
                // equirectangular image have been drawn. A failure leaves
                // the previous global map untouched and retries next frame.
                if(!global_env_converter)
                    global_env_converter=
                        std::make_unique<global_env_equirect_converter>();
                auto output=std::make_unique<offscreen_capture_target>();
                auto const background=get_fog_settings().color.value();
                if(global_env_converter->convert(
                        *probe_target,*output,side,
                        {{background[0],background[1],background[2]}})) {
                    global_env_previous=std::move(global_env_current);
                    global_env_current=std::move(output);
                    global_env_fade_seconds=global_request->fade_time;
                    global_env_fade_start=now;
                    global_env_completed_sequence=global_request->sequence;
                    LOG_INFO("CaptureEnvMap published: source=",selected_owner,
                             " face=",side," equirect=",side*4,"x",side*2,
                             " fade=",global_env_fade_seconds);
                }
                reflection_probes.erase(0U);
                continue;
            }
            if(the_config->profile_renderer && gpu_3do_present_serial%120U==0U)
                LOG_INFO(cube_mode?"CUBE capture owner=":"HEMI capture owner=",
                         selected_owner,
                         " side=",side," faces=",face_count,
                         " fov=",first_camera.fov,
                         " world_faces=",total_world_drawn,
                         " model_groups=",total_model_drawn,
                         " acquired_nodes=",total_acquired_nodes,
                         " cached_owners=",reflection_probes.size());
            } // Each owner receives its own independently retained probe.
        }

