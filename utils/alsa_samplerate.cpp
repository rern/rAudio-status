// g++ -std=c++17 main.cpp -lasound      (add -lstdc++fs on GCC 8)
#include <alsa/asoundlib.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// Fills format (16 / 24 / 32) and rate (e.g. 48000). Returns false if nothing is playing.
bool get_hw_params(int& format, int& rate) {
    format = rate = 0;
    std::error_code ec;

    for (const auto& card : fs::directory_iterator("/proc/asound", ec)) {
        if (card.is_symlink()) continue;                       // skip name aliases
        if (card.path().filename().string().rfind("card", 0)) continue;

        for (const auto& pcm : fs::directory_iterator(card, ec)) {
            const auto name = pcm.path().filename().string();
            if (name.rfind("pcm", 0) || name.back() != 'p') continue;   // playback only

            for (const auto& sub : fs::directory_iterator(pcm, ec)) {
                std::ifstream in(sub.path() / "hw_params");
                for (std::string line; std::getline(in, line); ) {
                    if (line.compare(0, 8, "format: ") == 0) {
                        auto f = snd_pcm_format_value(line.c_str() + 8);
                        if (f != SND_PCM_FORMAT_UNKNOWN)
                            format = snd_pcm_format_width(f);   // 16 / 24 / 32
                    } else if (line.compare(0, 6, "rate: ") == 0) {
                        rate = std::atoi(line.c_str() + 6);
                    }
                }
                if (format && rate) return true;
            }
        }
    }
    return false;
}

int main() {
    int format, rate;
    if (get_hw_params(format, rate))
        printf("Format: %d\nRate: %d\n", format, rate);
    else
        puts("nothing playing");
}
