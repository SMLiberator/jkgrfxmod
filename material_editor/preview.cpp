#include "preview.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"
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
uniform vec4 uOrientation; // normalized x,y,z,w
out vec3 wp, wn;
out vec2 uv;
vec3 transform(vec3 p) {
    vec3 t=2.0*cross(uOrientation.xyz,p);
    return p+uOrientation.w*t+cross(uOrientation.xyz,t);
}
void main() {
    wp=transform(aPos);
    wn=normalize(transform(aNormal));
    uv=aUv;
    vec3 q=wp-vec3(0.0,0.0,3.5);
    float f=2.9042; // 38-degree vertical FOV
    gl_Position=vec4(q.x*f/max(uAspect,0.01),q.y*f,
                     -q.z*1.00669-0.20067,-q.z);
}
)GLSL";
const char *fs = R"GLSL(
#version 330 core
in vec3 wp, wn;
in vec2 uv;
out vec4 outputColor;
uniform sampler2D tAlbedo,tSurface,tEnv,tNormal,tNormal2,tEmissive;
uniform sampler2D tMatcap,tMatcapSurface,tCoatSurface,tCoatNormal;
uniform int hasAlbedo,hasSurface,hasNormal,hasNormal2,hasEmissive;
uniform int hasMatcap,hasMatcapSurface,hasCoatSurface,hasCoatNormal;
uniform vec4 uAlbedo;
uniform vec3 uEmissive,uMatcapFactor;
uniform float uReflection,uRough,uMetal,uNormalStrength,uNormalStrength2;
uniform float uMatcapRough,uCoat,uCoatRough,uCoatTransmission;
uniform int uInspectionMode; // 0 lit, 1 EnvMap reflection only, 2 raw EnvMap
uniform int uUseEnvFresnel; // preview-only: disabled means full EnvMap contribution
uniform int uAlbedoPremultiplied, uAlphaMode, uBlendMode;
uniform float uAlphaCutoff;
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
vec2 normalSlope(sampler2D tex,float strength) {
    vec3 n=texture(tex,uv).xyz*2.0-1.0;
    return strength*n.xy/max(n.z,0.05);
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
    vec2 slope=vec2(0.0);
    if(hasNormal!=0)slope+=normalSlope(tNormal,uNormalStrength);
    if(hasNormal2!=0)slope+=normalSlope(tNormal2,uNormalStrength2);
    vec3 N=mappedNormal(raw,slope);
    vec3 V=normalize(vec3(0.0,0.0,3.5)-wp);
    vec3 R=reflect(-V,N);
    vec4 albedoTex=hasAlbedo!=0?texture(tAlbedo,uv):vec4(1.0);
    // Compiled albedo PNGs are sRGB-encoded but premultiplied in linear
    // space by compile.exe. Restore straight color for shading here; apply
    // coverage only once when compositing against the checkerboard.
    vec3 baseColor=albedoTex.rgb;
    if(hasAlbedo!=0 && uAlbedoPremultiplied!=0)
        baseColor=albedoTex.a>0.00001
            ?baseColor/max(albedoTex.a,0.00001):vec3(0.0);
    vec3 base=baseColor*uAlbedo.rgb;
    vec3 mask=hasSurface!=0?texture(tSurface,uv).rgb:vec3(1.0);
    float strength=max(uReflection*mask.r,0.0);
    float rough=clamp(uRough*mask.g,0.0,1.0);
    float metal=clamp(uMetal*mask.b,0.0,1.0);
    vec3 f0=mix(vec3(0.04),clamp(base,0.0,1.0),metal);
    float ndv=clamp(dot(N,V),0.0,1.0);
    // Mirrors the material's EnvMap Fresnel/strength/roughness behavior.
    vec3 baseFresnel=uUseEnvFresnel!=0?schlick(f0,ndv):vec3(1.0);
    vec3 baseEnvironment=env(R,rough)*baseFresnel*strength;
    vec3 baseSpec=baseEnvironment;
    if(uInspectionMode==0)
        baseSpec+=directSpec(N,V,rough,f0,strength);
    // Studio-only diffuse term: the game gets sector/vertex/point lighting.
    vec3 diffuse=base*(1.0-metal)*(0.48+0.38*max(dot(N,
                   normalize(vec3(-0.45,0.80,0.70))),0.0));

    // MatCap is artistic color in the emission-like composition path,
    // not another metallic reflection or GGX lobe.
    vec3 artistic=vec3(0.0);
    if(hasMatcap!=0) {
        vec2 m=hasMatcapSurface!=0?texture(tMatcapSurface,uv).rg:vec2(1.0);
        vec3 viewNormal=vec3(N.x,N.y,-N.z);
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
        texture(tCoatSurface,uv).rgb:vec3(1.0);
    float coverage=clamp(uCoat*coatMask.r,0.0,1.0);
    float coatRough=clamp(uCoatRough*coatMask.g,0.0,1.0);
    float coatTransmission=clamp(uCoatTransmission*coatMask.b,0.0,1.0);
    vec3 coatNormal=raw;
    if(hasCoatNormal!=0)coatNormal=mappedNormal(raw,normalSlope(tCoatNormal,1.0));
    float f=0.04+0.96*pow(1.0-clamp(abs(dot(coatNormal,V)),0.0,1.0),5.0);
    float specTrans=clamp(1.0-coverage*f,0.0,1.0);
    float diffuseTrans=specTrans*mix(1.0,coatTransmission,coverage);
    vec3 coatFresnel=uUseEnvFresnel!=0
        ?schlick(vec3(0.04),max(dot(coatNormal,V),0.0)):vec3(1.0);
    vec3 coatEnvironment=env(reflect(-V,coatNormal),coatRough)*
        coatFresnel*coverage;
    vec3 coatSpec=coatEnvironment;
    if(uInspectionMode==0)
        coatSpec+=directSpec(coatNormal,V,coatRough,vec3(0.04),coverage);
    // These diagnostic modes deliberately bypass studio lighting, while
    // preserving EnvMap sampling and material weights for troubleshooting.
    if(uInspectionMode==2) {
        outputColor=vec4(toSRGB(env(R,0.0)),1.0);
        return;
    }
    if(uInspectionMode==1) {
        outputColor=vec4(toSRGB(baseEnvironment*specTrans+coatEnvironment),1.0);
        return;
    }
    vec3 emission=uEmissive*(hasEmissive!=0?
        texture(tEmissive,uv).rgb:vec3(1.0));

    vec3 result=diffuse*diffuseTrans + baseSpec*specTrans+
                coatSpec+artistic*diffuseTrans+emission;

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
    vec3 composite=mix(background,result,alphaCoverage);
    if(uBlendMode==1)composite=background+result*alphaCoverage;
    if(uBlendMode==2)composite=mix(background,background*result,alphaCoverage);
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
GLuint MaterialPreview::loadTexture(const std::string &filename,bool srgb) {
    if(filename.empty())return 0;
    int w=0,h=0,channels=0;
    // Native Windows paths may contain non-ASCII characters. Decode the
    // UTF-8 metadata path via filesystem and feed stb_image from memory.
    std::ifstream source(std::filesystem::u8path(filename),std::ios::binary);
    if(!source)return 0;
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
    if(bytes.empty() ||
       bytes.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
        return 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *pixels=stbi_load_from_memory(
        bytes.data(),int(bytes.size()),&w,&h,&channels,4);
    if(!pixels||w<=0||h<=0) {
        if(pixels)stbi_image_free(pixels);
        return 0;
    }
    GLuint t=0;glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,srgb?GL_SRGB8_ALPHA8:GL_RGBA8,
                 w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(pixels);
    return t;
}
GLuint MaterialPreview::createPreset() {
    const int w=512,h=256;
    std::vector<unsigned char> pixels(size_t(w*h*4));
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        float u=float(x)/w,v=float(y)/h;
        float r,g,b;
        if(preset_==1) {r=v<.5f?.40f:.18f;g=v<.5f?.60f:.20f;b=v<.5f?.83f:.15f;}
        else if(preset_==2){r=.045f;g=.038f;b=v<.5f?.095f:.032f;}
        else {r=v<.52f?.32f:.16f;g=v<.52f?.35f:.17f;b=v<.52f?.39f:.19f;}
        if(u>.14f&&u<.24f&&v>.13f&&v<.42f)r+=.60f,g+=.60f,b+=.60f;
        if(u>.72f&&u<.85f&&v>.15f&&v<.40f)r+=.42f,g+=.42f,b+=.42f;
        // OpenGL bottom row corresponds to the south pole in this equirect map.
        size_t ix=size_t((y*w+x)*4);
        pixels[ix]=(unsigned char)(std::min(1.f,r)*255.f);
        pixels[ix+1]=(unsigned char)(std::min(1.f,g)*255.f);
        pixels[ix+2]=(unsigned char)(std::min(1.f,b)*255.f);
        pixels[ix+3]=255;
    }
    GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
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
    glDeleteTextures(GLsizei(textures_.size()),textures_.data());
    textures_.fill(0);
    for(size_t i=0;i<textures_.size();++i) {
        if(i==EnvironmentMap)continue;
        const bool srgb=(i==BaseMap||i==EmissiveMap||i==MatcapMap);
        if(i==BaseMap&&!material_.nativeRGBA.empty())
            textures_[i]=uploadNativeAlbedo();
        else
            textures_[i]=loadTexture(material_.files[i],srgb);
    }
    std::string envfile=envOverride_.empty()?material_.files[EnvironmentMap]:envOverride_;
    textures_[EnvironmentMap]=loadTexture(envfile,true);
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
}
void MaterialPreview::setMaterial(const PreviewMaterial &next) {
    bool changed=(material_.files!=next.files ||
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
void MaterialPreview::draw(int w,int h,const std::array<float,4> &orientation,
                           bool plane,int inspectionMode,bool useEnvFresnel) {
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
    glUniform4fv(glGetUniformLocation(program_,"uOrientation"),1,orientation.data());
    f1("uAspect",float(width_)/height_);
    glUniform4fv(glGetUniformLocation(program_,"uAlbedo"),1,material_.albedo.data());
    v3("uEmissive",material_.emissive);v3("uMatcapFactor",material_.matcapFactor);
    f1("uReflection",material_.reflection);f1("uRough",material_.roughness);
    f1("uMetal",material_.metalness);
    f1("uNormalStrength",material_.normalStrength);
    f1("uNormalStrength2",material_.normalStrength2);
    f1("uMatcapRough",material_.matcapRoughness);
    f1("uCoat",material_.coat);f1("uCoatRough",material_.coatRoughness);
    f1("uCoatTransmission",material_.coatTransmission);
    i1("uInspectionMode",std::clamp(inspectionMode,0,2));
    i1("uUseEnvFresnel",useEnvFresnel?1:0);
    i1("uAlbedoPremultiplied",material_.nativeRGBA.empty()?1:0);
    i1("uAlphaMode",material_.alphaMode);
    i1("uBlendMode",material_.blendMode);
    f1("uAlphaCutoff",material_.alphaCutoff);
    const char *samplers[]={"tAlbedo","tSurface","tEnv","tNormal","tNormal2",
        "tEmissive","tMatcap","tMatcapSurface","tCoatSurface","tCoatNormal"};
    const char *flags[]={"hasAlbedo","hasSurface",nullptr,"hasNormal","hasNormal2",
        "hasEmissive","hasMatcap","hasMatcapSurface","hasCoatSurface","hasCoatNormal"};
    for(int i=0;i<int(TextureCount);++i) {
        glActiveTexture(GL_TEXTURE0+i);
        glBindTexture(GL_TEXTURE_2D,textures_[i]);
        i1(samplers[i],i);
        if(flags[i])i1(flags[i],textures_[i]?1:0);
    }
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES,plane?sphereCount_:0,plane?planeCount_:sphereCount_);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDisable(GL_DEPTH_TEST);
}
