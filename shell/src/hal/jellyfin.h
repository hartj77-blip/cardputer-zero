// A small Jellyfin client for music: sign in, browse albums, artists and
// playlists, search, and build the URL a track streams from. HTTP goes
// through the curl command, as the rest of the hardware layer uses system
// tools; the JSON reader is hand-written so ZeroShell needs no library.
// Endpoints from Jellyfin's OpenAPI description (api.jellyfin.org).
#pragma once

#include <string>
#include <vector>

namespace zs {

// A signed-in session. The password is never kept; the token is.
struct JfSession {
    std::string server;      // http://host:8096, no trailing slash
    std::string user;
    std::string user_id;
    std::string token;
    std::string device_id;   // any stable string; Jellyfin lists it under Devices
    bool signed_in() const { return !server.empty() && !token.empty(); }
};

struct JfItem {
    enum Kind { Album, Artist, Playlist, Track } kind = Track;
    std::string id;
    std::string name;
    std::string detail;      // artist for albums and tracks
    int duration_s = -1;     // tracks
};

// What to list. arg: an item id for the *Tracks and ArtistAlbums views,
// the text for Search.
enum class JfView { Latest, Albums, Artists, Playlists, Search, AlbumTracks, ArtistAlbums, PlaylistTracks };

namespace jf {

// "nas", "nas:8096" or "https://music.example.com/" as typed by a person,
// made into a base URL. Bare hosts get http:// and Jellyfin's port, 8096.
std::string normalize_server(const std::string& typed);

// The Authorization header value Jellyfin expects from a client.
std::string auth_header(const JfSession& s);

// Where a track streams from: the original file when it is mp3, flac, ogg,
// opus, m4a or wav, otherwise transcoded to mp3 by the server.
std::string stream_url(const JfSession& s, const std::string& item_id);

// Request path and query for a view (after the server address).
std::string list_path(const JfSession& s, JfView view, const std::string& arg);

// Reads AccessToken and the user's id out of the sign-in reply.
bool parse_login(const std::string& json, JfSession& s);
// Reads the "Items" array of a list reply (or a bare array).
bool parse_items(const std::string& json, std::vector<JfItem>& out);

// Network calls (curl). On failure they return false and set error to a
// short sentence a person can act on.
bool login(const std::string& server, const std::string& user, const std::string& password,
           const std::string& device_id, JfSession& out, std::string& error);
bool list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
          std::string& error);

}  // namespace jf
}  // namespace zs
