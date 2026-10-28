#pragma once

#include "math/color.hpp"
#include "math/direction.hpp"
#include "math/point.hpp"
#include "renderer_fwd.hpp"
#include <cstdint>
#include <vector>

namespace jkgm {
    struct triangle_vertex {
        // Member order is deliberate. The 16-byte SIMD members come first so
        // the 8- and 4-byte members share one trailing 16-byte slot instead
        // of each forcing its own padding: 48 bytes per vertex, not 64.
        point<4, float> pos;
        jkgm::color color;
        point<2, float> texcoords;
        uint32_t nativeNormal = 0U;  // packed JK per-vertex view normal (0 = unavailable)

        triangle_vertex();
        triangle_vertex(point<4, float> pos, point<2, float> texcoords,
                        jkgm::color color, uint32_t normalPacked = 0U);
    };

    struct triangle {
        triangle_vertex v0, v1, v2;
        // SIMD member before the 4-byte ones: 176 bytes per triangle, not 240.
        // Sorting and the per-frame upload move these structs around, so the
        // size matters. (Constructed only through constructors; no code
        // depends on the member order or on sizeof(triangle).)
        direction<3, float> normal;
        material_instance_id material = material_instance_id(0U);
        int num_sup = 0;

        triangle();
        triangle(triangle_vertex v0,
                 triangle_vertex v1,
                 triangle_vertex v2,
                 material_instance_id material);
    };

    // Guard against accidental padding regressions (see member order above).
    static_assert(sizeof(triangle_vertex) <= 48, "triangle_vertex grew: check member order");
    static_assert(sizeof(triangle) <= 176, "triangle grew: check member order");

    class triangle_batch {
    protected:
        std::vector<triangle> buffer;
        size_t num_triangles = 0U;

        void expand();

    public:
        triangle_batch();
        virtual ~triangle_batch() = default;

        size_t size() const;
        size_t capacity() const;
        void clear();
        void insert(triangle const &tri);

        virtual void sort();

        inline auto begin() const
        {
            return buffer.begin();
        }

        inline auto end() const
        {
            return buffer.begin() + num_triangles;
        }

        inline auto begin()
        {
            return buffer.begin();
        }

        inline auto end()
        {
            return buffer.begin() + num_triangles;
        }
    };

    class sorted_triangle_batch : public triangle_batch {
    private:
        uint64_t fallback_groups=0U;
        uint64_t fallback_triangles=0U;

    public:
        uint64_t sort_fallback_groups() const { return fallback_groups; }
        uint64_t sort_fallback_triangles() const { return fallback_triangles; }
        void sort() override;
        // Leave [begin, begin+first) in submission order (depth-writing
        // cutouts); painter-sort only the remaining blended triangles.
        void sort_from(size_t first);
    };
}