// Camera: two things behind one icon (docs/CAMERA.md).
//
// Photos: M5Stack's CameraApp (MIT) does the real work: viewfinder, photos,
// zoom and a gallery. ZeroShell starts it and steps aside, as it does for
// the Terminal, and takes the screen back when it exits (hold 4 or Esc).
// The image installs it as /usr/bin/camera_app.
//
// QR codes: ZeroShell's own reader, on this page under "Scan a QR code" (or
// press Q). A code in view is read at once and the picture freezes on it:
//   a Wi-Fi code (a phone's hotspot, a router's label)   Enter joins it
//   a link                                               Enter opens it on the monitor (Dock mode)
//   a Syncthing device ID                                Enter adds it to Sync
//   anything                                             S saves the text to ~/Notes/scans.txt
//   Esc scans again, then leaves the reader.
// The reader's camera is off whenever the screen is, and when you leave.
#include <sys/stat.h>

#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

#include "apps/common.h"
#include "hal/qr.h"
#include "ui/power.h"

namespace zs {

namespace {
bool g_scan_first = false;
}
// Another app (Tether) asks for the reader, not the viewfinder.
void camera_open_scanner() {
    g_scan_first = true;
    shell_open_app("camera");
}

namespace {
using namespace theme;

constexpr int kViewW = 200, kViewH = 150;   // the picture; the rest is the side panel
constexpr int kSideX = kViewW + 8, kSideW = kScreenW - kSideX - 6;

// "WIFI:T:WPA;S:name;P:password;;" with \ before ; , : " and \ in values.
struct WifiCode {
    std::string ssid, password, type;
};

bool parse_wifi(const std::string& text, WifiCode& out) {
    if (text.size() < 7 || (text.compare(0, 5, "WIFI:") != 0 && text.compare(0, 5, "wifi:") != 0)) return false;
    size_t i = 5;
    while (i < text.size()) {
        size_t colon = text.find(':', i);
        if (colon == std::string::npos) break;
        std::string key = text.substr(i, colon - i), value;
        for (i = colon + 1; i < text.size() && text[i] != ';'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            value += text[i];
        }
        ++i;   // the ;
        if (key == "S") out.ssid = value;
        else if (key == "P") out.password = value;
        else if (key == "T") out.type = value;
    }
    return !out.ssid.empty();
}

bool is_link(const std::string& t) {
    return (t.rfind("http://", 0) == 0 || t.rfind("https://", 0) == 0) && t.find_first_of(" \n\t") == std::string::npos;
}

// Syncthing device ID: eight groups of seven letters and digits.
bool is_sync_id(const std::string& t) {
    if (t.size() != 63) return false;
    for (size_t i = 0; i < t.size(); ++i) {
        char ch = t[i];
        if (i % 8 == 7 ? ch != '-' : !((ch >= 'A' && ch <= 'Z') || (ch >= '2' && ch <= '7'))) return false;
    }
    return true;
}

// Text for the screen: no control characters, and not too long.
std::string printable(const std::string& t, size_t max) {
    std::string r;
    for (unsigned char ch : t) {
        if (r.size() >= max) return r + "...";
        r += ch < 32 ? ' ' : char(ch);
    }
    return r;
}

class CameraApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        CameraStatus st = hal().camera();

        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 308, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 4, 0);

        const char* head = !st.installed ? "Camera app missing"
                           : st.present  ? "Camera ready"
                                         : "No camera found";
        label(panel, (std::string(ZS_ICON_CAMERA "  ") + head).c_str(), &zs_font_16,
              st.installed && st.present ? kGood : kWarn);
        const char* help = !st.installed
                               ? "This image was built without the camera\napp. See docs/CAMERA.md."
                           : st.present
                               ? "Enter takes a photo. H shows every key\n(zoom, pan, gallery). Hold 4 for a second\n"
                                 "to come back here."
                               : "The Lite model has no camera. On the\nfull model, check the camera cable.\n"
                                 "A USB camera also works: open, then U.";
        lv_obj_t* h = label(panel, help, &zs_font_12, kMuted);
        lv_obj_set_width(h, 290);

