
// compile script: ./compile.sh

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

#define VERSION_STRING __TIME__ " " __DATE__

bool hasData(const std::string& k) {
    return S.find(k) != S.end() && !S[k].empty();
}

void fileCover(const std::string& file) {
    bool is_dir = false;
    std::string directory;
    fs::path path(file);
    if (fs::is_regular_file(path)) {
        directory = path.parent_path().string();
    } else {
        is_dir    = true;
        directory = file;
    }
    if (!fs::exists(directory)) return;

    // static unordered_set - allocated only once in memory
    static const std::unordered_set<std::string> names = {"cover", "album", "folder", "front"};
    static const std::unordered_set<std::string> exts  = {".jpg", ".png", ".gif"};
    std::string wildcard;
    for (const auto& file : fs::directory_iterator(directory)) {
        if (!file.is_regular_file()) continue;

        wildcard = file.path().extension().string(); // name[.ext]
        if (!exts.count(wildcard)) continue; // O(1)

        wildcard = file.path().stem().string(); // [name].ext
        if (names.count(wildcard)) {
            std::string path = file.path().string();
            if (path.starts_with("/srv")) path.erase(0, 9);
            if (V.WEBRADIO) {
                V.STATIONCOVER = path;
            } else {
                V.COVERART = path;
            }
            break;
        }
    }
    if (!V.COVERART.empty() || is_dir) return;
    
    std::string file_embedded;
    if (access(directory.c_str(), W_OK) == 0) {
        file_embedded = directory +"/cover"; // extract to .../cover.jpg(png)
    } else { // fallback if not writeable
        file_embedded = fileEmbedded(file); // get already extracted
        for (const std::string& ext : {".jpg", ".png"}) {
             if (fs::exists("/srv/http"+ file_embedded + ext)) {
                 V.COVERART = file_embedded + ext;
                 return;
             }
        }
    }
    
    AudioData AD = Utils::readFile(file, true);
    if (AD.error) return;

    AudioEmbedded AE = getEmbeddedAudio(AD);
    V.COVERART       = extractEmbedded(AD, AE, true, file, file_embedded);
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
                     if (key == "Artist")       S["Artist"]    = value;
                else if (key == "Title")        S["Title"]     = value;
                else if (key == "Album")        S["Album"]     = value;
                else if (key == "coverart")     V.COVERART     = value;
                else if (key == "state")        V.STATE        = value;
                else if (key == "sampling")     V.SAMPLING     = value;
                else if (key == "station")      V.STATION      = value;
                else if (key == "stationcover") V.STATIONCOVER = value;
                else if (key == "elapsed")      V.ELAPSED      = std::stoi(value);
                else if (key == "start")        V.START        = std::stoi(value);
                else if (key == "Time")         V.TIME         = std::stoi(value);

                stateSet();
            }
        }
    }
}

