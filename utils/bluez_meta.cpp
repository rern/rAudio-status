#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <dbus/dbus.h>

// Structure to hold our codec decoding results
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

// 1. SBC Codec Parser (Standard Baseline Codec)
CodecInfo parse_sbc(const std::vector<uint8_t>& config) {
    if (config.empty()) return {"SBC", 0};
    
    // First byte contains sampling frequency in the upper 4 bits
    uint8_t freq = config[0] & 0xF0;
    int rate = 0;
    if (freq == 0x80) rate = 16000;
    else if (freq == 0x40) rate = 32000;
    else if (freq == 0x20) rate = 44100;
    else if (freq == 0x10) rate = 48000;
    
    return {"SBC", rate};
}

// 2. AAC Codec Parser (Corrected for BlueZ native payload)
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

// 3. aptX and aptX-HD Codec Parser (Qualcomm)
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

// 4. LDAC Codec Parser (Sony High-Res Codec)
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

// Master router function for stream payload analysis
CodecInfo decode_sampling_rate(const std::vector<uint8_t>& config) {
    if (config.empty()) return {"Unknown", 0};

    if (config.size() == 4) {
        return parse_sbc(config);
    } else if (config.size() == 6) {
        return parse_aac(config);
    } else if (config.size() >= 7 && config.size() <= 9) {
        return parse_aptx(config);
    } else if (config.size() >= 10) {
        if (config[0] == 0x2D && config[1] == 0x01) {
            return parse_ldac(config);
        }
        return parse_aptx(config);
    }
    return {"Unknown Codec", 0};
}

// Parses the nested "Track" metadata dictionary inside the root property array
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

        std::string key_str(key);

        if (key_str == "Title" && type == DBUS_TYPE_STRING) {
            const char* val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::cout << "Title: " << val << '\n';
        } 
        else if (key_str == "Album" && type == DBUS_TYPE_STRING) {
            const char* val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::cout << "Album: " << val << '\n';
        } 
        else if (key_str == "Artist" && type == DBUS_TYPE_STRING) {
            const char* val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::cout << "Artist: " << val << '\n';
        } 
        else if (key_str == "Duration" && type == DBUS_TYPE_UINT32) {
            uint32_t val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::cout << "Time: " << (val / 1000) << '\n';
        }
        else if (key_str == "ImgHandle" && type == DBUS_TYPE_STRING) {
            const char* val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::cout << "Cover Art Image Token: " << val << '\n';
        }

        dbus_message_iter_next(&entry_iter);
    }
}

// Parses the root array response generated by the GetAll method
void parse_get_all_properties(DBusMessageIter* array_iter) {
    DBusMessageIter entry_iter;
    dbus_message_iter_recurse(array_iter, &entry_iter);
    std::string state;
    int elapsed = 0;

    while (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter sub_iter;
        dbus_message_iter_recurse(&entry_iter, &sub_iter);

        const char* key;
        dbus_message_iter_get_basic(&sub_iter, &key);
        dbus_message_iter_next(&sub_iter);

        DBusMessageIter variant_iter;
        dbus_message_iter_recurse(&sub_iter, &variant_iter);
        int type = dbus_message_iter_get_arg_type(&variant_iter);

        std::string key_str(key);

        if (key_str == "Status" && type == DBUS_TYPE_STRING) {
            const char* val; dbus_message_iter_get_basic(&variant_iter, &val);
            std::string Status(val);
                 if (Status == "paused")  state = "pause";
            else if (Status == "playing") state = "play";
            else                          state = "stop";
        } else if (key_str == "Position" && type == DBUS_TYPE_UINT32) {
            uint32_t val; dbus_message_iter_get_basic(&variant_iter, &val);
            elapsed = val / 1000;
        } else if (key_str == "Track" && type == DBUS_TYPE_ARRAY) {
            parse_track_metadata(&variant_iter);
        }
        dbus_message_iter_next(&entry_iter);
    }
    if (elapsed == 0) state = "stop";
    std::cout << "elapsed: " << elapsed << '\n';
    std::cout << "state: " << state << '\n';
}

