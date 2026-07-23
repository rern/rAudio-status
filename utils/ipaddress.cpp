//  g++ -O2 ipaddress.cpp -o ipaddress

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

std::string gateway() {
    std::ifstream route("/proc/net/route");
    if (!route.is_open()) {
        std::perror("open /proc/net/route");
        return "";
    }
    std::string line;
    std::getline(route, line); // skip header
    while (std::getline(route, line)) {
        std::istringstream iss(line);
        std::string iface, destHex, gwHex, flagsHex;
        iss >> iface >> destHex >> gwHex >> flagsHex;
        if (destHex.empty() || gwHex.empty()) continue;

        unsigned long dest = std::strtoul(destHex.c_str(), nullptr, 16);
        unsigned long flags = std::strtoul(flagsHex.c_str(), nullptr, 16);
        // Default route: destination 0.0.0.0 and RTF_GATEWAY flag set
        if (dest != 0 || !(flags & RTF_GATEWAY)) continue;

        unsigned long gw = std::strtoul(gwHex.c_str(), nullptr, 16);
        in_addr addr{};
        addr.s_addr = static_cast<uint32_t>(gw); // already in network byte order
        char buf[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
    }
    return "";
}

std::string ipAddress(const std::string& inf) {
    if (inf == "i") {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::perror("socket");
            return "";
        }
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_addr.s_addr = inet_addr("1.1.1.1"); // Dummy external IP
        remote.sin_port = htons(9);                    // Discard port
        // UDP connect() doesn't send packets; it just asks the kernel to
        // resolve the outgoing route/interface for that destination.
        if (connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0) {
            std::perror("connect");
            close(sock);
            return "";
        }
        sockaddr_in local_addr{};
        socklen_t addr_len = sizeof(local_addr);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local_addr), &addr_len) < 0) {
            std::perror("getsockname");
            close(sock);
            return "";
        }
        close(sock);
        char buf[INET_ADDRSTRLEN] = {0};
        if (!inet_ntop(AF_INET, &local_addr.sin_addr, buf, sizeof(buf))) return "";
        return std::string(buf);
    }
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0) {
        std::perror("getifaddrs");
        return "";
    }
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

int main(int argc, char** argv) {
    std::string inf = argc == 1 ? "i" : argv[1];
    
    if (inf == "-h") {
        std::cout
            << "\nGet IP address\n\n"
            << "Usage: " << argv[0] << " [-e|-g|-w]\n"
            << "       default: primary\n"
            << "  e    ethernet\n"
            << "  g    gateway\n"
            << "  w    wireless\n";
    } else if (inf == "g") {
        std::cout << gateway() << "\n";
    } else {
        std::cout << ipAddress(inf) << "\n";
    }
    return 0;
}
