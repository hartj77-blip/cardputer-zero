#include "ui/tracker.h"

#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "hal/hal.h"
#include "lvgl.h"

namespace zs {
namespace {

const char kFooter[] = "</trkseg></trk></gpx>\n";
constexpr double kMinStepM = 5;     // closer than this to the last point: standing still
constexpr double kMaxErrorM = 50;   // a worse fix than this isn't worth drawing
constexpr int kMinGapS = 2;

TrackState g_state;
FILE* g_file = nullptr;
uint32_t g_started = 0, g_last_tick = 0, g_last_point = 0;   // lv_tick_get() times
double g_lat = 0, g_lon = 0;

// Metres between two points (haversine).
double distance_m(double lat1, double lon1, double lat2, double lon2) {
    const double rad = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * rad, dlon = (lon2 - lon1) * rad;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * rad) * std::cos(lat2 * rad) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 6371000.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

void add_point(const GpsFix& fix, uint32_t now_ms) {
    std::time_t now = std::time(nullptr);
    char when[32];
    if (fix.time.size() >= 19) {
        std::snprintf(when, sizeof when, "%.19sZ", fix.time.c_str());   // gpsd's UTC, without fractions
    } else {
        std::tm ut{};
        gmtime_r(&now, &ut);
        std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &ut);
    }
    // Write over the footer, then put it back: the file is always whole.
    std::fseek(g_file, -long(sizeof kFooter - 1), SEEK_END);
    std::fprintf(g_file, "<trkpt lat=\"%.6f\" lon=\"%.6f\">", fix.lat, fix.lon);
    if (fix.mode >= 3) std::fprintf(g_file, "<ele>%.1f</ele>", fix.alt_m);
    std::fprintf(g_file, "<time>%s</time></trkpt>\n%s", when, kFooter);
    std::fflush(g_file);
    if (g_state.points) g_state.meters += distance_m(g_lat, g_lon, fix.lat, fix.lon);
    g_lat = fix.lat;
    g_lon = fix.lon;
    g_last_point = now_ms;
    ++g_state.points;
}

}  // namespace

bool track_start() {
    if (g_state.recording) return true;
    const char* h = std::getenv("HOME");
    std::string dir = std::string(h ? h : "/tmp") + "/Documents";
    mkdir(dir.c_str(), 0755);
    dir += "/Tracks";
    mkdir(dir.c_str(), 0755);
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    char name[48], title[32];
    std::strftime(name, sizeof name, "track-%Y%m%d-%H%M%S.gpx", &lt);
    std::strftime(title, sizeof title, "%Y-%m-%d %H:%M", &lt);
    g_file = std::fopen((dir + "/" + name).c_str(), "w+");
    if (!g_file) return false;
    std::fprintf(g_file,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<gpx version=\"1.1\" creator=\"ZeroOS\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                 "<trk><name>%s</name><trkseg>\n%s",
                 title, kFooter);
    std::fflush(g_file);
    g_state = TrackState{};
    g_state.recording = true;
    g_state.name = name;
    g_started = g_last_tick = lv_tick_get();
    g_last_point = 0;
    hal().set_ext_power(true);
    return true;
}

void track_stop() {
    if (!g_state.recording) return;
    g_state.recording = false;
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

const TrackState& track_state() { return g_state; }

void track_tick() {
    if (!g_state.recording) return;
    uint32_t now = lv_tick_get();
    if (now - g_last_tick < 1000) return;   // once a second is plenty
    g_last_tick = now;
    g_state.seconds = long((now - g_started) / 1000);
    GpsFix fix = hal().gps();         // asking also keeps the receiver on
    if (fix.mode < 2 || (fix.error_m > 0 && fix.error_m > kMaxErrorM)) return;
    if (g_state.points &&
        (now - g_last_point < kMinGapS * 1000u || distance_m(g_lat, g_lon, fix.lat, fix.lon) < kMinStepM))
        return;
    add_point(fix, now);
}

}  // namespace zs
