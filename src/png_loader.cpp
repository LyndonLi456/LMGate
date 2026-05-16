#include "png_loader.h"
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

static inline uint32_t readBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = int(a) + int(b) - int(c);
    int pa = abs(p - int(a));
    int pb = abs(p - int(b));
    int pc = abs(p - int(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static void unfilterRow(uint8_t* row, const uint8_t* prior, int bpp, int rowBytes) {
    uint8_t filter = row[0];
    uint8_t* scanline = row + 1;
    switch (filter) {
    case 0: break;
    case 1: // Sub
        for (int i = bpp; i < rowBytes; i++)
            scanline[i] += scanline[i - bpp];
        break;
    case 2: // Up
        if (prior)
            for (int i = 0; i < rowBytes; i++)
                scanline[i] += prior[i + 1];
        break;
    case 3: // Average
        for (int i = 0; i < rowBytes; i++) {
            int left = (i >= bpp) ? scanline[i - bpp] : 0;
            int up = prior ? prior[i + 1] : 0;
            scanline[i] += uint8_t((left + up) >> 1);
        }
        break;
    case 4: // Paeth
        for (int i = 0; i < rowBytes; i++) {
            int left  = (i >= bpp) ? scanline[i - bpp] : 0;
            int up    = prior ? prior[i + 1] : 0;
            int upleft = (i >= bpp && prior) ? prior[i + 1 - bpp] : 0;
            scanline[i] += paethPredictor(uint8_t(left), uint8_t(up), uint8_t(upleft));
        }
        break;
    default: break;
    }
}

static FILE* pngFopen(const char* path) {
#ifdef _WIN32
    // On Windows, fopen doesn't handle UTF-8 by default.
    // Convert UTF-8 path to wide chars and use _wfopen.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) return nullptr;
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), wlen);
    return _wfopen(wpath.data(), L"rb");
#else
    return fopen(path, "rb");
#endif
}

bool pngLoad(const std::string& filepath, PngImage& out) {
    FILE* f = pngFopen(filepath.c_str());
    if (!f) return false;

    // Read entire file
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> fileData(sz);
    if (fread(fileData.data(), 1, sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);

    // Verify PNG signature
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (sz < 8 || memcmp(fileData.data(), sig, 8) != 0) return false;

    size_t pos = 8;
    int width = 0, height = 0, bitDepth = 0, colorType = 0;
    std::vector<uint8_t> idatData;

    while (pos + 12 <= fileData.size()) {
        uint32_t len  = readBE32(&fileData[pos]);
        char type[5] = {};
        memcpy(type, &fileData[pos + 4], 4);
        pos += 8;

        if (pos + len > fileData.size()) return false;

        if (strcmp(type, "IHDR") == 0 && len == 13) {
            width  = readBE32(&fileData[pos]);
            height = readBE32(&fileData[pos + 4]);
            bitDepth = fileData[pos + 8];
            colorType = fileData[pos + 9];
            if (bitDepth != 8 || (colorType != 2 && colorType != 6)) return false;
        } else if (strcmp(type, "IDAT") == 0) {
            idatData.insert(idatData.end(), &fileData[pos], &fileData[pos + len]);
        } else if (strcmp(type, "IEND") == 0) {
            break;
        }
        pos += len + 4; // skip data + CRC
    }

    if (width <= 0 || height <= 0 || idatData.empty()) return false;

    int channels = (colorType == 6) ? 4 : 3;
    int rowBytes = width * channels;
    int rawRowSize = rowBytes + 1; // +1 for filter byte

    // Decompress with zlib
    std::vector<uint8_t> raw(rawRowSize * height);
    z_stream strm = {};
    int ret = inflateInit(&strm);
    if (ret != Z_OK) return false;

    strm.next_in = idatData.data();
    strm.avail_in = (uInt)idatData.size();
    strm.next_out = raw.data();
    strm.avail_out = (uInt)raw.size();

    ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (ret != Z_STREAM_END) return false;

    // Unfilter
    for (int y = 0; y < height; y++) {
        uint8_t* row = raw.data() + y * rawRowSize;
        uint8_t* prior = (y > 0) ? raw.data() + (y - 1) * rawRowSize : nullptr;
        unfilterRow(row, prior, channels, rowBytes);
    }

    // Build RGBA output
    out.width = width;
    out.height = height;
    out.pixels.resize(width * height * 4);

    for (int y = 0; y < height; y++) {
        const uint8_t* src = raw.data() + y * rawRowSize + 1;
        uint8_t* dst = out.pixels.data() + y * width * 4;
        if (colorType == 6) {
            memcpy(dst, src, width * 4);
        } else {
            for (int x = 0; x < width; x++) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
    }

    return true;
}
