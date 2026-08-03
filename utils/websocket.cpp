// As a header:            #include "websocket.cpp" and call wsSend/wsPush/wsBroadcast
// As a standalone CLI:    g++ -O2 -DWS_BUILD_MAIN websocket.cpp -o websocket
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

constexpr int PORT_WS  = 8080;
constexpr int PORT_UDP = 9000;
constexpr int TIMEOUT_MS = 1000;

namespace ws_detail {

// Generates a random 4-byte mask key. RFC 6455 requires client frames to be
// masked with an unpredictable key -- a fixed/zero mask defeats the purpose
// and some servers will reject it outright.
inline void randomMask(uint8_t mask[4]) {
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
    uint32_t r = dist(rng);
    std::memcpy(mask, &r, 4);
}

// Builds a single-frame masked text (opcode 0x1) WebSocket frame.
// Supports the full RFC 6455 length range via the 16-bit (126) and 64-bit
// (127) extended length fields, so payloads larger than 65535 bytes are
// sent as a single frame rather than rejected.
inline std::vector<uint8_t> buildTextFrame(const std::string& payload) {
    std::vector<uint8_t> frame;
    uint64_t len = payload.size();

    frame.push_back(0x81); // FIN=1, opcode=1 (text)

    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len | 0x80)); // MASK bit set
    } else if (len <= 0xFFFF) {
        frame.push_back(126 | 0x80);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127 | 0x80);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((len >> shift) & 0xFF));
        }
    }

    uint8_t mask[4];
    randomMask(mask);
    frame.insert(frame.end(), mask, mask + 4);

    size_t base = frame.size();
    frame.resize(base + static_cast<size_t>(len));
    for (uint64_t i = 0; i < len; ++i) {
        frame[base + i] = static_cast<uint8_t>(payload[i]) ^ mask[i % 4];
    }

    return frame;
}

// Sends the full buffer, looping over partial writes. Returns false on error.
inline bool sendAll(int sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

inline bool sendAll(int sock, const std::string& s) {
    return sendAll(sock, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// Connects with a bounded timeout instead of relying on the OS default,
// which can block indefinitely on an unreachable host.
inline int connectWithTimeout(const std::string& ip, int port, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close(sock);
        return -1;
    }

    int rc = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(sock);
        return -1;
    }

    if (rc < 0) {
        struct pollfd pfd{sock, POLLOUT, 0};
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            close(sock);
            return -1;
        }
        int err = 0;
        socklen_t elen = sizeof(err);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
            close(sock);
            return -1;
        }
    }

    fcntl(sock, F_SETFL, flags); // restore blocking mode
    return sock;
}

// Performs the client handshake and returns true iff the server replied with
// "101 Switching Protocols". response_out, if non-null, is left null-terminated.
inline bool doHandshake(int sock, const std::string& ip, int port,
                         char* response_out, size_t response_cap) {
    std::string handshake =
        "GET / HTTP/1.1\r\n"
        "Host: " + ip + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (!sendAll(sock, handshake)) return false;

    if (!response_out || response_cap == 0) return false;
    int bytesRead = recv(sock, response_out, static_cast<int>(response_cap - 1), 0);
    if (bytesRead <= 0) return false;
    response_out[bytesRead] = '\0';

    return std::string(response_out).find("101 Switching Protocols") != std::string::npos;
}

// Maximum payload size wsSend will accept from a reply frame. Guards against
// a malicious/broken server declaring an enormous length and exhausting
// memory; adjust if you genuinely expect larger replies.
constexpr uint64_t MAX_REPLY_PAYLOAD_BYTES = 128ull * 1024 * 1024; // 128 MB

