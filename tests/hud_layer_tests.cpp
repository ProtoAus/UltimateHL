#include "wgl_fixture.hpp"
#include "../native/scene_target.hpp"
#include "../native/hud_blend.hpp"
#include <cmath>
using Separate=void(APIENTRY*)(GLenum,GLenum,GLenum,GLenum);
static void Quad(float x,float y,float w,float h){glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
static void Pixel(int x,int y,int r,int g,int b){unsigned char p[4]{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GpuCheck(std::abs(static_cast<int>(p[0])-r)<=2 && std::abs(static_cast<int>(p[1])-g)<=2 && std::abs(static_cast<int>(p[2])-b)<=2,"filtered premultiplied/additive HUD color");}
int main(){
    WglFixture gl;SceneTarget layer;auto separate=reinterpret_cast<Separate>(wglGetProcAddress("glBlendFuncSeparate"));GpuCheck(separate!=nullptr,"separate blend support");
    GpuCheck(!ValidHudScale(1.0f) && !ValidHudScale(1.6f) && ValidHudScale(1.25f) && ValidHudScale(1.75f),"offered scales");
    for(float scale:HudScales){
        const int width=static_cast<int>(40*scale),height=static_cast<int>(20*scale); // a 40x20 canvas at each scale
        glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glClearColor(.2f,.4f,.6f,1);glClear(GL_COLOR_BUFFER_BIT);
        GpuCheck(layer.BeginHud(width,height,scale),layer.Error());
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,layer.Width(),layer.Height(),0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);
        auto alpha=HudCoverage(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);separate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,alpha.source,alpha.destination);
        glColor4f(1,0,0,.5f);Quad(2,2,8,8);
        alpha=HudCoverage(GL_ONE,GL_ONE);separate(GL_ONE,GL_ONE,alpha.source,alpha.destination);
        glColor4f(0,.25f,0,1);Quad(14,2,8,8); // black pixels in additive sprites must not obscure scene
        layer.End();
        Pixel(1,1,51,102,153);Pixel(static_cast<int>(5*scale),height-static_cast<int>(5*scale),153,51,76);
        Pixel(static_cast<int>(17*scale),height-static_cast<int>(5*scale),51,166,153);
        GpuCheck(glGetError()==GL_NO_ERROR,"no HUD compositor GL error");
    }
    layer.Reset();std::cout<<"HUD 1.25x/1.5x/1.75x/2x GPU layer: correct premultiplied and additive colors; no opaque black rectangles.\n";
}
