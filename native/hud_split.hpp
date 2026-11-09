#pragma once
#include <vector>

// Sorts the client's HUD draw calls for split scale: a sprite is "scaled" if its sprite and
// exact rectangle belong to health, armor or ammo; a FillRGBA right after one of them, on the
// same line, is that element's bar; text and everything else are not. No engine or GL calls.
struct HudRect {int left,right,top,bottom;};
class HudSorter {
public:
    struct Entry {int sprite;HudRect rc;};
    std::vector<Entry> scaled;
    void Begin(){sprite_=0;last_=false;}
    void Set(int sprite){sprite_=sprite;}
    bool Sprite(const HudRect* rc,int y){
        last_=false;
        if (rc) for (const auto& e:scaled)
            if (e.sprite==sprite_ && e.rc.left==rc->left && e.rc.right==rc->right && e.rc.top==rc->top && e.rc.bottom==rc->bottom) {
                last_=true;top_=y;bottom_=y+rc->bottom-rc->top;break;
            }
        return last_;
    }
    bool Fill(int y,int height){return last_=last_ && y<bottom_ && y+height>top_;}
    void Text(){last_=false;}
private:
    int sprite_=0,top_=0,bottom_=0;bool last_=false;
};
