#include "preview.hpp"
#include "common/stb_image.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr float pi = 3.14159265358979323846f;
const char *vs = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
uniform float uAspect;
uniform vec2 uPlaneScale;
uniform float uCameraDistance;
uniform int uOrthographic;
uniform vec3 uCameraTarget;
uniform vec3 uViewRight,uViewUp,uViewBack;
out vec3 wp, wn;
out vec2 uv;
void main() {
    // Unlike the old model-rotation preview, positions/normals stay
    // in world space; only the view camera changes.
    // A full 0..1 UV rectangle covers the whole scaled plane.
    // Sphere vertices use uPlaneScale=(1,1), so remain unchanged.
    wp=vec3(aPos.xy*uPlaneScale,aPos.z);
    wn=normalize(aNormal);
    uv=aUv;
    vec3 relative=wp-uCameraTarget;
    vec3 q=vec3(dot(relative,uViewRight),
                dot(relative,uViewUp),
                dot(relative,uViewBack)-uCameraDistance);
    float f=2.9042; // 38-degree vertical FOV
    if(uOrthographic!=0) {
        // Keep the image size at the orbit target comparable to perspective,
        // and let the usual wheel control change orthographic magnification.
        float scale=f/max(uCameraDistance,0.01);
        gl_Position=vec4(q.x*scale/max(uAspect,0.01),q.y*scale,
                         (-2.0*q.z-30.1)/29.9,1.0);
    } else {
        gl_Position=vec4(q.x*f/max(uAspect,0.01),q.y*f,
                         -q.z*1.00669-0.20067,-q.z);
    }
}
)GLSL";
const char *fs = R"GLSL(
#version 330 core
in vec3 wp, wn;
in vec2 uv;
out vec4 outputColor;
uniform sampler2D tAlbedo,tSurface,tEnv,tNormal,tNormal2,tEmissive;
uniform sampler2D tMatcap,tMatcapSurface,tCoatSurface,tCoatNormal,tDisplacement;
uniform int hasAlbedo,hasSurface,hasNormal,hasNormal2,hasEmissive;
uniform int hasMatcap,hasMatcapSurface,hasCoatSurface,hasCoatNormal,hasDisplacement;
uniform vec4 uAlbedo;
uniform vec3 uEmissive,uMatcapFactor;
uniform float uReflection,uRough,uMetal,uNormalStrength,uNormalStrength2;
uniform float uDisplacementFactor;
uniform float uMatcapRough,uCoat,uCoatRough,uCoatTransmission;
uniform vec3 uViewRight,uViewUp,uViewBack,uCameraPos;
uniform int uUseEnvFresnel; // preview-only: disabled means full EnvMap contribution
uniform int uOrthographic;
uniform int uAlbedoPremultiplied, uAlphaMode, uBlendMode;
uniform float uAlphaCutoff;
uniform vec2 uAlbedoScroll,uEmissiveScroll,uNormalScroll,uNormalScroll2;
uniform float uEmissivePulse;
// Preview lighting is independent of the reflection environment and emission.
uniform vec3 uPreviewAmbient,uPreviewKey;
uniform float uPreviewSpecular;
const float PI=3.14159265358979323846;
vec3 schlick(vec3 f0,float nv) {
    return f0+(vec3(1.0)-f0)*pow(1.0-clamp(nv,0.0,1.0),5.0);
}
vec3 mappedNormal(vec3 base,vec2 slope) {
    vec3 dp1=dFdx(wp), dp2=dFdy(wp);
    vec2 du1=dFdx(uv), du2=dFdy(uv);
    vec3 t=cross(dp2,base)*du1.x+cross(base,dp1)*du2.x;
    vec3 b=cross(dp2,base)*du1.y+cross(base,dp1)*du2.y;
    float basis=max(dot(t,t),dot(b,b));
    if(basis<1e-16)return base;
    return normalize(mat3(t,b,base)*vec3(slope*inversesqrt(basis),1.0));
}
vec2 normalSlope(sampler2D tex,float strength,vec2 tc) {
    vec3 n=texture(tex,tc).xyz*2.0-1.0;
    return strength*n.xy/max(n.z,0.05);
}
// Match the 3DO renderer's tangent-space parallax sampling: signed
// displacement, 32-128 layers, and interpolation of the last crossing.
vec2 parallaxUV(vec2 tc,vec3 geometricNormal) {
    if(hasDisplacement==0||uDisplacementFactor==0.0)return tc;
    vec3 N=normalize(geometricNormal);
    vec3 dp1=dFdx(wp),dp2=dFdy(wp);
    vec2 du1=dFdx(uv),du2=dFdy(uv);
    vec3 T=cross(dp2,N)*du1.x+cross(N,dp1)*du2.x;
    vec3 B=cross(dp2,N)*du1.y+cross(N,dp1)*du2.y;
    float basis=max(dot(T,T),dot(B,B));
    if(basis<1e-16)return tc;
    mat3 tbn=mat3(T/sqrt(basis),B/sqrt(basis),N);
    vec3 viewDir=normalize(transpose(tbn)*
                           (uOrthographic!=0?uViewBack:uCameraPos-wp));
    if(viewDir.z<=0.05)return tc;
    float steps=mix(128.0,32.0,abs(viewDir.z));
    float stepDepth=1.0/steps;
    vec2 uvStep=(viewDir.xy/viewDir.z)*uDisplacementFactor/steps;
    vec2 current=tc;
    float layer=0.0;
    float h=texture(tDisplacement,current).r;
    for(int i=0;i<128&&layer<h;i++) {
        current-=uvStep;
        layer+=stepDepth;
        h=texture(tDisplacement,current).r;
    }
    vec2 previous=current+uvStep;
    float after=h-layer;
    float before=texture(tDisplacement,previous).r-layer+stepDepth;
    float denom=after-before;
    if(abs(denom)<1e-6)return current;
    return mix(current,previous,clamp(after/denom,0.0,1.0));
}
vec3 env(vec3 dir,float rough) {
    dir=normalize(dir);
    vec2 coords=vec2(fract(atan(dir.x,dir.z)/(2.0*PI)+0.5),
                      0.5-asin(clamp(dir.y,-1.0,1.0))/PI);
    ivec2 dims=textureSize(tEnv,0);
    float lod=clamp(rough,0.0,1.0)*log2(max(float(max(dims.x,dims.y)),1.0));
    return textureLod(tEnv,coords,lod).rgb;
}
vec3 directSpec(vec3 N,vec3 V,float rough,vec3 f0,float strength) {
    vec3 L=normalize(vec3(-0.45,0.80,0.7));
    vec3 H=normalize(V+L);
    float nL=max(dot(N,L),0.0);
    float nH=max(dot(N,H),0.0);
    float a=pow(clamp(rough,0.08,1.0),2.0);
    float a2=a*a;
    float D=a2/(PI*pow(nH*nH*(a2-1.0)+1.0,2.0)+0.00001);
    float k=pow(rough+1.0,2.0)/8.0;
    float nV=max(dot(N,V),0.0);
    float G=(nV/(nV*(1.0-k)+k))*(nL/(nL*(1.0-k)+k));
    return schlick(f0,max(dot(H,V),0.0))*D*G*nL*
        strength/(4.0*max(nV*nL,0.001));
}
vec3 toSRGB(vec3 c) {
    c=max(c,vec3(0.0));
    return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,
               step(vec3(0.0031308),c));
}
void main() {
    vec3 raw=normalize(wn);
    vec2 tc=parallaxUV(uv,raw);
    vec2 slope=vec2(0.0);
    if(hasNormal!=0)slope+=normalSlope(tNormal,uNormalStrength,
                                         tc+uNormalScroll);
    if(hasNormal2!=0)slope+=normalSlope(tNormal2,uNormalStrength2,
                                          tc+uNormalScroll2);
    vec3 N=mappedNormal(raw,slope);
    vec3 V=uOrthographic!=0?normalize(uViewBack):normalize(uCameraPos-wp);
    vec3 R=reflect(-V,N);
    vec4 albedoTex=hasAlbedo!=0?texture(tAlbedo,tc+uAlbedoScroll):vec4(1.0);
    // Compiled albedo PNGs are sRGB-encoded but premultiplied in linear
    // space by compile.exe. Restore straight color for shading here; apply
    // coverage only once when compositing against the checkerboard.
    vec3 baseColor=albedoTex.rgb;
    if(hasAlbedo!=0 && uAlbedoPremultiplied!=0)
        baseColor=albedoTex.a>0.00001
            ?baseColor/max(albedoTex.a,0.00001):vec3(0.0);
    vec3 base=baseColor*uAlbedo.rgb;
    vec3 mask=hasSurface!=0?texture(tSurface,tc).rgb:vec3(1.0);
    float strength=max(uReflection*mask.r,0.0);
    float rough=clamp(uRough*mask.g,0.0,1.0);
    float metal=clamp(uMetal*mask.b,0.0,1.0);
    vec3 f0=mix(vec3(0.04),clamp(base,0.0,1.0),metal);
    float ndv=clamp(dot(N,V),0.0,1.0);
    // Mirrors the material's EnvMap Fresnel/strength/roughness behavior.
    vec3 baseFresnel=uUseEnvFresnel!=0?schlick(f0,ndv):vec3(1.0);
    vec3 baseEnvironment=env(R,rough)*baseFresnel*strength;
    vec3 baseSpec=baseEnvironment+
                  directSpec(N,V,rough,f0,strength)*uPreviewSpecular;
    // Preset-specific preview lighting. This stays independent from the
    // material's emissive factor, EnvMap sampling, and MatCap contribution.
    vec3 diffuse=base*(1.0-metal)*(uPreviewAmbient+
                   uPreviewKey*max(dot(N,
                   normalize(vec3(-0.45,0.80,0.70))),0.0));

    // MatCap is artistic color in the emission-like composition path,
    // not another metallic reflection or GGX lobe.
    vec3 artistic=vec3(0.0);
    if(hasMatcap!=0) {
        vec2 m=hasMatcapSurface!=0?texture(tMatcapSurface,tc).rg:vec2(1.0);
        vec3 viewNormal=vec3(dot(N,uViewRight),dot(N,uViewUp),
                             -dot(N,uViewBack));
        if(viewNormal.z>0.0)viewNormal=-viewNormal;
        vec2 mc=clamp(vec2(0.5+0.5*viewNormal.x,
                           0.5-0.5*viewNormal.y),vec2(0.001),vec2(0.999));
        ivec2 dim=textureSize(tMatcap,0);
        float lod=clamp(uMatcapRough*m.g,0.0,1.0)*
            log2(max(float(max(dim.x,dim.y)),1.0));
        artistic=textureLod(tMatcap,mc,lod).rgb*uMatcapFactor*m.r;
    }

    // Same clearcoat channel and Fresnel/transmission rules as cached world.
    vec3 coatMask=hasCoatSurface!=0?
        texture(tCoatSurface,tc).rgb:vec3(1.0);
    float coverage=clamp(uCoat*coatMask.r,0.0,1.0);
    float coatRough=clamp(uCoatRough*coatMask.g,0.0,1.0);
    float coatTransmission=clamp(uCoatTransmission*coatMask.b,0.0,1.0);
    vec3 coatNormal=raw;
    if(hasCoatNormal!=0)coatNormal=mappedNormal(raw,normalSlope(tCoatNormal,1.0,tc));
    float f=0.04+0.96*pow(1.0-clamp(abs(dot(coatNormal,V)),0.0,1.0),5.0);
    float specTrans=clamp(1.0-coverage*f,0.0,1.0);
    float diffuseTrans=specTrans*mix(1.0,coatTransmission,coverage);
    vec3 coatFresnel=uUseEnvFresnel!=0
        ?schlick(vec3(0.04),max(dot(coatNormal,V),0.0)):vec3(1.0);
    vec3 coatEnvironment=env(reflect(-V,coatNormal),coatRough)*
        coatFresnel*coverage;
    vec3 coatSpec=coatEnvironment+
                  directSpec(coatNormal,V,coatRough,vec3(0.04),coverage)*
                  uPreviewSpecular;
    vec3 emission=uEmissive*uEmissivePulse*(hasEmissive!=0?
        texture(tEmissive,tc+uEmissiveScroll).rgb:vec3(1.0));

    // In the game, emissive RGB is separately additive in Normal/Fluid
    // and Additive blending, including with zero-alpha base pigment.
    // Keep only the non-emissive surface in the albedo coverage term.
    vec3 surface=diffuse*diffuseTrans + baseSpec*specTrans+
                 coatSpec+artistic*diffuseTrans;

    // Preview's opaque FBO cannot use the game's scene alpha composition.
    // Compose against a visible checkered backdrop instead of writing the
    // transparent RGB straight into black (the previous "black holes").
    float alphaCoverage=clamp(albedoTex.a*uAlbedo.a,0.0,1.0);
    if(uAlphaMode==1)
        alphaCoverage=alphaCoverage>=uAlphaCutoff?1.0:0.0;
    if(uAlphaMode==2) {
        const float bayer[16]=float[16](
           0.,8.,2.,10.,12.,4.,14.,6.,
           3.,11.,1.,9.,15.,7.,13.,5.);
        ivec2 xy=ivec2(gl_FragCoord.xy)&3;
        alphaCoverage=alphaCoverage>(bayer[xy.y*4+xy.x]+0.5)/16.0?1.0:0.0;
    }
    float checker=mod(floor(gl_FragCoord.x/16.0)+
                      floor(gl_FragCoord.y/16.0),2.0);
    vec3 background=mix(vec3(0.13),vec3(0.27),checker);
    // Alpha masks/dither reject the whole fragment. Ordinary/Fluid and
    // Additive preserve emission even when albedo coverage is exactly zero.
    float emissionVisibility=(uAlphaMode==1||uAlphaMode==2)?
        alphaCoverage:1.0;
    vec3 composite=mix(background,surface,alphaCoverage)+
                   emission*emissionVisibility;
    if(uBlendMode==1)
        composite=background+surface*alphaCoverage+
                  emission*emissionVisibility;
    if(uBlendMode==2) {
        // The native Multiply blend uses (DST_COLOR,ZERO); emission changes
        // its tint instead of adding independent light.
        vec3 tint=clamp(surface+emission,0.0,1.0);
        composite=mix(background,background*tint,alphaCoverage);
    }
    outputColor=vec4(toSRGB(composite),1.0);
}
)GLSL";
}

