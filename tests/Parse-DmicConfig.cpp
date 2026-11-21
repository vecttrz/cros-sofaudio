#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static uint32_t rd32(const std::vector<unsigned char> &b, size_t o) {
    uint32_t v; memcpy(&v, &b[o], 4); return v;
}
static uint16_t rd16(const std::vector<unsigned char> &b, size_t o) {
    uint16_t v; memcpy(&v, &b[o], 2); return v;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { wprintf(L"Usage: Parse-DmicConfig <file.tplg>\n"); return 2; }
    FILE *f = nullptr;
    if (_wfopen_s(&f, argv[1], L"rb") != 0 || !f) return 1;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> b(size);
    if (fread(b.data(), 1, size, f) != (size_t)size) { fclose(f); return 1; }
    fclose(f);

    const uint32_t CMD_DAI_CONFIG = (8u << 28) | (1u << 16); // 0x80010000
    int found = 0;
    for (size_t off = 0; off + 8 <= b.size(); off += 4) {
        if (rd32(b, off + 4) != CMD_DAI_CONFIG) continue;
        uint32_t msgSize = rd32(b, off);
        if (msgSize < 120 || msgSize > 400 || off + msgSize > b.size()) continue;
        uint32_t type = rd32(b, off + 8);
        uint32_t dai = rd32(b, off + 12);
        uint16_t format = rd16(b, off + 16);
        uint8_t flags = b[off + 19];
        if (type > 10) continue;
        ++found;
        wprintf(L"\n[#%d] offset=0x%llx size=%u type=%u dai=%u format=0x%04x flags=0x%02x\n",
               found, (unsigned long long)off, msgSize, type, dai, format, flags);
        if (type == 2 && msgSize >= 220) {
            size_t d = off + 52;
            wprintf(L"  dmic ver=%u pdmclk=%u..%u fifo_fs=%u fifo_bits=%u duty=%u..%u pdms=%u wake=%u min_on=%u ramp=%u\n",
                   rd32(b,d+4), rd32(b,d+8), rd32(b,d+12), rd32(b,d+16), rd16(b,d+24),
                   rd16(b,d+28), rd16(b,d+30), rd32(b,d+32), rd32(b,d+36), rd32(b,d+40), rd32(b,d+44));
            for (int i = 0; i < 4; ++i) {
                size_t p = d + 68 + i * 24;
                wprintf(L"  pdm[%d] id=%u micA=%u micB=%u polA=%u polB=%u edge=%u skew=%u\n",
                       i, rd16(b,p+4), rd16(b,p+6), rd16(b,p+8), rd16(b,p+10), rd16(b,p+12), rd16(b,p+14), rd16(b,p+16));
            }
        }
    }
    wprintf(L"\nTotal DAI_CONFIG messages: %d\n", found);
    return 0;
}
