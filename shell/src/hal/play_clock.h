// Tracks elapsed play time across pauses. Shared by the mock and Linux audio.
#pragma once

#include <chrono>

namespace zs {

// Time source in ms. Real time by default; the headless build swaps in its
// virtual clock so scripted screenshots show realistic play positions.
inline long (*&play_clock_source())() {
    static long (*src)() = nullptr;
    return src;
}

class PlayClock {
public:
    void start() {
        t0_ = now();
        paused_total_ = 0;
        paused_at_ = -1;
    }
    void pause() {
        if (paused_at_ < 0) paused_at_ = now();
    }
    void resume() {
        if (paused_at_ >= 0) {
            paused_total_ += now() - paused_at_;
            paused_at_ = -1;
        }
    }
    long ms() const {
        long end = paused_at_ >= 0 ? paused_at_ : now();
        return end - t0_ - paused_total_;
    }

private:
    static long now() {
        if (auto f = play_clock_source()) return f();
        using namespace std::chrono;
        return long(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }
    long t0_ = 0, paused_total_ = 0, paused_at_ = -1;
};

}  // namespace zs
