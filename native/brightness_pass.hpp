#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>

// Display brightness for the finished 3D view: out = in^(1/(1+b)) on the current draw
// framebuffer, so black and white stay put and nothing clips. A GL 2.0 fragment shader;
// lightmaps, textures and the engine's gamma tables are not touched.
class BrightnessPass {
public:
    static float Exponent(float brightness);
    bool Apply(int width,int height,float brightness);
    void Reset();
    const char* Error() const { return error_; }
private:
    using GenFn=void (APIENTRY*)(GLsizei,GLuint*);
    using DeleteFn=void (APIENTRY*)(GLsizei,const GLuint*);
    using BindFn=void (APIENTRY*)(GLenum,GLuint);
    using StatusFn=GLenum (APIENTRY*)(GLenum);
    using AttachTextureFn=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);
    using BlitFn=void (APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    using ActiveTextureFn=void (APIENTRY*)(GLenum);
    using CreateShaderFn=GLuint (APIENTRY*)(GLenum);
    using ShaderSourceFn=void (APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
    using ObjectFn=void (APIENTRY*)(GLuint);
    using GetivFn=void (APIENTRY*)(GLuint,GLenum,GLint*);
    using CreateProgramFn=GLuint (APIENTRY*)();
    using AttachShaderFn=void (APIENTRY*)(GLuint,GLuint);
    using UniformLocationFn=GLint (APIENTRY*)(GLuint,const char*);
    using Uniform1iFn=void (APIENTRY*)(GLint,GLint);
    using Uniform1fFn=void (APIENTRY*)(GLint,GLfloat);
    GenFn genFbo_=nullptr;DeleteFn deleteFbo_=nullptr;BindFn bindFbo_=nullptr;StatusFn status_=nullptr;
    AttachTextureFn attachTexture_=nullptr;BlitFn blit_=nullptr;ActiveTextureFn activeTexture_=nullptr;
    CreateShaderFn createShader_=nullptr;ShaderSourceFn shaderSource_=nullptr;ObjectFn compileShader_=nullptr;
    GetivFn getShaderiv_=nullptr;CreateProgramFn createProgram_=nullptr;AttachShaderFn attachShader_=nullptr;
    ObjectFn linkProgram_=nullptr;GetivFn getProgramiv_=nullptr;ObjectFn useProgram_=nullptr;
    UniformLocationFn uniformLocation_=nullptr;Uniform1iFn uniform1i_=nullptr;Uniform1fFn uniform1f_=nullptr;
    ObjectFn deleteShader_=nullptr,deleteProgram_=nullptr;
    HGLRC context_=nullptr;bool ready_=false,programFailed_=false;
    GLuint fbo_=0,color_=0,program_=0;GLint frameLocation_=-1,exponentLocation_=-1;
    int width_=0,height_=0;
    const char* error_="not initialized";
    bool Functions();
    bool Program();
    bool Target(int width,int height);
    void DeleteObjects();
};
