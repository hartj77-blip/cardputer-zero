#include "hal/qr.h"

#include <cstring>

extern "C" {
#include "quirc.h"
}

namespace zs {

QrResult qr_decode(const uint8_t* gray, int w, int h) {
    QrResult r;
    static quirc* q = nullptr;
    static int qw = 0, qh = 0;
    if (!q) q = quirc_new();
    if (!q || !gray || w <= 0 || h <= 0) return r;
    if ((qw != w || qh != h) && quirc_resize(q, w, h) < 0) return r;
    qw = w, qh = h;
    uint8_t* buf = quirc_begin(q, nullptr, nullptr);
    std::memcpy(buf, gray, size_t(w) * size_t(h));
    quirc_end(q);
    // Both are too big for the stack of a small device.
    static quirc_code code;
    static quirc_data data;
    int n = quirc_count(q);
    for (int i = 0; i < n; ++i) {
        quirc_extract(q, i, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err == QUIRC_ERROR_DATA_ECC) {   // perhaps seen in a mirror
            quirc_flip(&code);
            err = quirc_decode(&code, &data);
        }
        if (err != QUIRC_SUCCESS) continue;
        r.found = true;
        r.text.assign(reinterpret_cast<const char*>(data.payload), size_t(data.payload_len));
        for (int c = 0; c < 4; ++c) {
            r.x[c] = code.corners[c].x;
            r.y[c] = code.corners[c].y;
        }
        break;
    }
    return r;
}

}  // namespace zs
