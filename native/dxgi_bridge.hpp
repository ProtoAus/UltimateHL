#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>

// Windowed bitblt DXGI presentation of the original OpenGL frame. GPU-only
// NV_DX_interop2 transfer; no pixel rescaling, fake frames or desktop readback.
class DxgiBridge {
public:
    bool Attach(HWND parent,int width,int height);
    bool Present(bool validateReadback=false);
    void Reset();
    bool Ready()const{return device_!=nullptr && registration_!=nullptr;}
    bool Capturing()const{return capturing_;}
    HGLRC Context()const{return context_;}
    HWND Parent()const{return parent_;}
    int Width()const{return width_;}int Height()const{return height_;}
    const char* Error()const{return error_;}
    unsigned GameErrors()const{return gameErrors_;}
    const std::vector<unsigned char>& TestPixels()const{return testPixels_;}
private:
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
    using OpenFn=HANDLE(WINAPI*)(void*);
    using CloseFn=BOOL(WINAPI*)(HANDLE);
    using RegisterFn=HANDLE(WINAPI*)(HANDLE,void*,GLuint,GLenum,GLenum);
    using UnregisterFn=BOOL(WINAPI*)(HANDLE,HANDLE);
    using LockFn=BOOL(WINAPI*)(HANDLE,GLint,HANDLE*);
    using GenFn=void(APIENTRY*)(GLsizei,GLuint*);
    using DeleteFn=void(APIENTRY*)(GLsizei,const GLuint*);
    using BindFn=void(APIENTRY*)(GLenum,GLuint);
    using StorageFn=void(APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
    using AttachFn=void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
    using StatusFn=GLenum(APIENTRY*)(GLenum);
    using BlitFn=void(APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    OpenFn open_=nullptr;CloseFn close_=nullptr;RegisterFn register_=nullptr;UnregisterFn unregister_=nullptr;
    LockFn lock_=nullptr,unlock_=nullptr;GenFn genFbo_=nullptr,genRbo_=nullptr;DeleteFn delFbo_=nullptr,delRbo_=nullptr;
    BindFn bindFbo_=nullptr,bindRbo_=nullptr;StorageFn storage_=nullptr;AttachFn attach_=nullptr;StatusFn status_=nullptr;BlitFn blit_=nullptr;
    HWND parent_=nullptr,child_=nullptr;HGLRC context_=nullptr;int width_=0,height_=0;
    HANDLE interopDevice_=nullptr,registration_=nullptr;
    GLuint screenFbo_=0,screenColor_=0,depth_=0,dxFbo_=0,dxColor_=0;
    bool capturing_=false,locked_=false;
    Com<ID3D11Device> device_;Com<ID3D11DeviceContext> immediate_;Com<IDXGISwapChain> swap_;Com<ID3D11Texture2D> buffer_,transfer_;
    const char* error_="not initialized";char errorText_[64]{};std::vector<unsigned char> testPixels_;unsigned gameErrors_=0;
    bool Functions();bool Fail(const char* reason);bool Readback();void ClearGameErrors();
    void BindScreen();
};
