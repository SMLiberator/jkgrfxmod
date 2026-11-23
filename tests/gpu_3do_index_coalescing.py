"""Cached GPU 3DO adjacent index-run merging wiring contracts."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
renderer=read("renderer/renderer.cpp")
profile=read("renderer/frame_profile.hpp")
helper=read("renderer/gpu_3do_index_coalescing.hpp")
test=read("tests/gpu_3do_index_coalescing.cpp")

assert '#include "gpu_3do_index_coalescing.hpp"' in renderer
assert 'static thread_local std::vector<gpu_3do_index_run> index_runs;' in renderer
draw=renderer.split('void draw_gpu_3do_preview(',1)[1].split(
    '// Deliberate world-cache isolation test.',1)[0]
assert 'other.instance!=draw.instance' in draw
assert 'other.policy!=draw.policy' in draw
assert 'other.group->material!=group.material' in draw
assert 'append_gpu_3do_index_run(index_runs,g.offset,g.count);' in draw
assert 'emitted_subranges=index_runs.size();' in draw
assert 'if(index_runs.size()==1U)' in draw
assert 'glMultiDrawElements(GL_TRIANGLES,multidraw_counts.data(),' in draw
assert 'profile_frame.gpu_index_coalesced_ranges+=' in draw
assert 'profile_frame.gpu_index_subdraws+=emitted_subranges;' in draw
assert 'else if(emitted_subranges>1U)' in draw
assert 'return a.depth>b.depth;' in draw
for k in ('gpu_index_subdraws','gpu_index_coalesced_ranges',
          'gpu_index_coalesced_singles'):
    assert f'{k}=0' in profile
    assert f'{k}+=o.{k}' in profile
    assert f'profile_window.{k}/frames' in renderer
assert 'uint64_t(prev.first_index)+prev.count==first_index' in helper
assert 'uint64_t(prev.count)+count<=max_draw_count' in helper
assert 'prev.count%3U==0U && count%3U==0U' in helper
assert 'expanded(runs)==original' in test
assert 'max_tri_count' in test
assert '<ClInclude Include="gpu_3do_index_coalescing.hpp" />' in read(
    'renderer/renderer.vcxproj')
assert '<ClInclude Include="gpu_3do_index_coalescing.hpp">' in read(
    'renderer/renderer.vcxproj.filters')
print("GPU 3DO adjacent index-run coalescing source contracts passed")
