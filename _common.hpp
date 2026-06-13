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
        POSITION,
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

std::string fileContent(const std::string& file, const std::string& def = {}) {
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

bool fileExists(const std::string& file) {
    return fs::exists(file);
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
         if (V.STATE == "stop") {V.STOP = true;  V.PLAY = false; V.PAUSE = false;}
    else if (V.STATE == "play") {V.STOP = false; V.PLAY = true;  V.PAUSE = false;}
    else                        {V.STOP = false; V.PLAY = false; V.PAUSE = true;}
}
