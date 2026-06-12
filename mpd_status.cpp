// g++ -O2 mpd_status.cpp -o /srv/http/bash/status \
    $( pkg-config --cflags --libs alsa,dbus-1,libcurl,libmpdclient,libupnpp,taglib )

#include "_common.hpp"

#include <mpd/client.h>

#include "audio_format.hpp"
#include "audio_sampling.hpp"
#include "alsa_volume.hpp"
#include "bluez_meta.hpp"
#include "embedded_meta.hpp"
#include "ip_hostname.hpp"
#include "upnp_coverart.hpp"
#include "websocket.hpp"

std::string fileCover(const std::string& file) {
    namespace fs = std::filesystem;
    fs::path pathObj(file);
    std::string directory = pathObj.parent_path().string();
    if (!fs::exists(directory)) return {};

    // Use static unordered_set so they are allocated only ONCE in memory
    static const std::unordered_set<std::string> names = {"cover", "album", "folder", "front"};
    static const std::unordered_set<std::string> exts  = {".jpg", ".png", ".gif"};
    std::string w;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        w = entry.path().extension().string(); // name[.ext]
        if (!exts.count(w)) continue; // O(1)

        w = entry.path().stem().string(); // [name].ext
        if (names.count(w)) return entry.path().string();
    }
    return {};
}

bool hasData(const std::string& k) {
    return S.find(k) != S.end() && !S[k].empty();
}

bool inKey(const std::string& k, const std::vector<std::string>& vector) {
    return std::find(vector.begin(), vector.end(), k) != vector.end();
}

void kv2var(const std::string& kv) {
    if (kv.empty()) return;
    
    std::istringstream iss(kv);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;

        std::string key, value;
        std::istringstream iss(line);

        if (std::getline(iss, key, '=')) {
            if (std::getline(iss, value)) {
                if (value.empty()) continue;
                
                if (value.front() == '"' && value.back() == '"') { // strip 1st " and  " last quotes
                    value = value.substr(1, value.size() - 2);
                }
                std::string::size_type p = 0;
                while ((p = value.find("\\\"", p)) != std::string::npos) { // unescape quotes \"
                    value.replace(p, 2, "\"");
                    p += 1; // move past the replaced quote
                }
                     if (key == "Album")        S["Album"]     = value;
                else if (key == "Artist")       S["Artist"]    = value;
                else if (key == "Title")        S["Title"]     = value;
                else if (key == "coverart")     V.COVERART     = value;
                else if (key == "state")        V.STATE        = value;
                else if (key == "elapsed")      V.ELAPSED      = std::stoi(value);
                else if (key == "start")        V.START        = std::stoi(value);
                else if (key == "Time")         V.TIME         = std::stoi(value);
                else if (key == "sampling")     V.SAMPLING     = value;
                else if (key == "station")      V.STATION      = value;
                else if (key == "stationcover") V.STATIONCOVER = value;
                
                stateSet();
            }
        }
    }
}

void statusFormat(const std::string& k, const std::string& v) {
    std::vector<std::string> key_BI = {"elapsed", "pllength", "song", "Time", "volume", "webradio"};
    if (!V.JSON && !inKey(k, key_BI)) return;
    
    std::string kv;
    if (V.JSON) {
        kv = ", \""+ k +"\": "+ v;
        if (V.SNAPCLIENT) { V.WS_STATUS += kv; return; }
        
    } else {
        kv = k +'='+ v;
    }
    std::cout << kv << '\n';
}

void statusFormatString(const std::string& k, std::string v) {
    std::vector<std::string> key_S = {"Album", "Artist", "Composer", "Conductor", "coverart", "file",
                                      "icon",  "player", "sampling", "station",   "state",    "Title"};
    if (!V.JSON && !inKey(k, key_S)) return;
    
    if (v.find('\"') != std::string::npos) { // escape double quotes
        std::string value;
        value.reserve(std::string_view(v).size() * 1.1);
        for (const char *p = v.c_str(); *p != '\0'; ++p) {
            if (*p == '"') value.push_back('\\');
            value.push_back(*p);
        }
        v = value;
    }
    std::string kv;
    if (V.JSON) {
        kv = ", \""+ k +"\": \""+ v +'"';
        if (V.SNAPCLIENT) { V.WS_STATUS += kv; return; }
        
    } else if (v.find(' ') != std::string::npos) {
        kv = k +"=\""+ v +'"';
    } else {
        kv = k +'='+ v;
    }
    std::cout << kv << '\n';
}