std::string samplingString() {
    if (V.SAMPLERATE == 0) return {};
    
    std::string sampling;
    if (V.BITDEPTH == 1) { // dsd
        uint32_t base = (V.SAMPLERATE % 48000 == 0) ? 48000 : 44100;
        sampling      = std::format("DSD{} {:.3f} MHz", V.SAMPLERATE / base, V.SAMPLERATE / 1000000.0);
    } else {
        if (V.BITDEPTH > 0)   sampling  = std::format("{} bit ", V.BITDEPTH);
                              sampling += std::format("{:.1f} kHz", V.SAMPLERATE / 1000.0);
        if (V.BITRATE > 0)    sampling += std::format(" {} kbit/s", V.BITRATE);
    }
    return sampling;
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
        std::string ip = fileContent(DIR.SHM +"snapserverip");
        kv = wsSend(ip, "status"); // websocket server status -k > reply key=value
        S["snapserverip"] = fileContent(DIR.SHM +"snapserverip");
    } else {
        if (V.AIRPLAY) {
            V.EXT = "AirPlay";
            std::string v;
            for (const std::string& k : {"Album", "Artist", "coverart", "elapsed", "start", "state", "Time", "Title"}) {
                v = fileContent(DIR.SHM +"airplay/"+ k);
                if (!v.empty()) kv += k +'='+ v +'\n';
            }
            V.SAMPLING = "16 bit 44.1 kHz 1.41 Mbit/s";
        } else if (V.SPOTIFY) {
            V.EXT = "Spotify";
            kv         = fileContent(DIR.SHM +"spotify/status");
            V.SAMPLING = "48 kHz 320 kbit/s";
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
            default:              V.STATE = "stop";  break;
        }
        stateSet();
        if (V.PLAY) V.TIMESTAMP = epochMs();

        V.TIME     = mpd_status_get_total_time(status);
        V.POS      = mpd_status_get_song_pos(status);
        V.PLLENGTH = mpd_status_get_queue_length(status);

        if (!V.STOP) {
            const mpd_audio_format *audio = mpd_status_get_audio_format(status);
            if (audio->bits == MPD_SAMPLE_FORMAT_DSD) {
                V.BITDEPTH   = 1;
                V.SAMPLERATE = audio->sample_rate * 8; // sample_rate in bytes
            } else {
                V.BITDEPTH   = audio->bits;
                V.SAMPLERATE = audio->sample_rate;
                V.BITRATE    = mpd_status_get_kbit_rate(status);
            }
        }

        B["updating"]  = mpd_status_get_update_id(status) > 0;
        B["consume"]   = mpd_status_get_consume(status);
        B["random"]    = mpd_status_get_random(status);
        B["repeat"]    = mpd_status_get_repeat(status);
        B["single"]    = mpd_status_get_single(status);
        I["crossfade"] = mpd_status_get_crossfade(status);

        I["pllength"]  = V.PLLENGTH;
        I["position"]  = V.POS;

        V.ELAPSED      = mpd_status_get_elapsed_time(status);
        V.VOLUME       = mpd_status_get_volume(status);

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
        V.URI     = mpd_song_get_uri(song);
        
        fs::path path(V.URI);
        fs::path parent = path.parent_path();
        if (parent.extension() == ".cue") {
            std::ifstream file("/mnt/MPD/" + parent.string());
            std::string line;
            while (std::getline(file, line)) {
                if (line.starts_with("FILE")) { // FILE "NAME.EXT" WAV
                    size_t q1 = line.find('"');
                    size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;

                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        std::string target_file = line.substr(q1 + 1, q2 - q1 - 1);
                        V.URI = (parent.parent_path() / target_file).string();
                        break;
                    }
                }
            }
        }
        
        V.FILE    = "/mnt/MPD/"+ V.URI;
        V.EXT     = V.FILE.extension().string().erase(0, 1);
        std::transform(V.EXT.begin(), V.EXT.end(), V.EXT.begin(), [](unsigned char c) {
            return std::toupper(c);
        });
        V.URI_INI = V.URI.substr(0, 4);
        std::unordered_set<std::string> scheme = {"http", "rtmp", "rtp:", "rtsp"};
        V.STREAM  = scheme.count(V.URI_INI) > 0;
        if (V.STOP) {
            V.TIME = mpd_song_get_duration(song);
            if (!V.STREAM && V.URI_INI != "cdda") {
                AudioData AD = Utils::readFile(V.FILE.c_str(), false);
                if (!AD.error) {
                    AudioMeta AM = getSampling(AD);
                    V.BITDEPTH   = AM.bitDepth;
                    V.SAMPLERATE = AM.sampleRate;
                }
            }
        }
        mpd_tag_type tags[] = { // keep order for status-coverartonline.sh
            MPD_TAG_ARTIST,
            MPD_TAG_TITLE,
            MPD_TAG_ALBUM,
            MPD_TAG_ALBUM_ARTIST,
            MPD_TAG_COMPOSER,
            MPD_TAG_CONDUCTOR
        };
        for (mpd_tag_type tag : tags) {
            const char* k = mpd_tag_name(tag);
            const char* v = mpd_song_get_tag(song, tag, 0);
// S[k] = v ? v : "";
            S.emplace(k, v ? v : "");
        }
        mpd_song_free(song);

        if (V.COVER && !V.STREAM) fileCover(V.FILE);
    }
};

