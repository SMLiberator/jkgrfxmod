        // Reuse the same live sky-flag/GeoMode handling as the GPU world
        // renderer, but collect ALL secondary-camera sky apertures rather
        // than borrowing primary-camera portal visibility. Depth and frustum
        // rejection then select only actually visible sky pixels.
        void draw_reflection_capture_sky(
            planar_reflection_capture_shader &shader,
            engine_camera const &camera,gpu_3do_matrix34 const &view,
            gpu_3do_native_frustum const &frustum,
            offscreen_capture_projection const &projection,
            int viewport_width,int viewport_height,
            std::array<float,4> const &restore_clip,bool clip_enabled,
            bool mirrored,bool paraboloid=false,
            float radial_far=0.0f,
            std::vector<uint8_t> const *visible_surfaces=nullptr)
        {
            if(!world_cache_target || !world_cache_target->ready)return;
            auto &cache=*world_cache_target;
            auto read=[](uintptr_t address,void *dst,size_t bytes) {
                return engine_material_debug::read_bytes(address,dst,bytes);
            };
            if(reflection_sky_frame!=gpu_3do_present_serial ||
               reflection_sky_revision!=cache.revision ||
               reflection_sky_world!=cache.loaded_world) {
                reflection_sky_candidates.clear();
                world_cache_frame_stats stats{};
                std::vector<world_cache_draw_candidate> ordinary;
                // Reuse the scratch vector for thousands of world surfaces
                // instead of allocating on each candidate metadata read.
                for(auto const &range:cache.mesh.surfaces) {
                    ordinary.clear();
                    append_world_cache_candidate(read,cache.world.surfaces_addr,
                        range,ordinary,stats,nullptr,nullptr,nullptr,
                        &reflection_sky_candidates);
                }
                reflection_sky_frame=gpu_3do_present_serial;
                reflection_sky_revision=cache.revision;
                reflection_sky_world=cache.loaded_world;
            }
            if(reflection_sky_candidates.empty())return;
            world_cache_sky_info sky_info{};
            std::array<float,3> sky_angles{};
            if(!read_world_cache_sky_info(read,cache.loaded_world,sky_info) ||
               !world_cache_sky_angles(camera,sky_angles))return;
            shader.set_modelview(view,false);
            shader.set_thing_fade(1.0f,0);
            shader.set_clip({{0.0f,0.0f,0.0f,0.0f}},false);
            gl::bind_vertex_array(cache.vao);
            glFrontFace(paraboloid?GL_CW:(mirrored?GL_CW:GL_CCW));
            glEnable(GL_CULL_FACE);
            // JK does not far-clip sky apertures. Never reject the sky
            // because its authored polygons lie beyond a capture depth.
            auto sky_frustum=frustum;
            sky_frustum.clip_far=0;
            size_t drawn=0;
            for(auto const &face:reflection_sky_candidates) {
                auto const sky_type=world_cache_sky_type(face.sky_flags);
                if(sky_type==world_cache_sky_kind::none ||
                   (face.face_type&0x2U)!=0U ||
                   (sky_type==world_cache_sky_kind::horizon &&
                    !sky_info.horizon_valid) ||
                   (sky_type==world_cache_sky_kind::ceiling &&
                    !sky_info.ceiling_valid) ||
                   face.surface_index>=cache.cached_surface_lookup.size() ||
                   (visible_surfaces &&
                    (face.surface_index>=visible_surfaces->size() ||
                     !(*visible_surfaces)[face.surface_index])))
                    continue;
                auto const index=cache.cached_surface_lookup[face.surface_index];
                if(index>=cache.mesh.surfaces.size())continue;
                auto const &surface=cache.mesh.surfaces[index];
                if(!paraboloid && gpu_3do_batch_outside_frustum(
                        sky_frustum,view,surface.bounds_min,surface.bounds_max))
                    continue;
                gpu_3do_group group{};
                group.material=face.material;
                group.cel=face.cel;
                bool const solid=face.geometry_mode==3U;
                auto const mat=solid
                    ?resolve_gpu_3do_solid_material(group,false)
                    :resolve_gpu_3do_material(group);
                // Match main-camera visible opaque JK sky eligibility.
                if(mat.albedo_source!=(solid?2:1) ||
                   (!solid && !mat.has_image) ||
                   mat.alpha || mat.masked || mat.dithered ||
                   mat.special || mat.emissive_only ||
                   !std::isfinite(mat.opacity) || mat.opacity<0.999f ||
                   mat.albedo_transition!=0.0f ||
                   (!solid && (!(mat.width>0.0f) ||
                               !(mat.height>0.0f))))
                    continue;
                shader.set_capture_sky(int(sky_type),sky_info,sky_angles,
                    camera,projection.half_horizontal,
                    viewport_width,viewport_height,face.uv_offset);
                shader.set_material(mat.width,mat.height,mat.tint,
                    mat.albedo_scroll,!solid,0.0f);
                gl::set_active_texture_unit(0);
                gl::bind_texture(gl::texture_bind_target::texture_2d,
                    solid?gl::default_texture:mat.texture);
                gl::draw_elements(gl::element_type::triangles,
                    GLsizei(face.count),gl::index_type::uint32,
                    static_cast<ptrdiff_t>(
                        face.first_index*sizeof(uint32_t)));
                ++drawn;
            }
            shader.set_capture_sky(0,sky_info,sky_angles,camera,
                projection.half_horizontal,viewport_width,viewport_height,{});
            shader.set_clip(restore_clip,clip_enabled);
            gl::set_active_texture_unit(0);
            if(the_config->profile_renderer &&
               gpu_3do_present_serial%120U==0U)
                LOG_INFO("REFLECTION sky captured apertures=",drawn,
                         " candidates=",reflection_sky_candidates.size(),
                         " mirrored=",mirrored?1:0);
        }
