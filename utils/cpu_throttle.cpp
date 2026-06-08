#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <cstdint>

// Mailbox constants
#define MAILBOX_DEVICE "/dev/vcio"
#define IOCTL_MBOX_PROPERTY _IOWR(100, 0, char*)
#define MBOX_REQUEST 0x00000000
#define MBOX_TAG_GET_THROTTLED 0x00030046

struct ThrottledStatus {
    bool undervoltage;
    bool frequencyCapped;
    bool throttled;
    bool undervoltageOccurred;
    bool frequencyCappedOccurred;
    bool throttledOccurred;
    bool tempLimit;
    bool tempLimitOccurred;
};

ThrottledStatus getThrottledStatus() {
    ThrottledStatus status{};

    int fd = open(MAILBOX_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/vcio");
        return status;
    }

    uint32_t buffer[32];
    int i = 0;
    buffer[i++] = sizeof(buffer);   // size
    buffer[i++] = MBOX_REQUEST;     // request code

    buffer[i++] = MBOX_TAG_GET_THROTTLED; // tag
    buffer[i++] = 4;                 // buffer size
    buffer[i++] = 0;                 // request size
    buffer[i++] = 0;                 // value (will be filled)

    buffer[i++] = 0;                 // end tag

    if (ioctl(fd, IOCTL_MBOX_PROPERTY, buffer) < 0) {
        perror("ioctl failed");
        close(fd);
        return status;
    }

    uint32_t throttled = buffer[5];
    close(fd);

    status.undervoltage            = throttled & (1 << 0);
    status.frequencyCapped         = throttled & (1 << 1);
    status.throttled               = throttled & (1 << 2);
    status.tempLimit               = throttled & (1 << 3);
    status.undervoltageOccurred    = throttled & (1 << 16);
    status.frequencyCappedOccurred = throttled & (1 << 17);
    status.throttledOccurred       = throttled & (1 << 18);
    status.tempLimitOccurred       = throttled & (1 << 19);

    return status;
}

int main() {
    ThrottledStatus s = getThrottledStatus();
    
    std::string
        occurred = "<gr>occurred</gr>\n",
        cpu_gr   = "<i class='i-templimit gr'></i>CPU ",
        cpu_yl   = "<i class='i-templimit yl'></i>CPU ",
        uv       = "<i class='i-voltage blink local'></i>Under-voltage";
        
    if (s.undervoltage)            std::cout << "<ora>"+ uv +"</ora>\n";
    if (s.throttled)               std::cout << cpu_yl +"throttled\n";
    if (s.tempLimit)               std::cout << cpu_yl +"temperature limit\n";
    if (s.frequencyCapped)         std::cout << cpu_yl +"frequency capped\n";
    
    if (s.undervoltageOccurred)    std::cout << "<yl>"+ uv +"</yl>"+ occurred;
    if (s.throttledOccurred)       std::cout << cpu_gr +"throttling"+ occurred;
    if (s.frequencyCappedOccurred) std::cout << cpu_gr +"frequency capping"+ occurred;
    if (s.tempLimitOccurred)       std::cout << cpu_gr +"temperature limit"+ occurred;
}
