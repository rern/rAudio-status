#pragma once

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

std::string hostName() {
    char buf[HOST_NAME_MAX];
    if (gethostname(buf, sizeof(buf)) == 0) return buf;
    return {};
}

std::string ipAddress(const std::string& inf = "") {
    if (inf.empty()) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return {};

        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_addr.s_addr = inet_addr("1.1.1.1");
        remote.sin_port = htons(9);

        if (connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0) {
            close(sock);
            return {};
        }

        sockaddr_in local_addr{};
        socklen_t addr_len = sizeof(local_addr);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local_addr), &addr_len) < 0) {
            close(sock);
            return {};
        }
        close(sock);

        char buf[INET_ADDRSTRLEN] = {0};
        if (!inet_ntop(AF_INET, &local_addr.sin_addr, buf, sizeof(buf))) return {};
        return std::string(buf);
    }

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0) return {};

    std::string ip;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;

        std::string name = ifa->ifa_name;
        bool wireless = std::ifstream("/sys/class/net/" + name + "/wireless").good();
        if (inf == "e" && wireless) continue;
        if (inf == "w" && !wireless) continue;

        auto* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf))) {
            ip = buf;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return ip;
}
