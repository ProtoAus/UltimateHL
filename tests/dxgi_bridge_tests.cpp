#include "wgl_fixture.hpp"
#include "../native/dxgi_bridge.hpp"
static void Bands(float r1,float g1,float b1,float r2,float g2,float b2){
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,64,16);glClearColor(r1,g1,b1,1);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0,16,64,16);glClearColor(r2,g2,b2,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
}
static void CheckBands(const std::vector<unsigned char>& pixels,int topR,int topG,int topB,int bottomR,int bottomG,int bottomB){
    GpuCheck(pixels.size()==64*32*4,"D3D dimensions");
    for(int y=0;y<32;++y)for(int x=0;x<64;++x){
        const auto* p=pixels.data()+(y*64+x)*4;
        GpuCheck(p[0]==(y<16 ? topR : bottomR) && p[1]==(y<16 ? topG : bottomG) && p[2]==(y<16 ? topB : bottomB),"orientation/colors are pixel exact");
    }
}
int main(){
    WglFixture gl;DxgiBridge bridge;Bands(1,0,0,0,1,0);
    if(!bridge.Attach(gl.window,64,32)){std::cout<<"SKIP: "<<bridge.Error()<<'\n';return 77;}
    GpuCheck(bridge.Present(true),bridge.Error());CheckBands(bridge.TestPixels(),0,255,0,255,0,0);
    GpuCheck(bridge.Capturing(),"next frame captured offscreen");
    Bands(0,0,1,1,1,0);GpuCheck(bridge.Present(true),bridge.Error());CheckBands(bridge.TestPixels(),255,255,0,0,0,255);
    // The game can leave a GL error pending (seen at level changes); that is not a copy failure.
    glEnable(0xffff);Bands(1,0,1,0,1,1);
    const bool presented=bridge.Present(true);GpuCheck(presented,bridge.Error());CheckBands(bridge.TestPixels(),0,255,255,255,0,255);
    GpuCheck(bridge.GameErrors()==1,"the game's pending error is cleared, not blamed on the copy");
    GpuCheck(glGetError()==GL_NO_ERROR,"no GL errors");bridge.Reset();
    GLint binding=-1;glGetIntegerv(0x8ca6,&binding);GpuCheck(binding==0,"GL framebuffer restored on shutdown");
    std::cout<<"GPU GL -> NV/D3D11 -> windowed DXGI: two frames exact RGBA/orientation, no CPU readback in production.\n";
}
