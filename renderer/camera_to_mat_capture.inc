        // Each COG call queues exactly one photograph. Completed images are
        // kept until explicit recapture or ReloadMaterial; idle frames do
        // not render, and the original source Thing need not remain alive.
        void render_camera_to_mat_captures()
        {
            auto const requests=get_camera_to_mat_controls().snapshot();
            std::set<uint32_t> active;
            for(auto const &request:requests)active.insert(request.mat);
            for(auto it=camera_to_mat_targets.begin();
                it!=camera_to_mat_targets.end();) {
                if(!active.count(it->first))
                    it=camera_to_mat_targets.erase(it);
                else ++it;
            }
            if(requests.empty())return;
            // Only a new request generation may schedule rendering. In
            // particular, don't revalidate a captured Thing or traverse
            // portals/scan the world again for a completed photograph.
            std::vector<camera_to_mat_request> pending;
            pending.reserve(requests.size());
            for(auto const &request:requests) {
                auto const it=camera_to_mat_targets.find(request.mat);
                if(it!=camera_to_mat_targets.end() &&
                   !camera_to_mat_controls::needs_capture(
                       it->second.ready,it->second.generation,
                       request.generation))
                    continue;
                pending.push_back(request);
            }
            if(pending.empty())return;
            if(!the_config->use_gpu_cache || !ogs ||
               mode!=renderer_mode::ingame || !world_cache_target ||
               !world_cache_target->ready || !frame_world_cache_camera.valid ||
               !engine_material_debug::matches_tested_executable()) {
                // A temporary unavailable renderer doesn't erase a photo.
                // Pending requests can retry at the next eligible frame.
                return;
            }
            uint32_t world=0;
            if(!engine_material_debug::read_object(
                    engine_material_debug::current_world_global,world) ||
               !world || world!=world_cache_target->loaded_world)return;
            auto read=[](uintptr_t at,void *dst,size_t bytes) {
                return engine_material_debug::read_bytes(at,dst,bytes);
            };
            auto &cache=*world_cache_target;
            reflection_world_buckets.refresh(
                cache.loaded_world,cache.revision,cache.mesh.surfaces);
            bool const portal_ready=cache.reflected_portal_graph.ensure(
                read,cache.loaded_world,cache.revision,cache.world,
                cache.mesh,cache.cached_surface_lookup);
            if(!planar_capture_program)
                planar_capture_program=
                    std::make_unique<planar_reflection_capture_shader>(
                        the_config->data_path);
            auto &shader=*planar_capture_program;

            for(auto const &request:pending) {
                auto &entry=camera_to_mat_targets[request.mat];
                if(request.world!=world) {continue;}
                uintptr_t const thing=uintptr_t(uint32_t(request.owner>>32U));
                uint32_t signature=0,kind=0,flags=0;
                std::array<float,9> axes{};
                engine_camera camera{};
                if(!planar_reflection_valid_native_pointer(thing) ||
                   !read(thing+0x74U,&signature,4U) ||
                   !read(thing+0x0cU,&kind,4U) ||
                   !read(thing,&flags,4U) ||
                   !read(thing+0x88U,axes.data(),sizeof(axes)) ||
                   !read(thing+0xb8U,camera.position.data(),
                         sizeof(camera.position)) ||
                   signature!=uint32_t(request.owner) ||
                   kind<1U || kind>15U || (flags&0x2U)!=0U) {
                    // Hidden/disabled rendering flags are valid for a
                    // remote camera source; only destroyed Things fail.
                    continue;
                }
                bool axes_ok=true;
                for(float v:axes)axes_ok=axes_ok && std::isfinite(v);
                for(float v:camera.position)
                    axes_ok=axes_ok && std::isfinite(v);
                if(!axes_ok) {continue;}
                for(unsigned i=0;i<3U;++i) {
                    camera.right[i]=axes[i];
                    camera.forward[i]=axes[i+3U];
                    camera.up[i]=axes[i+6U];
                }
                // SithThing::orient is a rigid world-space matrix. Invalid
                // transforms fail closed rather than producing wild captures.
                auto norm2=[](std::array<float,3> const &v) {
                    return v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
                };
                if(std::fabs(norm2(camera.right)-1.0f)>0.1f ||
                   std::fabs(norm2(camera.forward)-1.0f)>0.1f ||
                   std::fabs(norm2(camera.up)-1.0f)>0.1f ||
                   std::fabs(engine_camera_detail::dot(
                       camera.right,camera.forward))>0.1f ||
                   std::fabs(engine_camera_detail::dot(
                       camera.right,camera.up))>0.1f ||
                   std::fabs(engine_camera_detail::dot(
                       camera.forward,camera.up))>0.1f) {
                    continue;
                }
                camera.fov=request.fov;
                camera.valid=true;

                // Resolve actual uploaded native MAT dimensions. This
                // identifies static as well as level MATs and validates D3D
                // handle generations before using their cached metadata.
                int width=0,height=0;
                for(auto const &link:runtime_mat_by_handle) {
                    if(link.second.mat_id!=request.mat ||
                       link.first==0U ||
                       link.first>vidmem_texture_surfaces.size())continue;
                    auto const &surface=vidmem_texture_surfaces[link.first-1U];
                    if(!surface || surface.get()!=link.second.surface ||
                       surface->texture_generation!=link.second.generation)
                        continue;
                    int const w=int(surface->desc.dwWidth);
                    int const h=int(surface->desc.dwHeight);
                    if(w>0 && h>0 && w<=16384 && h<=16384) {
                        width=w;height=h;break;
                    }
                }
                if(!width || !height) {continue;}
                if(!entry.target)
                    entry.target=std::make_unique<offscreen_capture_target>();
                if(!entry.target->ensure(width,height)) {
                    continue;
                }
                float const scene_far=reflection_capture_scene_far(camera.position);
                auto const projection=make_offscreen_capture_projection(
                    width,height,camera.fov,1.0f,scene_far);
                if(!projection.valid) {continue;}
                auto const view=planar_camera_native_view(camera);
                auto frustum=frame_world_cache_camera.frustum;
                frustum.clip_far=0;
                frustum.near_plane=0.01f;
                frustum.far_plane=scene_far;
                frustum.far_left=-projection.half_horizontal;
                frustum.right=projection.half_horizontal;
                frustum.far_top=projection.half_horizontal/projection.aspect;
                frustum.bottom=-projection.half_horizontal/projection.aspect;

                // Secondary-camera portal traversal is entirely independent
                // of the main camera. Orthographic views use full-scene
                // GPU clipping instead of a false perspective window.
                std::vector<uint8_t> visible_surfaces,visible_thing_sectors;
                if(portal_ready && !projection.orthographic) {
                    std::vector<world_cache_sector_span> spans;
                    std::vector<planar_reflection_portal_rect> windows;
                    auto &portals=cache.reflected_portal_graph;
                    auto const stats=portals.traverse(
                        read,{},cache.mesh,cache.cached_surface_lookup,
                        view,camera.position,projection.half_horizontal,
                        projection.aspect,spans,&windows,&visible_thing_sectors);
                    if(stats.valid) {
                        visible_surfaces.assign(
                            cache.cached_surface_lookup.size(),uint8_t(0));
                        for(auto const &span:spans)
                            for(uint32_t i=0;i<span.surface_count;++i) {
                                uint32_t const si=span.first_surface+i;
                                if(si>=cache.cached_surface_lookup.size())continue;
                                uint32_t const index=cache.cached_surface_lookup[si];
                                if(index>=cache.mesh.surfaces.size())continue;
                                auto const &surface=cache.mesh.surfaces[index];
                                if(surface.surface_index==si &&
                                   portals.surface_in_window(
                                       si,surface.bounds_min,surface.bounds_max,
                                       view,projection.half_horizontal,
                                       projection.aspect,windows))
                                    visible_surfaces[si]=1U;
                            }
                    } else visible_thing_sectors.clear();
                }

                // Native Thing discovery must use the new camera, never the
                // main viewer. For orthographic capture the standard JK
                // perspective frustum is inapplicable: conservative full
                // discovery and GPU clipping avoid false negatives.
                planar_reflection_offscreen_gpu offscreen{};
                planar_reflection_thing_scan scan{};
                if(collect_planar_reflection_things(read,world,scan,
                       visible_thing_sectors.empty()?nullptr:
                           &visible_thing_sectors)) {
                    auto &things=scan.models;
                    if(!projection.orthographic)
                        things.erase(std::remove_if(
                            things.begin(),things.end(),
                            [&](auto const &t) {
                                return planar_reflection_thing_outside_frustum(
                                    read,t,frustum,view);
                            }),things.end());
                    offscreen=acquire_planar_reflection_offscreen_gpu(
                        things,things.size(),
                        projection.orthographic?nullptr:&frustum,
                        projection.orthographic?nullptr:&view);
                }
                std::unordered_set<uint64_t> acquired;
                for(auto const &inst:offscreen.nodes)
                    if(inst.shadow_owner)acquired.insert(inst.shadow_owner);

                // No live monitor textures may be sampled inside ANY camera
                // feed pass, avoiding same-target feedback and capture chains.
                camera_to_mat_drawing=true;
                {
                    offscreen_capture_gl_scope restore;
                    glBindFramebuffer(GL_FRAMEBUFFER,
                                      entry.target->framebuffer());
                    glViewport(0,0,width,height);
                    glDisable(GL_BLEND);
                    glDisable(GL_SCISSOR_TEST);
                    glDisable(GL_STENCIL_TEST);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glDisable(GL_FRAMEBUFFER_SRGB);
                    glDisable(GL_CLIP_DISTANCE0);
                    glDisable(GL_CLIP_DISTANCE1);
                    glDisable(GL_CLIP_DISTANCE2);
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    gl::set_cull_face_enabled(true);
                    gl::set_face_cull_mode(gl::face_mode::back);
                    gl::set_front_face_raw(GL_CCW);
                    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
                    auto const fog=get_fog_settings();
                    auto const fog_rgb=fog.color.value();
                    glClearColor(fog_rgb[0],fog_rgb[1],fog_rgb[2],1.0f);
                    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                    shader.use_program();
                    shader.set_projection(projection);
                    shader.set_clip({{0.0f,0.0f,0.0f,0.0f}},false);
                    auto const info=fog.info.value();
                    shader.set_fog(make_point(info[0],info[1],
                        the_config->enable_fog?info[2]:0.0f,0.0f),
                        make_point(fog_rgb[0],fog_rgb[1],fog_rgb[2]));
                    jk_palette_effect effect{};
                    if(native_sector_palette_mode().load(
                            std::memory_order_relaxed)!=0)
                        effect=read_native_jk_hardware_color_effects();
                    shader.set_color_effects(
                        make_point(float(effect.filter[0]),
                                   float(effect.filter[1]),
                                   float(effect.filter[2]),
                                   effect.active?1.0f:0.0f),
                        make_point(effect.tint[0],effect.tint[1],
                                   effect.tint[2],0.0f),
                        make_point(effect.add[0]/255.0f,
                                   effect.add[1]/255.0f,
                                   effect.add[2]/255.0f,effect.fade),
                        authored_light_limit(the_config->enable_baked_overbright));
                    auto submit_mat=[&](gpu_3do_texture const &mat,
                                        bool solid)->bool {
                        if((mat.special && !mat.emissive_only) ||
                           mat.blend==material_blend_mode::fluid ||
                           mat.blend==material_blend_mode::multiply ||
                           mat.emissive_only ||
                           !std::isfinite(mat.opacity) ||
                           mat.opacity<0.999f)return false;
                        bool const textured=!solid &&
                            mat.albedo_source==1 && mat.has_image;
                        if(!textured &&
                           !(solid || mat.albedo_source==2))return false;
                        gl::set_active_texture_unit(0);
                        gl::bind_texture(
                            gl::texture_bind_target::texture_2d,
                            textured?mat.texture:gl::default_texture);
                        gl::set_active_texture_unit(1);
                        gl::bind_texture(
                            gl::texture_bind_target::texture_2d,
                            mat.has_emission?mat.emission:
                                gl::default_texture);
                        shader.set_material(mat.width,mat.height,mat.tint,
                            mat.albedo_scroll,textured,
                            (mat.alpha||mat.masked||mat.dithered)
                                ?mat.alpha_cutoff:0.0f);
                        shader.set_emission(mat.has_emission,
                            mat.emission_factor,mat.emissive_scroll,false);
                        gl::set_active_texture_unit(0);
                        return true;
                    };
                    // Perspective sky apertures are rendered before ordinary
                    // depth. Orthographic FOV=0 uses fog as the background.
                    if(!projection.orthographic)
                        draw_reflection_capture_sky(
                            shader,camera,view,frustum,projection,
                            width,height,{{0.0f,0.0f,0.0f,0.0f}},
                            false,false,false,
                            visible_surfaces.empty()?nullptr:&visible_surfaces);
                    shader.set_modelview(view,false);
                    shader.set_thing_fade(1.0f,0);
                    gl::bind_vertex_array(cache.vao);
                    std::vector<GLsizei> counts;
                    std::vector<void const *> offsets;
                    for(auto const &bucket:reflection_world_buckets.buckets()) {
                        counts.clear();offsets.clear();
                        for(size_t index:bucket.surfaces) {
                            auto const &surface=cache.mesh.surfaces[index];
                            if(!visible_surfaces.empty() &&
                               (surface.surface_index>=visible_surfaces.size() ||
                                !visible_surfaces[surface.surface_index]))
                                continue;
                            if(!projection.orthographic &&
                               gpu_3do_batch_outside_frustum(
                                   frustum,view,surface.bounds_min,
                                   surface.bounds_max))continue;
                            counts.push_back(GLsizei(surface.count));
                            offsets.push_back(reinterpret_cast<void const *>(
                                uintptr_t(surface.first_index)*
                                sizeof(uint32_t)));
                        }
                        if(counts.empty())continue;
                        gpu_3do_group group{};
                        group.material=std::get<0>(bucket.key);
                        group.cel=std::get<1>(bucket.key);
                        bool const solid=std::get<2>(bucket.key);
                        auto const mat=solid
                            ?resolve_gpu_3do_solid_material(group,false)
                            :resolve_gpu_3do_material(group);
                        if(!submit_mat(mat,solid))continue;
                        glMultiDrawElements(GL_TRIANGLES,counts.data(),
                            GL_UNSIGNED_INT,offsets.data(),GLsizei(counts.size()));
                    }
                    auto draw_model=[&](gpu_3do_instance const &inst) {
                        if(inst.shadow_only || inst.camera_hud ||
                           inst.is_sprite_quad || inst.is_projected_decal ||
                           inst.thing_opacity<=0.0f ||
                           inst.effective_geometry_mode<3U ||
                           !inst.geometry ||
                           !gpu_3do_matrix_finite(inst.transform))return;
                        if(!visible_thing_sectors.empty() && inst.shadow_owner) {
                            uintptr_t const source=uintptr_t(uint32_t(
                                inst.shadow_owner>>32U));
                            uint32_t sector=0U;
                            if(planar_reflection_valid_native_pointer(source) &&
                               read(source+0x5cU,&sector,4U) &&
                               !cache.reflected_portal_graph.thing_sector_visible(
                                   sector,visible_thing_sectors))return;
                        }
                        auto const modelview=gpu_3do_native_modelview(
                            view,inst.transform);
                        if(!gpu_3do_matrix_finite(modelview))return;
                        auto it=gpu_3do_gl_cache.find(inst.id);
                        if(it==gpu_3do_gl_cache.end()) {
                            if(gpu_3do_gl_cache.size()>=8192U)return;
                            it=gpu_3do_gl_cache.emplace(inst.id,
                                std::make_unique<gpu_3do_gl_mesh>(
                                    the_config->experimental_packed_3do_vertices,
                                    *inst.geometry)).first;
                        }
                        shader.set_modelview(modelview,true,
                            it->second->packed_vertex_format);
                        shader.set_model_lighting(inst.ambient,
                            inst.effective_light_mode,
                            make_point(inst.colormap_tint[0],
                                inst.colormap_tint[1],
                                inst.colormap_tint[2]),
                            make_point(inst.sector_tint[0],
                                inst.sector_tint[1],
                                inst.sector_tint[2]));
                        shader.set_thing_fade(inst.thing_opacity,
                            inst.thing_opacity<0.999f?2:0);
                        gl::bind_vertex_array(it->second->vao);
                        gl::set_front_face_raw(gpu_3do_reversed_winding(inst.transform)
                                    ?GL_CW:GL_CCW);
                        for(auto const &group:inst.geometry->groups) {
                            if(!projection.orthographic &&
                               gpu_3do_batch_outside_frustum(
                                   frustum,modelview,
                                   group.bounds_min,group.bounds_max))continue;
                            bool const solid=group.solid_color ||
                                inst.effective_geometry_mode==3U;
                            auto const mat=solid
                                ?resolve_gpu_3do_solid_material(group,false)
                                :resolve_gpu_3do_material(group);
                            if(!submit_mat(mat,solid))continue;
                            if(group.nonplanar || (group.face_type&1U))
                                gl::set_cull_face_enabled(false);
                            else gl::set_cull_face_enabled(true);
                            gl::draw_elements(gl::element_type::triangles,
                                GLsizei(group.count),
                                gl::index_type::uint32,
                                static_cast<ptrdiff_t>(
                                    group.offset*sizeof(uint32_t)));
                        }
                    };
                    for(auto const &inst:offscreen.nodes)draw_model(inst);
                    for(auto const &inst:frame_gpu_3do_instances)
                        if(!inst.shadow_owner ||
                           !acquired.count(inst.shadow_owner))
                            draw_model(inst);
                    gl::set_active_texture_unit(0);
                    gl::bind_texture_raw(GL_TEXTURE_2D,
                                  entry.target->color_texture());
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                camera_to_mat_drawing=false;
                // A successful capture finishes this exact generation.
                // Subsequent frames merely sample the retained texture.
                entry.generation=request.generation;
                entry.ready=true;
                if(the_config->profile_renderer)
                    LOG_INFO("CameraToMat photographed MAT=",request.mat,
                             " size=",width,"x",height,
                             " FOV=",request.fov,
                             " acquired_nodes=",offscreen.stats.ready_nodes);
            }
        }
