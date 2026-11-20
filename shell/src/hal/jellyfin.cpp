#include "hal/jellyfin.h"

#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace zs::jf {
namespace {

constexpr int kPage = 200;       // most rows fetched for one list
constexpr int kTimeoutS = 12;

// ---- a small JSON reader: just enough to walk objects and arrays ----------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;                             // Array
    std::vector<std::pair<std::string, Json>> fields;    // Object

    const Json& operator[](const char* key) const {
        static const Json none;
        for (auto& f : fields)
            if (f.first == key) return f.second;
        return none;
    }
    const std::string& text() const { return str; }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Json& out) {
        bool ok = value(out, 0);
        ws();
        return ok && i_ == s_.size();
    }

private:
    void ws() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    }
    bool lit(const char* word) {
        size_t n = std::strlen(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char ch = s_[i_++];
            if (!std::isxdigit((unsigned char)ch)) return false;
            v = v * 16 + unsigned(std::isdigit((unsigned char)ch) ? ch - '0' : std::tolower(ch) - 'a' + 10);
        }
        return true;
    }
    bool string(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char ch = s_[i_++];
            if (ch == '"') return true;
            if (ch != '\\') { o += ch; continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        unsigned lo;
                        i_ += 2;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;   // \" \\ \/
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 32) return false;
        ws();
        if (i_ >= s_.size()) return false;
        char ch = s_[i_];
        if (ch == '{') {
            out.type = Json::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '[') {
            out.type = Json::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '"') {
            out.type = Json::String;
            return string(out.str);
        }
        if (lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (lit("false")) { out.type = Json::Bool; return true; }
        if (lit("null")) return true;
        char* end = nullptr;
        out.num = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        out.type = Json::Number;
        i_ = size_t(end - s_.c_str());
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += char(ch); }
        else if (ch < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += char(ch);
    }
    return o;
}

std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += char(ch);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", ch); o += b; }
    }
    return o;
}

std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char ch : s) {
        if (ch == '\'') r += "'\\''";
        else r += ch;
    }
    return r + "'";
}

// Jellyfin ids are hex, with or without dashes. Anything else never goes
// into a URL.
bool safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char ch : id)
        if (!std::isxdigit(ch) && ch != '-') return false;
    return true;
}

struct Reply {
    int status = 0;        // HTTP status, 0 = no answer
    std::string body;
};

// GET, or POST when body is set. The body travels in a private temporary
// file so a password never shows in the process list.
Reply http(const std::string& url, const std::string& auth, const std::string* body) {
    Reply r;
    std::string cmd = "curl -sS -L --max-time " + std::to_string(kTimeoutS) + " --connect-timeout 6" +
                      " -H 'Accept: application/json' -H " + shell_quote("Authorization: " + auth);
    char tmp[] = "/tmp/zeroos-jf-XXXXXX";
    bool have_tmp = false;
    if (body) {
        int fd = mkstemp(tmp);   // created 0600
        if (fd < 0) return r;
        have_tmp = true;
        bool ok = write(fd, body->data(), body->size()) == ssize_t(body->size());
        close(fd);
        if (!ok) { unlink(tmp); return r; }
        cmd += " -H 'Content-Type: application/json' --data-binary @" + std::string(tmp);
    }
    cmd += " -w '\\n%{http_code}' " + shell_quote(url) + " 2>/dev/null";
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.body.append(buf, n);
        pclose(p);
    }
    if (have_tmp) unlink(tmp);
    auto nl = r.body.rfind('\n');
    if (nl != std::string::npos) {
        r.status = std::atoi(r.body.c_str() + nl + 1);
        r.body.erase(nl);
    }
    return r;
}

std::string status_error(const Reply& r) {
    if (r.status == 0) return "Can't reach the server";
    if (r.status == 401) return "Signed out by the server; sign in again";
    if (r.status == 403) return "This user may not do that";
    if (r.status == 404) return "The server doesn't have that";
    return "Server error " + std::to_string(r.status);
}

std::string artist_of(const Json& it) {
    if (!it["AlbumArtist"].text().empty()) return it["AlbumArtist"].text();
    const Json& a = it["Artists"];
    if (a.type == Json::Array && !a.items.empty()) return a.items[0].text();
    return "";
}

}  // namespace

std::string normalize_server(const std::string& typed) {
    std::string s;
    for (char ch : typed)
        if (!std::isspace((unsigned char)ch)) s += ch;
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return s;
    if (s.find("://") == std::string::npos) {
        // A bare host: plain HTTP on Jellyfin's own port unless one is given.
        auto slash = s.find('/');
        std::string host = s.substr(0, slash);
        bool has_port = host.rfind(':') != std::string::npos && host.back() != ']';
        if (!has_port) s.insert(host.size(), ":8096");
        s = "http://" + s;
    }
    return s;
}

std::string auth_header(const JfSession& s) {
    std::string h = "MediaBrowser Client=\"ZeroOS\", Device=\"Cardputer Zero\", DeviceId=\"" + s.device_id +
                    "\", Version=\"0.1.0\"";
    if (!s.token.empty()) h += ", Token=\"" + s.token + "\"";
    return h;
}

