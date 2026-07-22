#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits.h>
#include <map>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/vfs.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct Global {
    bool
        COVER       = true,
        JSON        = true,
        PAUSE       = false,
        PLAY        = false,
        SNAPCLIENT  = false,
        STOP        = true,
        STREAM      = false,
        VOLUMENONE  = false,
        WEBRADIO    = false,

        ALBUM       = false,
        ARTIST      = false,
        TITLE       = false,

        AIRPLAY     = false,
        BLUETOOTH   = false,
        MPD         = false,
        SNAPCAST    = false,
        SPOTIFY     = false,
        UPNP        = false;

    int
        BITDEPTH   = 0,
        BITRATE    = 0,
        ELAPSED    = 0,
        PLLENGTH   = 0,
        POS        = 0,
        TIME       = 0,
        VOLUME     = 0;

    int64_t
        START      = 0,
        TIMESTAMP  = 0;

    uint32_t
        SAMPLERATE = 0;

    std::string
        COVERART,
        CONTROL,
        EXT,
        ICON,
        PLAYER,
        SAMPLING,
        STATE,
        STATION,
        STATIONCOVER,
        URI,
        URI_INI,
        WS_STATUS,

        DATA   = "/srv/http/data/",
        SHM    = DATA +"shm/",
        SYSTEM = DATA +"system/";

    fs::path
        FILE;
};

Global DIR, V;

std::unordered_map<std::string, bool>        B;
std::unordered_map<std::string, std::string> S;
std::unordered_map<std::string, int>         I;

std::vector<std::string>                VECTOR;

std::string alphaNumericLower(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        char lower = std::tolower(c);
        if (std::isalnum(lower)) result.push_back(lower);
    }
    return result;
}

bool fileContains(const std::string& sub, const std::string& file) {
    if (!fs::exists(file)) return false;

    std::ifstream f(file);
    if (!f) {
        std::cerr << "Error: fileContains - " << file << '\n';
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.find(sub) != std::string::npos) {
            return true;
//..............................................................................
        }
    }
    return false;
}

std::string fileContent(const std::string& file, const std::string& def = "") {
    if (!fs::exists(file)) return def;

    std::ifstream f(file);
    if (!f) {
        if (def.empty()) std::cerr << "Error: fileContent - " << file << '\n';
        return def;
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();
    if (!content.empty() && content.back() == '\n') content.pop_back();
    return content;
}

std::vector<std::string> fileContentLines(const std::string& file) {
    std::vector<std::string> lines;
    if (!fs::exists(file)) return lines;

    std::ifstream f(file);
    if (!f) {
        std::cerr << "Error: fileContentLines - " << file << '\n';
        return lines;
    }
//..............................................................................
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
    }
    return lines; // vector
}

std::string fileEmbedded(const std::string& file) {
    std::string filename;
    if (S["Artist"].empty() || S["Album"].empty()) {
        std::filesystem::path p = file;
        filename                = p.filename().string();
    } else {
        filename                = S["Artist"] + S["Album"];
    }
    return "/data/shm/embedded/"+ alphaNumericLower(filename);
}

int64_t epochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
}

int64_t epochS() {
    return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
}

void stateSet() {
    V.STOP  = false;
    V.PLAY  = false;
    V.PAUSE = false;
         if (V.STATE == "stop") V.STOP  = true;
    else if (V.STATE == "play") V.PLAY  = true;
    else                        V.PAUSE = true;
}