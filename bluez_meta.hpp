#pragma once

#include <dbus/dbus.h>

// Structure to hold codec decoding results
struct CodecInfo {
    std::string codec_name;
    int sample_rate;
};

// Error wrapper logic
void check_error(DBusError* err) {
    if (dbus_error_is_set(err)) {
        std::cerr << "D-Bus Error: " << err->message << '\n';
        dbus_error_free(err);
        exit(EXIT_FAILURE);
    }
}

// Internal codec parsing logic
CodecInfo parse_sbc(const std::vector<uint8_t>& config) {
    if (config.empty()) return {"SBC", 0};
    uint8_t freq = config[0] & 0xF0;
    int rate = 0;
    if (freq == 0x80) rate = 16000;
    else if (freq == 0x40) rate = 32000;
    else if (freq == 0x20) rate = 44100;
    else if (freq == 0x10) rate = 48000;
    return {"SBC", rate};
}

CodecInfo parse_aac(const std::vector<uint8_t>& config) {
    if (config.size() < 3) return {"AAC", 0};
    uint8_t freq_byte1 = config[1];
    uint8_t freq_byte2 = config[2];
    int rate = 0;
    if (freq_byte1 & 0x80) rate = 8000;
    else if (freq_byte1 & 0x40) rate = 11025;
    else if (freq_byte1 & 0x20) rate = 12000;
    else if (freq_byte1 & 0x10) rate = 16000;
    else if (freq_byte1 & 0x08) rate = 22050;
    else if (freq_byte1 & 0x04) rate = 24000;
    else if (freq_byte1 & 0x02) rate = 32000;
    else if (freq_byte1 & 0x01) rate = 44100;
    else if (freq_byte2 & 0x80) rate = 48000;
    else if (freq_byte2 & 0x40) rate = 64000;
    else if (freq_byte2 & 0x20) rate = 88200;
    else if (freq_byte2 & 0x10) rate = 96000;
    return {"AAC", rate};
}

CodecInfo parse_aptx(const std::vector<uint8_t>& config) {
    if (config.size() < 7) return {"aptX/aptX-HD", 0};
    uint8_t freq = config[6] & 0xF0;
    int rate = 0;
    if (freq & 0x40) rate = 16000;
    else if (freq & 0x20) rate = 32000;
    else if (freq & 0x10) rate = 44100;
    else if (freq & 0x08) rate = 48000;
    std::string name = (config.size() >= 10) ? "aptX-HD" : "aptX";
    return {name, rate};
}

CodecInfo parse_ldac(const std::vector<uint8_t>& config) {
    if (config.size() < 7) return {"LDAC", 0};
    uint8_t freq = config[6] & 0x3F;
    int rate = 0;
    if (freq & 0x20) rate = 44100;
    else if (freq & 0x10) rate = 48000;
    else if (freq & 0x08) rate = 88200;
    else if (freq & 0x04) rate = 96000;
    else if (freq & 0x02) rate = 176400;
    else if (freq & 0x01) rate = 192000;
    return {"LDAC", rate};
}

CodecInfo decode_sampling_rate(const std::vector<uint8_t>& config) {
    if (config.empty()) return {"Unknown", 0};
    if (config.size() == 4) return parse_sbc(config);
    if (config.size() == 6) return parse_aac(config);
    if (config.size() >= 7 && config.size() <= 9) return parse_aptx(config);
    if (config.size() >= 10) {
        if (config[0] == 0x2D && config[1] == 0x01) return parse_ldac(config);
        return parse_aptx(config);
    }
    return {"Unknown Codec", 0};
}

void parse_track_metadata(DBusMessageIter* dict_iter) {
    DBusMessageIter entry_iter;
    dbus_message_iter_recurse(dict_iter, &entry_iter);

    while (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter sub_iter;
        dbus_message_iter_recurse(&entry_iter, &sub_iter);

        const char* key;
        dbus_message_iter_get_basic(&sub_iter, &key);
        dbus_message_iter_next(&sub_iter);

        DBusMessageIter variant_iter;
        dbus_message_iter_recurse(&sub_iter, &variant_iter);
        int type = dbus_message_iter_get_arg_type(&variant_iter);

        std::string_view k(key);
        
        if (type == DBUS_TYPE_STRING) {
            if (k == "Title" || k == "Artist" || k == "Album") {
                const char* val; 
                dbus_message_iter_get_basic(&variant_iter, &val);
                S[std::string(k)] = val;
            }
            else if (k == "ImgHandle") {
                const char* val;
                dbus_message_iter_get_basic(&variant_iter, &val);
                V.COVERART = val;
            }
        }
        else if (k == "Duration" && type == DBUS_TYPE_UINT32) {
            uint32_t val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            V.TIME = val / 1000;
        }
        dbus_message_iter_next(&entry_iter);
    }
}

