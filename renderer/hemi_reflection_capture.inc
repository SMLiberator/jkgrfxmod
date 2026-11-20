
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
            if(!the_config->enable_reflections || !the_config->use_gpu_cache ||
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
            bool const realtime_supported=fragment_units>23 &&
                the_config->enable_reflection_capture;
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
                bool is_shared=false;
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
                       the_config->enable_reflection_probes &&
                       (reflection_mode==5 || reflection_mode==6) &&
                        (it->second.reflection_strength>0.0f ||
                         (fragment_units>=28 &&
                          get<0>(it->second.clearcoat_params)>0.0f))) {
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
            // One camera-centered six-face probe, captured only while a
            // currently eligible visible 3DO or world material requests it.
            // It is outside the per-Thing probe budget.
            bool shared_requested=false;
            if(realtime_supported) {
                for(auto const &inst:frame_gpu_3do_instances) {
                    if(!inst.geometry || inst.shadow_only || inst.camera_hud ||
                       inst.is_sprite_quad || inst.is_projected_decal ||
                       inst.thing_opacity<=0.0f)continue;
                    for(auto const &group:inst.geometry->groups) {
                        auto const key=std::make_pair(group.material,group.cel);
                        auto it=mats.find(key);
                        if(it==mats.end())
                            it=mats.emplace(
                                key,resolve_gpu_3do_material(group)).first;
                        if(effective_capture_reflection_mode(
                               it->second.reflection_mode)==7 &&
                           (it->second.reflection_strength>0.0f ||
                            (fragment_units>=28 &&
                             get<0>(it->second.clearcoat_params)>0.0f))) {
                            shared_requested=true;break;
                        }
                    }
                    if(shared_requested)break;
                }
                if(!shared_requested && world_cache_target &&
                   world_cache_target->ready) {
                    auto const &cache=*world_cache_target;
                    std::set<std::tuple<uintptr_t,int32_t,bool>> checked;
                    auto wants_shared=[&](auto const &faces) {
                        for(auto const &face:faces) {
                            if(face.count<3U || !face.material ||
                               face.geometry_mode<3U)continue;
                            bool const solid=face.geometry_mode==3U;
                            if(!checked.emplace(
                                face.material,face.cel,solid).second)continue;
                            gpu_3do_group group{};
                            group.material=face.material;group.cel=face.cel;
                            auto const mat=solid
                                ?resolve_gpu_3do_solid_material(group,false)
                                :resolve_gpu_3do_material(group);
                            if(effective_capture_reflection_mode(
                                   mat.reflection_mode)==7 &&
                               (mat.reflection_strength>0.0f ||
                                (fragment_units>=28 &&
                                 get<0>(mat.clearcoat_params)>0.0f)))return true;
                        }
                        return false;
                    };
                    shared_requested=wants_shared(cache.sector_candidates) ||
                        wants_shared(cache.sector_special_candidates);
                }
            }
            if(shared_requested) {
                candidate shared{};
                shared.owner=shared_reflection_owner_key;
                shared.mode=7;
                shared.position=viewer.position;
                shared.is_shared=true;
                candidates.push_back(shared);
                active_owners.insert(shared.owner);
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
            // Share the planar portal graph, but traverse independently for
            // each capture camera. Never borrow main-camera portal visibility.
            auto read_portals=[](uintptr_t at,void *dst,size_t n) {
                return engine_material_debug::read_bytes(at,dst,n);
            };
            auto &probe_world=*world_cache_target;
            auto &portals=probe_world.reflected_portal_graph;
            bool const portal_ready=
                engine_material_debug::matches_tested_executable() &&
                portals.ensure(read_portals,probe_world.loaded_world,
                    probe_world.revision,probe_world.world,
                    probe_world.mesh,probe_world.cached_surface_lookup);
            struct capture_portal_view {
                bool valid=false;
                std::vector<uint8_t> surfaces,thing_sectors;
            };
            auto build_capture_portal_view=[&](
                    engine_camera const &camera)->capture_portal_view {
                capture_portal_view result{};
                if(!portal_ready || !camera.valid)return result;
                auto const view=planar_camera_native_view(camera);
                float const half_fov=std::tan(
                    camera.fov*0.008726646259971648f);
                std::vector<world_cache_sector_span> spans;
                std::vector<planar_reflection_portal_rect> windows;
                auto const stats=portals.traverse(
                    read_portals,{},probe_world.mesh,
                    probe_world.cached_surface_lookup,view,camera.position,
                    half_fov,1.0f,spans,&windows,&result.thing_sectors);
                if(!stats.valid)return {};
                auto const &lookup=probe_world.cached_surface_lookup;
                result.surfaces.assign(lookup.size(),uint8_t(0));
                for(auto const &span:spans) {
                    for(uint32_t i=0;i<span.surface_count;++i) {
                        uint32_t const si=span.first_surface+i;
                        if(si>=lookup.size())continue;
                        uint32_t const cached=lookup[si];
                        if(cached>=probe_world.mesh.surfaces.size())continue;
                        auto const &range=probe_world.mesh.surfaces[cached];
                        if(range.surface_index!=si)continue;
                        if(portals.surface_in_window(
                            si,range.bounds_min,range.bounds_max,
                            view,half_fov,1.0f,windows))
                            result.surfaces[si]=1U;
                    }
                }
                result.valid=true;
                return result;
            };
            auto const now=std::chrono::steady_clock::now();
            for(auto const &candidate:candidates) {
                uint64_t const selected_owner=candidate.is_global
                    ?candidate.exclude_owner
                    :(candidate.is_shared?0U:candidate.owner);
                int const selected_mode=candidate.mode;
                auto const &focus=candidate.position;
                // Global COG capture uses native Thing eligibility, not the
                // sun-shadow dynamic-caster hint. That hint can be set by
                // PATH movement or assigned puppet resources, both allowed
                // in an explicit one-time environment snapshot.
                std::unordered_map<uint64_t,bool> static_capture_owners;
                // Keep each native ownership decision stable across six
                // directions, including when shadows are disabled entirely.
                auto capture_static_owner=[&](uint64_t owner)->bool {
                    if(!candidate.is_global)return true;
                    if(!owner)return false; // Unknown owners are never scenery.
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
                bool const paraboloid_mode=selected_mode==5;
                int const face_count=paraboloid_mode?2:6;
                if(paraboloid_mode && !paraboloid_capture_program)
                    paraboloid_capture_program=
                        std::make_unique<planar_reflection_capture_shader>(
                            the_config->data_path,true);
                auto &shader=paraboloid_mode
                    ?*paraboloid_capture_program:*hemi_capture_program;
                auto const far_plane=reflection_capture_scene_far(focus);
                if(!(std::isfinite(far_plane)&&far_plane>0.1f))continue;
                // Capture source: original 128x128 cube face. Hemi output:
                // two 256x256 disks with 5-degree overlap past the horizon.
                int const side=candidate.is_global
                    ?the_config->envmap_capture_face_resolution
                    :the_config->reflection_capture_resolution;
                int const output_side=paraboloid_mode?side*2:side;
                int const output_layers=paraboloid_mode?2:6;
                int const capture_side=paraboloid_mode?output_side:side;
                auto &entry=reflection_probes[candidate.owner];
                entry.seen_frame=gpu_3do_present_serial;
                bool const needs_reconfigure=entry.mode!=selected_mode ||
                    !entry.target || !entry.target->ready() ||
                    entry.target->width()!=output_side ||
                    entry.target->layers()!=output_layers;
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
                if(!probe_target->ensure(output_side,output_layers))continue;
                // Build a Thing-attached reflection family once per probe
                // refresh; all hemi/cube views share the same exclusions.
                // CaptureEnvMap keeps its separate Cog-only capture policy.
                std::unordered_set<uint64_t> reflection_excluded_owners{
                    selected_owner};
                if(!candidate.is_global && !candidate.is_shared &&
                   engine_material_debug::matches_tested_executable()) {
                    auto read=[](uintptr_t at,void *dst,size_t bytes) {
                        return engine_material_debug::read_bytes(at,dst,bytes);
                    };
                    reflection_excluded_owners=
                        build_reflection_attachment_family(
                            read,selected_owner);
                }
                size_t total_world_drawn=0,total_model_drawn=0;
                size_t total_acquired_nodes=0,total_model_batches=0;
                size_t total_model_groups_culled=0,total_world_bounds_culled=0;
                size_t total_thing_range_culled=0,capture_scene_preps=0;
                uint32_t portal_views_used=0U;
                // All 2-/6-view probe captures cover a full sphere.
                // Build directional portals once. Shared acquisition uses
                // their union, while rendering retains per-face masks.
                // If any directional portal view fails, the union fails
                // open instead of erasing potentially visible Things.
                std::array<capture_portal_view,6> cube_visibility{};
                capture_portal_view all_directions_visibility{};
                if(portal_ready) {
                    bool complete=true;
                    for(unsigned direction=0;direction<6U;++direction) {
                        auto const cube_camera=
                            make_cube_reflection_camera(focus,direction);
                        cube_visibility[direction]=
                            build_capture_portal_view(cube_camera);
                        auto const &face_visibility=cube_visibility[direction];
                        if(!face_visibility.valid) {
                            complete=false;continue;
                        }
                        if(!complete)continue;
                        if(direction==0U)
                            all_directions_visibility=face_visibility;
                        else {
                            if(all_directions_visibility.surfaces.size()!=
                                   face_visibility.surfaces.size() ||
                               all_directions_visibility.thing_sectors.size()!=
                                   face_visibility.thing_sectors.size()) {
                                complete=false;continue;
                            }
                            for(size_t i=0;i<
                                all_directions_visibility.surfaces.size();++i)
                                all_directions_visibility.surfaces[i] |=
                                    face_visibility.surfaces[i];
                            for(size_t i=0;i<
                                all_directions_visibility.thing_sectors.size();++i)
                                all_directions_visibility.thing_sectors[i] |=
                                    face_visibility.thing_sectors[i];
                        }
                    }
                    if(!complete)all_directions_visibility={};
                }
                // Independent discovery and posing are shared by every
                // face of the same probe (two disks or six cube faces).
                // Individual face visibility is still checked when drawing.
                auto prepare_scene=[&](
                    std::vector<uint8_t> const *sector_mask)
                    ->planar_reflection_offscreen_gpu {
                    ++capture_scene_preps;
                    planar_reflection_offscreen_gpu scene{};
                    if(!engine_material_debug::matches_tested_executable())
                        return scene;
                    uint32_t world=0;
                    if(!engine_material_debug::read_object(
                           engine_material_debug::current_world_global,world))
                        return scene;
                    auto read=[](uintptr_t at,void *dst,size_t bytes) {
                        return engine_material_debug::read_bytes(at,dst,bytes);
                    };
                    planar_reflection_thing_scan scan{};
                    if(!collect_planar_reflection_things(
                           read,world,scan,sector_mask))
                        return scene;
                    auto &things=scan.models;
                    things.erase(std::remove_if(things.begin(),things.end(),
                        [&](auto const &thing) {
                            uint64_t const owner=
                                planar_reflection_thing_owner_key(thing);
                            if(reflection_excluded_owners.count(owner)!=0U ||
                               (candidate.is_global &&
                                !capture_static_owner(owner)))return true;
                            // A shared full-sphere snapshot may only use
                            // radial culling, not an individual face frustum.
                            if(planar_reflection_thing_outside_probe_range(
                                   read,thing,focus,far_plane)) {
                                ++total_thing_range_culled;
                                return true;
                            }
                            return false;
                        }),things.end());
                    std::sort(things.begin(),things.end(),
                        [&](auto const &a,auto const &b) {
                            auto distance2=[&](auto const &t) {
                                float sum=0.0f;
                                for(unsigned j=0;j<3;++j) {
                                    float const d=t.position[j]-focus[j];
                                    sum+=d*d;
                                }
                                return sum;
                            };
                            return distance2(a)<distance2(b);
                        });
                    return acquire_planar_reflection_offscreen_gpu(
                        things,things.size(),nullptr,nullptr);
                };
                auto const *capture_thing_mask=
                    all_directions_visibility.valid &&
                    !all_directions_visibility.thing_sectors.empty()
                    ?&all_directions_visibility.thing_sectors:nullptr;
                // One complete animated node snapshot for every face.
                planar_reflection_offscreen_gpu capture_offscreen=
                    prepare_scene(capture_thing_mask);
                std::unordered_set<uint64_t> acquired;
                for(auto const &inst:capture_offscreen.nodes)
                    if(inst.shadow_owner)acquired.insert(inst.shadow_owner);
                engine_camera first_camera{};
                bool all_faces_ready=true;
                for(int face=0;face<face_count;++face) {
                    auto const camera=paraboloid_mode
                        ?make_dual_paraboloid_camera(focus,unsigned(face))
                        :make_cube_reflection_camera(focus,unsigned(face));
                    if(!camera.valid) {all_faces_ready=false;break;}
                    if(face==0)first_camera=camera;
                    auto const view=planar_camera_native_view(camera);
                    auto const projection=make_offscreen_capture_projection(
                        capture_side,capture_side,camera.fov,0.0f,far_plane);
                    if(!projection.valid) {all_faces_ready=false;break;}
                    auto const &portal_visibility=paraboloid_mode
                        ?all_directions_visibility
                        :cube_visibility[size_t(face)];
                    if(portal_visibility.valid)++portal_views_used;
                    auto const *thing_sector_mask=portal_visibility.valid &&
                        !portal_visibility.thing_sectors.empty()
                        ?&portal_visibility.thing_sectors:nullptr;
                    auto const *surface_mask=portal_visibility.valid
                        ?&portal_visibility.surfaces:nullptr;
                    gpu_3do_native_frustum frustum=
                        frame_world_cache_camera.frustum;
                    frustum.clip_far=0; // No artificial CPU far-distance cull.
                    frustum.near_plane=0.01f;
                    frustum.far_plane=far_plane;
                    frustum.far_left=-projection.half_horizontal;
                    frustum.right=projection.half_horizontal;
                    frustum.far_top=projection.half_horizontal;
                    frustum.bottom=-projection.half_horizontal;
                    auto const &offscreen=capture_offscreen;
            offscreen_capture_gl_scope restore;
            glBindFramebuffer(GL_FRAMEBUFFER,probe_target->framebuffer());
            if(!probe_target->select_layer(face)) {all_faces_ready=false;break;}
            glViewport(0,0,capture_side,capture_side);
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
            glFrontFace(paraboloid_mode?GL_CW:GL_CCW);
            if(paraboloid_mode) {
                glEnable(GL_CLIP_DISTANCE0);
                glEnable(GL_CLIP_DISTANCE1);
                glEnable(GL_CLIP_DISTANCE2);
            }
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
            if(paraboloid_mode)
                shader.set_paraboloid(unsigned(face),far_plane);
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
                projection,capture_side,capture_side,
                {{0.0f,0.0f,0.0f,0.0f}},false,false,
                paraboloid_mode,surface_mask);
            glFrontFace(paraboloid_mode?GL_CW:GL_CCW);
            shader.set_modelview(view,false);
            shader.set_thing_fade(1.0f,0);
            gl::bind_vertex_array(cache.vao);
            // Independently clipped portal sectors filter the world, while
            // unknown/ambiguous eyes fail open to whole-world frustum tests.
            // This never reuses the primary camera's visible sectors.
            std::map<std::tuple<uintptr_t,int32_t,bool>,
                     std::vector<world_cache_surface_range const *>> groups;
            for(auto const &surface:cache.mesh.surfaces) {
                if(surface_mask &&
                   (surface.surface_index>=surface_mask->size() ||
                    !(*surface_mask)[surface.surface_index]))continue;
                if(surface.count<3U || !surface.material ||
                   surface.passable_adjoin ||
                   (surface.surface_flags&0x600U)!=0U)continue;
                if(paraboloid_mode &&
                   dual_paraboloid_bounds_outside(
                       view,surface.bounds_min,surface.bounds_max,far_plane)) {
                    ++total_world_bounds_culled;continue;
                }
                if(!paraboloid_mode &&
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
            std::vector<gpu_3do_group const *> model_group_scratch;
            auto draw_model=[&](gpu_3do_instance const &inst) {
                if(reflection_excluded_owners.count(inst.shadow_owner)!=0U ||
                   inst.shadow_only || inst.camera_hud ||
                   inst.is_sprite_quad || inst.is_projected_decal ||
                   inst.thing_opacity<0.999f ||
                   inst.effective_geometry_mode<3U ||
                   !inst.geometry || !gpu_3do_matrix_finite(inst.transform))
                    return;
                // The same static+motion eligibility applies to
                // primary-camera instances, which bypass offscreen discovery.
                if(candidate.is_global &&
                   !capture_static_owner(inst.shadow_owner))return;
                // Main-view GPU fallback nodes are also filtered by this
                // capture's sector mask. Unknown Thing pointers fail open.
                if(thing_sector_mask && inst.shadow_owner) {
                    uintptr_t const source_thing=
                        uintptr_t(uint32_t(inst.shadow_owner>>32U));
                    uint32_t source_sector=0U;
                    if(planar_reflection_valid_native_pointer(source_thing) &&
                       read_portals(source_thing+0x5cU,&source_sector,4U) &&
                       !portals.thing_sector_visible(
                           source_sector,*thing_sector_mask))
                        return;
                }
                auto const modelview=gpu_3do_native_modelview(
                    view,inst.transform);
                if(!gpu_3do_matrix_finite(modelview))return;
                // Capture blending is disabled; alpha-tested and opaque
                // groups preserve their original ordering within the instance.
                // Only consecutive compatible groups share one MultiDraw.
                model_group_scratch.clear();
                auto group_key=[&](gpu_3do_group const *group) {
                    return std::make_tuple(
                        group->material,group->cel,
                        group->solid_color ||
                            inst.effective_geometry_mode==3U,
                        group->nonplanar || (group->face_type&1U)!=0U);
                };
                for(auto const &group:inst.geometry->groups) {
                    if(paraboloid_mode &&
                       dual_paraboloid_bounds_outside(
                           modelview,group.bounds_min,group.bounds_max,
                           far_plane)) {
                        ++total_model_groups_culled;continue;
                    }
                    if(!paraboloid_mode &&
                       gpu_3do_batch_outside_frustum(
                           frustum,modelview,
                           group.bounds_min,group.bounds_max))continue;
                    model_group_scratch.push_back(&group);
                }
                // No drawable groups: skip GL cache lookup, shader uniforms,
                // VAO binding and material resolution for this 3DO node.
                if(model_group_scratch.empty())return;
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
                shader.set_modelview(modelview,true,
                                     it->second->packed_vertex_format);
                shader.set_model_lighting(inst.ambient,inst.effective_light_mode,
                    make_point(inst.colormap_tint[0],inst.colormap_tint[1],
                               inst.colormap_tint[2]),
                    make_point(inst.sector_tint[0],inst.sector_tint[1],
                               inst.sector_tint[2]));
                shader.set_thing_fade(1.0f,0);
                gl::bind_vertex_array(it->second->vao);
                glFrontFace(paraboloid_mode
                    ?(gpu_3do_reversed_winding(inst.transform)?GL_CCW:GL_CW)
                    :(gpu_3do_reversed_winding(inst.transform)?GL_CW:GL_CCW));
                // Keep source order for exactly coplanar alpha cutouts.
                for(size_t i=0;i<model_group_scratch.size();) {
                    auto const key=group_key(model_group_scratch[i]);
                    size_t group_end=i+1U;
                    while(group_end<model_group_scratch.size() &&
                          group_key(model_group_scratch[group_end])==key)
                        ++group_end;
                    gpu_3do_group material_group{};
                    material_group.material=std::get<0>(key);
                    material_group.cel=std::get<1>(key);
                    bool const solid=std::get<2>(key);
                    auto const mat=solid
                        ?resolve_gpu_3do_solid_material(material_group,false)
                        :resolve_gpu_3do_material(material_group);
                    if(submit_material(mat,solid)) {
                        if(std::get<3>(key))glDisable(GL_CULL_FACE);
                        else glEnable(GL_CULL_FACE);
                        counts.clear();offsets.clear();
                        for(size_t k=i;k<group_end;++k) {
                            auto const *group=model_group_scratch[k];
                            counts.push_back(GLsizei(group->count));
                            offsets.push_back(
                                reinterpret_cast<void const *>(
                                    uintptr_t(group->offset)*sizeof(uint32_t)));
                        }
                        glMultiDrawElements(GL_TRIANGLES,counts.data(),
                            GL_UNSIGNED_INT,offsets.data(),
                            GLsizei(counts.size()));
                        model_drawn+=counts.size();
                        ++total_model_batches;
                    }
                    i=group_end;
                }
            };
            for(auto const &inst:offscreen.nodes)draw_model(inst);
            for(auto const &inst:frame_gpu_3do_instances)
                if(!inst.shadow_owner||!acquired.count(inst.shadow_owner))
                    draw_model(inst);
            total_world_drawn+=world_drawn;
            total_model_drawn+=model_drawn;
            if(face==0)
                total_acquired_nodes+=offscreen.stats.ready_nodes;
            } // Direct: two paraboloid passes; otherwise six cube faces.
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
                LOG_INFO(candidate.is_shared?"SHARED cube owner=":
                         (paraboloid_mode?"HEMI paraboloid owner=":"CUBE capture owner="),
                         selected_owner,
                         " capture_side=",capture_side,
                         " source_faces=",face_count,
                         " portal_views=",portal_views_used,
                         " result_side=",output_side,
                         " result_layers=",output_layers,
                         " world_faces=",total_world_drawn,
                         " model_groups=",total_model_drawn,
                         " multi_draw_batches=",total_model_batches,
                         " groups_bounds_culled=",total_model_groups_culled,
                         " world_bounds_culled=",total_world_bounds_culled,
                         " thing_range_culled=",total_thing_range_culled,
                         " scene_preparations=",capture_scene_preps,
                         " acquired_nodes=",total_acquired_nodes,
                         " cached_owners=",reflection_probes.size());
            } // Each owner receives its own independently retained probe.
        }