void rendererStatus() {
    V.TIMESTAMP = epochMs();
    if (V.BLUETOOTH) {
        bluezMeta();
        return;
    }
    
    std::string kv;
    if (V.SNAPCAST) {                // V.SNAPCLIENT js: REFRESHDATA() > PLAYBACK.get()
        std::string ip = fileContent(V.DIR_SHM +"snapserverip");
        kv = wsSend(ip, "status"); // websocket server status -k > reply key=value
    } else {
        if (V.AIRPLAY) {
            std::string v;
            for (const std::string& k : {"Album", "Artist", "coverart", "elapsed", "start", "state", "Time", "Title"}) {
                v = fileContent(V.DIR_SHM +"airplay/"+ k);
                if (!v.empty()) kv += k +'='+ v +'\n';
            }
            V.SAMPLING  = "16 bit 44.1 kHz 1.41 Mbit/s • AirPlay";
        } else if (V.SPOTIFY) {
            kv = fileContent(V.DIR_SHM +"spotify/status");
            V.SAMPLING  = "48 kHz 320 kbit/s • Spotify";
        }
        if (V.PLAY && V.ELAPSED) V.ELAPSED = epochS() - V.START + 1;
    }
    if (!kv.empty()) kv2var(kv);
}

class MPDclient {
private:
    mpd_connection *conn = nullptr;

public:
    MPDclient() {
        conn = mpd_connection_new(nullptr, 0, 30000);
    }

    ~MPDclient() {
        if (conn) mpd_connection_free(conn);
    }

    bool ok() {
        return conn && mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS;
    }
    
    void runStatus() {
        mpd_status *status = mpd_run_status(conn);
        if (status == nullptr) return;
//..............................................................................
        switch (mpd_status_get_state(status)) {
            case MPD_STATE_PLAY:  V.STATE = "play";  break;
            case MPD_STATE_PAUSE: V.STATE = "pause"; break;
            case MPD_STATE_STOP:  V.STATE = "stop";  break;
        }
        stateSet();
        if (V.PLAY) V.TIMESTAMP = epochMs();
        
        V.TIME     = mpd_status_get_total_time(status);
        V.POS      = mpd_status_get_song_pos(status);
        V.PLLENGTH = mpd_status_get_queue_length(status);
        
        const mpd_audio_format *audio = mpd_status_get_audio_format(status);
        if (audio != nullptr) {
            V.BITDEPTH   = audio->bits;
            V.SAMPLERATE = audio->sample_rate;
            V.BITRATE    = mpd_status_get_kbit_rate(status);
        }
        
        B["updating_db"] = mpd_status_get_update_id(status) > 0;
        B["consume"]     = mpd_status_get_consume_state(status) == MPD_CONSUME_ON;
        B["random"]      = mpd_status_get_random(status);
        B["repeat"]      = mpd_status_get_repeat(status);
        B["single"]      = mpd_status_get_single_state(status) == MPD_SINGLE_ON;
        I["crossfade"]   = mpd_status_get_crossfade(status);
        
        I["pllength"]    = V.PLLENGTH;
        I["song"]        = V.POS;
        
        V.ELAPSED          = mpd_status_get_elapsed_time(status);
        V.VOLUME           = mpd_status_get_volume(status);
        
        mpd_status_free(status);
    }
    
