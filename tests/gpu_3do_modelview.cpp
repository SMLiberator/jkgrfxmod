#include "renderer/gpu_3do_modelview.hpp"
#include <array>
#include <cassert>
#include <cmath>
static bool close(float a,float b,float e=1e-4f)
{
    return std::fabs(a-b)<=e;
}
int main()
{
    using namespace jkgm;
    gpu_3do_matrix34 const identity{{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    gpu_3do_matrix34 const camera_view{{0,-1,0, 1,0,0, 0,0,1,
                                         -1000,2000,-10}};
    gpu_3do_matrix34 const model{{0,2,0, -1,0,0, 0,0,-3,
                                   1005,2004,12}};
    auto const r=gpu_3do_native_modelview(camera_view,model);
    auto const pos=gpu_3do_native_transform_point(r,{{2,3,4}});
    // Composition must agree with applying two matrices, with only
    // normal single-precision rounding differences.
    auto const first=gpu_3do_native_transform_point(model,{{2,3,4}});
    auto const second=gpu_3do_native_transform_point(camera_view,first);
    for(unsigned i=0;i<3;++i) assert(close(pos[i],second[i],0.0005f));
    auto const left=gpu_3do_native_modelview(identity,model);
    auto const right=gpu_3do_native_modelview(model,identity);
    for(unsigned i=0;i<12;++i) {
        assert(close(left[i],model[i]));
        assert(close(right[i],model[i]));
    }

    // Large absolute coordinates / nearby camera. The composed translation
    // stays small instead of subtracting two huge values in each shader vertex.
    gpu_3do_matrix34 view=identity;
    view[9]=-100000.0f;view[10]=-200000.0f;view[11]=-300000.0f;
    gpu_3do_matrix34 object=identity;
    object[9]=100002.0f;object[10]=200003.0f;object[11]=300004.0f;
    auto const composed=gpu_3do_native_modelview(view,object);
    assert(close(composed[9],2.0f));
    assert(close(composed[10],3.0f));
    assert(close(composed[11],4.0f));
    assert(gpu_3do_matrix_finite(composed));

    // A mirrored matrix must keep its handedness: existing culling logic
    // continues to use the original object transform determinant.
    auto mirrored=object; mirrored[0]=-1;
    auto const mirror_view=gpu_3do_native_modelview(view,mirrored);
    assert(close(mirror_view[0],-1.0f));
    assert(close(mirror_view[9],2.0f));

    // Non-axis-aligned camera and mesh rotation: verify native multiply
    // for *all* basis vectors, not just the translated origin.
    gpu_3do_matrix34 const a{{0.6f,0.8f,0, -0.8f,0.6f,0, 0,0,1,
                               -12.0f,44.0f,-7.0f}};
    gpu_3do_matrix34 const b{{0.8f,0,0.6f, 0,1,0, -0.6f,0,0.8f,
                               3.0f,-15.0f,4.0f}};
    auto const c=gpu_3do_native_modelview(a,b);
    for(auto const p:std::array<std::array<float,3>,4>{{
        {{0,0,0}},{{1,2,3}},{{-12,17,6}},{{30,-15,21}}
    }}) {
        auto const l=gpu_3do_native_transform_point(c,p);
        auto const mid=gpu_3do_native_transform_point(b,p);
        auto const expected=gpu_3do_native_transform_point(a,mid);
        for(unsigned i=0;i<3;++i) assert(close(l[i],expected[i],0.0001f));
    }
    return 0;
}
