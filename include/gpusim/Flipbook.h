#pragma once
#include <string>
#include <cstdio>


//  A spritesheet whose stem ends in "_<cols>x<rows>" (e.g. "fire_8x4") is a
//  flipbook: a grid of cols*rows frames in reading order — frame 0 is the
//  TOP-LEFT cell, advancing left-to-right then top-to-bottom. The animation
//  length (frameCount) is therefore derived from the filename, not authored by
//  hand. Each frame is extracted to its own texture-array layer named
//  "<stem>_f000", "<stem>_f001", … so both sides agree on names and ordering.
namespace gpusim {


inline bool ParseFlipbookDims(const std::string& stem, int& cols, int& rows)
{
    const std::string::size_type us = stem.rfind('_');
    if (us == std::string::npos || us + 1 >= stem.size()) return false;

    const std::string::size_type x = stem.find('x', us + 1);
    if (x == std::string::npos || x == us + 1 || x + 1 >= stem.size()) return false;

    // digits-only on both sides of the 'x' (parsed inline — no allocs, no <cstdlib>)
    long c = 0, r = 0;
    for (std::string::size_type i = us + 1; i < x; ++i) {
        if (stem[i] < '0' || stem[i] > '9') return false;
        c = c * 10 + (stem[i] - '0');
    }
    for (std::string::size_type i = x + 1; i < stem.size(); ++i) {
        if (stem[i] < '0' || stem[i] > '9') return false;
        r = r * 10 + (stem[i] - '0');
    }
    if (c <= 0 || r <= 0) return false;
    cols = static_cast<int>(c);
    rows = static_cast<int>(r);
    return true;
}

// True if the stem follows the flipbook naming convention.
inline bool IsFlipbook(const std::string& stem)
{
    int cols, rows;
    return ParseFlipbookDims(stem, cols, rows);
}


inline std::string FlipbookFrameName(const std::string& stem, int index)
{
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_f%03d", index);
    return stem + suffix;
}


inline std::string FlipbookStemFromFrame(const std::string& frameName)
{
    const std::string::size_type us = frameName.rfind("_f");
    if (us == std::string::npos || us + 2 >= frameName.size()) return frameName;
    for (std::string::size_type i = us + 2; i < frameName.size(); ++i)
        if (frameName[i] < '0' || frameName[i] > '9') return frameName;
    return frameName.substr(0, us);
}

} // namespace gpusim
