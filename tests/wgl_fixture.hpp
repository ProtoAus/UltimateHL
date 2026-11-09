#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <cstdlib>
#include <iostream>
inline void GpuCheck(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
struct WglFixture {
    HWND window=nullptr;HDC dc=nullptr;HGLRC context=nullptr;
    WglFixture(){
        WNDCLASSW cls{};cls.style=CS_OWNDC;cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"UhlHiddenFixture34";
        GpuCheck(RegisterClassW(&cls)!=0,"class");window=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,64,32,nullptr,nullptr,cls.hInstance,nullptr);
        GpuCheck(window!=nullptr,"hidden window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cAlphaBits=8;p.cDepthBits=24;p.cStencilBits=8;
        const int format=ChoosePixelFormat(dc,&p);GpuCheck(format && SetPixelFormat(dc,format,&p),"pixel format");
        context=wglCreateContext(dc);GpuCheck(context && wglMakeCurrent(dc,context),"GL context");
        glViewport(0,0,64,32);glDrawBuffer(GL_BACK);glReadBuffer(GL_BACK);glDisable(GL_DITHER);glDisable(0x809d);
    }
    ~WglFixture(){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassW(L"UhlHiddenFixture34",GetModuleHandleW(nullptr));}
};