int status() {
    V.PLAYER = fileContent(DIR.SHM +"player");
         if (V.PLAYER == "airplay")   V.AIRPLAY   = true;
    else if (V.PLAYER == "bluetooth") V.BLUETOOTH = true;
    else if (V.PLAYER == "mpd")       V.MPD       = true;
    else if (V.PLAYER == "snapcast")  V.SNAPCAST  = true;
    else if (V.PLAYER == "spotify")   V.SPOTIFY   = true;
    else if (V.PLAYER == "upnp")      V.UPNP      = true;

    if (V.MPD || V.UPNP) {
        struct statfs buf;
        if (fs::is_symlink(DIR.MPD) && statfs(DIR.MPD.c_str(), &buf) != 0) {
            std::system("timeout 1 mount -a &> /dev/null");
            if (statfs(DIR.MPD.c_str(), &buf) != 0) {
                std::cerr << "Shared Data server not found.\n";
                return 1;
            }

            std::system("systemctl start mpd");
        }

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

    std::ifstream file(DIR.SYSTEM +"display.json");
    if (!file) {
        std::cerr << "Error: display.json\n";
    } else {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("\"cover\": false")  != std::string::npos ||
                line.find("\"vumeter\": true") != std::string::npos) {
                V.COVER = false;
                break;
            }
        }
    }

    if (fs::exists(DIR.SHM +"btmixer") && !fs::exists(DIR.SYSTEM +"devicewithbt")) {
        V.CONTROL = fileContent(DIR.SHM +"btmixer");
        V.VOLUME  = getVolume("bluealsa", V.CONTROL);
    } else {
        if (fs::exists(DIR.SHM +"nosound") || fs::exists(DIR.SYSTEM +"mixernone")) {
            V.VOLUMENONE = true;
        } else {
            V.CONTROL = fileContent(DIR.SHM +"amixercontrol");
        }
    }

    if (V.URI_INI == "cdda") {
        V.EXT      = "CD";
        V.ICON     = "audiocd";
        V.SAMPLING = "16 bit 44.1 kHz 1.41 Mbit/s";
        
        std::string dir, discid, line, track;
        discid     = fileContent(DIR.SHM +"audiocd");
        if (!discid.empty()) {
            dir         = DIR.DATA +"audiocd/"+ discid;
            track       = V.URI.substr(V.URI.find(":///") + 4); // cdda:///N > N
            VECTOR      = fileContentLines(dir +"/data");       // 0:Album, 1:Artist, N+1:Artist^Title^^Time
            line        = VECTOR[stoi(track) + 1];
            std::vector<std::string> data;
            size_t start = 0;
            while (true) {
                size_t p = line.find("^^", start);
                if (p == std::string::npos) {
                    data.push_back(line.substr(start));
                    break;
                }
                data.push_back(line.substr(start, p - start));
                start = p + 2;
            }
            S["Album"]  = VECTOR[0];
            S["Artist"] = data[0];
            S["Title"]  = data[1];
            V.TIME      = std::stoi( data[2]);
            fileCover(dir);
        }
    } else if (V.STREAM) {
        if (V.UPNP) {
            V.EXT      = "UPnP";
        } else {
            V.WEBRADIO = true;
            size_t p   = V.URI.find("#charset");
            if (p != std::string::npos) V.URI.erase(p);
            bool rp_rf      = false;
            std::string dir_radio;
            if (V.URI_INI == "rtsp") {
                V.EXT      = "DAB";
                V.ICON     = "dabradio";
                dir_radio  = "dabradio/";
                V.SAMPLING = "16 bit 48 kHz 160 kbit/s";
            } else {
                V.EXT      = "Radio";
                dir_radio  = "webradio/";
                if (V.URI.find("icecast.radiofrance.fr") != std::string::npos) {
                    V.ICON = "radiofrance";
                    rp_rf  = true;
                } else if (V.URI.find("stream.radioparadise.com") != std::string::npos) {
                    V.ICON = "radioparadise";
                    rp_rf  = true;
                } else {
                    V.ICON = "webradio";
                }
            }
            
            std::string dir;
            std::string line;
            std::ifstream file_radio(DIR.MPD +"radio");
            while (std::getline(file_radio, line)) {
                if (line.starts_with(V.URI +"^^")) {
                    dir = line.substr(line.find("^^") + 2);
                    break;
                }
            }
            if (!dir.empty() && fs::exists(dir)) {
                V.STATION = fs::path(dir).filename().string();
                if (rp_rf && V.PLAY) V.EXT = V.STATION.substr(V.STATION.find(" - ") + 3);
                if (V.PLAY) V.SAMPLING = samplingString();
                if (V.SAMPLING.empty()) {
                    VECTOR = fileContentLines(dir +"/data");
                    if (VECTOR.size() > 1) V.SAMPLING = VECTOR[1];
                }
                if (V.COVER) fileCover(dir);
                S["station"]      = V.STATION;
                S["stationcover"] = V.STATIONCOVER;
            }
            
            if (V.PLAY) {
                if (rp_rf) { // radioparadise / radiofrance
                    if (fs::exists(DIR.SHM +"radio")) {
                        std::string status = fileContent(DIR.SHM +"status");
                        kv2var(status);
                    } else {
                        std::string cmd = "systemctl start "+ std::string(V.EXT == "DAB" ? "dab" : "radio") +" &> /dev/null &";
                        std::system(cmd.c_str());
                    }
                } else if (!S["Title"].empty()) {
                    std::string title = S["Title"];
                    std::string split;
                         if (title.find(" - ") != std::string::npos) split = " - ";
                    else if (title.find(": ")  != std::string::npos) split = ": ";
                    if (!split.empty()) {
                        size_t p    = title.find(split);
                        S["Artist"] = title.substr(0, p);
                        title       = title.substr(p + split.length());
                        size_t end  = title.find_last_not_of(' ');
                        if (end != std::string::npos) title.erase(end + 1);
                        S["Title"]  = title;
                    }
                }
            } else { // force reset
                V.ELAPSED   = 0;
                V.PAUSE     = false;
                V.STATE     = "stop";
                V.STOP      = true;
                S["Title"]  = "";
            }
        }
    }

    if (V.MPD && V.SAMPLING.empty()) V.SAMPLING = samplingString();
    if (V.SAMPLING.empty()) {
        V.SAMPLING  = V.EXT;
    } else {
        V.SAMPLING += " • "+ V.EXT;
    }
    if (V.MPD && V.PLLENGTH > 1) V.SAMPLING = std::format("{}/{} • {}", V.POS + 1, V.PLLENGTH, V.SAMPLING);
    
    if (V.COVER &&
        V.COVERART.empty() &&
        !S["Artist"].empty() &&
        (!S["Album"].empty() || !S["Title"].empty())
        ) {
        std::string name = alphaNumericLower(S["Artist"] + (S["Album"].empty() ? S["Title"] : S["Album"]));
        std::string file = "/data/shm/online/"+ name;
        for (const std::string& ext : {".jpg", ".png"}) {
             if (fs::exists("/srv/http"+ file + ext)) {
                 V.COVERART = file + ext;
                 break;
             }
        }
    }
    
    S["control"]  = V.CONTROL;
    S["coverart"] = V.COVERART;
    S["icon"]     = V.ICON.empty() && V.PLAYER != "mpd" ? V.PLAYER : V.ICON;
    S["file"]     = V.URI;
    S["player"]   = V.PLAYER;
    S["sampling"] = V.SAMPLING;
    S["state"]    = V.STATE;

    B["btsender"]     = fs::exists(DIR.SHM +"btmixer");
    B["librandom"]    = fs::exists(DIR.SYSTEM +"librandom");
    B["relays"]       = fs::exists(DIR.SYSTEM +"relays");
    B["relayson"]     = fs::exists(DIR.SHM +"relayson");
    B["scrobble"]     = fs::exists(DIR.SYSTEM +"scrobble");
    B["shareddata"]   = fs::exists("/mnt/MPD/NAS/data/sharedip");
    B["stoptimer"]    = fs::exists(DIR.SHM +"pidstoptimer");
    B["updateaddons"] = fs::exists(DIR.DATA +"addons/update");
    B["webradio"]     = V.WEBRADIO;

    B["pause"]        = V.PAUSE;
    B["play"]         = V.PLAY;
    B["stop"]         = V.STOP;

    I["elapsed"]      = V.ELAPSED;
    I["Time"]         = V.TIME;
    I["volume"]       = V.VOLUME;
    I["volumemute"]   = std::stoi(fileContent(DIR.SYSTEM +"volumemute",  "0"));

    bool volumelimit  = false;
    int volumemax     = 100;
    if (fs::exists(DIR.SYSTEM +"volumelimit")) {
        VECTOR = fileContentLines(DIR.SYSTEM +"volumelimit.conf");
        for (const std::string& l : VECTOR) {
            if (l.find("max") == 0) {
                volumemax   = std::stoi(l.substr(4));
                volumelimit = volumemax < 100;
                break;
            }
        }
    }
    B["volumelimit"]  = volumelimit;
    I["volumemax"]    = volumemax;

