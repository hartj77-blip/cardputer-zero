// System monitor, two pages (Left/Right to switch):
//   Overview  CPU (busy %, clock, governor), memory, storage, temperature, versions
//   Memory    where the RAM goes: available, cache, zram, SD swap, the shell
//             itself, and the five biggest processes
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class SysmonApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        tab_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_set_pos(tab_, 8, 2);
        sim_ = label(root, "", &zs_font_12, kWarn);
        lv_obj_align(sim_, LV_ALIGN_TOP_RIGHT, -8, 2);
        show(0);
    }

    void tick() override {
        if (++n_ % 10 == 0) refresh();
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == 9) {
            show(1 - page_);
            return true;
        }
        return false;
    }

private:
    lv_obj_t* panel(int x, int w) {
        lv_obj_t* p = lv_obj_create(root_);
        style_panel(p);
        lv_obj_set_pos(p, x, 20);
        lv_obj_set_size(p, w, kContentH - 24);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(p, 2, 0);
        lv_obj_set_style_pad_ver(p, 5, 0);
        return p;
    }

    void show(int page) {
        page_ = page;
        if (left_) lv_obj_delete(left_);
        if (right_) lv_obj_delete(right_);
        lv_label_set_text(tab_, page == 0 ? "Overview   " LV_SYMBOL_RIGHT " Memory"
                                          : LV_SYMBOL_LEFT " Overview   Memory");
        if (page == 0) build_overview();
        else build_memory();
        refresh();
    }

    void build_overview() {
        left_ = panel(6, 150);
        cpu_ = kv_row(left_, "CPU", "");
        cpu_bar_ = thin_bar(left_, 136, 5, kAccent);
        clock_ = kv_row(left_, "Clock", "");
        mem_ = kv_row(left_, "Memory", "");
        mem_bar_ = thin_bar(left_, 136, 5, kInfo);
        disk_ = kv_row(left_, "Storage", "");
        disk_bar_ = thin_bar(left_, 136, 5, kGood);
        temp_ = kv_row(left_, "CPU temp", "");

        right_ = panel(162, 152);
        model_ = kv_row(right_, "Model", "");
        os_ = kv_row(right_, "OS", "");
        host_ = kv_row(right_, "Host", "");
        ip_ = kv_row(right_, "IP", "");
        up_ = kv_row(right_, "Uptime", "");
        kernel_ = label(right_, "", &zs_font_12, kMuted);
        lv_label_set_long_mode(kernel_, LV_LABEL_LONG_DOT);
        lv_obj_set_width(kernel_, 140);
    }

    void build_memory() {
        left_ = panel(6, 150);
        ram_ = kv_row(left_, "RAM used", "");
        ram_bar_ = thin_bar(left_, 136, 5, kInfo);
        avail_ = kv_row(left_, "Available", "");
        cache_ = kv_row(left_, "File cache", "");
        zram_ = kv_row(left_, "zram", "");
        swap_ = kv_row(left_, "SD swap", "");
        self_ = kv_row(left_, "This UI", "");

        right_ = panel(162, 152);
        label(right_, "Biggest processes", &zs_font_12, kMuted);
        for (auto& r : procs_) r = kv_row(right_, "", "");
    }

    void refresh() {
        SystemInfo s = hal().system();
        lv_label_set_text(sim_, s.simulated ? "simulated" : "");
        if (page_ == 0) {
            NetStatus n = hal().net();
            lv_label_set_text(cpu_, fmt("%d%%", s.cpu_load_percent).c_str());
            lv_bar_set_value(cpu_bar_, s.cpu_load_percent, LV_ANIM_ON);
            lv_label_set_text(clock_, s.cpu_mhz ? fmt("%d MHz", s.cpu_mhz).c_str() : "--");
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
        } else {
            lv_label_set_text(ram_, fmt("%d / %d MB", s.mem_used_mb, s.mem_total_mb).c_str());
            lv_bar_set_value(ram_bar_, s.mem_total_mb ? s.mem_used_mb * 100 / s.mem_total_mb : 0, LV_ANIM_ON);
            lv_label_set_text(avail_, fmt("%d MB", s.mem_avail_mb).c_str());
            lv_label_set_text(cache_, fmt("%d MB", s.mem_cache_mb).c_str());
            // "zram 2.8x   18 in 7 MB": 18 MB of data held in 7 MB of RAM.
            lv_obj_t* zkey = lv_obj_get_child(lv_obj_get_parent(zram_), 0);
            if (s.zram_capacity_mb > 0 && s.zram_ram_mb > 0.1f) {
                lv_label_set_text(zkey, fmt("zram %.1fx", s.zram_stored_mb / s.zram_ram_mb).c_str());
                lv_label_set_text(zram_, fmt("%.0f in %.0f MB", s.zram_stored_mb, s.zram_ram_mb).c_str());
            } else {
                lv_label_set_text(zkey, "zram");
                lv_label_set_text(zram_, s.zram_capacity_mb > 0 ? "empty" : "off");
            }
            lv_label_set_text(swap_, s.disk_swap_total_mb
                                         ? fmt("%d / %d MB", s.disk_swap_used_mb, s.disk_swap_total_mb).c_str()
                                         : "none");
            lv_label_set_text(self_, fmt("%.1f MB", s.self_rss_kb / 1024.0).c_str());
            for (size_t i = 0; i < 5; ++i) {
                lv_obj_t* row = lv_obj_get_parent(procs_[i]);
                lv_obj_t* name = lv_obj_get_child(row, 0);
                if (i < s.top_procs.size()) {
                    std::string n = s.top_procs[i].name;
                    if (n.size() > 11) n = n.substr(0, 10) + "~";
                    lv_label_set_text(name, n.c_str());
                    lv_label_set_text(procs_[i], fmt("%.1f MB", s.top_procs[i].rss_kb / 1024.0).c_str());
                } else {
                    lv_label_set_text(name, "");
                    lv_label_set_text(procs_[i], "");
                }
            }
        }
    }

    lv_obj_t *root_ = nullptr, *tab_ = nullptr, *sim_ = nullptr, *left_ = nullptr, *right_ = nullptr;
    // overview
    lv_obj_t *cpu_, *cpu_bar_, *clock_, *mem_, *mem_bar_, *disk_, *disk_bar_, *temp_;
    lv_obj_t *model_, *os_, *host_, *ip_, *up_, *kernel_;
    // memory
    lv_obj_t *ram_, *ram_bar_, *avail_, *cache_, *zram_, *swap_, *self_;
    lv_obj_t* procs_[5] = {};
    int page_ = 0;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_sysmon_app() { return std::make_unique<SysmonApp>(); }

}  // namespace zs
