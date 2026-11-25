#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <random>

constexpr int PORT_UDP   = 9001;
constexpr int PORT_WS    = 8080;
constexpr int TIMEOUT_MS = 1000;  // per-read

namespace ws_detail {

constexpr uint64_t MAX_REPLY_BYTES = 16ull * 1024 * 1024;  // max 16MB
constexpr size_t   MAX_HANDSHAKE   = 16 * 1024;

// ---------- RAII socket ----------
struct Socket {
    int fd;
    explicit Socket(int f) : fd(f) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

inline bool sendAll(int fd, const uint8_t* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

// ---------- buffered reader: copes with TCP splitting/coalescing ----------
class Reader {
public:
    Reader(int fd, int timeout_ms) : fd_(fd), timeout_(timeout_ms) {}

    bool readExact(uint8_t* out, size_t n) {
        while (buf_.size() - pos_ < n) {
            if (!fill()) return false;
        }
        std::memcpy(out, buf_.data() + pos_, n);
        pos_ += n;
        compact();
        return true;
    }

    bool readHeaders(std::string& out) { // Read up to and including "\r\n\r\n"; extra bytes stay buffered.
        static const std::string delim = "\r\n\r\n";
        for (;;) {
            std::string view(buf_.begin() + pos_, buf_.end());
            size_t idx = view.find(delim);
            if (idx != std::string::npos) {
                out = view.substr(0, idx + delim.size());
                pos_ += idx + delim.size();
                compact();
                return true;
            }
            if (view.size() > MAX_HANDSHAKE) return false;
            if (!fill()) return false;
        }
    }

private:
    bool fill() {
        struct pollfd pfd{fd_, POLLIN, 0};
        int pr;
        do { pr = ::poll(&pfd, 1, timeout_); } while (pr < 0 && errno == EINTR);
        if (pr <= 0) return false;  // timeout or error

        uint8_t tmp[4096];
        ssize_t r;
        do { r = ::recv(fd_, tmp, sizeof(tmp), 0); } while (r < 0 && errno == EINTR);
        if (r <= 0) return false;   // closed or error
        buf_.insert(buf_.end(), tmp, tmp + r);
        return true;
    }

    void compact() {
        if (pos_ == buf_.size()) { buf_.clear(); pos_ = 0; }
    }

    int fd_;
    int timeout_;
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
};

struct Connection {
    Socket sock;
    Reader rd;
    Connection() : sock(::socket(AF_INET, SOCK_STREAM, 0)), rd(sock.fd, TIMEOUT_MS) {}
};

// ---------- helpers ----------
inline std::string base64(const uint8_t* d, size_t n) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < n) ? tbl[v & 63] : '=';
    }
    return out;
}

inline std::mt19937& rng() {
    static std::mt19937 g{std::random_device{}()};
    return g;
}

inline std::string wrapMessage(const std::string& msg) {
    return (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";
}

// Masked client frame (clients MUST mask per RFC 6455). Supports any length.
inline std::vector<uint8_t> buildFrame(uint8_t opcode, const std::string& payload) {
    std::vector<uint8_t> f;
    const uint64_t len = payload.size();

    f.push_back(0x80 | (opcode & 0x0F));  // FIN=1
    if (len < 126) {
        f.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 0xFFFF) {
        f.push_back(0x80 | 126);
        f.push_back(static_cast<uint8_t>(len >> 8));
        f.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        f.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    }

    uint8_t mask[4];
    uint32_t m = rng()();
    std::memcpy(mask, &m, 4);
    f.insert(f.end(), mask, mask + 4);

    for (size_t i = 0; i < payload.size(); ++i)
        f.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);

    return f;
}