    void runCurrentSong() {
        int i = 0;
        mpd_song* song = nullptr;
        while ((song = mpd_run_current_song(conn)) == nullptr && i < 3) { // not yet played - no current song
            if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) return;
//..............................................................................
            if ( i == 0 ) {                                               // trigger play-stop once
                mpd_run_play(conn);
                mpd_run_stop(conn);
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ++i;
        }
        V.URI      = mpd_song_get_uri(song);
        V.FILE_COVER = "/mnt/MPD/"+ V.URI;
        V.URI_INI  = V.URI.substr(0, 4);
        V.STREAM   = V.URI_INI == "http" || V.URI_INI == "rtmp" || V.URI_INI == "rtp:" || V.URI_INI == "rtsp";
        if (V.STOP) V.TIME = mpd_song_get_duration(song); // 0 / false
        mpd_tag_type tags[] = {
            MPD_TAG_ARTIST,
            MPD_TAG_ALBUM,
            MPD_TAG_ALBUM_ARTIST,
            MPD_TAG_COMPOSER,
            MPD_TAG_CONDUCTOR,
            MPD_TAG_TITLE
        };
        for (mpd_tag_type tag : tags) {
            const char* k = mpd_tag_name(tag);
            const char* v = mpd_song_get_tag(song, tag, 0);
// S[k]
            S.emplace(k, v ? v : ""); // S[k] = v ? v : "";
        }
        mpd_song_free(song);
    }
};