MaterialPreview::MaterialPreview() {
    GLuint v=compileShader(GL_VERTEX_SHADER,vs);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fs);
    if(v&&f) {
        program_=glCreateProgram();
        glAttachShader(program_,v);
        glAttachShader(program_,f);
        glLinkProgram(program_);
        GLint ok=GL_FALSE;
        glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if(!ok) {
            char log[2048] = {};
            glGetProgramInfoLog(program_,sizeof(log),nullptr,log);
            error_=log;
            glDeleteProgram(program_);program_=0;
        }
    }
    if(v)glDeleteShader(v);
    if(f)glDeleteShader(f);
    glGenVertexArrays(1,&vao_);
    glGenBuffers(1,&vbo_);
    buildMeshes();
    glGenFramebuffers(1,&fbo_);
    glGenTextures(1,&output_);
    glGenRenderbuffers(1,&depth_);
    reloadImages();
}
MaterialPreview::~MaterialPreview() {
    for(auto &channels:channelTextures_)
        glDeleteTextures(GLsizei(channels.size()),channels.data());
    glDeleteTextures(GLsizei(textures_.size()),textures_.data());
    if(output_)glDeleteTextures(1,&output_);
    if(depth_)glDeleteRenderbuffers(1,&depth_);
    if(fbo_)glDeleteFramebuffers(1,&fbo_);
    if(vbo_)glDeleteBuffers(1,&vbo_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    if(program_)glDeleteProgram(program_);
}
GLuint MaterialPreview::compileShader(GLenum type,const char *source) {
    GLuint sh=glCreateShader(type);
    glShaderSource(sh,1,&source,nullptr);
    glCompileShader(sh);
    GLint ok=GL_FALSE;
    glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[2048]={};
        glGetShaderInfoLog(sh,sizeof(log),nullptr,log);
        error_ += log;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}
GLuint MaterialPreview::loadTexture(const std::string &filename,bool srgb,
                                    PreviewImageInfo *details,bool heightMap) {
    if(filename.empty())return 0;
    int w=0,h=0,channels=0;
    // Decode Windows Unicode metadata paths from file bytes; preserve
    // RGB/R channels on upload rather than forcing RGBA everywhere.
    std::ifstream source(std::filesystem::u8path(filename),std::ios::binary);
    if(!source)return 0;
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
    if(bytes.empty()||
       bytes.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
        return 0;
    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),
                              &w,&h,&channels)||w<=0||h<=0)
        return 0;
    // Grayscale art used for a full-color slot must replicate R into RGB.
    // Only displacement can safely use a single GL_RED channel.
    const int components=channels==3?3:channels==1?
        (heightMap?1:3):4;
    stbi_set_flip_vertically_on_load(1);
    int decodedChannels=0;
    unsigned char *pixels=stbi_load_from_memory(
        bytes.data(),int(bytes.size()),&w,&h,&decodedChannels,components);
    if(!pixels)return 0;
    const GLenum format=components==1?GL_RED:components==3?GL_RGB:GL_RGBA;
    const GLenum internal=srgb?
        (components==4?GL_SRGB8_ALPHA8:GL_SRGB8):
        (components==1?GL_R8:components==3?GL_RGB8:GL_RGBA8);
    GLint oldUnpack=4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldUnpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    GLuint tex=0;glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,
                 GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT,oldUnpack);
    if(details) {
        details->width=w;details->height=h;
        details->channels=channels;
        details->bytes=bytes.size();
    }
    stbi_image_free(pixels);
    return tex;
}
GLuint MaterialPreview::uploadStaged(const editor_images::Image &image,
                                     bool srgb,PreviewImageInfo *details) {
    if(!image.valid())return 0;
    const int w=image.width,h=image.height,c=image.channels;
    std::vector<std::uint8_t> flipped(image.pixels.size());
    const std::size_t stride=std::size_t(w)*c;
    for(int y=0;y<h;++y)
        std::copy_n(image.pixels.data()+std::size_t(y)*stride,stride,
                    flipped.data()+std::size_t(h-1-y)*stride);
    const GLenum format=c==4?GL_RGBA:c==3?GL_RGB:GL_RED;
    const GLenum internal=srgb?(c==4?GL_SRGB8_ALPHA8:GL_SRGB8):
                           (c==4?GL_RGBA8:c==3?GL_RGB8:GL_R8);
    GLint unpack=4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    GLuint tex=0;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,
                 GL_UNSIGNED_BYTE,flipped.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
    if(details) {
        details->width=w;details->height=h;
        details->channels=c;details->bytes=image.pixels.size();
    }
    return tex;
}
GLuint MaterialPreview::channelTexture(PreviewTexture slot,int channel) {
    if(channel<0||channel>3||!imageInfo_[slot].valid())return 0;
    GLuint &cached=channelTextures_[slot][size_t(channel)];
    if(cached)return cached;
    const PreviewImageInfo &info=imageInfo_[slot];
    const int maxDimension=256;
    const double scale=std::min(1.0,
        double(maxDimension)/double(std::max(info.width,info.height)));
    const int outWidth=std::max(1,int(std::round(info.width*scale)));
    const int outHeight=std::max(1,int(std::round(info.height*scale)));
    std::vector<unsigned char> pixels(size_t(outWidth)*outHeight*4);
    const bool native=slot==BaseMap&&!material_.nativeRGBA.empty();
    const auto &staged=material_.stagedImages[slot];
    std::vector<unsigned char> bytes;
    unsigned char *decoded=nullptr;
    int w=0,h=0,ch=0;
    if(staged) {
        w=staged->width;
        h=staged->height;
        ch=staged->channels;
    } else if(!native) {
        const std::string file=slot==EnvironmentMap?
            (envOverride_.empty()?material_.files[slot]:envOverride_):
            material_.files[slot];
        std::ifstream source(std::filesystem::u8path(file),std::ios::binary);
        if(!source)return 0;
        bytes.assign(std::istreambuf_iterator<char>(source),
                     std::istreambuf_iterator<char>());
        if(bytes.empty()||
           bytes.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
            return 0;
        stbi_set_flip_vertically_on_load(1);
        decoded=stbi_load_from_memory(
            bytes.data(),int(bytes.size()),&w,&h,&ch,4);
        if(!decoded)return 0;
    } else {
        w=material_.nativeWidth;
        h=material_.nativeHeight;
        if(material_.nativeRGBA.size()!=size_t(w)*h*4)return 0;
    }
    for(int y=0;y<outHeight;++y)for(int x=0;x<outWidth;++x) {
        const int sx=std::min(w-1,int(double(x)*w/outWidth));
        const int sy=std::min(h-1,int(double(y)*h/outHeight));
        // Native MAT RGBA comes top-left first; stb decodes bottom-first.
        const int sourceY=(native||staged)?h-1-sy:sy;
        const std::size_t offset=std::size_t(sourceY)*w+sx;
        const auto value=staged?
            (channel<ch?staged->pixels[offset*ch+channel]:
             (channel==3?std::uint8_t(255):std::uint8_t(0))):
            (native?material_.nativeRGBA[offset*4+channel]:
                    decoded[offset*4+channel]);
        const auto dest=(size_t(y)*outWidth+x)*4;
        pixels[dest]=pixels[dest+1]=pixels[dest+2]=value;
        pixels[dest+3]=255;
    }
    if(decoded)stbi_image_free(decoded);
    glGenTextures(1,&cached);
    glBindTexture(GL_TEXTURE_2D,cached);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,outWidth,outHeight,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);
    return cached;
}
// The three photographic Poly Haven 1K HDR panoramas are embedded as
// Windows RCDATA, not installed next to the portable EXE. Tone-map once on
// loading: the preview's shader already expects sRGB LDR environment images.
// Retain a generated offline fallback when assets were not bundled.
namespace {
bool loadBundledPreset(int preset,int &width,int &height,
                       std::vector<unsigned char> &out) {
#if defined(_WIN32)
    if(preset<0||preset>2)return false;
    const HMODULE module=GetModuleHandleW(nullptr);
    const HRSRC info=module?FindResourceW(
        module,MAKEINTRESOURCEW(401+preset),RT_RCDATA):nullptr;
    if(!info)return false;
    const HGLOBAL handle=LoadResource(module,info);
    const void *bytes=handle?LockResource(handle):nullptr;
    const DWORD length=SizeofResource(module,info);
    if(!bytes||length==0||length>INT_MAX)return false;
    int w=0,h=0,channels=0;
    float *decoded=stbi_loadf_from_memory(
        static_cast<const stbi_uc *>(bytes),static_cast<int>(length),
        &w,&h,&channels,3);
    if(!decoded)return false;
    const bool valid=w>0&&h>0&&w<=4096&&h<=2048&&
                     std::uint64_t(w)*h<=8388608ULL;
    if(!valid) {
        stbi_image_free(decoded);
        return false;
    }
    width=w;height=h;
    out.resize(std::size_t(w)*h*3);
    const float exposure=preset==0?0.65f:preset==1?0.40f:1.65f;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        // HDR files store north at the top; GL_TEXTURE_2D expects south
        // at the first row for the shader's equirectangular lookup.
        const std::size_t src=(std::size_t(h-1-y)*w+x)*3;
        const std::size_t dst=(std::size_t(y)*w+x)*3;
        for(int channel=0;channel<3;++channel) {
            const float value=decoded[src+channel];
            const float linear=std::isfinite(value)
                ?1.0f-std::exp(-std::max(value,0.0f)*exposure):0.0f;
            const float encoded=linear<=0.0031308f?12.92f*linear:
                1.055f*std::pow(linear,1.0f/2.4f)-0.055f;
            out[dst+channel]=static_cast<unsigned char>(
                std::lround(std::clamp(encoded,0.0f,1.0f)*255.0f));
        }
    }
    stbi_image_free(decoded);
    return true;
#else
    (void)preset;(void)width;(void)height;(void)out;
    return false;
#endif
}
} // namespace