        lv_obj_t* row = lv_obj_create(panel);
        style_plain(row);
        lv_obj_set_size(row, 296, 28);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        auto button = [&](const char* text, int x, int w, lv_event_cb_t cb) {
            lv_obj_t* b = lv_button_create(row);
            lv_obj_set_size(b, w, 26);
            lv_obj_set_pos(b, x, 0);
            lv_obj_set_style_bg_color(b, c(kSurface2), 0);
            lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(b, c(kOnAccent), LV_STATE_FOCUSED);
            lv_obj_t* l = lv_label_create(b);
            lv_label_set_text(l, text);
            lv_obj_center(l);
            lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
            shell_add_focus(b);
        };
        button(LV_SYMBOL_PLAY "  Open camera", 0, 138, [](lv_event_t* e) {
            static_cast<CameraApp*>(lv_event_get_user_data(e))->open();
        });
        // Deferred: the reader covers (and outlives) the button being clicked.
        button(ZS_ICON_SEARCH "  Scan a QR code (Q)", 144, 152, [](lv_event_t* e) {
            lv_async_call([](void* p) { static_cast<CameraApp*>(p)->start_scan(); }, lv_event_get_user_data(e));
        });

        if (g_scan_first) {
            g_scan_first = false;
            start_scan();
        } else if (st.installed && st.present) {
            // Picking Camera on the home screen goes straight to the viewfinder.
            open();
        }
    }

    ~CameraApp() override {
        lv_async_call_cancel([](void* p) { static_cast<CameraApp*>(p)->start_scan(); }, this);
        if (scan_) hal().scan_stop();
    }

    void tick() override {
        if (!scan_ || !result_.empty()) return;   // not reading, or frozen on a code
        if (power().screen_off()) {               // nobody is looking: don't run the camera
            if (hal().scan_state() != CamState::Off) hal().scan_stop();
            slept_ = true;
            return;
        }
        if (slept_) {
            slept_ = false;
            hal().scan_start();
        }
        CameraFrame f;
        bool got = hal().scan_frame(f);
        CamState st = hal().scan_state();
        if (st != state_) {
            state_ = st;
            lv_label_set_text(wait_, st == CamState::NoCamera
                                         ? "No camera found.\nThe CardputerZero Lite doesn't have one."
                                         : st == CamState::Starting ? "Starting the camera..." : "");
        }
        if (!got || f.seq == seq_) return;
        seq_ = f.seq;
        draw(f);
        // Reading a code costs more than drawing: look at every third frame.
        if (++frames_ % 3 == 0) {
            QrResult r = qr_decode(f.y, f.w, f.h);
            if (r.found && !r.text.empty()) found(r, f);
        }
        lv_obj_invalidate(img_);
    }

    bool on_key(uint32_t key) override {
        if (!scan_) {
            if (fake_ || (key != 'q' && key != 'Q')) return false;
            start_scan();
            return true;
        }
        if (key == LV_KEY_ESC) return false;   // on_back()
        if (!result_.empty()) {
            if (key == 's' || key == 'S') save_text();
            else if (key == LV_KEY_ENTER) act();
        }
        return true;   // the page's buttons are underneath: keep keys from them
    }

    bool on_back() override {
        if (fake_) {
            lv_obj_delete(fake_);
            fake_ = nullptr;
            return true;
        }
        if (scan_ && !result_.empty()) {   // back to the live picture
            result_.clear();
            hal().scan_start();
            show_live();
            return true;
        }
        if (scan_) {
            hal().scan_stop();
            lv_obj_delete(scan_);
            scan_ = nullptr;
            return true;
        }
        return false;
    }

