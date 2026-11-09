#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>

// Enlarged HUD scales offered in settings (exact binary fractions, so cvar values compare exactly).
constexpr float HudScales[]={1.25f,1.5f,1.75f,2.0f};
inline bool ValidHudScale(float scale){for(float s:HudScales)if(scale==s)return true;return false;}

// A private single-sample render target. No engine globals, timing or input.
// Begin/End preserve the native output FBO, read/draw buffers, viewport/scissor.
class SceneTarget {
public:
    bool Begin(int width,int height,int scale);
    bool BeginHud(int physicalWidth,int physicalHeight,float scale);
    void End();
    void Reset();
    bool Active() const { return active_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const char* Error() const { return error_; }
private:
    using GenFn=void (APIENTRY*)(GLsizei,GLuint*);
    using DeleteFn=void (APIENTRY*)(GLsizei,const GLuint*);
    using BindFn=void (APIENTRY*)(GLenum,GLuint);
    using StorageFn=void (APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
    using AttachFn=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
    using StatusFn=GLenum (APIENTRY*)(GLenum);
    using AttachTextureFn=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);
    using ActiveTextureFn=void (APIENTRY*)(GLenum);
    using UseProgramFn=void (APIENTRY*)(GLuint);
    using BlitFn=void (APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    GenFn genFbo_=nullptr,genRbo_=nullptr;
    DeleteFn deleteFbo_=nullptr,deleteRbo_=nullptr;
    BindFn bindFbo_=nullptr,bindRbo_=nullptr;
    StorageFn storage_=nullptr;
    AttachFn attach_=nullptr;
    AttachTextureFn attachTexture_=nullptr;
    ActiveTextureFn activeTexture_=nullptr;
    UseProgramFn useProgram_=nullptr;
    StatusFn status_=nullptr;
    BlitFn blit_=nullptr;
    HGLRC context_=nullptr;
    bool ready_=false,active_=false,hud_=false;
    GLuint fbo_=0,color_=0,depth_=0;
    int width_=0,height_=0,outputWidth_=0,outputHeight_=0;
    GLint readFbo_=0,drawFbo_=0,readBuffer_=0,drawBuffer_=0,viewport_[4]{},scissor_[4]{};
    const char* error_="not initialized";
    bool Functions();
    bool BeginTarget(int targetWidth,int targetHeight,int outputWidth,int outputHeight,bool hud);
    void DeleteObjects();
    void PresentTexture();
};
