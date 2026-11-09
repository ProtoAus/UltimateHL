// Bounded Desktop Duplication check. Reads ONLY two 32x32 diagnostic corner
// patches of the explicit, path-validated, foreground game. Saves them ONLY
// when both contain the known game-generated colors (no desktop screenshot). No hooks, injection, input or saved OBS changes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
template<class T>using Com=Microsoft::WRL::ComPtr<T>;
namespace fs=std::filesystem;
struct Target {DWORD pid;HWND window=nullptr;};
BOOL CALLBACK Find(HWND window,LPARAM value){
    auto& target=*reinterpret_cast<Target*>(value);DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t name[64]{};GetClassNameW(window,name,64);
    if(pid==target.pid && !wcscmp(name,L"Valve001") && IsWindowVisible(window)){target.window=window;return FALSE;}return TRUE;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=4){std::cerr<<"Usage: desktop_probe PID exact-game-exe NEW-output.bmp\n";return 2;}
    SetProcessDPIAware();Target target{static_cast<DWORD>(wcstoul(argv[1],nullptr,10))};
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,target.pid);wchar_t image[32768]{};DWORD length=32768;
    const bool valid=process && QueryFullProcessImageNameW(process,0,image,&length) &&
        !_wcsicmp(fs::weakly_canonical(image).c_str(),fs::weakly_canonical(argv[2]).c_str());if(process)CloseHandle(process);
    if(!valid || fs::exists(argv[3]) || !fs::is_directory(fs::path(argv[3]).parent_path())){std::cerr<<"PID/path/output validation failed\n";return 2;}
    EnumWindows(Find,reinterpret_cast<LPARAM>(&target));
    if(!target.window || GetForegroundWindow()!=target.window){std::cout<<"SKIP: target game is not foreground; no pixels read\n";return 77;}
    RECT client{};GetClientRect(target.window,&client);POINT position{0,0};ClientToScreen(target.window,&position);
    const int width=client.right,height=client.bottom;const HMONITOR monitor=MonitorFromWindow(target.window,MONITOR_DEFAULTTONEAREST);
    Com<IDXGIFactory1> factory;HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr))return 3;
    Com<IDXGIAdapter1> chosen;Com<IDXGIOutput1> output;DXGI_OUTPUT_DESC outputDesc{};
    for(UINT a=0;!output;++a){
        Com<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        for(UINT i=0;;++i){Com<IDXGIOutput> candidate;if(adapter->EnumOutputs(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_OUTPUT_DESC desc{};candidate->GetDesc(&desc);
            if(desc.Monitor==monitor && SUCCEEDED(candidate.As(&output))){chosen=adapter;outputDesc=desc;break;}
        }
    }
    if(!output || outputDesc.Rotation!=DXGI_MODE_ROTATION_IDENTITY){std::cerr<<"Monitor/rotation unsupported\n";return 3;}
    const int x=position.x-outputDesc.DesktopCoordinates.left,y=position.y-outputDesc.DesktopCoordinates.top;
    if(width<64 || height<64 || x<0 || y<0 || position.x+width>outputDesc.DesktopCoordinates.right || position.y+height>outputDesc.DesktopCoordinates.bottom){std::cerr<<"Target client not fully inside one monitor\n";return 3;}
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL selected{};
    hr=D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&selected,&context);
    if(FAILED(hr))return 3;
    Com<IDXGIOutputDuplication> duplication;hr=output->DuplicateOutput(device.Get(),&duplication);
    if(FAILED(hr)){std::cout<<"DuplicateOutput HRESULT="<<std::hex<<static_cast<unsigned>(hr)<<'\n';return 4;}
    constexpr int patchWidth=64,patchHeight=32;
    std::vector<unsigned char> pixels(patchWidth*patchHeight*4);bool captured=false,markers=false;
    const ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<3500 && !markers){
        if(GetForegroundWindow()!=target.window){std::cout<<"SKIP: foreground changed; no image saved\n";return 77;}
        DXGI_OUTDUPL_FRAME_INFO frame{};Com<IDXGIResource> resource;hr=duplication->AcquireNextFrame(200,&frame,&resource);
        if(hr==DXGI_ERROR_WAIT_TIMEOUT)continue;
        if(hr==DXGI_ERROR_ACCESS_LOST){
            duplication.Reset();Sleep(40);hr=output->DuplicateOutput(device.Get(),&duplication);
            if(SUCCEEDED(hr))continue;
        }
        if(FAILED(hr)){std::cout<<"AcquireNextFrame HRESULT="<<std::hex<<static_cast<unsigned>(hr)<<'\n';return 4;}
        Com<ID3D11Texture2D> desktop;resource.As(&desktop);D3D11_TEXTURE2D_DESC desc{};desktop->GetDesc(&desc);
        desc.Width=patchWidth;desc.Height=patchHeight;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.ArraySize=1;desc.MipLevels=1;
        Com<ID3D11Texture2D> staging;hr=device->CreateTexture2D(&desc,nullptr,&staging);
        if(SUCCEEDED(hr)){
            const D3D11_BOX top{static_cast<UINT>(x),static_cast<UINT>(y),0,static_cast<UINT>(x+32),static_cast<UINT>(y+32),1};
            const D3D11_BOX bottom{static_cast<UINT>(x+width-32),static_cast<UINT>(y+height-32),0,static_cast<UINT>(x+width),static_cast<UINT>(y+height),1};
            context->CopySubresourceRegion(staging.Get(),0,0,0,0,desktop.Get(),0,&top);
            context->CopySubresourceRegion(staging.Get(),0,32,0,0,desktop.Get(),0,&bottom);
        }
        duplication->ReleaseFrame();
        if(FAILED(hr)){std::cerr<<"Readback allocation failed\n";return 4;}
        D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
        if(FAILED(hr))return 4;
        for(int row=0;row<patchHeight;++row)std::memcpy(pixels.data()+static_cast<std::size_t>(row)*patchWidth*4,
            static_cast<const unsigned char*>(mapped.pData)+static_cast<std::size_t>(row)*mapped.RowPitch,patchWidth*4);
        context->Unmap(staging.Get(),0);captured=true;
        const auto* top=pixels.data()+(8*patchWidth+8)*4;const auto* bottom=pixels.data()+(8*patchWidth+40)*4;
        markers=top[0]>230 && top[1]<25 && top[2]>230 && bottom[0]<25 && bottom[1]>230 && bottom[2]<25;
    }
    if(!captured || GetForegroundWindow()!=target.window){std::cout<<"SKIP: no stable foreground capture\n";return 77;}
    if(!markers){std::cout<<"FAIL: game markers not visible in Desktop Duplication; no pixels saved\n";return 5;}
    BITMAPFILEHEADER header{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=patchWidth;info.biHeight=-patchHeight;
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info);
    header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
    std::ofstream file(fs::path(argv[3]),std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));
    file.write(reinterpret_cast<const char*>(&info),sizeof(info));file.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    std::size_t nonBlack=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i] || pixels[i+1] || pixels[i+2])++nonBlack;
    std::cout<<"Desktop Duplication: "<<width<<'x'<<height<<", readback=two 32x32 game markers, nonblack="<<nonBlack<<", both_game_markers="<<markers<<", foreground PID="<<target.pid<<'\n';
    return markers ? 0 : 5;
}