std::string stream_url(const JfSession& s, const std::string& item_id) {
    if (!safe_id(item_id)) return "";
    return s.server + "/Audio/" + item_id + "/universal?userId=" + url_encode(s.user_id) +
           "&deviceId=" + url_encode(s.device_id) +
           "&container=mp3,flac,ogg,opus,m4a,wav&transcodingContainer=mp3&transcodingProtocol=http"
           "&audioCodec=mp3&maxStreamingBitrate=320000";
}

std::string list_path(const JfSession& s, JfView view, const std::string& arg) {
    const std::string user = "userId=" + url_encode(s.user_id);
    const std::string limit = "&limit=" + std::to_string(kPage);
    const std::string albums = "/Items?" + user + "&includeItemTypes=MusicAlbum&recursive=true";
    switch (view) {
        case JfView::Latest: return albums + "&sortBy=DateCreated&sortOrder=Descending&limit=40";
        case JfView::Albums: return albums + "&sortBy=SortName" + limit;
        case JfView::Artists:
            return "/Items?" + user + "&includeItemTypes=MusicArtist&recursive=true&sortBy=SortName" + limit;
        case JfView::Playlists:
            return "/Items?" + user + "&includeItemTypes=Playlist&recursive=true&sortBy=SortName" + limit;
        case JfView::Search:
            return "/Items?" + user + "&includeItemTypes=Audio,MusicAlbum,MusicArtist&recursive=true&limit=60"
                   "&searchTerm=" + url_encode(arg);
        case JfView::AlbumTracks:
            if (!safe_id(arg)) return "";
            return "/Items?" + user + "&parentId=" + arg +
                   "&includeItemTypes=Audio&recursive=true&sortBy=ParentIndexNumber,IndexNumber,SortName" + limit;
        case JfView::ArtistAlbums:
            if (!safe_id(arg)) return "";
            return albums + "&albumArtistIds=" + arg + "&sortBy=ProductionYear,SortName" + limit;
        case JfView::PlaylistTracks:
            if (!safe_id(arg)) return "";
            return "/Playlists/" + arg + "/Items?" + user + limit;
    }
    return "";
}

bool parse_login(const std::string& json, JfSession& s) {
    Json j;
    if (!Parser(json).parse(j)) return false;
    s.token = j["AccessToken"].text();
    s.user_id = j["User"]["Id"].text();
    if (!j["User"]["Name"].text().empty()) s.user = j["User"]["Name"].text();
    return !s.token.empty() && !s.user_id.empty();
}

bool parse_items(const std::string& json, std::vector<JfItem>& out) {
    Json j;
    if (!Parser(json).parse(j)) return false;
    const Json& arr = j.type == Json::Array ? j : j["Items"];
    if (arr.type != Json::Array) return false;
    for (const Json& it : arr.items) {
        JfItem x;
        const std::string& type = it["Type"].text();
        if (type == "MusicAlbum") x.kind = JfItem::Album;
        else if (type == "MusicArtist") x.kind = JfItem::Artist;
        else if (type == "Playlist") x.kind = JfItem::Playlist;
        else if (type == "Audio") x.kind = JfItem::Track;
        else continue;
        x.id = it["Id"].text();
        x.name = it["Name"].text();
        if (!safe_id(x.id) || x.name.empty()) continue;
        if (x.kind == JfItem::Album || x.kind == JfItem::Track) x.detail = artist_of(it);
        // RunTimeTicks: 10 000 000 to the second.
        if (it["RunTimeTicks"].type == Json::Number) x.duration_s = int(it["RunTimeTicks"].num / 1e7 + 0.5);
        out.push_back(std::move(x));
    }
    return true;
}

bool login(const std::string& server, const std::string& user, const std::string& password,
           const std::string& device_id, JfSession& out, std::string& error) {
    JfSession s;
    s.server = normalize_server(server);
    s.user = user;
    s.device_id = device_id;
    if (s.server.empty() || user.empty()) {
        error = "Server and user name are needed";
        return false;
    }
    std::string body = "{\"Username\":\"" + json_escape(user) + "\",\"Pw\":\"" + json_escape(password) + "\"}";
    Reply r = http(s.server + "/Users/AuthenticateByName", auth_header(s), &body);
    if (r.status == 401 || r.status == 403) {
        error = "Wrong user name or password";
        return false;
    }
    if (r.status != 200) {
        error = r.status == 0 ? "Can't reach " + s.server : status_error(r);
        return false;
    }
    if (!parse_login(r.body, s)) {
        error = "That address isn't a Jellyfin server";
        return false;
    }
    out = s;
    return true;
}

bool list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
          std::string& error) {
    std::string path = list_path(s, view, arg);
    if (path.empty()) {
        error = "Nothing to open";
        return false;
    }
    Reply r = http(s.server + path, auth_header(s), nullptr);
    std::vector<JfItem> got;
    bool ok = r.status == 200 && parse_items(r.body, got);
    if (view == JfView::Artists && (!ok || got.empty())) {
        // Servers differ on whether artists come from /Items; this is the
        // older, dedicated list.
        got.clear();
        r = http(s.server + "/Artists/AlbumArtists?userId=" + url_encode(s.user_id) + "&limit=" +
                     std::to_string(kPage),
                 auth_header(s), nullptr);
        ok = r.status == 200 && parse_items(r.body, got);
    }
    if (!ok) {
        error = r.status == 200 ? "Unexpected answer from the server" : status_error(r);
        return false;
    }
    out = std::move(got);
    return true;
}

}  // namespace zs::jf
