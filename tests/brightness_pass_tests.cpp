#include "wgl_fixture.hpp"
#include "../native/brightness_pass.hpp"
#include <cmath>
using GenFn=void(APIENTRY*)(GLsizei,GLuint*);using BindFn=void(APIENTRY*)(GLenum,GLuint);
using StorageFn=void(APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);using AttachFn=void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
static void Bands(){
    glEnable(GL_SCISSOR_TEST);
    glScissor(0,0,16,32);glClearColor(64/255.f,128/255.f,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(16,0,16,32);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(32,0,32,32);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}
static void Pixel(int x,int r,int g,int b,const char* what){unsigned char p[4]{};glReadPixels(x,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(std::abs(p[0]-r)>1 || std::abs(p[1]-g)>1 || std::abs(p[2]-b)>1)std::cerr<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<" expected "<<r<<','<<g<<','<<b<<'\n';
    GpuCheck(std::abs(p[0]-r)<=1 && std::abs(p[1]-g)<=1 && std::abs(p[2]-b)<=1,what);}
static int Curve(int value,float brightness){return static_cast<int>(std::lround(255*std::pow(value/255.0,BrightnessPass::Exponent(brightness))));}
static void CheckCurve(BrightnessPass& pass,float brightness,const char* where){
    Bands();GpuCheck(pass.Apply(64,32,brightness),pass.Error());
    Pixel(4,Curve(64,brightness),Curve(128,brightness),255,where);
    Pixel(20,0,0,0,"black stays black");Pixel(40,255,255,255,"white stays white (no clipping)");
}
int main(){
    WglFixture gl;BrightnessPass pass;
    GpuCheck(BrightnessPass::Exponent(0)==1 && BrightnessPass::Exponent(1)==0.5f && BrightnessPass::Exponent(-0.5f)==1.5f,"curve");
    Bands();GpuCheck(pass.Apply(64,32,0),"zero is a no-op");Pixel(4,64,128,255,"brightness 0 leaves the frame untouched");
    CheckCurve(pass,1.0f,"back buffer brighter");
    CheckCurve(pass,-0.5f,"back buffer darker");
    // The DXGI presenter draws the frame into its own FBO; the pass must work there too.
    auto gen=reinterpret_cast<GenFn>(wglGetProcAddress("glGenFramebuffers"));auto genR=reinterpret_cast<GenFn>(wglGetProcAddress("glGenRenderbuffers"));
    auto bind=reinterpret_cast<BindFn>(wglGetProcAddress("glBindFramebuffer"));auto bindR=reinterpret_cast<BindFn>(wglGetProcAddress("glBindRenderbuffer"));
    auto storage=reinterpret_cast<StorageFn>(wglGetProcAddress("glRenderbufferStorage"));auto attach=reinterpret_cast<AttachFn>(wglGetProcAddress("glFramebufferRenderbuffer"));
    GLuint fbo=0,rbo=0;gen(1,&fbo);genR(1,&rbo);bindR(0x8d41,rbo);storage(0x8d41,0x8058,64,32);
    bind(0x8d40,fbo);attach(0x8d40,0x8ce0,0x8d41,rbo);glDrawBuffer(0x8ce0);glReadBuffer(0x8ce0);
    CheckCurve(pass,0.5f,"capture FBO brighter");
    GLint binding=-1,program=-1,viewport[4]{};glGetIntegerv(0x8ca6,&binding);glGetIntegerv(0x8b8d,&program);glGetIntegerv(GL_VIEWPORT,viewport);
    GpuCheck(binding==static_cast<GLint>(fbo) && program==0 && viewport[2]==64 && viewport[3]==32,"framebuffer, program and viewport restored");
    GpuCheck(glIsEnabled(GL_BLEND)==GL_FALSE && glGetError()==GL_NO_ERROR,"no GL errors or leaked state");
    bind(0x8d40,0);pass.Reset();
    std::cout<<"Brightness pass: exact curve in back buffer and capture FBO; black/white fixed; state restored.\n";
}
