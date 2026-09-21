#pragma once

#include <systemd/sd-bus.h>

std::string get_prop(sd_bus* bus
                    , const char* prop
                    , const char* inf = "org.gnome.ShairportSync.RemoteControl") {
    sd_bus_message* m = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char* val = nullptr;

    int r = sd_bus_get_property_string(bus,
                                       "org.gnome.ShairportSync",
                                       "/org/gnome/ShairportSync",
                                       inf,
                                       prop,
                                       &error,
                                       &val);
    
    std::string result = (r >= 0) ? val : "Unknown";
    free(val);
    return result;
}

void getMetadata(sd_bus* bus) {
    sd_bus_message* m = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_get_property(bus,
                                "org.gnome.ShairportSync",
                                "/org/mpris/MediaPlayer2",
                                "org.mpris.MediaPlayer2.Player",
                                "Metadata",
                                &error,
                                &m,
                                "a{sv}");
    if (r < 0) return;
    
    sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    const char* key;
    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        sd_bus_message_read(m, "s", &key);
        std::string s_key(key);
        
        if (s_key == "xesam:album") {
            const char* album;
            sd_bus_message_read(m, "v", "s", &album);
            S["Album"] = album;
            
        } else if (s_key == "xesam:artist") {
            sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as");
            sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s");
            const char* artist;
            sd_bus_message_read(m, "s", &artist); // index 0 only
            S["Artist"] = artist;
            sd_bus_message_exit_container(m); // array
            sd_bus_message_exit_container(m); // variant
            
        } else if (s_key == "xesam:title") {
            const char* title;
            sd_bus_message_read(m, "v", "s", &title);
            S["Title"] = title;
            
        } else if (s_key == "mpris:artUrl") {
            const char* arturl;
            sd_bus_message_read(m, "v", "s", &arturl);
            std::string file = arturl;
            if (!file.empty()) {
                fs::path source = file.substr(7); 
                fs::path target = DIR.SHM +"/coverart";
                std::error_code ec;
                fs::path source_current = fs::read_symlink(target, ec);
                if (ec || source_current != source) { // ec - not exists
                    fs::remove(target, ec);
                    fs::create_symlink(source, target);
                }
                V.COVERART = "/data/shm/coverart";
            }
            
        } else if (s_key == "mpris:length") {
            int64_t time = 0;
            sd_bus_message_read(m, "v", "x", &time);
            V.TIME = (time + 500000) / 1000000;
            
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    sd_bus_message_unref(m);
}

void shairportMeta(sd_bus* bus) {
    std::string state = get_prop(bus, "PlayerState");
    if ( state == "Not Available" ) {
        std::cerr << "Error: Not connected.\n";
        return;
    }
    
    int elapsed = 0;
    if ( state == "Paused" ) {
        V.STATE     = "pause";
        elapsed     = std::stoi(fileContent(DIR.SHM +"elapsed")); // s (epoch pause - progress)
    } else if ( state == "Playing" ) {
        V.STATE     = "play";
        V.TIMESTAMP = std::stoll(fileContent(DIR.SHM +"timestamp")); // ms
    }
    
    getMetadata(bus); // Album, Artist, Title, V.COVERART, V.TIME
    
    std::string format, progress;
    int64_t current, end, start;
    size_t p0, p1, p2;
    progress = get_prop(bus, "ProgressString"); // start/current/end (@V.SAMPLERATE)
    p0       = progress.find('/');
    p1       = progress.find('/', p0 + 1);
    p2       = progress.find('/', p1 + 1);
    start    = std::stoll(progress.substr(0, p0));               // 0
    current  = std::stoll(progress.substr(p0 + 1, p1 - p0 - 1)); // 1
    end      = std::stoll(progress.substr(p1 + 1, p2 - p1 - 1)); // 2
    
    format   = get_prop(bus, "SourceFormat", "org.gnome.ShairportSync"); // AAC/48000/F24/2
    if (format.empty()) {
        bool F24 = (end - start) / V.TIME > 45000;
        V.EXT        = "AirPlay";
        V.BITDEPTH   = F24 ? 24    : 16;
        V.SAMPLERATE = F24 ? 48000 : 44100;
    } else {
        p0           = format.find('/');
        p1           = format.find('/', p0 + 1);
        V.EXT        = format.substr(0, p0);                          // 0
        V.BITDEPTH   = std::stoi(format.substr(p1 + 2, 2));           // 2 - .../F24... > 24
        V.SAMPLERATE = std::stoi(format.substr(p0 + 1, p1 - p0 - 1)); // 1
    }
    
    V.ELAPSED = ((current - start) / V.SAMPLERATE) + elapsed;
}
