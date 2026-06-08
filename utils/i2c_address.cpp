// g++ -O2 i2c_address.cpp -o i2c-address

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

std::vector<std::string> listI2CBuses() {
    std::vector<std::string> buses;
    for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
        if (entry.path().string().find("/dev/i2c-") == 0) {
            buses.push_back(entry.path().string());
        }
    }
    return buses;
}

std::vector<int> scanI2CDevices(const std::string& bus) {
    std::vector<int> found;
    int file = open(bus.c_str(), O_RDWR);
    if (file < 0) {
        perror(("Failed to open " + bus).c_str());
        return found;
    }

    for (int addr = 0x03; addr <= 0x77; ++addr) {
        if (ioctl(file, I2C_SLAVE, addr) >= 0) {
            char buf;
            if (read(file, &buf, 1) == 1) {
                found.push_back(addr);
            }
        }
    }
    close(file);
    return found;
}

int main() {
    auto buses = listI2CBuses();
    for (const auto& bus : buses) {
        auto devices = scanI2CDevices(bus);
        std::cout << "{ ";
        for (size_t i = 0; i < devices.size(); ++i) {
            int addr = devices[i];
            std::cout << "\"0x" << std::hex << addr << "\": " << std::dec << addr;
            if (i + 1 < devices.size()) std::cout << ", ";
        }
        std::cout << " }\n";
    }
}
