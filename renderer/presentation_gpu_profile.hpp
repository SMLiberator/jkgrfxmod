#pragma once
// Asynchronous OpenGL timestamp profiler for the three presentation stages.
// Query results are consumed only after the last timestamp reports ready;
// never use GL_QUERY_RESULT to wait on a pending GPU command.
#include "glad/glad.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace jkgm {
    class presentation_gpu_profile {
    public:
        static constexpr size_t num_stages=3;
        static constexpr size_t ring_size=8;
        struct completed_sample {
            std::array<uint64_t,num_stages> nanoseconds{};
            bool ready=false;
            bool skipped=false;
        };

    private:
        struct slot {
            std::array<GLuint,num_stages*2> queries{};
            bool pending=false;
        };
        std::array<slot,ring_size> slots{};
        size_t cursor=0;
        bool checked_support=false;
        bool supported=false;

    public:
        bool is_supported() const { return supported; }

        // Called once per profiled game frame before stage 0. A full ring is
        // skipped for this frame; reusing an unfinished query would corrupt
        // the previous frame's result or introduce a GPU synchronization.
        bool begin(completed_sample &completed)
        {
            if(!checked_support) {
                checked_support=true;
                supported=glGenQueries && glDeleteQueries && glQueryCounter &&
                          glGetQueryObjectuiv && glGetQueryObjectui64v;
                if(supported) {
                    for(auto &entry:slots)
                        glGenQueries(GLsizei(entry.queries.size()),entry.queries.data());
                }
            }
            if(!supported) return false;
            auto &entry=slots[cursor];
            if(entry.pending) {
                GLuint ready=GL_FALSE;
                glGetQueryObjectuiv(entry.queries.back(),GL_QUERY_RESULT_AVAILABLE,&ready);
                if(ready!=GL_TRUE) {
                    completed.skipped=true;
                    return false;
                }
                for(size_t i=0;i<num_stages;++i) {
                    GLuint64 before=0,after=0;
                    glGetQueryObjectui64v(entry.queries[2*i],GL_QUERY_RESULT,&before);
                    glGetQueryObjectui64v(entry.queries[2*i+1],GL_QUERY_RESULT,&after);
                    completed.nanoseconds[i]=after>=before
                        ?uint64_t(after-before):0U;
                }
                completed.ready=true;
                entry.pending=false;
            }
            entry.pending=true;
            return true;
        }

        // start/end timestamp commands delimit actual GPU work. These are
        // never called unless begin() returned true for this frame.
        void timestamp(size_t stage,bool start)
        {
            glQueryCounter(slots[cursor].queries[stage*2+(start?0U:1U)],
                           GL_TIMESTAMP);
        }

        void end() { cursor=(cursor+1U)%ring_size; }

        // Called only when the owning OpenGL context is current.
        void release()
        {
            if(!supported)return;
            for(auto &entry:slots) {
                glDeleteQueries(GLsizei(entry.queries.size()),entry.queries.data());
                entry.pending=false;
            }
            supported=false;
            checked_support=false;
        }
    };
}
