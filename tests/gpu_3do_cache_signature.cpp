#include "renderer/gpu_3do_cache_signature.hpp"
#include <array>
#include <cassert>
#include <cstdint>

struct mock_face {
    uint32_t type=0, count=3;
    uint32_t index[3]{0,1,2};
};
struct mock_mesh {
    std::array<float,9> vertices{{0,0,0,1,0,0,0,1,0}};
    std::array<float,6> uv{{0,0,1,0,0,1}};
    std::array<float,9> normals{{0,0,1,0,0,1,0,0,1}};
    std::array<float,3> light{{1,1,1}};
    std::array<float,9> rgb{{1,1,1,1,1,1,1,1,1}};
    mock_face face{};
};
static uint64_t signature(mock_mesh const &m) {
    jkgm::gpu_3do_byte_signature h;
    h.add_bytes(m.vertices.data(),sizeof(m.vertices));
    h.add_bytes(m.uv.data(),sizeof(m.uv));
    h.add_bytes(m.normals.data(),sizeof(m.normals));
    h.add_bytes(m.light.data(),sizeof(m.light));
    h.add_bytes(m.rgb.data(),sizeof(m.rgb));
    h.add(m.face.type);
    h.add(m.face.count);
    h.add_bytes(m.face.index,sizeof(m.face.index));
    return h.value;
}
int main() {
    mock_mesh m{};
    auto const original=signature(m);
    assert(signature(m)==original);
    m.vertices[4]=0.25f; // same pointer and vertex/face count
    auto const vertices=signature(m);
    assert(vertices!=original);
    m.vertices[4]=0;
    assert(signature(m)==original);
    m.uv[0]=0.25f;
    assert(signature(m)!=original);
    m.uv[0]=0;
    m.normals[2]=-1;
    assert(signature(m)!=original);
    m.normals[2]=1;
    m.light[0]=0.5f;
    assert(signature(m)!=original);
    m.light[0]=1;
    m.rgb[0]=0.5f;
    assert(signature(m)!=original);
    m.rgb[0]=1;
    m.face.index[1]=2;
    assert(signature(m)!=original);
    m.face.index[1]=1;
    m.face.type=2;
    assert(signature(m)!=original);
    m.face.type=0;
    assert(signature(m)==original);
}
