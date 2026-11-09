#include "../native/scene_target.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
static void Check(bool value,const char* name) { if (!value) { std::cerr<<"FAIL: "<<name<<"\n";std::exit(1); } }
int main() {
    // Hidden own-process WGL context: never activates a window, injects input,
    // launches the game, changes display modes or calls mouse preference APIs.
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};
    wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"UhlHiddenGpuTest";
    Check(RegisterClassW(&wc)!=0,"hidden test class");
    HWND window=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,32,nullptr,nullptr,instance,nullptr);
    Check(window!=nullptr,"hidden test window");HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;
    pfd.cColorBits=32;pfd.cAlphaBits=8;pfd.cDepthBits=24;pfd.cStencilBits=8;
    const int format=ChoosePixelFormat(dc,&pfd);Check(format && SetPixelFormat(dc,format,&pfd),"pixel format");
    HGLRC context=wglCreateContext(dc);Check(context && wglMakeCurrent(dc,context),"WGL context");
    glViewport(0,0,64,32);glScissor(3,4,20,12);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_TEXTURE_2D);
    glDrawBuffer(GL_BACK);glReadBuffer(GL_BACK);
    SceneTarget target;
    Check(!target.Begin(0,32,2) && !target.Begin(64,32,3),"invalid targets refused");
    Check(target.Begin(64,32,2),target.Error());
    Check(target.Width()==128 && target.Height()==64,"2x each axis, four times scene pixels");
    Check(!target.Begin(64,32,2),"nested capture rejected");
    std::vector<unsigned char> pixels(128*64*4,255);
    for (int y=0;y<64;++y) for (int x=0;x<128;++x) for (int c=0;c<3;++c)
        pixels[(y*128+x)*4+c]=static_cast<unsigned char>((x+y)%2 ? 255 : 0);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,128,0,64,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRasterPos2i(0,0);
    glDrawPixels(128,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    target.End();
    GLint viewport[4]{},scissor[4]{},binding=-1;
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetIntegerv(0x8ca6,&binding);
    Check(viewport[0]==0 && viewport[1]==0 && viewport[2]==64 && viewport[3]==32,"native viewport restored for HUD");
    Check(scissor[0]==3 && scissor[1]==4 && scissor[2]==20 && scissor[3]==12,"native scissor restored");
    Check(binding==0 && !target.Active(),"default output framebuffer restored");
    std::vector<unsigned char> output(64*32*4);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
    for (std::size_t i=0;i<output.size();i+=4)
        Check(output[i]>=127 && output[i]<=128 && output[i+1]==output[i] && output[i+2]==output[i],"four source checker texels averaged, not nearest-resized");
    Check(glGetError()==GL_NO_ERROR,"no GL errors");
    // Reproduce a driver-forced multisample output. A scaled blit here used
    // to fail with GL_INVALID_OPERATION despite the hook reporting resolves.
    using Gen=void(APIENTRY*)(GLsizei,GLuint*);using Bind=void(APIENTRY*)(GLenum,GLuint);
    using StorageMs=void(APIENTRY*)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
    using Storage=void(APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
    using Attach=void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
    using Delete=void(APIENTRY*)(GLsizei,const GLuint*);
    using Blit=void(APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    const auto genFbo=reinterpret_cast<Gen>(wglGetProcAddress("glGenFramebuffers"));
    const auto genRbo=reinterpret_cast<Gen>(wglGetProcAddress("glGenRenderbuffers"));
    const auto bindFbo=reinterpret_cast<Bind>(wglGetProcAddress("glBindFramebuffer"));
    const auto bindRbo=reinterpret_cast<Bind>(wglGetProcAddress("glBindRenderbuffer"));
    const auto storageMs=reinterpret_cast<StorageMs>(wglGetProcAddress("glRenderbufferStorageMultisample"));
    const auto storage=reinterpret_cast<Storage>(wglGetProcAddress("glRenderbufferStorage"));
    const auto attach=reinterpret_cast<Attach>(wglGetProcAddress("glFramebufferRenderbuffer"));
    const auto delFbo=reinterpret_cast<Delete>(wglGetProcAddress("glDeleteFramebuffers"));
    const auto delRbo=reinterpret_cast<Delete>(wglGetProcAddress("glDeleteRenderbuffers"));
    const auto blit=reinterpret_cast<Blit>(wglGetProcAddress("glBlitFramebuffer"));
    Check(genFbo && genRbo && bindFbo && bindRbo && storageMs && storage && attach && delFbo && delRbo && blit,"MSAA test functions");
    GLuint fbos[2]{},rbos[2]{};genFbo(2,fbos);genRbo(2,rbos);
    for(int i=0;i<2;++i){
        bindFbo(0x8d40,fbos[i]);bindRbo(0x8d41,rbos[i]);
        if(i==0)storageMs(0x8d41,4,0x8058,64,32);else storage(0x8d41,0x8058,64,32);
        attach(0x8d40,0x8ce0,0x8d41,rbos[i]);glReadBuffer(0x8ce0);glDrawBuffer(0x8ce0);
    }
    bindRbo(0x8d41,0);bindFbo(0x8d40,fbos[0]);
    Check(target.Begin(64,32,2),"capture into a multisampled output");
    glRasterPos2i(0,0);glDrawPixels(128,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());target.End();
    Check(glGetError()==GL_NO_ERROR,"2x textured presentation into MSAA has no GL error");
    bindFbo(0x8ca8,fbos[0]);glReadBuffer(0x8ce0);bindFbo(0x8ca9,fbos[1]);glDrawBuffer(0x8ce0);
    blit(0,0,64,32,0,0,64,32,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    bindFbo(0x8ca8,fbos[1]);glReadBuffer(0x8ce0);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
    for(std::size_t i=0;i<output.size();i+=4)
        Check(output[i]>=127 && output[i]<=128 && output[i+1]==output[i] && output[i+2]==output[i],"MSAA output receives actual four-texel averaged pixels");
    Check(glGetError()==GL_NO_ERROR,"MSAA image readback has no GL error");
    bindFbo(0x8d40,0);glReadBuffer(GL_BACK);glDrawBuffer(GL_BACK);delFbo(2,fbos);delRbo(2,rbos);
    Check(target.Begin(32,16,2),"target resize");target.End();target.Reset();
    Check(glGetError()==GL_NO_ERROR,"resize/release no GL errors");
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,instance);
    std::cout<<"Hidden-WGL 2x scene FBO/linear resolve/state restoration/resize passed. No desktop input or display changes.\n";
}