// Waits up to timeout_ms for at least one more byte and reads whatever is
// available, appending it to buf. Returns false on timeout/error/EOF.
inline bool recvMore(int sock, int timeout_ms, std::string& buf) {
    struct pollfd pfd{sock, POLLIN, 0};
    if (poll(&pfd, 1, timeout_ms) <= 0 || !(pfd.revents & POLLIN)) return false;

    char chunk[65536];
    int n = recv(sock, chunk, sizeof(chunk), 0);
    if (n <= 0) return false; // 0 = peer closed, <0 = error

    buf.append(chunk, n);
    return true;
}

// Reads one complete WebSocket frame from sock, looping recv() as needed
// until the full payload (per the header's declared length) has arrived,
// rather than trusting whatever a single recv() call happened to return.
// Returns true and fills out_payload on success. Rejects control frames,
// fragmented frames, and payloads over MAX_REPLY_PAYLOAD_BYTES.
inline bool recvFrame(int sock, int timeout_ms, std::string& out_payload) {
    std::string buf;

    // 1) Read enough to see the base 2-byte header.
    while (buf.size() < 2) {
        if (!recvMore(sock, timeout_ms, buf)) return false;
    }

    bool fin = (static_cast<uint8_t>(buf[0]) & 0x80) != 0;
    uint8_t opcode = static_cast<uint8_t>(buf[0]) & 0x0F;
    uint8_t base_len = static_cast<uint8_t>(buf[1]) & 0x7F;

    if (!fin || (opcode != 0x1 && opcode != 0x2)) {
        // Control frame (ping/pong/close) or fragmented message: not
        // handled by this simple synchronous helper.
        return false;
    }

    // 2) Read enough to see the full length header (2, 4, or 10 bytes).
    size_t header_len = 2;
    if (base_len == 126) header_len = 4;
    else if (base_len == 127) header_len = 10;

    while (buf.size() < header_len) {
        if (!recvMore(sock, timeout_ms, buf)) return false;
    }

    uint64_t payload_len = base_len;
    if (base_len == 126) {
        payload_len = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[3]);
    } else if (base_len == 127) {
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | static_cast<uint8_t>(buf[2 + i]);
        }
    }

    if (payload_len > MAX_REPLY_PAYLOAD_BYTES) {
        std::cerr << "Reply payload (" << payload_len << " bytes) exceeds max of "
                  << MAX_REPLY_PAYLOAD_BYTES << " bytes; rejecting\n";
        return false;
    }

    // 3) Read until the full payload has arrived (server->client frames are
    // never masked, per RFC 6455, so no unmasking needed here).
    size_t total_needed = header_len + static_cast<size_t>(payload_len);
    while (buf.size() < total_needed) {
        if (!recvMore(sock, timeout_ms, buf)) return false;
    }

    out_payload.assign(buf, header_len, static_cast<size_t>(payload_len));
    return true;
}

} // namespace ws_detail

// Connects to ws_ip:PORT_WS, sends msg as a single masked text frame, and
// waits (up to TIMEOUT_MS) for one reply frame, returning its payload.
// Returns an empty string on any failure (connect, handshake, send, timeout).
std::string wsSend(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = ws_detail::connectWithTimeout(ws_ip, PORT_WS, TIMEOUT_MS);
    if (sock < 0) {
        std::cerr << "Error: Could not connect to WS server on port " << PORT_WS << "\n";
        return {};
    }

    char handshakeResp[1024];
    if (!ws_detail::doHandshake(sock, ws_ip, PORT_WS, handshakeResp, sizeof(handshakeResp))) {
        close(sock);
        return {};
    }

    std::vector<uint8_t> frame = ws_detail::buildTextFrame(msg);
    if (frame.empty() || !ws_detail::sendAll(sock, frame.data(), frame.size())) {
        close(sock);
        return {};
    }

    std::string reply;
    if (!ws_detail::recvFrame(sock, TIMEOUT_MS, reply)) {
        std::cerr << "Timed out or failed waiting for reply from port " << PORT_WS << "\n";
        close(sock);
        return {};
    }

    close(sock);
    return reply;
}

