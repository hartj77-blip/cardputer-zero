// The play queue. It belongs to the shell, not the Media app, so music moves
// on to the next track with the app closed and with the screen off.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs {

struct QueueItem {
    std::string path;       // a file, or an http(s) address for a Jellyfin track
    std::string title;
    std::string artist;
    int duration_s = -1;
    std::string auth;       // Authorization header for a stream, else empty
};

// Replace the queue and start at index. False if that track won't start.
bool player_play(std::vector<QueueItem> queue, int index);
// Next (+1) or previous (-1) track, wrapping round. False with no queue.
bool player_step(int dir);
// Stop and forget the queue.
void player_stop();
// What is playing or paused, or nullptr.
const QueueItem* player_current();
// From the shell's timer: moves on when a track ends.
void player_tick();

}  // namespace zs
