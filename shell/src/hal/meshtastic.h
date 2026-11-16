// Just enough of the Meshtastic client API (protobufs over TCP port 4403)
// for text chat: hand-written so ZeroShell needs no protobuf library.
// Field numbers from meshtastic/protobufs (mesh.proto, portnums.proto).
#pragma once

#include <cstdint>
#include <string>

namespace zs::mt {

constexpr uint32_t kBroadcast = 0xffffffffu;

struct Event {
    enum Kind { None, MyNode, Node, Text, ConfigDone } kind = None;
    uint32_t num = 0;            // MyNode, Node: node number
    std::string long_name, short_name;   // Node
    uint32_t from = 0, to = 0, channel = 0, rx_time = 0;   // Text
    std::string text;            // Text
};

// A framed ToRadio message (0x94 0xC3, 16-bit length, protobuf).
std::string want_config(uint32_t nonce);
std::string text_message(const std::string& text, uint32_t to, uint32_t channel, uint32_t packet_id);
std::string heartbeat();

// Bytes in, FromRadio payloads out.
class Stream {
public:
    void feed(const char* data, size_t n) { buf_.append(data, n); }
    bool next(std::string& payload);   // one complete frame's payload
    void clear() { buf_.clear(); }

private:
    std::string buf_;
};

Event decode(const std::string& from_radio);

}  // namespace zs::mt
