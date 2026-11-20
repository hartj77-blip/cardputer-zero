#include "ui/player.h"

#include <chrono>

#include "hal/hal.h"
#include "ui/app.h"

namespace zs {
namespace {

std::vector<QueueItem> g_queue;
int g_at = -1;
std::chrono::steady_clock::time_point g_started;
int g_quick_ends = 0;   // tracks in a row that ended almost at once

bool start(int i) {
    const QueueItem& q = g_queue[i];
    bool stream = q.path.rfind("http://", 0) == 0 || q.path.rfind("https://", 0) == 0;
    bool ok = stream ? hal().play_stream(q.path, q.auth, q.duration_s) : hal().play(q.path);
    g_at = ok ? i : -1;
    g_started = std::chrono::steady_clock::now();
    return ok;
}

}  // namespace

bool player_play(std::vector<QueueItem> queue, int index) {
    if (index < 0 || index >= int(queue.size())) return false;
    g_quick_ends = 0;
    if (hal().record_state().recording) hal().record_stop();
    g_queue = std::move(queue);
    return start(index);
}

bool player_step(int dir) {
    int n = int(g_queue.size());
    if (n == 0) return false;
    return start(((g_at < 0 ? (dir > 0 ? -1 : 0) : g_at) + dir + n) % n);
}

void player_stop() {
    g_queue.clear();
    g_at = -1;
    hal().stop();
}

const QueueItem* player_current() {
    if (g_at < 0 || g_at >= int(g_queue.size())) return nullptr;
    PlaybackState pb = hal().playback();
    return pb.path == g_queue[g_at].path ? &g_queue[g_at] : nullptr;
}

namespace {
void on_media_key(MediaKey k) {
    // Play pressed in the car with nothing queued: queue everything in ~/Music.
    if (g_queue.empty() && k != MediaKey::Pause && k != MediaKey::Stop)
        for (auto& t : hal().music_library()) g_queue.push_back({t.path, t.title, t.artist, t.duration_s, ""});
    PlaybackState pb = hal().playback();
    bool loaded = !pb.path.empty();
    switch (k) {
        case MediaKey::PlayPause:
            if (loaded) hal().set_paused(!pb.paused);
            else player_step(+1);          // nothing loaded: start the queue
            break;
        case MediaKey::Play:
            if (loaded) hal().set_paused(false);
            else player_step(+1);
            break;
        case MediaKey::Pause:
            if (loaded) hal().set_paused(true);
            break;
        case MediaKey::Stop:
            if (loaded) hal().set_paused(true);   // keep the queue: Play picks it up again
            break;
        case MediaKey::Next: player_step(+1); break;
        case MediaKey::Previous: player_step(-1); break;
        case MediaKey::None: break;
    }
}
}  // namespace

void player_tick() {
    for (int i = 0; i < 4; ++i) {
        MediaKey k = hal().media_key();
        if (k == MediaKey::None) break;
        on_media_key(k);
    }
    if (g_at < 0) return;
    PlaybackState pb = hal().playback();
    if (!pb.path.empty()) return;          // still playing or paused
    if (!pb.finished) {                    // stopped by something else
        g_at = -1;
        return;
    }
    // A stream that ends within two seconds didn't play (server gone, Wi-Fi
    // down). Don't race through the whole queue failing each one.
    bool stream = !g_queue[g_at].auth.empty();
    bool quick = std::chrono::steady_clock::now() - g_started < std::chrono::seconds(2);
    g_quick_ends = stream && quick ? g_quick_ends + 1 : 0;
    if (g_quick_ends >= 2) {
        g_at = -1;
        shell_toast("Can't play from the server");
        return;
    }
    // The track ended: play the next one; the queue ends after the last.
    int next = g_at + 1;
    g_at = -1;
    if (next < int(g_queue.size()) && !start(next)) shell_toast("Can't play " + g_queue[next].title);
}

}  // namespace zs
