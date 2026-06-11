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
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

bool
    json        = true,
    snapclient  = false,
    stream      = false,
    volumenone  = false,
    webradio    = false,
    ws_push     = false,
    
    AIRPLAY     = false,
    BLUETOOTH   = false,
    MPD         = false,
    SNAPCAST    = false,
    SPOTIFY     = false,
    UPNP        = false;
int
    bitdepth   = 0,
    bitrate    = 0,
    elapsed    = 0,
    pllength   = 0,
    pos        = 0,
    samplerate = 0,
    Time       = 0,
    volume     = 0;
int64_t
    start      = 0,
    timestamp  = 0;
std::string
    coverart,
    control,
    dir_data   = "/srv/http/data/",
    dir_radio,
    dir_shm      = dir_data +"shm/",
    dir_system   = dir_data +"system/",
    ext,
    file_cover,
    file_radio,
    icon,
    mixer,
    player,
    position,
    sampling,
    state,
    station,
    stationcover,
    uri,
    uri_ini,
    url,
    ws_status;
    
std::filesystem::path F;

std::unordered_map<std::string, bool> B;

std::unordered_map<std::string, std::string> S;

std::unordered_map<std::string, int> I;

std::vector<std::string>
    key_BI = {"elapsed", "pllength", "song",     "Time",      "volume",   "webradio"},
    key_S  = {"Album",   "Artist",   "Composer", "Conductor", "coverart", "file",
              "icon",    "player",   "sampling", "station",   "state",    "Title"},
    vector;

std::string alphaNumericLower(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        char lower = std::tolower(c);
        if (std::isalnum(lower)) result.push_back(lower);
    }
    return result;
}

bool fileExists(const std::string& file) {
    return std::filesystem::exists(file);
}

bool fileContains(const std::string& sub, const std::string& file) {
    std::ifstream f(file);
    if (!f) return false;
    
    std::string line;
    while (std::getline(f, line)) {
        if (line.find(sub) != std::string::npos) {
            f.close();
            return true;
//..............................................................................
        }
    }
    f.close();
    return false;
}

std::string fileContent(const std::string& file, const std::string& def = {}) {
    std::ifstream f(file);
    if (!f) return def;
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();
    if (!content.empty() && content.back() == '\n') content.pop_back();
    return content;
}

std::vector<std::string> fileContentLines(const std::string& file) {
    std::vector<std::string> lines;
    std::ifstream file_object(file);
    if (!file_object.is_open()) return lines;
//..............................................................................
    std::string line;
    while (std::getline(file_object, line)) {
        lines.push_back(line);
    }
    return lines; // vetor
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