void parse_get_all_properties(DBusMessageIter* array_iter) {
    DBusMessageIter entry_iter;
    dbus_message_iter_recurse(array_iter, &entry_iter);

    while (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter sub_iter;
        dbus_message_iter_recurse(&entry_iter, &sub_iter);

        const char* key;
        dbus_message_iter_get_basic(&sub_iter, &key);
        dbus_message_iter_next(&sub_iter);

        DBusMessageIter variant_iter;
        dbus_message_iter_recurse(&sub_iter, &variant_iter);
        int type = dbus_message_iter_get_arg_type(&variant_iter);

        std::string_view k(key);
        
        if (k == "Status" && type == DBUS_TYPE_STRING) {
            const char* val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            std::string_view v(val);
                 if (v == "paused")  V.STATE = "pause";
            else if (v == "playing") V.STATE = "play";
            else                     V.STATE = "stop";
        } else if (k == "Position" && type == DBUS_TYPE_UINT32) {
            uint32_t val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            V.ELAPSED = val / 1000;
        } else if (k == "Track" && type == DBUS_TYPE_ARRAY) {
            parse_track_metadata(&variant_iter);
        }
        dbus_message_iter_next(&entry_iter);
    }
    if (V.ELAPSED == 0) V.STATE = "stop";
    stateSet();
}

// =============================================================================
// HELPER FUNCTION 1: Fetches Track Metadata & Play State from MediaPlayer1
// =============================================================================
bool get_media_player_properties(DBusConnection* conn, const std::string& player_path) {
    DBusError err;
    dbus_error_init(&err);

    DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        player_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "GetAll"
    );

    const char* interface_player = "org.bluez.MediaPlayer1";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface_player, DBUS_TYPE_INVALID);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    dbus_message_unref(msg);

    if (!reply) {
        std::cerr << "MediaPlayer1 Query Error: " << err.message << '\n';
        dbus_error_free(&err);
        return false;
    }

    DBusMessageIter reply_iter;
    dbus_message_iter_init(reply, &reply_iter);

    if (dbus_message_iter_get_arg_type(&reply_iter) == DBUS_TYPE_ARRAY) {
        parse_get_all_properties(&reply_iter);
        dbus_message_unref(reply);
        return true;
    }

    dbus_message_unref(reply);
    return false;
}

// =============================================================================
// HELPER FUNCTION 2: Fetches and decodes pipeline characteristics from MediaTransport1
// =============================================================================
bool get_media_transport_properties(DBusConnection* conn, const std::string& player_path) {
    // Dynamically deduce the transport endpoint path from the player node
    std::string transport_path = player_path;
    size_t avrcp_pos = transport_path.find("/avrcp/player");
    if (avrcp_pos != std::string::npos) {
        transport_path = transport_path.substr(0, avrcp_pos) + "/fd0";
    }

    DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        transport_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "GetAll"
    );

    const char* interface_transport = "org.bluez.MediaTransport1";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface_transport, DBUS_TYPE_INVALID);

    DBusError trans_err;
    dbus_error_init(&trans_err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &trans_err);
    dbus_message_unref(msg);

    if (!reply) {
        std::cout << "Codec parameters suspended or unavailable (Stream idle).\n";
        dbus_error_free(&trans_err);
        return false;
    }

    DBusMessageIter root_iter, dict_iter;
    dbus_message_iter_init(reply, &root_iter);
    bool found_config = false;

    if (dbus_message_iter_get_arg_type(&root_iter) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&root_iter, &dict_iter);

        while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry_iter, variant_iter, array_iter;
            dbus_message_iter_recurse(&dict_iter, &entry_iter);

            const char* key;
            dbus_message_iter_get_basic(&entry_iter, &key);
            dbus_message_iter_next(&entry_iter); 

            if (std::string(key) == "Configuration") {
                dbus_message_iter_recurse(&entry_iter, &variant_iter); 
                
                if (dbus_message_iter_get_arg_type(&variant_iter) == DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&variant_iter, &array_iter);
                    
                    std::vector<uint8_t> config_bytes;
                    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_BYTE) {
                        uint8_t byte_val;
                        dbus_message_iter_get_basic(&array_iter, &byte_val);
                        config_bytes.push_back(byte_val);
                        dbus_message_iter_next(&array_iter);
                    }

                    CodecInfo result = decode_sampling_rate(config_bytes);
                    if (result.sample_rate > 0) {
                        V.SAMPLING = std::format("{:.1f}", result.sample_rate / 1000.0) +" kHz • "+ result.codec_name;
                    }
                    
                    found_config = true;
                    break;
                }
            }
            dbus_message_iter_next(&dict_iter);
        }
    }

    dbus_message_unref(reply);
    return found_config;
}

// =============================================================================
// Execution Runtime Entrypoint
// =============================================================================
void bluezMeta() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    check_error(&err);
    
    std::ifstream file("/srv/http/data/shm/bluetoothdest");
    if (!file) {
        std::cerr << "Error: bluezMeta\n";
        return;
    }
    
    std::string player_dest;
    std::getline(file, player_dest);

    if (player_dest.empty()) {
        std::cerr << "Error: file empty\n";
        dbus_connection_unref(conn);
        return;
    }

    // Call helper 1: Metadata processing
    get_media_player_properties(conn, player_dest);

    // Call helper 2: Audio pipeline extraction 
    get_media_transport_properties(conn, player_dest);

    dbus_connection_unref(conn);
}
