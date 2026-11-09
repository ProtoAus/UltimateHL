#include "dxgi_bridge.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace {
constexpr GLenum Fbo=0x8d40,ReadFbo=0x8ca8,DrawFbo=0x8ca9,Rbo=0x8d41,Color=0x8ce0,Depth=0x8d00,Stencil=0x8d20;
constexpr GLenum Complete=0x8cd5,Rgba8=0x8058,DepthStencil=0x88f0;
constexpr wchar_t ChildClass[]=L"UltimateHLCompositedSurface34";
PROC Proc(const char* name,const char* fallback=nullptr) {
    auto valid=[](PROC p){const auto n=reinterpret_cast<std::intptr_t>(p);return p && n!=1 && n!=2 && n!=3 && n!=-1;};
    PROC p=wglGetProcAddress(name);if(!valid(p) && fallback)p=wglGetProcAddress(fallback);return valid(p) ? p : nullptr;
}
LRESULT CALLBACK Surface(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wp,lp);
}
}
bool DxgiBridge::Functions() {
    open_=reinterpret_cast<OpenFn>(Proc("wglDXOpenDeviceNV"));close_=reinterpret_cast<CloseFn>(Proc("wglDXCloseDeviceNV"));
    register_=reinterpret_cast<RegisterFn>(Proc("wglDXRegisterObjectNV"));unregister_=reinterpret_cast<UnregisterFn>(Proc("wglDXUnregisterObjectNV"));
    lock_=reinterpret_cast<LockFn>(Proc("wglDXLockObjectsNV"));unlock_=reinterpret_cast<LockFn>(Proc("wglDXUnlockObjectsNV"));
    genFbo_=reinterpret_cast<GenFn>(Proc("glGenFramebuffers","glGenFramebuffersEXT"));
    genRbo_=reinterpret_cast<GenFn>(Proc("glGenRenderbuffers","glGenRenderbuffersEXT"));
    delFbo_=reinterpret_cast<DeleteFn>(Proc("glDeleteFramebuffers","glDeleteFramebuffersEXT"));
    delRbo_=reinterpret_cast<DeleteFn>(Proc("glDeleteRenderbuffers","glDeleteRenderbuffersEXT"));
    bindFbo_=reinterpret_cast<BindFn>(Proc("glBindFramebuffer","glBindFramebufferEXT"));bindRbo_=reinterpret_cast<BindFn>(Proc("glBindRenderbuffer","glBindRenderbufferEXT"));
    storage_=reinterpret_cast<StorageFn>(Proc("glRenderbufferStorage","glRenderbufferStorageEXT"));
    attach_=reinterpret_cast<AttachFn>(Proc("glFramebufferRenderbuffer","glFramebufferRenderbufferEXT"));
    status_=reinterpret_cast<StatusFn>(Proc("glCheckFramebufferStatus","glCheckFramebufferStatusEXT"));
    blit_=reinterpret_cast<BlitFn>(Proc("glBlitFramebuffer","glBlitFramebufferEXT"));
    return open_ && close_ && register_ && unregister_ && lock_ && unlock_ && genFbo_ && genRbo_ && delFbo_ && delRbo_ && bindFbo_ && bindRbo_ && storage_ && attach_ && status_ && blit_;
}
bool DxgiBridge::Fail(const char* reason){error_=reason;Reset();return false;}
// GL errors stay pending until read. The game can leave one (seen at level changes),
// so clear them first: only errors raised by the bridge's own calls mean it failed.
void DxgiBridge::ClearGameErrors(){for(int i=0;i<32 && glGetError()!=GL_NO_ERROR;++i)++gameErrors_;}
void DxgiBridge::BindScreen(){bindFbo_(Fbo,screenFbo_);glReadBuffer(Color);glDrawBuffer(Color);}
bool DxgiBridge::Attach(HWND parent,int width,int height) {
    Reset();
    context_=wglGetCurrentContext();parent_=parent;width_=width;height_=height;
    if(!parent || !context_ || width<=0 || height<=0)return Fail("invalid GL window or dimensions");
    if(!Functions())return Fail("NV_DX_interop2/FBO functions unavailable");
    ClearGameErrors();
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Surface;cls.lpszClassName=ChildClass;
    RegisterClassW(&cls);
    child_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOPARENTNOTIFY,ChildClass,L"",WS_CHILD|WS_DISABLED,0,0,width,height,parent,nullptr,cls.hInstance,nullptr);
    if(!child_)return Fail("DXGI child surface creation failed");
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=static_cast<UINT>(width);desc.BufferDesc.Height=static_cast<UINT>(height);
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=1;desc.OutputWindow=child_;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selected{};
    HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,3,D3D11_SDK_VERSION,&desc,&swap_,&device_,&selected,&immediate_);
    if(FAILED(result))return Fail("D3D11 windowed bitblt swapchain creation failed");
    Com<IDXGIDevice> dxDevice;Com<IDXGIAdapter> adapter;Com<IDXGIFactory> factory;
    if(SUCCEEDED(device_.As(&dxDevice)) && SUCCEEDED(dxDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        factory->MakeWindowAssociation(parent,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
    Com<IDXGIDevice1> latency;if(SUCCEEDED(device_.As(&latency)))latency->SetMaximumFrameLatency(1);
    if(FAILED(swap_->GetBuffer(0,IID_PPV_ARGS(&buffer_))))return Fail("DXGI backbuffer unavailable");
    D3D11_TEXTURE2D_DESC shared{};buffer_->GetDesc(&shared);shared.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    shared.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
    if(FAILED(device_->CreateTexture2D(&shared,nullptr,&transfer_)))return Fail("D3D11 interop texture allocation failed");
    interopDevice_=open_(device_.Get());if(!interopDevice_)return Fail("D3D11 device is not compatible with this GL GPU");
    genRbo_(1,&dxColor_);
    registration_=register_(interopDevice_,transfer_.Get(),dxColor_,Rbo,0x0002); // WRITE_DISCARD_NV
    if(!registration_)return Fail("NV backbuffer registration failed");
    if(!lock_(interopDevice_,1,&registration_))return Fail("NV backbuffer lock failed");locked_=true;
    GLint read=0,draw=0,rbo=0;glGetIntegerv(0x8caa,&read);glGetIntegerv(0x8ca6,&draw);glGetIntegerv(0x8ca7,&rbo);
    genFbo_(1,&dxFbo_);bindFbo_(Fbo,dxFbo_);attach_(Fbo,Color,Rbo,dxColor_);
    const bool dxComplete=status_(Fbo)==Complete;
    genFbo_(1,&screenFbo_);genRbo_(1,&screenColor_);genRbo_(1,&depth_);
    bindFbo_(Fbo,screenFbo_);bindRbo_(Rbo,screenColor_);storage_(Rbo,Rgba8,width,height);attach_(Fbo,Color,Rbo,screenColor_);
    bindRbo_(Rbo,depth_);storage_(Rbo,DepthStencil,width,height);attach_(Fbo,Depth,Rbo,depth_);attach_(Fbo,Stencil,Rbo,depth_);
    const bool screenComplete=status_(Fbo)==Complete;
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_SCISSOR_BIT|GL_STENCIL_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);glColorMask(TRUE,TRUE,TRUE,TRUE);glDepthMask(TRUE);glStencilMask(~0u);
    glClearColor(0,0,0,1);glClearDepth(1);glClearStencil(0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);glPopAttrib();
    bindRbo_(Rbo,static_cast<GLuint>(rbo));bindFbo_(ReadFbo,static_cast<GLuint>(read));bindFbo_(DrawFbo,static_cast<GLuint>(draw));
    const bool unlocked=unlock_(interopDevice_,1,&registration_)!=FALSE;locked_=false;
    if(!dxComplete || !screenComplete || !unlocked || glGetError()!=GL_NO_ERROR)return Fail("interop/native framebuffer allocation failed");
    error_="none";return true;
}
bool DxgiBridge::Readback() {
    D3D11_TEXTURE2D_DESC desc{};buffer_->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Com<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging)))return false;
    immediate_->CopyResource(staging.Get(),buffer_.Get());D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(immediate_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return false;
    testPixels_.resize(static_cast<std::size_t>(width_)*height_*4);
    for(int y=0;y<height_;++y)std::memcpy(testPixels_.data()+static_cast<std::size_t>(y)*width_*4,
        static_cast<const unsigned char*>(map.pData)+static_cast<std::size_t>(y)*map.RowPitch,static_cast<std::size_t>(width_)*4);
    immediate_->Unmap(staging.Get(),0);return true;
}
bool DxgiBridge::Present(bool validateReadback) {
    if(!Ready() || wglGetCurrentContext()!=context_)return false;
    ClearGameErrors();
    glPushAttrib(GL_SCISSOR_BIT);glDisable(GL_SCISSOR_TEST);
    if(!capturing_) {
        bindFbo_(ReadFbo,0);glReadBuffer(GL_BACK);bindFbo_(DrawFbo,screenFbo_);glDrawBuffer(Color);
        // Bootstrap the already rendered original frame before covering its GL window.
        blit_(0,0,width_,height_,0,0,width_,height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    if(!lock_(interopDevice_,1,&registration_)){glPopAttrib();return Fail("NV frame lock failed");}locked_=true;
    bindFbo_(ReadFbo,screenFbo_);glReadBuffer(Color);bindFbo_(DrawFbo,dxFbo_);glDrawBuffer(Color);
    // OpenGL bottom row -> DXGI last row. No image rescaling or CPU transfer.
    blit_(0,0,width_,height_,0,height_,width_,0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    const GLenum error=glGetError();
    const bool unlocked=unlock_(interopDevice_,1,&registration_)!=FALSE;locked_=false;
    glPopAttrib();
    if(error!=GL_NO_ERROR){std::snprintf(errorText_,sizeof(errorText_),"NV GPU frame copy failed (GL error 0x%04x)",error);return Fail(errorText_);}
    if(!unlocked)return Fail("NV GPU frame copy failed (interop unlock)");
    immediate_->CopyResource(buffer_.Get(),transfer_.Get());
    if(validateReadback && !Readback())return Fail("test-only D3D readback failed");
    ShowWindow(child_,SW_SHOWNA);
    const HRESULT result=swap_->Present(0,0);
    if(FAILED(result))return Fail("DXGI Present failed");
    capturing_=true;BindScreen();return true;
}
void DxgiBridge::Reset() {
    if(child_)ShowWindow(child_,SW_HIDE);
    if(context_ && context_==wglGetCurrentContext() && bindFbo_) {
        bindFbo_(Fbo,0);glReadBuffer(GL_BACK);glDrawBuffer(GL_BACK);
        if(locked_ && unlock_)unlock_(interopDevice_,1,&registration_);
        if(registration_ && unregister_)unregister_(interopDevice_,registration_);
        if(dxFbo_)delFbo_(1,&dxFbo_);if(screenFbo_)delFbo_(1,&screenFbo_);
        if(dxColor_)delRbo_(1,&dxColor_);if(screenColor_)delRbo_(1,&screenColor_);if(depth_)delRbo_(1,&depth_);
    }
    if(interopDevice_ && close_)close_(interopDevice_);
    registration_=interopDevice_=nullptr;locked_=capturing_=false;dxFbo_=screenFbo_=dxColor_=screenColor_=depth_=0;
    transfer_.Reset();buffer_.Reset();swap_.Reset();immediate_.Reset();device_.Reset();
    if(child_)DestroyWindow(child_);child_=parent_=nullptr;context_=nullptr;width_=height_=0;
}
