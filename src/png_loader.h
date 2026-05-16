#pragma once
#include <vector>
#include <cstdint>
#include <string>

struct PngImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA interleaved, top-to-bottom
};

bool pngLoad(const std::string& filepath, PngImage& out);