int status() {
    V.PLAYER = fileContent(V.DIR_SHM +"player");
         if (V.PLAYER == "airplay")   V.AIRPLAY   = true;
    else if (V.PLAYER == "bluetooth") V.BLUETOOTH = true;
    else if (V.PLAYER == "mpd")       V.MPD       = true;
    else if (V.PLAYER == "snapcast")  V.SNAPCAST  = true;
    else if (V.PLAYER == "spotify")   V.SPOTIFY   = true;
    else if (V.PLAYER == "upnp")      V.UPNP      = true;
            
    if (V.MPD || V.UPNP) {
        MPDclient MPD;
        if (!MPD.ok()) {
            std::cerr << "MPD connection failed\n";
            return 1;
//..............................................................................
        }
        MPD.runStatus();
        if (V.PLLENGTH) {
            MPD.runCurrentSong();
        } else {
            S["hostname"] = hostName();
            S["ip"]       = ipAddress();
        }
    } else {
        rendererStatus();
    }
    
    if (fileExists(V.DIR_SHM +"btmixer") && !fileExists(V.DIR_SYSTEM +"devicewithbt")) {
        V.CONTROL = fileContent(V.DIR_SHM +"btmixer");
        V.VOLUME  = getVolume("bluealsa", V.CONTROL);
    } else {
        V.CONTROL = fileContent(V.DIR_SHM +"amixercontrol");
        if (V.CONTROL == "none" || fileExists(V.DIR_SHM +"nosound")) {
            V.VOLUMENONE = true;
        } else if (fileContains("mixertype=hardware", V.DIR_SHM +"output")) {
            V.VOLUME = getVolume("default", V.CONTROL);
        }
    }
        
    if (V.URI_INI == "cdda") {
        V.EXT      = "CD";
        V.ICON     = "audiocd";
        V.SAMPLING = "16 bit 44.1 kHz 1.41 Mbit/s";
        std::string
            discid = fileContent(V.DIR_SHM +"audiocd"),
            track  = V.URI.substr(V.URI.find("://") + 3); // cdda://N > N
        kv2var(fileContent(V.DIR_DATA +"audiocd/"+ discid +'/'+ track));
    } else if (V.STREAM) {
        if (V.UPNP) {
            V.EXT      = "UPnP";
        } else {
            V.WEBRADIO = true;
            if (V.URI.ends_with("#charset")) V.URI.resize(V.URI.length() - 8);
            std::string url = V.URI;
            std::string dir_radio;
            if (V.URI_INI == "rtsp") {
                V.EXT       = "DAB";
                V.ICON      = "dabradio";
                dir_radio = "dabradio/";
                if (V.STOP) V.SAMPLING = "48 kHz";
            } else {
                V.EXT       = "Radio";
                dir_radio = "webradio/";
                std::replace(url.begin(), url.end(), '/', '|');
                if (url.find("icecast.radiofrance.fr") != std::string::npos) {
                    V.ICON = "radiofrance";
                } else if (url.find("stream.radioparadise.com") != std::string::npos) {
                    V.ICON = "radioparadise";
                } else {
                    V.ICON = "webradio";
                }
                if (V.PLAY && V.ICON != "webradio") { // radiofrance / radioparadise
                    if (!V.STATION.empty()) V.EXT = V.STATION.substr(V.STATION.find(" - ") + 3);
                    if (fileExists(V.DIR_SHM +"radio")) {
                        std::string status = fileContent(V.DIR_SHM +"status");
                        kv2var(status);
                    } else {
                        std::string cmd = "systemctl start "+ std::string(V.EXT == "DAB" ? "dab" : "radio") +" &> /dev/null &";
                        std::system(cmd.c_str());
                    }
                }
            }
            for (const std::string& x : {".jpg", ".png", ".gif"}) {
                std::string f = V.DIR_DATA + dir_radio +"img/"+ url + x;
                if (fileExists(f)) {
                    V.STATIONCOVER = f.substr(9);
                    break;
                }
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(V.DIR_DATA + dir_radio)) {
                if (entry.is_regular_file() && entry.path().filename() == url) {
                    VECTOR = fileContentLines(entry.path().string());
                    if (VECTOR.size()) {
                        V.STATION = VECTOR[0];
                        if (VECTOR.size() > 1) V.SAMPLING = VECTOR[1];
                    }
                    break;
                }
            }
        }
    } else if (V.PLLENGTH || V.SNAPCLIENT) {
        V.COVERART = fileCover(V.FILE_COVER);
        V.EXT      = V.FILE_COVER.extension().string().erase(0, 1);
        std::transform(V.EXT.begin(), V.EXT.end(), V.EXT.begin(), [](unsigned char c) {
            return std::toupper(c);
        });
        if (V.COVERART.empty() || V.STOP) {
            AudioData AD = Utils::readFile(V.FILE_COVER.c_str(), false);
            if (!AD.error) {
                if (V.STOP) {
                    AudioMeta AM = getSampling(AD);
                    V.SAMPLERATE   = AM.sampleRate;
                    V.BITDEPTH     = AM.bitDepth;
                }
                if (V.COVERART.empty()) {
                    AudioEmbedded AE = getEmbeddedAudio(AD);
                    V.COVERART       = extractEmbedded(AD, AE, true, V.FILE_COVER);
                }
            }
        }
    }
    if (V.SAMPLING.empty()) {
        if (V.BITDEPTH)   V.SAMPLING += std::to_string(V.BITDEPTH) +"bit ";
        if (V.SAMPLERATE) V.SAMPLING += std::format("{:.1f}", V.SAMPLERATE / 1000.0) +" kHz";
        if (V.BITRATE)    V.SAMPLING += " "+ std::to_string(V.BITRATE) +" kHz";
    }
    V.SAMPLING += V.SAMPLING.empty() ? V.EXT : " • "+ V.EXT;
    if (V.PLLENGTH > 1) V.POSITION = std::to_string(V.POS + 1) +"/"+ std::to_string(V.PLLENGTH) +" • ";
    
    V.ALBUM  = hasData("Album");
    V.ARTIST = hasData("Artist");
    V.TITLE  = hasData("Title");
    if (V.COVERART.empty()) {
        std::string path_no_ext;
        if (V.ARTIST && (V.ALBUM || V.TITLE)) { // get already fetched
            std::string path_no_ext = V.DIR_SHM +"online/";
            path_no_ext += alphaNumericLower(S["Artist"] + S[V.ALBUM ? "Album" : "Title"]);
            for (const std::string& x : {".jpg", ".png"}) {
                if (fileExists(path_no_ext + x)) {
                    V.COVERART = path_no_ext.substr(9) + x;
                    break;
                }
            }
            if (V.COVERART.empty() && V.UPNP) coverartUpnp(path_no_ext);
        }
    }
    if (V.ICON.empty() && V.PLAYER != "mpd") V.ICON = V.PLAYER;

    S["control"]  = V.CONTROL;
    S["coverart"] = V.COVERART;
    S["ext"]      = V.EXT;
    S["icon"]     = V.ICON;
    S["file"]     = V.URI;
    S["player"]   = V.PLAYER;
    S["position"] = V.POSITION;
    S["sampling"] = V.SAMPLING;
    S["state"]    = V.STATE;
    if (V.WEBRADIO) {
        S["station"]      = V.STATION;
        S["stationcover"] = V.STATIONCOVER;
    }
    if (V.SNAPCLIENT) S["snapserverip"] = fileContent(V.DIR_SHM +"snapserverip");
    
    B["btsender"]     = fileExists(V.DIR_SHM +"btmixer");
    B["librandom"]    = fileExists(V.DIR_SYSTEM +"librandom");
    B["relays"]       = fileExists(V.DIR_SYSTEM +"relays");
    B["relayson"]     = fileExists(V.DIR_SHM +"relayson");
    B["scrobble"]     = fileExists(V.DIR_SYSTEM +"scrobble");
    B["shareddata"]   = fileExists("/mnt/MPD/NAS/data/sharedip");
    B["stoptimer"]    = fileExists(V.DIR_SHM +"pidstoptimer");
    B["updateaddons"] = fileExists(V.DIR_DATA +"addons/update");
    B["stream"]       = V.STREAM;
    B["webradio"]     = V.WEBRADIO;
    
    B["pause"]        = V.PAUSE;
    B["play"]         = V.PLAY;
    B["stop"]         = V.STOP;
    
    I["elapsed"]    = V.ELAPSED;
    I["Time"]       = V.TIME;
    I["volume"]     = V.VOLUME;
    I["volumemute"] = std::stoi(fileContent(V.DIR_SYSTEM +"volumemute",  "0"));
    I["volumemax"]  = std::stoi(fileContent(V.DIR_SYSTEM +"volumelimit", "-1"));
    
////////////////////////////////////////////////////////////////////////////////
    if (V.JSON && !V.SNAPCLIENT) { // page, counts, display
        std::cout
            << "{\n"
            << "  \"page\": false\n";
        std::string display = "{\n";
        VECTOR = {"ap", "camilladsp", "dabradio", "equalizer", "loginsetting", "multiraudio", "relays", "snapclient"};
        for (const std::string& k : VECTOR) {
            display += "  \""+ k +"\": "+ (fileExists(V.DIR_SYSTEM + k) ? "true" : "false") +",\n";
        }
        display += "  \"volumenone\": "+ std::string(V.VOLUMENONE ? "true" : "false") +",\n"+
                    fileContent(V.DIR_SYSTEM +"display.json").substr(2); // "{\n" remove
        
        std::cout
            << ", \"counts\"    : " << fileContent(V.DIR_DATA +"mpd/counts") << '\n'
            << ", \"display\"   : " << display << '\n';
    }
    
    for (const auto& [k, v] : S) statusFormatString(k, v);
    for (const auto& [k, v] : I) statusFormat(k, v >= 0 ? std::to_string(v) : "false");
    for (const auto& [k, v] : B) statusFormat(k, v ? "true" : V.JSON ? "false" : "");
    
    if (V.PLAY) statusFormat("timestamp", std::to_string(V.TIMESTAMP));
    if (V.JSON && !V.SNAPCLIENT) std::cout << "}\n";
////////////////////////////////////////////////////////////////////////////////
    std::string file_play = V.DIR_SHM +"play";
    if (V.PLAY) {
        std::ofstream(file_play);
    } else {
        std::filesystem::remove(file_play);
    }
    
    if (V.COVERART.empty() && V.ARTIST && (V.ALBUM || V.TITLE)) { // online coverart (in background)
        std::string args = S["Artist"] +'\n';
        if (V.ALBUM) {
            args += S["Album"] +"\nCMD ARTIST ALBUM";
        } else {
            args += S["Title"] +"\nCMD ARTIST TITLE";
        }
        std::string cmd = "/srv/http/bash/status-coverartonline.sh \"cmd\n"+ args +"\" &> /dev/null &";
        std::system(cmd.c_str());
    }
    return 0;
}

