#pragma once

#include <systemd/sd-bus.h>

std::string get_prop(sd_bus* bus, const char* iface, const char* prop) {
    sd_bus_message* m = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char* val = nullptr;

    int r = sd_bus_get_property_string(bus, "org.gnome.ShairportSync", 
            "/org/gnome/ShairportSync", iface, prop, &error, &val);
    
    std::string result = (r >= 0) ? val : "Unknown";
    free(val);
    return result;
}

void getMetadata(sd_bus* bus) {
    sd_bus_message* m = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_get_property(bus, "org.gnome.ShairportSync", 
            "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", 
            "Metadata", &error, &m, "a{sv}");
    if (r < 0) return;
    
    sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    const char* key;
    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        sd_bus_message_read(m, "s", &key);
        std::string s_key(key);
        if (s_key == "xesam:artist") {
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
        } else if (s_key == "xesam:album") {
            const char* album;
            sd_bus_message_read(m, "v", "s", &album);
            S["Album"] = album;
        } else if (s_key == "mpris:artUrl") {
            const char* arturl;
            sd_bus_message_read(m, "v", "s", &arturl);
            V.COVERART = arturl;
            V.COVERART.erase(0, 7);
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

std::string getProperty(sd_bus* bus, const char* prop) {
    return get_prop(bus, "org.gnome.ShairportSync.RemoteControl", prop);
}

void read_current_state(sd_bus* bus) {
    std::string state = getProperty(bus, "PlayerState");
    if ( state == "Not Available" ) {
        std::cerr << "Error: Not connected.\n";
        return;
    }
    
         if ( state == "Paused" )  V.STATE = "pause";
    else if ( state == "Playing" ) V.STATE = "play";
    else if ( state == "Stopped" ) V.STATE = "stop";
    
    int sampling       = 0;
    std::string format = get_prop(bus, "org.gnome.ShairportSync", "OutputFormat");
    size_t pos         = format.find('/');
    if (pos != std::string::npos) sampling = stoi(format.substr(0, pos));
        
    std::string progress = getProperty(bus, "ProgressString");
    long long start, current;
    size_t pos1 = progress.find('/');
    size_t pos2 = progress.find('/', pos1 + 1);
    start       = std::stoll(progress.substr(0, pos1));
    current     = std::stoll(progress.substr(pos1 + 1, pos2 - pos1 - 1));
    V.ELAPSED   = (current - start + (sampling / 2)) / sampling;
    
    getMetadata(bus);
}
