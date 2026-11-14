// Remote mode helpers shared by the Remote, Terminal and Files apps.
//
// Tailscale runs "when needed" by default: it starts when you open Remote,
// and stops after 15 minutes with no Remote app, terminal or Dock session.
// tailscale.mode = needed | always | off (Settings).
#pragma once

#include <cstdint>
#include <string>

namespace zs {

void remote_tick(uint32_t now_ms);   // from the shell's timer
void remote_note_use();               // Tailscale is wanted right now

std::string tailscale_mode();
void set_tailscale_mode(const std::string& m);

// A file waiting to be sent with Taildrop (set by Files, used by Remote).
void remote_set_send(const std::string& path);
std::string remote_take_send();
const std::string& remote_pending_send();

// "ssh desktop" or "ssh john@desktop" with the saved user name.
std::string ssh_command(const std::string& host);
std::string ssh_user();
void set_ssh_user(const std::string& u);
bool use_mosh();
void set_use_mosh(bool on);

}  // namespace zs
