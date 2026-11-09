#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

inline bool PowerOfTwo(int value) { return value > 0 && !(value & (value - 1)); }

// Keep the source texel grid; do not silently override requested downsampling
// or the legacy engine's conservative allocation cap.
inline bool NativeTextureSize(int width, int height, float picmip, float roundDown, float maximum) {
    return width > 0 && height > 0 && width <= 512 && height <= 512 &&
        picmip == 0 && roundDown == 0 && width <= maximum && height <= maximum;
}

inline bool SafeMipInPlace(std::uint8_t* pixels, int width, int height, int channels) {
    if (!pixels || width < 1 || height < 1 || width > 512 || height > 512 ||
        (channels != 1 && channels != 4)) return false;
    const int outWidth = std::max(1, width / 2), outHeight = std::max(1, height / 2);
    std::vector<std::uint8_t> output(static_cast<std::size_t>(outWidth) * outHeight * channels);
    for (int y = 0; y < outHeight; ++y) for (int x = 0; x < outWidth; ++x) {
        const int x0 = x * width / outWidth, x1 = (x + 1) * width / outWidth;
        const int y0 = y * height / outHeight, y1 = (y + 1) * height / outHeight;
        const unsigned count = static_cast<unsigned>((x1 - x0) * (y1 - y0));
        for (int channel = 0; channel < channels; ++channel) {
            unsigned sum = 0;
            for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx)
                sum += pixels[(static_cast<std::size_t>(sy) * width + sx) * channels + channel];
            output[(static_cast<std::size_t>(y) * outWidth + x) * channels + channel] = static_cast<std::uint8_t>(sum / count);
        }
    }
    std::memcpy(pixels, output.data(), output.size());
    return true;
}

// Keep nearest sprites almost full-span, but safely INSIDE the atlas crop.
// Zero can read adjacent cells. Full .5 restores the old N-1/N compression.
// Linear/blended sprites retain the stock half-texel guard.
inline double HudTexelInset(bool pixelExact,bool blended) { return pixelExact && !blended ? 0.0001 : 0.5; }
// 3248 adds .5 to nearest sprite quad X/Y. Changing UVs alone then samples
// an edge instead of pixel centers and duplicates/drops rows and columns.
// The blended v2 path skips that addition entirely.
inline float HudPixelOriginShift(bool pixelExact,bool blended) { return pixelExact && !blended ? 0.0f : 0.5f; }