std::string ARGV1;
enum Option { EMBEDDED, IP, HELP, STATUS, WEBSOCKET };
Option parseOption(const std::string& argv1) {
    ARGV1 = argv1;
    if (argv1 == "-c") return EMBEDDED;
    if (argv1 == "-h") return HELP;
    if (argv1 == "-i") return IP;
    if (argv1 == "-l") return EMBEDDED;
    if (argv1 == "-w") return WEBSOCKET;
    if (argv1 == "-W") return WEBSOCKET;
                       return STATUS;
}

int main(int argc, char **argv) {
    Option opt = argc == 1 ? STATUS : parseOption(argv[1]);
    switch (opt) {
        case EMBEDDED: {
            if (argc == 2) {
                std::cerr << "Error: Target file missing\n";
                return 1;
            }
            
            std::string file = argv[2];
            AudioData AD = Utils::readFile(file, true);
            if (AD.error) {
                std::cerr << "Error: Read file\n";
                return 1;
            }
            
            AudioEmbedded AE = getEmbeddedAudio(AD);
            std::cout << extractEmbedded(AD, AE, ARGV1 == "-c", file);
            return 0;
        }
        case IP:
            std::cout << ipAddress() << '\n';
            return 0;
        case HELP:
            std::cerr
                << "\nGet status and data for rAudio\n\n"
                << "Usage: " << argv[0] << " [OPTION]\n"
                << "                 json format\n"
                << "  -k             key=value format (no counts and display)\n" // snapserver reply - client refresh
                << "  -s             ws broadcast json(no counts and display)\n" // snapserver push  - server change
                << "  -so              stdout instead of broadcast\n\n"
                
                << "  -l <FILE>      extract embedded lyrics to stdout\n"
                << "  -c <FILE>      extract embedded coverart to cover.jpg/png\n"
                << "                 and save to directory of FILE\n\n"
                
                << "  -w [IP] [MSG]  websocket: send - wait for reply\n"
                << "  -W [IP] [MSG]  websocket: send only - exit immediately (push)\n"
                << "                 IP default: 127.0.0.1 (localhost)\n\n"
                
                << "  -i             get system IP address\n";
            return 0;
        case WEBSOCKET: {
            std::string
                ip  = "127.0.0.1",
                msg = "ping";
            if (argc > 2) {
                char v0 = argv[2][0];
                if (v0 >= '0' && v0 <= '9') {ip  = argv[2]; if (argc > 3) msg = argv[3];}
                else                        {msg = argv[2]; if (argc > 3) ip  = argv[3];}
            }
            if (ARGV1 == "-W") {
                return wsPush(ip, msg);
            } else {
                V.WS_STATUS = wsSend(ip, msg);
                if (V.WS_STATUS.empty()) return 1;
                
                std::cout << V.WS_STATUS << '\n';
                return 0;
            }
        }
        case STATUS: {
            if (argc > 1) {
                     if (ARGV1 == "-k")                   V.JSON       = false;
                else if (ARGV1 == "-s" || ARGV1 == "-so") V.SNAPCLIENT = true; // status-push on track changed / ws on each client refresh
            }
            int ok_status = status(); // std::cout in function
            if (!V.SNAPCLIENT || ok_status == 1) return ok_status;
            
            V.WS_STATUS.erase(0, 1);
            V.WS_STATUS = "{\"channel\": \"mpdplayer\", \"data\": {"+ V.WS_STATUS +"}}";
            if (ARGV1 == "-so") { // -so
                std::cout << V.WS_STATUS << '\n';
                return 0;
            }
            
            return wsBroadcast(V.WS_STATUS); // snapserver broadcast on change
        }
    }
}
