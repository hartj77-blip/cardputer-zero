// QR code reading for the Camera app (the quirc library, pinned in
// CMakeLists.txt). Works on a greyscale picture, such as the luma plane of a
// camera frame.
#pragma once

#include <cstdint>
#include <string>

namespace zs {

struct QrResult {
    bool found = false;
    std::string text;
    int x[4] = {0, 0, 0, 0}, y[4] = {0, 0, 0, 0};   // the code's corners in the picture
};

QrResult qr_decode(const uint8_t* gray, int w, int h);

}  // namespace zs
