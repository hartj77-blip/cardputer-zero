// System monitor: CPU, memory, storage, network and versions.
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class SysmonApp : public App {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* left = lv_obj_create(root);
        style_panel(left);
        lv_obj_set_pos(left, 6, 6);
        lv_obj_set_size(left, 150, 138);
        lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(left, 2, 0);

        cpu_ = kv_row(left, "CPU", "");
        cpu_bar_ = thin_bar(left, 136, 6, kAccent);
        mem_ = kv_row(left, "Memory", "");
        mem_bar_ = thin_bar(left, 136, 6, 0x58a6ff);
        disk_ = kv_row(left, "Storage", "");
        disk_bar_ = thin_bar(left, 136, 6, kGood);
        temp_ = kv_row(left, "CPU temp", "");

        lv_obj_t* right = lv_obj_create(root);
        style_panel(right);
        lv_obj_set_pos(right, 162, 6);
        lv_obj_set_size(right, 152, 138);
        lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(right, 3, 0);
        model_ = kv_row(right, "Model", "");
        os_ = kv_row(right, "OS", "");
        host_ = kv_row(right, "Host", "");
        ip_ = kv_row(right, "IP", "");
        up_ = kv_row(right, "Uptime", "");
        kernel_ = label(right, "", &lv_font_montserrat_12, kMuted);
        lv_label_set_long_mode(kernel_, LV_LABEL_LONG_DOT);
        lv_obj_set_width(kernel_, 140);
        refresh();
    }

    void tick() override {
        if (++n_ % 10 == 0) refresh();
    }

private:
    void refresh() {
        SystemInfo s = hal().system();
        NetStatus n = hal().net();
        lv_label_set_text(cpu_, fmt("%d%%", s.cpu_load_percent).c_str());
        lv_bar_set_value(cpu_bar_, s.cpu_load_percent, LV_ANIM_ON);
        lv_label_set_text(mem_, fmt("%d / %d MB", s.mem_used_mb, s.mem_total_mb).c_str());
        lv_bar_set_value(mem_bar_, s.mem_total_mb ? s.mem_used_mb * 100 / s.mem_total_mb : 0, LV_ANIM_ON);
        lv_label_set_text(disk_, fmt("%.1f / %.1f GB", s.disk_used_gb10 / 10.0, s.disk_total_gb10 / 10.0).c_str());
        lv_bar_set_value(disk_bar_, s.disk_total_gb10 ? s.disk_used_gb10 * 100 / s.disk_total_gb10 : 0, LV_ANIM_ON);
        lv_label_set_text(temp_, fmt("%d C", s.cpu_temp_c).c_str());
        lv_label_set_text(model_, s.model == "CardputerZero" ? "Standard" : "Lite");
        lv_label_set_text(os_, s.os_version.c_str());
        lv_label_set_text(host_, s.hostname.c_str());
        lv_label_set_text(ip_, n.ip.empty() ? "offline" : n.ip.c_str());
        lv_label_set_text(up_, fmt("%ldh %02ldm", s.uptime_s / 3600, (s.uptime_s / 60) % 60).c_str());
        lv_label_set_text(kernel_, ("Linux " + s.kernel).c_str());
    }

    lv_obj_t *cpu_, *cpu_bar_, *mem_, *mem_bar_, *disk_, *disk_bar_, *temp_;
    lv_obj_t *model_, *os_, *host_, *ip_, *up_, *kernel_;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_sysmon_app() { return std::make_unique<SysmonApp>(); }

}  // namespace zs