////////////////////////////////////////////////////////////////////////////////
    if (V.JSON && !V.SNAPCLIENT) { // page, counts, display
        std::cout
            << "{\n"
            << "  \"page\": false\n";
        std::string display = "{\n";
        VECTOR = {"ap", "camilladsp", "dabradio", "equalizer", "loginsetting", "multiraudio", "relays", "snapclient"};
        for (const std::string& k : VECTOR) {
            display += "  \""+ k +"\": "+ (fs::exists(DIR.SYSTEM + k) ? "true" : "false") +",\n";
        }
        display += "  \"volumenone\": "+ std::string(V.VOLUMENONE ? "true" : "false") +",\n"+
                    fileContent(DIR.SYSTEM +"display.json").substr(2); // "{\n" remove

        std::cout
            << ", \"counts\"    : " << fileContent(DIR.MPD +"counts") << '\n'
            << ", \"display\"   : " << display << '\n';
    }

    for (const auto& [k, v] : S) statusFormatString(k, v);
    for (const auto& [k, v] : I) statusFormat(k, v >= 0 ? std::to_string(v) : "false");
    for (const auto& [k, v] : B) statusFormat(k, v ? "true" : V.JSON ? "false" : "");

    if (V.PLAY) statusFormat("timestamp", std::to_string(V.TIMESTAMP));
    if (V.JSON && !V.SNAPCLIENT) std::cout << "}\n";