int main() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    check_error(&err);
    
    // Read targeted string location block
    std::ifstream file("/srv/http/data/shm/bluetoothdest");
    std::string player_dest;
    std::getline(file, player_dest);

    if (player_dest.empty()) {
        std::cerr << "Error: Source destination configuration data file is empty.\n";
        dbus_connection_unref(conn);
        return 1;
    }

    // =========================================================================
    // QUERY 1: Fetch Audio Control / Metadata from MediaPlayer1
    // =========================================================================
    DBusMessage* msg_meta = dbus_message_new_method_call(
        "org.bluez",
        player_dest.c_str(),
        "org.freedesktop.DBus.Properties",
        "GetAll"
    );

    const char* interface_player = "org.bluez.MediaPlayer1";
    dbus_message_append_args(msg_meta, DBUS_TYPE_STRING, &interface_player, DBUS_TYPE_INVALID);

    DBusMessage* reply_meta = dbus_connection_send_with_reply_and_block(conn, msg_meta, -1, &err);
    dbus_message_unref(msg_meta);
    check_error(&err);

    DBusMessageIter reply_iter;
    dbus_message_iter_init(reply_meta, &reply_iter);

    if (dbus_message_iter_get_arg_type(&reply_iter) == DBUS_TYPE_ARRAY) {
        parse_get_all_properties(&reply_iter);
    } else {
        std::cerr << "Unexpected response format returned from MediaPlayer1." << '\n';
    }
    dbus_message_unref(reply_meta);

    // =========================================================================
    // QUERY 2: Fetch Codec & Hardware Audio Sample Parameters from MediaTransport1
    // =========================================================================
    
    // Convert path dynamically from metadata location to transport endpoint node.
    // e.g., transforms "/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX/avrcp/player0" -> ".../dev_XX_XX_XX_XX_XX_XX/fd0"
    std::string transport_dest = player_dest;
    size_t avrcp_pos = transport_dest.find("/avrcp/player");
    if (avrcp_pos != std::string::npos) {
        transport_dest = transport_dest.substr(0, avrcp_pos) + "/fd0";
    }

    DBusMessage* msg_trans = dbus_message_new_method_call(
        "org.bluez",
        transport_dest.c_str(),
        "org.freedesktop.DBus.Properties",
        "GetAll"
    );

    const char* interface_transport = "org.bluez.MediaTransport1";
    dbus_message_append_args(msg_trans, DBUS_TYPE_STRING, &interface_transport, DBUS_TYPE_INVALID);

    // Using a separate error handle logic to gracefully handle if pipeline/transport is sleeping
    DBusError trans_err;
    dbus_error_init(&trans_err);
    DBusMessage* reply_trans = dbus_connection_send_with_reply_and_block(conn, msg_trans, -1, &trans_err);
    dbus_message_unref(msg_trans);

    if (!reply_trans) {
        std::cout << "Codec parameters suspended or unavailable (Stream idle).\n";
        dbus_error_free(&trans_err);
        dbus_connection_unref(conn);
        return 0;
    }

    DBusMessageIter root_trans_iter, dict_trans_iter;
    dbus_message_iter_init(reply_trans, &root_trans_iter);
    bool found_config = false;

    if (dbus_message_iter_get_arg_type(&root_trans_iter) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&root_trans_iter, &dict_trans_iter);

        while (dbus_message_iter_get_arg_type(&dict_trans_iter) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry_iter, variant_iter, array_iter;
            dbus_message_iter_recurse(&dict_trans_iter, &entry_iter);

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
                    std::cout << "codec: " << result.codec_name << '\n';
                    if (result.sample_rate > 0) {
                        std::cout << "samplerate: " << (result.sample_rate / 1000.0) << " kHz\n";
                    } else {
                        std::cout << "samplerate: unknown\n";
                    }
                    
                    found_config = true;
                    break;
                }
            }
            dbus_message_iter_next(&dict_trans_iter);
        }
    }

    if (!found_config) {
        std::cout << "Configuration parameters missing or audio transport interface offline.\n";
    }

    dbus_message_unref(reply_trans);
    dbus_connection_unref(conn);
    return 0;
}
