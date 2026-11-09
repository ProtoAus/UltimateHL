#include "wgl_fixture.hpp"
#include "../native/texture_math.hpp"
#include "../native/transition_policy.hpp"
#include <vector>
static int Render(float origin,int sizeX,int sizeY){
    constexpr int atlasX=8,atlasY=4;
    std::vector<unsigned char> atlas(64*32*4,0);
    for(int y=0;y<32;++y)for(int x=0;x<64;++x){
        auto* p=atlas.data()+(y*64+x)*4;p[0]=255;p[3]=255;
        if(x>=atlasX && x<atlasX+sizeX && y>=atlasY && y<atlasY+sizeY){p[0]=static_cast<unsigned char>(10+(x-atlasX)*10);p[1]=static_cast<unsigned char>(10+(y-atlasY)*12);p[2]=123;}
    }
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,32,0,GL_RGBA,GL_UNSIGNED_BYTE,atlas.data());
    glDisable(GL_DEPTH_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,64,0,32,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    const float inset=static_cast<float>(HudTexelInset(true,false));
    const float left=(atlasX+inset)/64,right=(atlasX+sizeX-inset)/64,bottom=(atlasY+inset)/32,top=(atlasY+sizeY-inset)/32;
    const float x=5+origin,y=5+origin;
    glBegin(GL_QUADS);glTexCoord2f(left,bottom);glVertex2f(x,y);glTexCoord2f(right,bottom);glVertex2f(x+sizeX,y);
    glTexCoord2f(right,top);glVertex2f(x+sizeX,y+sizeY);glTexCoord2f(left,top);glVertex2f(x,y+sizeY);glEnd();
    std::vector<unsigned char> pixels(sizeX*sizeY*4);glReadPixels(5,5,sizeX,sizeY,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    int mismatch=0;
    for(int row=0;row<sizeY;++row)for(int col=0;col<sizeX;++col){
        const auto* p=pixels.data()+(row*sizeX+col)*4;const auto* expected=atlas.data()+((row+atlasY)*64+col+atlasX)*4;
        if(std::memcmp(p,expected,4))++mismatch;
    }
    if(origin==0){
        std::vector<unsigned char> border((sizeX+2)*(sizeY+2)*4);
        glReadPixels(4,4,sizeX+2,sizeY+2,GL_RGBA,GL_UNSIGNED_BYTE,border.data());
        for(int row=0;row<sizeY+2;++row)for(int col=0;col<sizeX+2;++col){
            if(row && col && row<sizeY+1 && col<sizeX+1)continue;
            const auto* p=border.data()+(row*(sizeX+2)+col)*4;
            GpuCheck(!p[0]&&!p[1]&&!p[2],"no extra atlas line/column around sprite");
        }
    }
    glDeleteTextures(1,&tex);return mismatch;
}
int main(){
    WglFixture gl;const int previous=Render(.5f,20,16),corrected=Render(HudPixelOriginShift(true,false),20,16);
    for(const auto size:std::vector<std::pair<int,int>>{{12,16},{20,24},{12,12},{16,16}})
        GpuCheck(Render(HudPixelOriginShift(true,false),size.first,size.second)==0,"compact/stock digit, reticle and icon source texels exact");
    GpuCheck(previous>0,"reproduce v3.3 duplicate/drop artifact with real half-pixel origin");
    GpuCheck(corrected==0,"corrected nearest geometry reproduces every source texel");
    GpuCheck(glGetError()==GL_NO_ERROR,"no sprite GL errors");
    GpuCheck(!TransitionMayResume(true,0,true),"old active command cannot end GUI level load");
    GpuCheck(!TransitionMayResume(true,1,false),"nested synchronous load stays protected");
    GpuCheck(!TransitionMayResume(false,0,false) && TransitionMayResume(true,0,false),"resume only after GUI and inactive tail finish");
    std::cout<<"Sprite grid: old geometry mismatches="<<previous<<", corrected="<<corrected<<". Shared transition resume policy passed.\n";
}
