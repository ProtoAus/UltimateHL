#include "../native/hud_split.hpp"
#include <cstdlib>
#include <iostream>
static void Check(bool value,const char* label){if(!value){std::cerr<<label<<'\n';std::exit(1);}}
int main(){
    // Handles/rectangles as a client with AG art would use them: digits, cross, an ammo icon.
    const HudRect digit{0,30,5,45},cross{0,8,0,8},ammo{0,24,0,24},crosshair{24,48,0,24};
    HudSorter s;s.scaled={{3,digit},{4,cross},{7,ammo}};
    s.Begin();
    // Health: cross, two digits, then its bar on the same line.
    s.Set(4);Check(s.Sprite(&cross,1150),"health cross is scaled");
    s.Set(3);Check(s.Sprite(&digit,1150) && s.Sprite(&digit,1150),"health digits are scaled");
    Check(s.Fill(1152,36),"the health bar right after them is scaled");
    // Ammo: clip digits, bar, reserve digits, icon.
    Check(s.Sprite(&digit,1150) && s.Fill(1150,40) && s.Sprite(&digit,1150),"ammo digits and bar are scaled");
    s.Set(7);Check(s.Sprite(&ammo,1160),"ammo icon is scaled");
    // Text resets: a box drawn after text is not part of a scaled element.
    s.Text();Check(!s.Fill(1150,10),"a fill after text stays 1x");
    // Same sprite but another rectangle (e.g. a crosshair cell) is not scaled.
    s.Set(7);Check(!s.Sprite(&crosshair,588),"crosshair stays 1x");
    s.Set(3);Check(s.Sprite(&digit,1150) && !s.Fill(300,20),"a fill on another line stays 1x");
    Check(!s.Sprite(nullptr,0),"whole-frame sprites stay 1x");
    std::cout<<"Split HUD sort: health/armor/ammo sprites and bars enlarged; text, crosshair and others 1x.\n";
}
