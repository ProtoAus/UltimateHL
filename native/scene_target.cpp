#include "scene_target.hpp"
#include <cstdint>
#include <limits>
#include <cmath>
namespace {
constexpr GLenum Framebuffer=0x8d40,ReadFramebuffer=0x8ca8,DrawFramebuffer=0x8ca9;
constexpr GLenum ReadBinding=0x8caa,DrawBinding=0x8ca6,Renderbuffer=0x8d41,RboBinding=0x8ca7;
constexpr GLenum ColorAttachment=0x8ce0,DepthAttachment=0x8d00,StencilAttachment=0x8d20;
constexpr GLenum Complete=0x8cd5,Depth24Stencil8=0x88f0,Rgba8=0x8058,MaxRboSize=0x84e8;
constexpr GLenum ActiveTexture=0x84e0,Texture0=0x84c0,MaxTextureUnits=0x84e2,CurrentProgram=0x8b8d;
PROC Proc(const char* name,const char* fallback) {
    PROC proc=wglGetProcAddress(name);
    const auto invalid=[](PROC p) { const auto v=reinterpret_cast<std::intptr_t>(p);return !p || v==1 || v==2 || v==3 || v==-1; };
    if (invalid(proc)) proc=wglGetProcAddress(fallback);
    return invalid(proc) ? nullptr : proc;
}
}
bool SceneTarget::Functions() {
    const HGLRC current=wglGetCurrentContext();
    if (!current) { error_="no current GL context";return false; }
    if (context_==current) return ready_;
    fbo_=color_=depth_=0;width_=height_=0;active_=false;context_=current;
    genFbo_=reinterpret_cast<GenFn>(Proc("glGenFramebuffers","glGenFramebuffersEXT"));
    genRbo_=reinterpret_cast<GenFn>(Proc("glGenRenderbuffers","glGenRenderbuffersEXT"));
    deleteFbo_=reinterpret_cast<DeleteFn>(Proc("glDeleteFramebuffers","glDeleteFramebuffersEXT"));
    deleteRbo_=reinterpret_cast<DeleteFn>(Proc("glDeleteRenderbuffers","glDeleteRenderbuffersEXT"));
    bindFbo_=reinterpret_cast<BindFn>(Proc("glBindFramebuffer","glBindFramebufferEXT"));
    bindRbo_=reinterpret_cast<BindFn>(Proc("glBindRenderbuffer","glBindRenderbufferEXT"));
    storage_=reinterpret_cast<StorageFn>(Proc("glRenderbufferStorage","glRenderbufferStorageEXT"));
    attach_=reinterpret_cast<AttachFn>(Proc("glFramebufferRenderbuffer","glFramebufferRenderbufferEXT"));
    attachTexture_=reinterpret_cast<AttachTextureFn>(Proc("glFramebufferTexture2D","glFramebufferTexture2DEXT"));
    activeTexture_=reinterpret_cast<ActiveTextureFn>(Proc("glActiveTexture","glActiveTextureARB"));
    useProgram_=reinterpret_cast<UseProgramFn>(Proc("glUseProgram","glUseProgramObjectARB"));
    status_=reinterpret_cast<StatusFn>(Proc("glCheckFramebufferStatus","glCheckFramebufferStatusEXT"));
    blit_=reinterpret_cast<BlitFn>(Proc("glBlitFramebuffer","glBlitFramebufferEXT"));
    ready_=genFbo_ && genRbo_ && deleteFbo_ && deleteRbo_ && bindFbo_ && bindRbo_ && storage_ && attach_ && attachTexture_ && status_ && blit_;
    error_=ready_ ? "none" : "FBO/blit functions unavailable";return ready_;
}
void SceneTarget::DeleteObjects() {
    if (fbo_) deleteFbo_(1,&fbo_);
    if (color_) glDeleteTextures(1,&color_);
    if (depth_) deleteRbo_(1,&depth_);
    fbo_=color_=depth_=0;width_=height_=0;
}
void SceneTarget::Reset() {
    if (active_) End();
    if (context_ && wglGetCurrentContext()==context_ && ready_) DeleteObjects();
    context_=nullptr;ready_=false;fbo_=color_=depth_=0;width_=height_=0;
}
bool SceneTarget::Begin(int width,int height,int scale) {
    if (width<=0 || height<=0 || scale!=2 || width>std::numeric_limits<int>::max()/scale || height>std::numeric_limits<int>::max()/scale) {
        error_="invalid dimensions/scale";return false;
    }
    return BeginTarget(width*scale,height*scale,width,height,false);
}
bool SceneTarget::BeginHud(int width,int height,float scale) {
    if(width<1 || height<1 || !ValidHudScale(scale)){error_="invalid HUD dimensions/scale";return false;}
    return BeginTarget(static_cast<int>(std::ceil(static_cast<double>(width)/scale)),static_cast<int>(std::ceil(static_cast<double>(height)/scale)),width,height,true);
}
bool SceneTarget::BeginTarget(int highWidth,int highHeight,int width,int height,bool hud) {
    if (active_) { error_="nested capture rejected";return false; }
    if (!Functions()) return false;
    GLint limit=0,textureLimit=0,viewportLimit[2]{};
    glGetIntegerv(MaxRboSize,&limit);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&textureLimit);glGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewportLimit);
    if (highWidth>limit || highHeight>limit || highWidth>textureLimit || highHeight>textureLimit || highWidth>viewportLimit[0] || highHeight>viewportLimit[1]) {
        error_="requested scene exceeds GPU limits";return false;
    }
    glGetIntegerv(ReadBinding,&readFbo_);glGetIntegerv(DrawBinding,&drawFbo_);
    glGetIntegerv(GL_READ_BUFFER,&readBuffer_);glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer_);
    glGetIntegerv(GL_VIEWPORT,viewport_);glGetIntegerv(GL_SCISSOR_BOX,scissor_);
    GLint oldRbo=0,oldTexture=0;glGetIntegerv(RboBinding,&oldRbo);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    if (!fbo_ || width_!=highWidth || height_!=highHeight) {
        DeleteObjects();genFbo_(1,&fbo_);genRbo_(1,&depth_);
        // GoldSrc manually increments low texture names instead of always using
        // glGenTextures. Reserve a private high name to avoid later map collisions.
        static GLuint nextPrivate=0x55480000;
        do {color_=++nextPrivate;}while(glIsTexture(color_));
        glBindTexture(GL_TEXTURE_2D,color_);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812f);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812f);
        glTexImage2D(GL_TEXTURE_2D,0,Rgba8,highWidth,highHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(oldTexture));
        bindFbo_(Framebuffer,fbo_);attachTexture_(Framebuffer,ColorAttachment,GL_TEXTURE_2D,color_,0);
        bindRbo_(Renderbuffer,depth_);storage_(Renderbuffer,Depth24Stencil8,highWidth,highHeight);
        attach_(Framebuffer,DepthAttachment,Renderbuffer,depth_);attach_(Framebuffer,StencilAttachment,Renderbuffer,depth_);
        bindRbo_(Renderbuffer,static_cast<GLuint>(oldRbo));
        if (!fbo_ || !color_ || !depth_ || status_(Framebuffer)!=Complete) {
            DeleteObjects();bindFbo_(ReadFramebuffer,static_cast<GLuint>(readFbo_));bindFbo_(DrawFramebuffer,static_cast<GLuint>(drawFbo_));
            error_="scene FBO allocation failed";return false;
        }
        width_=highWidth;height_=highHeight;
    } else bindFbo_(Framebuffer,fbo_);
    glReadBuffer(ColorAttachment);glDrawBuffer(ColorAttachment);
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_SCISSOR_BIT);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glStencilMask(~0u);
    glClearColor(0,0,0,hud ? 0.0f : 1.0f);glClearDepth(1);glClearStencil(0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glPopAttrib();
    if(hud){glViewport(0,0,highWidth,highHeight);glScissor(0,0,highWidth,highHeight);}
    else {
        glViewport(viewport_[0]*2,viewport_[1]*2,viewport_[2]*2,viewport_[3]*2);
        glScissor(scissor_[0]*2,scissor_[1]*2,scissor_[2]*2,scissor_[3]*2);
    }
    outputWidth_=width;outputHeight_=height;hud_=hud;active_=true;error_="none";return true;
}
void SceneTarget::PresentTexture() {
    // Scaled glBlitFramebuffer into a driver-forced MSAA window is illegal.
    // A textured quad performs the same exact 2:1 bilinear four-texel average
    // and is legal for either single- or multisample output. Save legacy state.
    GLint matrixMode=GL_MODELVIEW,activeTexture=Texture0,program=0,units=1;
    glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
    if(activeTexture_){glGetIntegerv(ActiveTexture,&activeTexture);glGetIntegerv(MaxTextureUnits,&units);}
    if(useProgram_){glGetIntegerv(CurrentProgram,&program);if(program)useProgram_(0);}
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);glDisable(GL_FOG);glDisable(GL_LIGHTING);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_COLOR_LOGIC_OP);glDisable(GL_POLYGON_SMOOTH);glDisable(GL_POLYGON_STIPPLE);glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(0x809d); // no multisample coverage needed for a full output quad
    for(GLenum plane=GL_CLIP_PLANE0;plane<=GL_CLIP_PLANE5;++plane)glDisable(plane);
    for(int unit=0;unit<units && unit<32;++unit){
        if(activeTexture_)activeTexture_(Texture0+unit);
        glDisable(GL_TEXTURE_1D);glDisable(GL_TEXTURE_2D);glDisable(0x806f);glDisable(0x8513);
        glDisable(GL_TEXTURE_GEN_S);glDisable(GL_TEXTURE_GEN_T);glDisable(GL_TEXTURE_GEN_R);glDisable(GL_TEXTURE_GEN_Q);
    }
    if(activeTexture_)activeTexture_(Texture0);
    glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,color_);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    // HUD target carries premultiplied normal-alpha color plus additive RGB
    // with zero alpha. Bilinear interpolation cannot leak black atlas borders.
    if(hud_){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
    glViewport(0,0,outputWidth_,outputHeight_);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_TEXTURE);glPopMatrix();
    glMatrixMode(static_cast<GLenum>(matrixMode));glPopAttrib();
    if(activeTexture_)activeTexture_(static_cast<GLenum>(activeTexture));
    if(useProgram_ && program)useProgram_(static_cast<GLuint>(program));
}
void SceneTarget::End() {
    if (!active_ || wglGetCurrentContext()!=context_) { active_=false;return; }
    bindFbo_(ReadFramebuffer,fbo_);glReadBuffer(ColorAttachment);
    bindFbo_(DrawFramebuffer,static_cast<GLuint>(drawFbo_));glDrawBuffer(static_cast<GLenum>(drawBuffer_));
    GLint samples=0;glGetIntegerv(0x80a9,&samples);
    if(hud_ || samples>0)PresentTexture();
    else {
        glPushAttrib(GL_SCISSOR_BIT);glDisable(GL_SCISSOR_TEST);
        blit_(0,0,width_,height_,0,0,outputWidth_,outputHeight_,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glPopAttrib();
    }
    bindFbo_(ReadFramebuffer,static_cast<GLuint>(readFbo_));glReadBuffer(static_cast<GLenum>(readBuffer_));
    glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);glScissor(scissor_[0],scissor_[1],scissor_[2],scissor_[3]);
    active_=false;
}
