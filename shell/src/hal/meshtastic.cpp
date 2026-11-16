#include "hal/meshtastic.h"

namespace zs::mt {
namespace {

// --- writing
void varint(std::string& o, uint64_t v) {
    while (v >= 0x80) {
        o += char((v & 0x7f) | 0x80);
        v >>= 7;
    }
    o += char(v);
}
void key(std::string& o, uint32_t field, uint32_t wire) { varint(o, (uint64_t(field) << 3) | wire); }
void put_varint(std::string& o, uint32_t field, uint64_t v) {
    key(o, field, 0);
    varint(o, v);
}
void put_fixed32(std::string& o, uint32_t field, uint32_t v) {
    key(o, field, 5);
    for (int i = 0; i < 4; ++i) o += char((v >> (8 * i)) & 0xff);
}
void put_bytes(std::string& o, uint32_t field, const std::string& b) {
    key(o, field, 2);
    varint(o, b.size());
    o += b;
}
std::string framed(const std::string& p) {
    std::string o;
    o += char(0x94);
    o += char(0xc3);
    o += char((p.size() >> 8) & 0xff);
    o += char(p.size() & 0xff);
    return o + p;
}

// --- reading
struct Reader {
    const std::string& s;
    size_t i = 0, end;
    explicit Reader(const std::string& str) : s(str), end(str.size()) {}
    bool more() const { return i < end; }
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64 && i < end; shift += 7) {
            uint8_t b = uint8_t(s[i++]);
            v |= uint64_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    // Next field: number, wire type, and its value (varint/fixed in v, bytes in b).
    bool field(uint32_t& num, uint32_t& wire, uint64_t& v, std::string& b) {
        uint64_t k;
        if (!varint(k)) return false;
        num = uint32_t(k >> 3);
        wire = uint32_t(k & 7);
        switch (wire) {
        case 0: return varint(v);
        case 1:
            if (end - i < 8) return false;
            v = 0;
            for (int n = 0; n < 8; ++n) v |= uint64_t(uint8_t(s[i + n])) << (8 * n);
            i += 8;
            return true;
        case 5:
            if (end - i < 4) return false;
            v = 0;
            for (int n = 0; n < 4; ++n) v |= uint64_t(uint8_t(s[i + n])) << (8 * n);
            i += 4;
            return true;
        case 2: {
            uint64_t len;
            if (!varint(len) || len > end - i) return false;
            b = s.substr(i, size_t(len));
            i += size_t(len);
            return true;
        }
        default: return false;   // groups: not used by Meshtastic
        }
    }
};

void read_user(const std::string& m, Event& e) {
    Reader r(m);
    uint32_t n, w;
    uint64_t v;
    std::string b;
    while (r.more() && r.field(n, w, v, b)) {
        if (n == 2 && w == 2) e.long_name = b;
        if (n == 3 && w == 2) e.short_name = b;
    }
}

void read_packet(const std::string& m, Event& e) {
    Reader r(m);
    uint32_t n, w;
    uint64_t v;
    std::string b, decoded;
    while (r.more() && r.field(n, w, v, b)) {
        if (n == 1 && w == 5) e.from = uint32_t(v);
        else if (n == 2 && w == 5) e.to = uint32_t(v);
        else if (n == 3 && w == 0) e.channel = uint32_t(v);
        else if (n == 4 && w == 2) decoded = b;
        else if (n == 7 && w == 5) e.rx_time = uint32_t(v);
    }
    if (decoded.empty()) return;   // encrypted for someone else
    Reader d(decoded);
    uint64_t port = 0;
    std::string payload;
    while (d.more() && d.field(n, w, v, b)) {
        if (n == 1 && w == 0) port = v;
        if (n == 2 && w == 2) payload = b;
    }
    if (port == 1) {   // TEXT_MESSAGE_APP
        e.kind = Event::Text;
        e.text = payload;
    }
}

}  // namespace

std::string want_config(uint32_t nonce) {
    std::string m;
    put_varint(m, 3, nonce);   // ToRadio.want_config_id
    return framed(m);
}

std::string text_message(const std::string& text, uint32_t to, uint32_t channel, uint32_t packet_id) {
    std::string data, pkt, m;
    put_varint(data, 1, 1);         // Data.portnum = TEXT_MESSAGE_APP
    put_bytes(data, 2, text);       // Data.payload
    put_fixed32(pkt, 2, to);        // MeshPacket.to
    if (channel) put_varint(pkt, 3, channel);
    put_bytes(pkt, 4, data);        // MeshPacket.decoded
    put_fixed32(pkt, 6, packet_id); // MeshPacket.id
    put_varint(pkt, 9, 3);          // hop_limit
    put_varint(pkt, 10, 1);         // want_ack
    put_bytes(m, 1, pkt);           // ToRadio.packet
    return framed(m);
}

std::string heartbeat() {
    std::string m;
    put_bytes(m, 7, "");            // ToRadio.heartbeat (empty message)
    return framed(m);
}

bool Stream::next(std::string& payload) {
    for (;;) {
        // Resync on the 0x94 0xC3 start bytes (the daemon may interleave text logs).
        size_t s = 0;
        while (s + 1 < buf_.size() && !(uint8_t(buf_[s]) == 0x94 && uint8_t(buf_[s + 1]) == 0xc3)) ++s;
        if (s) buf_.erase(0, s);
        if (buf_.size() < 4) return false;
        size_t len = (size_t(uint8_t(buf_[2])) << 8) | uint8_t(buf_[3]);
        if (len > 512) {   // not a real frame: skip the start bytes
            buf_.erase(0, 2);
            continue;
        }
        if (buf_.size() < 4 + len) return false;
        payload = buf_.substr(4, len);
        buf_.erase(0, 4 + len);
        return true;
    }
}

Event decode(const std::string& from_radio) {
    Event e;
    Reader r(from_radio);
    uint32_t n, w;
    uint64_t v;
    std::string b;
    while (r.more() && r.field(n, w, v, b)) {
        if (n == 2 && w == 2) {
            read_packet(b, e);
        } else if (n == 3 && w == 2) {   // my_info
            Reader m(b);
            uint32_t n2, w2;
            uint64_t v2;
            std::string b2;
            while (m.more() && m.field(n2, w2, v2, b2))
                if (n2 == 1 && w2 == 0) e.num = uint32_t(v2);
            e.kind = Event::MyNode;
        } else if (n == 4 && w == 2) {   // node_info
            Reader m(b);
            uint32_t n2, w2;
            uint64_t v2;
            std::string b2;
            while (m.more() && m.field(n2, w2, v2, b2)) {
                if (n2 == 1 && w2 == 0) e.num = uint32_t(v2);
                if (n2 == 2 && w2 == 2) read_user(b2, e);
            }
            e.kind = Event::Node;
        } else if (n == 7 && w == 0) {
            e.kind = Event::ConfigDone;
        }
    }
    return e;
}

}  // namespace zs::mt