// Fire-and-forget send: connects, handshakes, sends one masked text frame,
// and closes. Returns 0 on success, 1 on any failure.
int wsPush(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = ws_detail::connectWithTimeout(ws_ip, PORT_WS, TIMEOUT_MS);
    if (sock < 0) return 1;

    char handshakeResp[512];
    if (!ws_detail::doHandshake(sock, ws_ip, PORT_WS, handshakeResp, sizeof(handshakeResp))) {
        close(sock);
        return 1;
    }

    std::vector<uint8_t> frame = ws_detail::buildTextFrame(msg);
    if (frame.empty() || !ws_detail::sendAll(sock, frame.data(), frame.size())) {
        close(sock);
        return 1;
    }

    close(sock);
    return 0;
}

// Broadcasts msg via UDP to the local subnet on PORT_UDP.
// Returns 0 on success, 1 on any failure.
int wsBroadcast(const std::string& msg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 1;

    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        close(sock);
        return 1;
    }

    struct sockaddr_in targetAddr;
    std::memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(PORT_UDP);
    targetAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    ssize_t bytesSent = sendto(sock, msg.c_str(), msg.length(), 0,
                                reinterpret_cast<struct sockaddr*>(&targetAddr), sizeof(targetAddr));

    close(sock);
    return (bytesSent == static_cast<ssize_t>(msg.length())) ? 0 : 1;
}

#ifdef WS_BUILD_MAIN
// Minimal CLI wrapper, matching the compile comment at the top of this file.
//
// Usage:
//   websocket send   <ip> <message>
//   websocket push   <ip> <message>
//   websocket broadcast <message>
//   websocket send   <ip> --large-payload <bytes>   (send N bytes of filler data, single frame)
//   websocket push   <ip> --large-payload <bytes>
//
// --large-payload exists as its own explicit option (rather than just
// pasting a huge string on the command line) so that using the 64-bit
// extended-length frame path is a deliberate choice, and so payload size
// can be scripted/tested independently of message content.
#include <cstdlib>

static std::string makeFillerPayload(uint64_t bytes) {
    // JSON so it survives the msg.front() == '{' check unchanged.
    std::string body = "{\"large_payload_bytes\":" + std::to_string(bytes) + ",\"data\":\"";
    std::string closing = "\"}";
    if (bytes <= body.size() + closing.size()) {
        // Degenerate small size: just return a minimal valid JSON object.
        return "{\"large_payload_bytes\":0,\"data\":\"\"}";
    }
    body.append(bytes - body.size() - closing.size(), 'A');
    body += closing;
    return body;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " send <ip> <message>\n"
                  << "  " << argv[0] << " send <ip> --large-payload <bytes>\n"
                  << "  " << argv[0] << " push <ip> <message>\n"
                  << "  " << argv[0] << " push <ip> --large-payload <bytes>\n"
                  << "  " << argv[0] << " broadcast <message>\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "broadcast") {
        if (argc < 3) { std::cerr << "broadcast requires <message>\n"; return 1; }
        return wsBroadcast(argv[2]);
    }

    if (cmd == "send" || cmd == "push") {
        if (argc < 4) { std::cerr << cmd << " requires <ip> <message>\n"; return 1; }
        std::string ip = argv[2];
        std::string msg;

        if (std::string(argv[3]) == "--large-payload") {
            if (argc < 5) { std::cerr << "--large-payload requires <bytes>\n"; return 1; }
            uint64_t bytes = std::strtoull(argv[4], nullptr, 10);
            msg = makeFillerPayload(bytes);
            std::cerr << "Sending large payload: " << msg.size() << " bytes\n";
        } else {
            msg = argv[3];
        }

        if (cmd == "send") {
            std::string reply = wsSend(ip, msg);
            if (reply.empty()) {
                std::cerr << "No reply received (or an error occurred)\n";
                return 1;
            }
            std::cout << reply << "\n";
            return 0;
        } else {
            return wsPush(ip, msg);
        }
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
#endif // WS_BUILD_MAIN
