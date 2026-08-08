#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

constexpr int PORT_WS    = 8080;
constexpr int TIMEOUT_MS = 1000;

std::string wsSend(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return {};

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_WS);
    serverAddr.sin_addr.s_addr = inet_addr(ws_ip.c_str());

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Could not connect to WS server on port " << PORT_WS << "\n";
        close(sock);
        return {};
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
        return {};
    }

    char response[1024];
    int bytesRead = recv(sock, response, sizeof(response) - 1, 0);
    if (bytesRead <= 0 || std::string(response).find("101 Switching Protocols") == std::string::npos) {
        close(sock);
        return {};
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
        return {};
    }

    uint8_t mask[4] = {0x00, 0x00, 0x00, 0x00}; 
    frame.insert(frame.end(), mask, mask + 4);
    frame.insert(frame.end(), msg.begin(), msg.end());

    send(sock, frame.data(), frame.size(), 0);

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, TIMEOUT_MS) > 0 && (pfd.revents & POLLIN)) {
        char rx_buf[8192];
        std::memset(rx_buf, 0, sizeof(rx_buf));
        
        int bytesRx = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (bytesRx > 2) {
            uint8_t base_len = rx_buf[1] & 0x7F;
            char* ws_reply = rx_buf + 2;
            size_t payload_len = base_len;

            if (base_len == 126) {
                payload_len = (static_cast<uint8_t>(rx_buf[2]) << 8) | static_cast<uint8_t>(rx_buf[3]);
                ws_reply = rx_buf + 4;
            } else if (base_len == 127) {
                ws_reply = rx_buf + 10;
                payload_len = bytesRx - 10;
            }

            if ((ws_reply - rx_buf) + payload_len < (size_t)bytesRx) {
                ws_reply[payload_len] = '\0';
            } else {
                rx_buf[bytesRx] = '\0';
            }

            close(sock);
            return ws_reply;
        }
    } else {
        std::cerr << "Timed out waiting for reply from port " << PORT_WS << "\n";
    }

    close(sock);
    return {};
}

int wsPush(const std::string& ws_ip, std::string msg) {
    msg = (!msg.empty() && msg.front() == '{') ? msg : "\"" + msg + "\"";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 1;

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_WS);
    serverAddr.sin_addr.s_addr = inet_addr(ws_ip.c_str());

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return 1;
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
    
    close(sock);
    return 0;
}

int wsBroadcast(const std::string& msg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 1;
    
    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in targetAddr;
    std::memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family      = AF_INET;
    int port                   = std::stoi(fileContent(DIR.SYSTEM + "websocket"));
    targetAddr.sin_port        = htons(port);
    targetAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    ssize_t bytesSent          = sendto(sock, msg.c_str(), msg.length(), 0, (struct sockaddr*)&targetAddr, sizeof(targetAddr));
    
    close(sock);
    return 0;
}
