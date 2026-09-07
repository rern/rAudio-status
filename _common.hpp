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

struct Dir {
    std::string
        DATA   = "/srv/http/data/",
        MPD    = DATA +"mpd/",
        SHM    = DATA +"shm/",
        SYSTEM = DATA +"system/";
};
Dir DIR;

struct Var {
    bool
        BT_MIXER    = fs::exists(DIR.SHM +"btmixer"),
        CAMILLADSP  = fs::exists(DIR.SYSTEM +"camilladsp"),
        COVER       = true,
        GET_COVER   = false,
        PAUSE       = false,
        PLAY        = false,
        STOP        = true,
        STREAM      = false,
        TRACK_ONLY  = false,
        VOLUME_NONE = fs::exists(DIR.SHM +"nosound") || fs::exists(DIR.SYSTEM +"mixernone"),
        WEBRADIO    = false,

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
        PL_LENGTH  = 0,
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
        SNAPSERVER_IP,
        STATE,
        STATION,
        STATIONART,
        TRACK,
        URI,
        URI_INI;
};
Var V;

std::unordered_map<std::string, bool>        B;
std::unordered_map<std::string, std::string> S;
std::unordered_map<std::string, int>         I;

std::vector<std::string>                VECTOR;

void escapeQuotes(std::string& v) {
    size_t extra = 0;
    for (char c : v) {
        if (c == '"') ++extra;
    }
    if (extra == 0) return;

    size_t old_size = v.size();
    size_t new_size = old_size + extra;
    v.resize(new_size);

    char* data = v.data(); // grab raw pointer once
    size_t i = old_size;
    size_t j = new_size;
    while (i > 0) {
        char c = data[--i];
        if (c == '"') {
            data[--j] = '"';
            data[--j] = '\\';
        } else {
            data[--j] = c;
        }
    }
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

std::string alphaNumericLower(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        char lower = std::tolower(c);
        if (std::isalnum(lower)) result.push_back(lower);
    }
    return result;
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

void json2var(std::string& json) {
    size_t i = 0, n = json.size();
    while (i < n && json[i] != '{') i++;
    i++;

    while (i < n) {
        while (i < n && (isspace(json[i]) || json[i] == ',')) i++;
        if (i >= n || json[i] == '}') break;

        // key (always quoted)
        i++; // skip opening quote
        size_t keyStart = i;
        while (json[i] != '"') i++;
        std::string key = json.substr(keyStart, i - keyStart);
        i++; // skip closing quote

        while (i < n && (isspace(json[i]) || json[i] == ':')) i++;

        // value
        std::string value;
        bool isString = false;
        if (json[i] == '"') { // Album, Artist, coverart, state, Title
            isString = true;
            i++;
            size_t valStart = i;
            while (json[i] != '"') i++; // note: doesn't handle escaped \" — see caveat below
            value = json.substr(valStart, i - valStart);
            escapeQuotes(value);
            if (key == "coverart") {
                V.COVERART = value;
            } else {
                S[key] = value;
            }
            i++;
        } else {
            size_t valStart = i;
            while (i < n && json[i] != ',' && json[i] != '}' && !isspace(json[i])) i++;
            value = json.substr(valStart, i - valStart);
            if (key == "pllength") {
                I[key] = std::stoi(value);
            } else if (key == "timestamp") {
                V.TIMESTAMP = std::stoll(value);
            } else {
                B[key] = value == "true";
            }
        }
    }
}
