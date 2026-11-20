// Camera: M5Stack's CameraApp (MIT) does the real work: viewfinder, photos,
// zoom and a gallery. ZeroShell starts it and steps aside, as it does for
// the Terminal, and takes the screen back when it exits (hold 4 or Esc).
// The image installs it as /usr/bin/camera_app; see docs/CAMERA.md.
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

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
                                 "to come back here.\nPhotos go to Pictures/DCIM/Camera."
                               : "The Lite model has no camera. On the\nfull model, check the camera cable.\n"
                                 "A USB camera also works: open, then U.";
        lv_obj_t* h = label(panel, help, &zs_font_12, kMuted);
        lv_obj_set_width(h, 290);

        lv_obj_t* b = lv_button_create(panel);
        lv_obj_set_size(b, 160, 26);
        lv_obj_set_style_bg_color(b, c(kSurface2), 0);
        lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(b, c(kOnAccent), LV_STATE_FOCUSED);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, LV_SYMBOL_PLAY "  Open camera");
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            static_cast<CameraApp*>(lv_event_get_user_data(e))->open();
        }, LV_EVENT_CLICKED, this);
        shell_add_focus(b);

        // Picking Camera on the home screen goes straight to the viewfinder.
        if (st.installed && st.present) open();
    }

    bool on_back() override {
        if (fake_) {
            lv_obj_delete(fake_);
            fake_ = nullptr;
            return true;
        }
        return false;
    }

private:
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

    lv_obj_t* root_ = nullptr;
    lv_obj_t* fake_ = nullptr;
};
}  // namespace

std::unique_ptr<App> make_camera_app() { return std::make_unique<CameraApp>(); }

}  // namespace zs
