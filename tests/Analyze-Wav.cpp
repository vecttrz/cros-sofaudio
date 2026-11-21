#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        wprintf(L"Usage: Analyze-Wav <file.wav>\n");
        return 2;
    }
    FILE *f = nullptr;
    if (_wfopen_s(&f, argv[1], L"rb") != 0 || !f) return 1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> bytes(size);
    if (fread(bytes.data(), 1, size, f) != (size_t)size) { fclose(f); return 1; }
    fclose(f);

    size_t dataPos = std::string::npos;
    for (size_t i = 0; i + 4 < bytes.size(); ++i) {
        if (memcmp(&bytes[i], "data", 4) == 0) { dataPos = i + 8; break; }
    }
    if (dataPos == std::string::npos || dataPos >= bytes.size()) return 1;
    uint32_t channels = 1, rate = 48000;
    for (size_t i = 0; i + 8 < bytes.size(); ++i) {
        if (memcmp(&bytes[i], "fmt ", 4) == 0) {
            memcpy(&channels, &bytes[i + 10], 2);
            memcpy(&rate, &bytes[i + 12], 4);
            break;
        }
    }

    size_t frames = (bytes.size() - dataPos) / (channels * 4);
    std::vector<double> ch0(frames);
    for (size_t i = 0; i < frames; ++i) {
        float v;
        memcpy(&v, &bytes[dataPos + i * channels * 4], 4);
        ch0[i] = v;
    }

    double sum = 0, sumSq = 0, min = 1e30, max = -1e30;
    for (double v : ch0) {
        sum += v; sumSq += v * v;
        min = std::min(min, v); max = std::max(max, v);
    }
    double mean = sum / frames;
    double rms = sqrt(sumSq / frames);
    double ac = sqrt(std::max(0.0, rms * rms - mean * mean));

    std::vector<double> absdev;
    absdev.reserve(frames);
    for (double v : ch0) absdev.push_back(fabs(v - mean));
    std::sort(absdev.begin(), absdev.end());
    auto pct = [&](double p) { return absdev[(size_t)(p * (absdev.size() - 1))]; };

    long long zc = 0;
    for (size_t i = 1; i < frames; ++i) {
        if ((ch0[i - 1] - mean) * (ch0[i] - mean) < 0) ++zc;
    }

    const int blocks = 20;
    size_t step = frames / blocks;
    double blockAc[20];
    for (int b = 0; b < blocks; ++b) {
        double s = 0, ss = 0;
        for (size_t i = b * step; i < (b + 1) * step; ++i) {
            double d = ch0[i] - mean;
            s += d; ss += d * d;
        }
        double m = s / step;
        blockAc[b] = sqrt(std::max(0.0, ss / step - m * m));
    }

    wprintf(L"frames=%zu rate=%u channels=%u\n", frames, rate, channels);
    wprintf(L"mean=%.6f rms=%.6f ac_rms=%.6f min=%.6f max=%.6f\n", mean, rms, ac, min, max);
    wprintf(L"absdev p50=%.6f p90=%.6f p99=%.6f p999=%.6f max=%.6f\n",
            pct(0.50), pct(0.90), pct(0.99), pct(0.999), absdev.back());
    wprintf(L"zero_crossings=%lld approx_hz=%.2f\n", zc, (double)zc / 2.0 / ((double)frames / rate));
    wprintf(L"block_ac_rms:");
    for (int b = 0; b < blocks; ++b) wprintf(L" %.6f", blockAc[b]);
    wprintf(L"\n");
    return 0;
}