private:
    enum class Kind { Text, Wifi, Link, SyncId };

    void open() {
        if (shell_open_camera()) return;
        if (hal().camera().simulated) show_fake();
        else shell_toast("The camera app didn't start");
    }

    // Simulator: roughly what the viewfinder looks like.
    void show_fake() {
        if (fake_) return;
        fake_ = lv_obj_create(root_);
        style_plain(fake_);
        lv_obj_set_size(fake_, kScreenW, kContentH);
        lv_obj_set_style_bg_color(fake_, c(0x101418), 0);
        lv_obj_set_style_bg_grad_color(fake_, c(0x2a3b4d), 0);
        lv_obj_set_style_bg_grad_dir(fake_, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(fake_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(fake_, LV_OBJ_FLAG_SCROLLABLE);
        // Focus brackets in the middle.
        lv_obj_t* frame = lv_obj_create(fake_);
        style_plain(frame);
        lv_obj_set_size(frame, 90, 64);
        lv_obj_center(frame);
        lv_obj_set_style_border_color(frame, c(0xffffff), 0);
        lv_obj_set_style_border_width(frame, 1, 0);
        lv_obj_set_style_border_opa(frame, LV_OPA_60, 0);
        lv_obj_set_style_radius(frame, 4, 0);
        lv_obj_t* t = label(fake_, "[simulator] On the Cardputer this is the live\nviewfinder. Esc closes this preview.",
                            &zs_font_12, 0xffffff);
        lv_obj_align(t, LV_ALIGN_BOTTOM_LEFT, 8, -6);
        lv_obj_t* z = label(fake_, "1.0x", &zs_font_12, 0xffffff);
        lv_obj_align(z, LV_ALIGN_TOP_RIGHT, -8, 6);
    }

    // The QR reader: a live picture on the left, what it found on the right.
    void start_scan() {
        if (scan_) return;
        if (fake_) {
            lv_obj_delete(fake_);
            fake_ = nullptr;
        }
        scan_ = lv_obj_create(root_);
        style_plain(scan_);
        lv_obj_set_size(scan_, kScreenW, kContentH);
        lv_obj_set_style_bg_color(scan_, c(kBg), 0);
        lv_obj_set_style_bg_opa(scan_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scan_, LV_OBJ_FLAG_SCROLLABLE);

        pix_.assign(size_t(kViewW) * kViewH, 0);
        dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        dsc_.header.w = kViewW;
        dsc_.header.h = kViewH;
        dsc_.header.stride = kViewW * 2;
        dsc_.data = reinterpret_cast<const uint8_t*>(pix_.data());
        dsc_.data_size = uint32_t(pix_.size() * 2);
        img_ = lv_image_create(scan_);
        lv_image_set_src(img_, &dsc_);
        lv_obj_set_pos(img_, 0, 0);

        // Shown over the picture until the first frame, or with no camera.
        wait_ = label(scan_, "", &zs_font_12, kMuted);
        lv_obj_set_width(wait_, kViewW - 16);
        lv_obj_set_style_text_align(wait_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(wait_, LV_ALIGN_LEFT_MID, 8, 0);

        title_ = label(scan_, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, kSideX, 4);
        body_ = label(scan_, "", &zs_font_12, kText);
        lv_obj_set_pos(body_, kSideX, 26);
        lv_obj_set_size(body_, kSideW, 72);
        lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
        hints_ = label(scan_, "", &zs_font_12, kMuted);
        lv_obj_set_width(hints_, kSideW);
        lv_obj_align(hints_, LV_ALIGN_BOTTOM_LEFT, kSideX, -4);

        result_.clear();
        seq_ = 0;
        slept_ = false;
        hal().scan_start();
        show_live();
    }

    void show_live() {
        lv_label_set_text(title_, "Scan");
        lv_label_set_text(body_, "Point it at a QR code.");
        lv_label_set_text(hints_, "Esc  back");
        state_ = CamState::Off;   // so tick() puts the right message up
    }

    // Scale the frame to the view (nearest pixel) and convert YUV to RGB565.
    void draw(const CameraFrame& f) {
        for (int y = 0; y < kViewH; ++y) {
            int sy = y * f.h / kViewH;
            const uint8_t* yr = f.y + sy * f.w;
            const uint8_t* ur = f.u + (sy / 2) * (f.w / 2);
            const uint8_t* vr = f.v + (sy / 2) * (f.w / 2);
            uint16_t* out = &pix_[size_t(y) * kViewW];
            for (int x = 0; x < kViewW; ++x) {
                int sx = x * f.w / kViewW;
                int c298 = 298 * (int(yr[sx]) - 16) + 128;
                int d = int(ur[sx / 2]) - 128, e = int(vr[sx / 2]) - 128;
                int r = (c298 + 409 * e) >> 8, g = (c298 - 100 * d - 208 * e) >> 8, b = (c298 + 516 * d) >> 8;
                r = r < 0 ? 0 : r > 255 ? 255 : r;
                g = g < 0 ? 0 : g > 255 ? 255 : g;
                b = b < 0 ? 0 : b > 255 ? 255 : b;
                out[x] = uint16_t(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
            }
        }
    }

    // A two-pixel line on the picture, for the outline of a code.
    void line(int x0, int y0, int x1, int y1, uint16_t color) {
        int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), steps = dx > dy ? dx : dy;
        for (int i = 0; i <= steps; ++i) {
            int x = steps ? x0 + (x1 - x0) * i / steps : x0, y = steps ? y0 + (y1 - y0) * i / steps : y0;
            for (int oy = 0; oy < 2; ++oy)
                for (int ox = 0; ox < 2; ++ox)
                    if (x + ox >= 0 && x + ox < kViewW && y + oy >= 0 && y + oy < kViewH)
                        pix_[size_t(y + oy) * kViewW + x + ox] = color;
        }
    }

    void found(const QrResult& r, const CameraFrame& f) {
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            line(r.x[i] * kViewW / f.w, r.y[i] * kViewH / f.h, r.x[j] * kViewW / f.w, r.y[j] * kViewH / f.h, 0x07e0);
        }
        result_ = r.text;
        hal().scan_stop();   // the frozen picture stays; no need to keep the sensor on
        hal().beep(1760, 50);

        WifiCode w;
        std::string body, enter;
        if (parse_wifi(result_, w)) {
            kind_ = Kind::Wifi;
            lv_label_set_text(title_, "Wi-Fi network");
            body = printable(w.ssid, 40) + "\n" + (w.password.empty() ? "Open, no password" : "Password included");
            enter = "Enter  join\n";
        } else if (is_link(result_)) {
            kind_ = Kind::Link;
            lv_label_set_text(title_, "Link");
            body = printable(result_, 90);
            if (hal().display().dock_running) enter = "Enter  on monitor\n";
        } else if (is_sync_id(result_)) {
            kind_ = Kind::SyncId;
            lv_label_set_text(title_, "Sync device");
            body = result_.substr(0, 15) + "...";
            enter = "Enter  add to Sync\n";
        } else {
            kind_ = Kind::Text;
            lv_label_set_text(title_, "QR code");
            body = printable(result_, 90);
        }
        lv_label_set_text(body_, body.c_str());
        lv_label_set_text(hints_, (enter + "S  save   Esc  again").c_str());
    }

    void act() {
        WifiCode w;
        switch (kind_) {
        case Kind::Wifi:
            parse_wifi(result_, w);
            shell_toast("Connecting to " + w.ssid + "...");
            lv_refr_now(nullptr);   // connecting takes a few seconds: show the notice first
            shell_toast(hal().wifi_connect(w.ssid, w.password) ? "Connected to " + w.ssid : "Could not connect");
            break;
        case Kind::Link:
            if (hal().dock_open("browser", result_)) shell_toast("Opened on the monitor");
            else shell_toast("Links open on a monitor (Dock). S saves it.");
            break;
        case Kind::SyncId:
            if (!hal().sync_running()) shell_toast("Turn on Sync first");
            else shell_toast(hal().sync_add_device(result_, "") ? "Added; accept it on the other device too"
                                                                : "Couldn't add that device");
            break;
        case Kind::Text: save_text(); break;
        }
    }

    void save_text() {
        std::string dir = home_dir() + "/Notes";
        mkdir(dir.c_str(), 0755);
        std::ofstream f(dir + "/scans.txt", std::ios::app);
        char when[32];
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_r(&now, &lt);
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &lt);
        if (f) f << when << "  " << result_ << "\n";
        shell_toast(f ? "Saved to Notes/scans.txt" : "Couldn't save to ~/Notes");
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* fake_ = nullptr;
    lv_obj_t* scan_ = nullptr;   // the reader's page, over the rest
    std::vector<uint16_t> pix_;
    lv_image_dsc_t dsc_{};
    lv_obj_t *img_ = nullptr, *wait_ = nullptr, *title_ = nullptr, *body_ = nullptr, *hints_ = nullptr;
    CamState state_ = CamState::Off;
    uint32_t seq_ = 0;
    int frames_ = 0;
    bool slept_ = false;
    std::string result_;
    Kind kind_ = Kind::Text;
};
}  // namespace

std::unique_ptr<App> make_camera_app() { return std::make_unique<CameraApp>(); }

}  // namespace zs