////////////////////////////////////////////////////////////////////////////////
    std::string file_play = DIR.SHM +"play";
    if (V.PLAY) {
        std::ofstream(file_play);
    } else {
        fs::remove(file_play);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) return status();

    std::string ARGV1 = argv[1];

    if (ARGV1 == "-I") {
        std::string inf;
        if (argc > 2) inf = argv[2];
        std::cout << ipAddress(inf) << '\n';
        return 0;
    }

    if (ARGV1 == "-v") {
        std::cout << "Build: " << VERSION_STRING << '\n';
        return 0;
    }

    if (ARGV1 == "-C") {
        std::string file = argv[2];
        if (argc > 3) {
            S["Artist"] = argv[3];
            S["Album"]  = argv[4];
        } else {
            V.GET_COVER = true;
        }
        fileCover(file);
        if (!V.COVERART.empty()) std::cout << V.COVERART << '\n';
        return 0;
    }

    if (ARGV1 == "-L") {
        std::string file = argv[2];
        AudioData AD = Utils::readFile(file, true);
        if (AD.error) return 1;

        AudioEmbedded AE = getEmbeddedAudio(AD);
        std::cout << extractEmbedded(AD, AE, false, file) << std::endl;
        return 0;
    }
    
    if (ARGV1 == "-Bp") {
        std::cout << UDP_PORT << '\n';
        return 0;
    }

    if (ARGV1 == "-B" || ARGV1 == "-W" || ARGV1 == "-P") {
        std::string ip = "127.0.0.1";
        std::string msg;
        if (argc == 2) { // no message - debug/test
            if (ARGV1 == "-W") {
                msg = "status [IP]";
            } else {
                std::string type = ARGV1 == "-B" ? "Broadcast to all servers." : "Push to clients of this server.";
                msg = "{ \"channel\": \"notify\", \"data\": { \"icon\": \"raudio\", \"title\": \"WebSocket\", \"message\": \""+ ARGV1 +"\" } }";
            }
            std::cout << "status " << ARGV1 << " " << msg << "\n\n";
        } else if (argc == 3) { // message
            msg = argv[2];
        } else if (argc == 4) { // ip + message
            char v0 = argv[2][0];
            if (v0 >= '0' && v0 <= '9') {ip  = argv[2]; if (argc > 3) msg = argv[3];}
            else                        {msg = argv[2]; if (argc > 3) ip  = argv[3];}
        }
        if (ARGV1 == "-B") return wsBroadcast(msg); // to all servers (then to each clients)
            
        if (ARGV1 == "-P") return wsPush(ip, msg);  // to clients of this server

        V.WS_STATUS = wsSend(ip, msg);
        if (V.WS_STATUS.empty()) return 1;

        std::cout << V.WS_STATUS << '\n';
        return 0;
    }

    if (ARGV1 == "-k") {
        V.JSON = false;
        return status();
    }

    V.SNAPCLIENT = true; // status-push on track changed / ws on each client refresh

    int ok_status = status();
    if (ok_status == 1) return 1;

    V.WS_STATUS.erase(0, 1);
    if (ARGV1 == "-o") {
        std::cout << "{"+ V.WS_STATUS +"}" << '\n';
        return 0;
    }

    V.WS_STATUS = "{\"channel\": \"mpdplayer\", \"data\": {"+ V.WS_STATUS +"}}";

    if (ARGV1 == "-p") return wsPush("127.0.0.1", V.WS_STATUS);

    if (ARGV1 == "-b") return wsBroadcast(V.WS_STATUS); // snapserver broadcast on change

    std::cerr
        << "\nPlayback status of rAudio\n\n"

        << "Usage: " << argv[0] << " [-o|-p|-b|-k]\n"
        << "        default: json format\n"
        << "          (with option: no counts and diaplay)\n"
        << "  -o    \n"
        << "  -p    websocket push      (normal push on change)\n"
        << "  -b    websocket broadcast (snapserver push on change)\n"
        << "  -k    key=value format    (snapserver data on client refresh)\n"
        << "  -v    version\n\n"

        << "Websocket: " << argv[0] << " [-W|-P|-B] [IP] [MESSAGE]\n"
        << "        default IP     : 127.0.0.1\n"
        << "  -P    push - exit immediately\n"
        << "  -B    broadcast\n"
        << "  -W    send - wait for reply\n\n"

        << "Coverart: " << argv[0] << " -C SOURCE_FILE/DIR\n"
        << "        1. file     : {cover, album, folder, front} + ext: {jpg, png, gif}\n"
        << "        2. embedded : if file not found and SOURCE_FILE not SOURCE_DIR,\n"
        << "                      extract as cover.jpg(png) in the same directory.\n\n"

        << "Embedded lyrics: " << argv[0] << " -L SOURCE_FILE\n\n"

        << "IP address: " << argv[0] << " -I [e|w]\n"
        << "        default: main\n"
        << "  e     ethernet\n"
        << "  w     wireless lan\n";
    return 1;
}
