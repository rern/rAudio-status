// g++ -O2 websocket.cpp -o websocket
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

constexpr int PORT_WS  = 8080;
constexpr int PORT_UDP = 9000;
constexpr int TIMEOUT_MS = 1000;

// Internal TCP/WebSocket handler for Port 8080 (With Reply parsing)
bool wsSend(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_WS);
    serverAddr.sin_addr.s_addr = inet_addr(ws_ip.c_str());

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Could not connect to WS server on port " << PORT_WS << "\n";
        close(sock);
        return false;
    }

    std::string handshake = 
        "GET / HTTP/1.1\r\n"
        "Host: " + ws_ip + ":" + std::to_string(PORT_WS) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (send(sock, handshake.c_str(), handshake.length(), 0) < 0) {
        close(sock);
        return false;
    }

    char response[1024];
    int bytesRead = recv(sock, response, sizeof(response) - 1, 0);
    if (bytesRead <= 0 || std::string(response).find("101 Switching Protocols") == std::string::npos) {
        close(sock);
        return false;
    }

    size_t len = msg.length();
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // Fin=1, Opcode=1 (Text)

    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len | 0x80));
    } else if (len <= 65535) {
        frame.push_back(126 | 0x80);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        close(sock);
        return false;
    }

    uint8_t mask[4] = {0x00, 0x00, 0x00, 0x00}; 
    frame.insert(frame.end(), mask, mask + 4);
    frame.insert(frame.end(), msg.begin(), msg.end());

    send(sock, frame.data(), frame.size(), 0);
    std::cout << "Successfully sent payload to " << ws_ip << ":" << PORT_WS << " via TCP/WS\n";
    std::cout << "Awaiting reply...\n";

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, TIMEOUT_MS) > 0 && (pfd.revents & POLLIN)) {
        char rx_buf[8192];
        std::memset(rx_buf, 0, sizeof(rx_buf));
        
        int bytesRx = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (bytesRx > 2) {
            uint8_t base_len = rx_buf[1] & 0x7F;
            char* payload_start = rx_buf + 2;
            size_t actual_payload_len = base_len;

            if (base_len == 126) {
                actual_payload_len = (static_cast<uint8_t>(rx_buf[2]) << 8) | static_cast<uint8_t>(rx_buf[3]);
                payload_start = rx_buf + 4;
            } else if (base_len == 127) {
                payload_start = rx_buf + 10;
                actual_payload_len = bytesRx - 10;
            }

            if ((payload_start - rx_buf) + actual_payload_len < (size_t)bytesRx) {
                payload_start[actual_payload_len] = '\0';
            } else {
                rx_buf[bytesRx] = '\0';
            }

            std::cout << "\n--- REPLY RECEIVED ---\n" << payload_start << "\n----------------------\n";
            close(sock);
            return true;
        }
    } else {
        std::cerr << "Timed out waiting for reply from port " << PORT_WS << "\n";
    }

    close(sock);
    return false;
}

// Internal TCP WebSocket Fire-and-Forget (No reply check)
bool wsPush(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_WS);
    serverAddr.sin_addr.s_addr = inet_addr(ws_ip.c_str());

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return false;
    }

    std::string handshake = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    send(sock, handshake.c_str(), handshake.length(), 0);

    char response[512];
    recv(sock, response, sizeof(response) - 1, 0); // Clear handshake sync

    size_t len = msg.length();
    std::vector<uint8_t> frame;
    frame.push_back(0x81);
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len | 0x80));
    } else {
        frame.push_back(126 | 0x80);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    }
    uint8_t mask[4] = {0x00, 0x00, 0x00, 0x00};
    frame.insert(frame.end(), mask, mask + 4);
    frame.insert(frame.end(), msg.begin(), msg.end());

    send(sock, frame.data(), frame.size(), 0);
    std::cout << "Successfully sent fire-and-forget payload to " << ws_ip << ":" << PORT_WS << "\n";
    
    close(sock);
    return true;
}

// Internal UDP handler for Broadcast (Port 9000)
bool wsBroadcast(const std::string& msg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in targetAddr;
    std::memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(PORT_UDP);
    targetAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    ssize_t bytesSent = sendto(sock, msg.c_str(), msg.length(), 0,
                               (struct sockaddr*)&targetAddr, sizeof(targetAddr));
    
    if (bytesSent >= 0) {
        std::cout << "Successfully broadcasted " << bytesSent << " bytes to port " << PORT_UDP << " via UDP\n";
        close(sock);
        return true;
    }

    close(sock);
    return false;
}

int main(int argc, char* argv[]) {
    std::string ws_ip = "127.0.0.1";
    std::string ws_message = "ping";
    
    std::string argv1 = argv[1];
    if (argv1 == "-b") {
        if (argc == 3) wsBroadcast(argv[2]);
    } else if (argv1 == "-x") {
        if (argc == 3) wsPush(ws_ip, argv[2]);
    } else if (argc == 2) {
        wsSend(ws_ip, argv1);
    } else if (argc == 3) {
        wsSend(argv1, argv[2]);
    } else {
        std::cerr << "Usage: " << argv[0] << " [-b|-x] [IP] <MESSAGE>\n";
        std::cerr << "         send and wait for reply\n";
        std::cerr << "  -b     broadcast\n";
        std::cerr << "  -x     send and exit (no wait)\n";
        return 1;
    }
    return 0;
}
