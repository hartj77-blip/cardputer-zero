// GPS track recording. It belongs to the shell, not the GPS app, so a track
// keeps recording with the app closed and with the screen off.
//
// Tracks are GPX files in ~/Documents/Tracks (Sync copies them to your PC).
// The file is complete after every point, so nothing is lost if the battery
// runs out mid-walk.
#pragma once

#include <string>

namespace zs {

struct TrackState {
    bool recording = false;
    std::string name;      // file name, e.g. track-20261120-142031.gpx
    double meters = 0;     // length so far
    long seconds = 0;      // time since it started
    int points = 0;
};

// Start a new track. Turns the expansion port's 5 V on for the GPS.
// False if the file can't be made.
bool track_start();
void track_stop();
const TrackState& track_state();
// From the shell's timer.
void track_tick();

}  // namespace zs