// connect() + HTTP upgrade handshake.
inline bool openWebSocket(Connection& c, const std::string& ip, int port, bool verbose) {
    if (c.sock.fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        if (verbose) std::cerr << "Error: invalid IP address '" << ip << "'\n";
        return false;
    }

    if (::connect(c.sock.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (verbose) std::cerr << "Error: Could not connect to WS server on port " << port << "\n";
        return false;
    }

    uint8_t keyBytes[16];
    for (auto& b : keyBytes) b = static_cast<uint8_t>(rng()());
    const std::string key = base64(keyBytes, sizeof(keyBytes));

    const std::string handshake =
        "GET / HTTP/1.1\r\n"
        "Host: " + ip + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (!sendAll(c.sock.fd, reinterpret_cast<const uint8_t*>(handshake.data()), handshake.size()))
        return false;

    std::string resp;
    if (!c.rd.readHeaders(resp) || resp.compare(0, 12, "HTTP/1.1 101") != 0) {
        if (verbose) std::cerr << "Error: WebSocket handshake failed on port " << port << "\n";
        return false;
    }
    return true;
}

// Read one complete message: handles any payload length, fragmentation,
// ping/pong and close frames. Returns false on timeout/error/close.
inline bool readMessage(Connection& c, std::string& message) {
    message.clear();
    bool started = false;

    for (;;) {
        uint8_t hdr[2];
        if (!c.rd.readExact(hdr, 2)) return false;

        const bool    fin    = hdr[0] & 0x80;
        const uint8_t opcode = hdr[0] & 0x0F;
        const bool    masked = hdr[1] & 0x80;
        uint64_t      len    = hdr[1] & 0x7F;

        if (len == 126) {
            uint8_t ext[2];
            if (!c.rd.readExact(ext, 2)) return false;
            len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (!c.rd.readExact(ext, 8)) return false;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
        }

        if (len > MAX_REPLY_BYTES || message.size() + len > MAX_REPLY_BYTES) {
            std::cerr << "Error: reply too large\n";
            return false;
        }

        uint8_t maskKey[4] = {0, 0, 0, 0};
        if (masked && !c.rd.readExact(maskKey, 4)) return false;  // servers normally don't mask

        std::string payload(static_cast<size_t>(len), '\0');
        if (len && !c.rd.readExact(reinterpret_cast<uint8_t*>(&payload[0]), static_cast<size_t>(len)))
            return false;
        if (masked)
            for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= maskKey[i % 4];

        switch (opcode) {
            case 0x8:  // close
                return false;
            case 0x9: {  // ping -> pong
                const auto pong = buildFrame(0xA, payload);
                if (!sendAll(c.sock.fd, pong.data(), pong.size())) return false;
                break;
            }
            case 0xA:  // pong -> ignore
                break;
            case 0x1:  // text
            case 0x2:  // binary
                message = std::move(payload);
                started = true;
                break;
            case 0x0:  // continuation
                if (!started) return false;
                message += payload;
                break;
            default:
                return false;  // unknown opcode
        }

        if (fin && (opcode == 0x0 || opcode == 0x1 || opcode == 0x2)) return true;
    }
}

inline void sendClose(Connection& c) {
    const auto f = buildFrame(0x8, std::string{});
    sendAll(c.sock.fd, f.data(), f.size());  // best effort
}

}  // namespace ws_detail

// Send a message and return the server's reply (empty string on failure).
inline std::string wsSend(const std::string& ws_ip, std::string msg, int port = PORT_WS) {
    using namespace ws_detail;

    Connection c;
    if (!openWebSocket(c, ws_ip, port, true)) return {};

    const auto frame = buildFrame(0x1, wrapMessage(msg));
    if (!sendAll(c.sock.fd, frame.data(), frame.size())) return {};

    std::string reply;
    if (!readMessage(c, reply)) {
        std::cerr << "No reply (timeout or connection closed) from port " << port << "\n";
        return {};
    }

    sendClose(c);
    return reply;
}

// Fire-and-forget: send a message, don't wait for a reply. Returns 0 on success, 1 on failure.
inline int wsPush(const std::string& ws_ip, std::string msg, int port = PORT_WS) {
    using namespace ws_detail;

    Connection c;
    if (!openWebSocket(c, ws_ip, port, false)) return 1;

    const auto frame = buildFrame(0x1, wrapMessage(msg));
    if (!sendAll(c.sock.fd, frame.data(), frame.size())) return 1;

    sendClose(c);
    return 0;
}

// Send a UDP broadcast datagram to all remote websocket.py asyncio.DatagramProtocol listeners.
inline int wsBroadcast(const std::string& msg) {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 1;

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0) {
        ::close(sock);
        return 1;
    }

    sockaddr_in target{};
    target.sin_family      = AF_INET;
    target.sin_port        = htons(PORT_UDP);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    ssize_t sent = ::sendto(sock, msg.data(), msg.size(), 0,
                            reinterpret_cast<sockaddr*>(&target), sizeof(target));
    ::close(sock);
    return (sent == static_cast<ssize_t>(msg.size())) ? 0 : 1;
}

inline std::string camillaWebsocket(const std::string& msg) {
    return wsSend("127.0.0.1", msg, 1234);
}
