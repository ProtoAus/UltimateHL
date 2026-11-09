#pragma once
struct HudAlphaBlend {unsigned source,destination;};
// Fixed-function HUD RGB is rendered into a transparent layer. Additive RGB
// must NOT make its black sprite rectangle opaque. Normal alpha accumulates
// premultiplied coverage, suitable for final ONE / ONE_MINUS_SRC_ALPHA blend.
inline HudAlphaBlend HudCoverage(unsigned source,unsigned destination){
    constexpr unsigned zero=0,one=1,srcAlpha=0x302,invSrcAlpha=0x303;
    if(destination==one)return {zero,one};
    if((source==srcAlpha || source==one) && destination==invSrcAlpha)return {one,invSrcAlpha};
    return {source,destination};
}