GLuint MaterialPreview::createPreset() {
    int w=512,h=256;
    std::vector<unsigned char> pixels;
    const bool photographic=loadBundledPreset(preset_,w,h,pixels);
    if(!photographic) {
        pixels.resize(std::size_t(w)*h*4);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float u=float(x)/w,v=float(y)/h;
            float r,g,b;
            if(preset_==1) {r=v<.5f?.40f:.18f;g=v<.5f?.60f:.20f;b=v<.5f?.83f:.15f;}
            else if(preset_==2){r=.045f;g=.038f;b=v<.5f?.095f:.032f;}
            else {r=v<.52f?.32f:.16f;g=v<.52f?.35f:.17f;b=v<.52f?.39f:.19f;}
            if(u>.14f&&u<.24f&&v>.13f&&v<.42f)r+=.60f,g+=.60f,b+=.60f;
            if(u>.72f&&u<.85f&&v>.15f&&v<.40f)r+=.42f,g+=.42f,b+=.42f;
            const std::size_t ix=(std::size_t(y)*w+x)*4;
            pixels[ix]=static_cast<unsigned char>(std::min(1.f,r)*255.f);
            pixels[ix+1]=static_cast<unsigned char>(std::min(1.f,g)*255.f);
            pixels[ix+2]=static_cast<unsigned char>(std::min(1.f,b)*255.f);
            pixels[ix+3]=255;
        }
    }
    GLuint t=0;
    glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    GLint unpack=4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,photographic?GL_SRGB8:GL_SRGB8_ALPHA8,
                 w,h,0,photographic?GL_RGB:GL_RGBA,GL_UNSIGNED_BYTE,
                 pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    return t;
}
GLuint MaterialPreview::uploadNativeAlbedo() {
    if(material_.nativeWidth<=0||material_.nativeHeight<=0)return 0;
    int w=material_.nativeWidth,h=material_.nativeHeight;
    if(w>4096||h>4096||material_.nativeRGBA.size()!=size_t(w)*h*4)
        return 0;
    std::vector<std::uint8_t> flipped(material_.nativeRGBA.size());
    for(int row=0;row<h;++row) {
        std::copy_n(material_.nativeRGBA.data()+size_t(row)*w*4,
                    size_t(w)*4,
                    flipped.data()+size_t(h-1-row)*w*4);
    }
    GLuint tex=0;
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,flipped.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}
void MaterialPreview::reloadImages() {
    for(auto &channels:channelTextures_) {
        glDeleteTextures(GLsizei(channels.size()),channels.data());
        channels.fill(0);
    }
    glDeleteTextures(GLsizei(textures_.size()),textures_.data());
    textures_.fill(0);
    imageInfo_.fill({});
    for(size_t i=0;i<textures_.size();++i) {
        if(i==EnvironmentMap)continue;
        const bool srgb=(i==BaseMap||i==EmissiveMap||i==MatcapMap);
        if(i==BaseMap&&!material_.nativeRGBA.empty()) {
            textures_[i]=uploadNativeAlbedo();
            if(textures_[i]) {
                imageInfo_[i].width=material_.nativeWidth;
                imageInfo_[i].height=material_.nativeHeight;
                imageInfo_[i].channels=4;
                imageInfo_[i].bytes=material_.nativeRGBA.size();
            }
        } else if(material_.stagedImages[i])
            textures_[i]=uploadStaged(*material_.stagedImages[i],
                                     srgb,&imageInfo_[i]);
        else
            textures_[i]=loadTexture(material_.files[i],srgb,&imageInfo_[i],
                                     i==DisplacementMap);
        // UV-animated material maps must repeat along both axes. Equirect
        // environment images retain their vertical edge clamp.
        if(textures_[i]) {
            glBindTexture(GL_TEXTURE_2D,textures_[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        }
    }
    std::string envfile=envOverride_.empty()?material_.files[EnvironmentMap]:envOverride_;
    if(envOverride_.empty()&&material_.stagedImages[EnvironmentMap])
        textures_[EnvironmentMap]=uploadStaged(
            *material_.stagedImages[EnvironmentMap],true,
            &imageInfo_[EnvironmentMap]);
    else
        textures_[EnvironmentMap]=loadTexture(
            envfile,true,&imageInfo_[EnvironmentMap]);
    loadedEnvironmentFile_=textures_[EnvironmentMap]!=0;
    const char *presetNames[]={"Studio","Outdoor","Night"};
    const char *fallbackName=presetNames[std::clamp(preset_,0,2)];
    if(loadedEnvironmentFile_) {
        envDescription_=std::string(envOverride_.empty()?"Material / pack EnvMap: ":"Preview override: ")+envfile;
    } else {
        envDescription_=envfile.empty()
            ?std::string("No EnvMap defined; using ")+fallbackName+" fallback"
            :std::string("EnvMap image could not be loaded: ")+envfile+
               ". Using "+fallbackName+" fallback";
        textures_[EnvironmentMap]=createPreset();
    }
    // Query only on material/texture reload, not every preview frame.
    // A native JK MAT is bound into BaseMap, so participates automatically.
    auto getAspect=[this](PreviewTexture slot)->float {
        GLuint texture=textures_[slot];
        if(!texture)return 0.0f;
        GLint textureWidth=0,textureHeight=0;
        glBindTexture(GL_TEXTURE_2D,texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&textureWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&textureHeight);
        return textureWidth>0&&textureHeight>0
            ?float(textureWidth)/float(textureHeight):0.0f;
    };
    planeAspect_=getAspect(BaseMap);
    if(planeAspect_<=0.0f)planeAspect_=getAspect(EmissiveMap);
    if(planeAspect_<=0.0f)planeAspect_=1.0f;
}
void MaterialPreview::setMaterial(const PreviewMaterial &next) {
    bool changed=(material_.files!=next.files ||
        material_.stagedImages!=next.stagedImages ||
        material_.nativeSourceKey!=next.nativeSourceKey ||
        material_.nativeWidth!=next.nativeWidth ||
        material_.nativeHeight!=next.nativeHeight);
    material_=next;
    if(changed)reloadImages();
}
void MaterialPreview::setPreset(int preset) {
    preset_=preset;
    if(envOverride_.empty()&&material_.files[EnvironmentMap].empty())reloadImages();
}
void MaterialPreview::setEnvironmentOverride(const std::string &path) {
    envOverride_=path;
    reloadImages();
}
void MaterialPreview::resize(int w,int h) {
    w=std::clamp(w,1,4096);h=std::clamp(h,1,4096);
    if(width_==w&&height_==h)return;
    width_=w;height_=h;
    glBindTexture(GL_TEXTURE_2D,output_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER,depth_);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output_,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth_);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        error_="Preview framebuffer is incomplete";
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void MaterialPreview::buildMeshes() {
    std::vector<Vertex> vertices;
    constexpr int rows=36,cols=48;
    auto point=[](int x,int y)->Vertex {
        float u=x/48.f,v=y/36.f,a=u*2.f*pi,b=v*pi;
        float nx=std::sin(b)*std::cos(a),ny=std::cos(b),nz=std::sin(b)*std::sin(a);
        return {nx,ny,nz,nx,ny,nz,u,1-v};
    };
    for(int y=0;y<rows;y++)for(int x=0;x<cols;x++) {
        vertices.push_back(point(x,y));vertices.push_back(point(x,y+1));
        vertices.push_back(point(x+1,y));vertices.push_back(point(x+1,y));
        vertices.push_back(point(x,y+1));vertices.push_back(point(x+1,y+1));
    }
    sphereCount_=GLsizei(vertices.size());
    const Vertex plane[]={
        {-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},{1,1,0,0,0,1,1,1},
        {-1,-1,0,0,0,1,0,0},{1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}};
    vertices.insert(vertices.end(),std::begin(plane),std::end(plane));
    planeCount_=6;

    // Six independent flat-shaded UV faces. Use outward normals and
    // counterclockwise winding so each face receives consistent lighting.
    // The sphere, plane, and cube share a single immutable GPU vertex buffer.
    const auto appendCubeFace=[&](std::array<float,3> normal,
                                  std::array<float,3> right,
                                  std::array<float,3> up) {
        constexpr float size=kPreviewCubeHalfExtent;
        const auto corner=[&](float x,float y,float u,float v)->Vertex {
            return {
                size*(normal[0]+x*right[0]+y*up[0]),
                size*(normal[1]+x*right[1]+y*up[1]),
                size*(normal[2]+x*right[2]+y*up[2]),
                normal[0],normal[1],normal[2],u,v
            };
        };
        const Vertex a=corner(-1,-1,0,0),b=corner(1,-1,1,0);
        const Vertex c=corner(1,1,1,1),d=corner(-1,1,0,1);
        vertices.insert(vertices.end(),{a,b,c,a,c,d});
    };
    appendCubeFace({0,0,1}, {1,0,0}, {0,1,0});   // Front
    appendCubeFace({0,0,-1},{-1,0,0},{0,1,0});  // Back
    appendCubeFace({1,0,0}, {0,0,-1},{0,1,0});  // Right
    appendCubeFace({-1,0,0},{0,0,1}, {0,1,0});  // Left
    appendCubeFace({0,1,0}, {1,0,0}, {0,0,-1}); // Top
    appendCubeFace({0,-1,0},{1,0,0}, {0,0,1});  // Bottom
    cubeCount_=36;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(Vertex)),
                 vertices.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,x));
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,nx));
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,u));
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}
void MaterialPreview::draw(int w,int h,float orbitYaw,float orbitPitch,
                           float orbitDistance,
                           const std::array<float,3> &orbitTarget,
                           PreviewShape shape,bool useEnvFresnel,
                           bool orthographic,double animationSeconds,
                           float lightingIntensity) {
    if(!program_)return;
    resize(w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
    glViewport(0,0,width_,height_);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(.105f,.115f,.135f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    auto f1=[this](const char *name,float value) {
        glUniform1f(glGetUniformLocation(program_,name),value);
    };
    auto i1=[this](const char *name,int value) {
        glUniform1i(glGetUniformLocation(program_,name),value);
    };
    auto v3=[this](const char *name,const std::array<float,3> &v) {
        glUniform3f(glGetUniformLocation(program_,name),v[0],v[1],v[2]);
    };
    // Stable turntable basis in world space: world +Y always defines "up".
    // The camera looks at its movable target without roll. Panning moves
    // that target and the camera together; wheel zoom changes orbit radius.
    const float cy=std::cos(orbitYaw),sy=std::sin(orbitYaw);
    const float cp=std::cos(orbitPitch),sp=std::sin(orbitPitch);
    const std::array<float,3> back={sy*cp,sp,cy*cp};
    const std::array<float,3> right={cy,0.0f,-sy};
    const std::array<float,3> up={-sy*sp,cp,-cy*sp};
    const std::array<float,3> camera={
        orbitTarget[0]+back[0]*orbitDistance,
        orbitTarget[1]+back[1]*orbitDistance,
        orbitTarget[2]+back[2]*orbitDistance};
    const float light=std::clamp(lightingIntensity,0.0f,2.0f);
    // Preserve the existing Studio appearance at 1.0. Outdoor is bright
    // with warm sun and cool sky fill; Night gives emissive materials a
    // deliberately low-lit reference without touching their intensity.
    const std::array<float,3> ambient=preset_==1
        ?std::array<float,3>{0.72f,0.79f,0.88f}
        :preset_==2?std::array<float,3>{0.018f,0.030f,0.060f}
                   :std::array<float,3>{0.48f,0.48f,0.48f};
    const std::array<float,3> key=preset_==1
        ?std::array<float,3>{0.78f,0.68f,0.56f}
        :preset_==2?std::array<float,3>{0.055f,0.077f,0.115f}
                   :std::array<float,3>{0.38f,0.38f,0.38f};
    const auto scaled=[light](const std::array<float,3> &v) {
        return std::array<float,3>{v[0]*light,v[1]*light,v[2]*light};
    };
    v3("uPreviewAmbient",scaled(ambient));
    v3("uPreviewKey",scaled(key));
    f1("uPreviewSpecular",light*(preset_==1?1.2f:
                                preset_==2?0.13f:1.0f));
    v3("uViewRight",right);v3("uViewUp",up);
    v3("uViewBack",back);v3("uCameraPos",camera);
    v3("uCameraTarget",orbitTarget);
    f1("uCameraDistance",orbitDistance);
    i1("uOrthographic",orthographic?1:0);
    f1("uAspect",float(width_)/height_);
    // Keep the longer side at the previous plane size, without stretching
    // UVs or affecting the sphere; both wide and tall maps remain framed.
    const float aspect=shape==PreviewShape::Plane?planeAspect_:1.0f;
    glUniform2f(glGetUniformLocation(program_,"uPlaneScale"),
                std::min(aspect,1.0f),std::min(1.0f,1.0f/aspect));
    glUniform4fv(glGetUniformLocation(program_,"uAlbedo"),1,material_.albedo.data());
    v3("uEmissive",material_.emissive);v3("uMatcapFactor",material_.matcapFactor);
    const auto albedoUV=editor_animation::scrollOffset(
        material_.albedoScroll,animationSeconds);
    const auto emissiveUV=editor_animation::scrollOffset(
        material_.emissiveScroll,animationSeconds);
    const auto normalUV=editor_animation::scrollOffset(
        material_.normalScroll,animationSeconds);
    const auto normalUV2=editor_animation::scrollOffset(
        material_.normalScroll2,animationSeconds);
    const auto v2=[this](const char *name,const std::array<float,2> &v) {
        glUniform2f(glGetUniformLocation(program_,name),v[0],v[1]);
    };
    v2("uAlbedoScroll",albedoUV);
    v2("uEmissiveScroll",emissiveUV);
    v2("uNormalScroll",normalUV);
    v2("uNormalScroll2",normalUV2);
    f1("uEmissivePulse",editor_animation::multiplier(
        material_.emissivePulse,animationSeconds,material_.pulseSeed));
    f1("uReflection",material_.reflection);f1("uRough",material_.roughness);
    f1("uMetal",material_.metalness);
    f1("uNormalStrength",material_.normalStrength);
    f1("uNormalStrength2",material_.normalStrength2);
    f1("uDisplacementFactor",material_.displacementFactor);
    f1("uMatcapRough",material_.matcapRoughness);
    f1("uCoat",material_.coat);f1("uCoatRough",material_.coatRoughness);
    f1("uCoatTransmission",material_.coatTransmission);
    i1("uUseEnvFresnel",useEnvFresnel?1:0);
    i1("uAlbedoPremultiplied",
       material_.nativeRGBA.empty()&&material_.albedoPremultiplied?1:0);
    i1("uAlphaMode",material_.alphaMode);
    i1("uBlendMode",material_.blendMode);
    f1("uAlphaCutoff",material_.alphaCutoff);
    const char *samplers[]={"tAlbedo","tSurface","tEnv","tNormal","tNormal2",
        "tEmissive","tMatcap","tMatcapSurface","tCoatSurface","tCoatNormal",
        "tDisplacement"};
    const char *flags[]={"hasAlbedo","hasSurface",nullptr,"hasNormal","hasNormal2",
        "hasEmissive","hasMatcap","hasMatcapSurface","hasCoatSurface","hasCoatNormal",
        "hasDisplacement"};
    for(int i=0;i<int(TextureCount);++i) {
        glActiveTexture(GL_TEXTURE0+i);
        glBindTexture(GL_TEXTURE_2D,textures_[i]);
        i1(samplers[i],i);
        if(flags[i])i1(flags[i],textures_[i]?1:0);
    }
    glBindVertexArray(vao_);
    const GLint offset=shape==PreviewShape::Plane?sphereCount_:
                       shape==PreviewShape::Cube?sphereCount_+planeCount_:0;
    const GLsizei count=shape==PreviewShape::Plane?planeCount_:
                        shape==PreviewShape::Cube?cubeCount_:sphereCount_;
    glDrawArrays(GL_TRIANGLES,offset,count);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDisable(GL_DEPTH_TEST);
}
