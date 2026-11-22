"""G-buffer 10:10:10:2 normal/class ABI, readback and resolve wiring."""
from pathlib import Path
import math
import re
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
fmt=read("glutil/texture.hpp")
basic=read("renderer/renderer_screen_basic.cpp")
msaa=read("renderer/renderer_screen_msaa.cpp")
helper=read("assets/shaders/gbuffer_normal_packed.glsl")
assert "rgb10_a2 = 0x8059" in fmt
assert basic.count("gl::texture_internal_format::rgb10_a2")==1
assert msaa.count("gl::texture_internal_format::rgb10_a2")==2
assert "gbuffer_normal_resolve(screen_res," in msaa
assert "gl::framebuffer_attachment::color2" in basic
assert "gl::framebuffer_attachment::color2" in msaa
assert "gl::framebuffer_attachment::color3" in basic
assert "gl::framebuffer_attachment::color3" in msaa
assert "gl::texture_internal_format::rgba16f" in basic # HDR color/emission unchanged
assert "gl::texture_internal_format::rgba16f" in msaa
assert "gbuffer_pack_normal" in helper and "gbuffer_unpack_normal" in helper
assert "receiver_class=floor(encoded.a*3.0+0.5)" in helper
assert "if(receiver_class<0.5)return vec4(0.0);" in helper
assert "all(equal(encoded.rgb,vec3(0.0)))" in helper
writers=("assets/shaders/game_opaque_pass.frag",
         "assets/shaders/gpu_3do_preview.frag",
         "assets/shaders/world_cache.frag")
readers=("assets/shaders/post_ssao.frag",
         "assets/shaders/post_ssao_half.frag",
         "assets/shaders/post_ssao_half_upsample.frag",
         "assets/shaders/post_box4.frag",
         "assets/shaders/post_sun_visibility_half.frag",
         "assets/shaders/post_opaque_composite.frag")
for path in writers+readers:
    s=read(path)
    assert '#include "gbuffer_normal_packed.glsl"' in s,path
for path in writers:
    s=read(path)
    for line in s.splitlines():
        if "out_normal=" in line or "out_normal =" in line:
            assert "gbuffer_pack_normal(" in line,path
for path in readers:
    s=read(path)
    # Never allow accidental consumption of biased RGB or A2 class as raw float.
    for line in s.splitlines():
        if re.search(r"(texture|texelFetch|textureOffset)\(normal_image",line):
            assert "gbuffer_unpack_normal(" in line,(path,line)
assert read("assets/shaders/post_box4.frag").count(
    "gbuffer_unpack_normal(textureOffset(normal_image")==16
assert "chosen_normal.a>1.5" in read("assets/shaders/post_sun_visibility_half.frag")
assert "normal_pixel.a>1.5" in read("assets/shaders/post_opaque_composite.frag")

# Simulate the fixed-point UNORM framebuffer conversion, including the
# unusual case of a covered sky/fullbright pixel with no lighting normal.
def roundtrip(n,classification):
    a=round(classification)/3.
    qa=round(a*3)/3.
    if sum(x*x for x in n)<0.0001:
        rgb=(0.,0.,0.)
    else:
        rgb=tuple(round(min(1.,max(0.,x*.5+.5))*1023)/1023 for x in n)
    klass=math.floor(qa*3+.5)
    decoded=(0.,0.,0.) if klass==0 or all(v==0. for v in rgb) else tuple(2*v-1 for v in rgb)
    return decoded,klass
for normal in ((0.,0.,0.),(1.,0.,0.),(-1.,0.,0.),(0.,1.,0.),
               (0.,-1.,0.),(0.,0.,1.),(0.,0.,-1.),
               (.577350,-.577350,.577350)):
    for cls in (1,2):
        out,k=roundtrip(normal,cls)
        assert k==cls
        assert max(abs(x-y) for x,y in zip(normal,out))<0.002
assert roundtrip((0.,0.,0.),0)==((0.,0.,0.),0)
assert roundtrip((0.,0.,0.),1)==((0.,0.,0.),1)
print("Packed RGB10_A2 normal G-buffer: writers, readers, classification, MSAA and quantization OK")
