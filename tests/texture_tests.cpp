#include "../native/texture_math.hpp"
#include <cstdlib>
#include <iostream>
static void Check(bool value,const char* name) { if (!value) { std::cerr<<"FAIL: "<<name<<'\n';std::exit(1); } }
int main() {
    Check(NativeTextureSize(48,128,0,0,512) && NativeTextureSize(96,128,0,0,512),"preserve computer panel NPOT widths");
    Check(!NativeTextureSize(768,128,0,0,32768),"do not trust hardware limit beyond engine cap");
    Check(!NativeTextureSize(96,128,1,0,512) && !NativeTextureSize(96,128,0,1,512),"respect explicit reduction controls");
    Check(HudTexelInset(true,false)>0 && HudTexelInset(true,false)<0.001 && HudTexelInset(false,false)==0.5 && HudTexelInset(true,true)==0.5,"nearest pixel-grid epsilon, stock linear inset");
    for (int size : {12,16,20,24}) {
        for (int x=0;x<size;++x) {
            const double inset=HudTexelInset(true,false);
            const double coordinate=inset+(size-2*inset)*(x+0.5)/size;
            Check(static_cast<int>(coordinate)==x,"guarded full-span grid retains nearest texels at 1:1");
        }
        const double inset=HudTexelInset(true,false);
        Check(static_cast<int>(inset)==0 && static_cast<int>(size-inset)==size-1,"crop endpoints cannot sample neighboring atlas cells");
        const float edge=static_cast<float>((128+size-inset)/512);
        Check(static_cast<int>(edge*512)<128+size,"float-rounded far atlas boundary stays in crop");
    }
    std::vector<std::uint8_t> pixels(48*8*4,255);
    for(int y=0;y<8;++y)for(int x=0;x<48;++x)for(int c=0;c<3;++c)pixels[(y*48+x)*4+c]=static_cast<std::uint8_t>(x%2?0:255);
    int w=48,h=8,levels=0;
    while(w>1||h>1) { Check(SafeMipInPlace(pixels.data(),w,h,4),"safe NPOT mip step");w=std::max(1,w/2);h=std::max(1,h/2);++levels; }
    Check(levels==5 && pixels[0]==127 && pixels[3]==255,"48x8 reaches 1x1 with valid averaged color and alpha");
    std::uint8_t odd[]{0,90,180,255,255,255};
    Check(SafeMipInPlace(odd,3,2,1) && odd[0]==172,"odd-size luminance mip includes all source texels");
    std::uint8_t line[]{0,100,200};
    Check(SafeMipInPlace(line,1,3,1) && line[0]==100,"one-wide odd mip has no out of bounds neighbor");
    Check(!SafeMipInPlace(line,0,1,1) && !SafeMipInPlace(line,1,1,3),"invalid dimensions/format refused");
    std::cout<<"Native texture dimensions, safe mipmaps and HUD texel math passed.\n";
}
