// g++ -O2 mpd_idle.cpp -o mpd-idle $( pkg-config --cflags --libs libmpdclient )

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <mpd/client.h>
#include <mpd/idle.h>
#include <mpd/status.h>

namespace fs = std::filesystem;

bool
    CONSUME   = false; 
int
    PREV_ID   = 0,
    UPDATE_ID = 0;
std::string
    DIR_BASH  = "/srv/http/bash/",
    DIR_DATA  = "/srv/http/data/",
    DIR_SHM   = DIR_DATA +"shm/",
    PLAYER;

void runCommand(std::string command) {
    std::string cmd = DIR_BASH + command;
    std::system(cmd.c_str());
}

void mpdStatus(struct mpd_connection* conn, const std::string& type) {
    struct mpd_status* status = mpd_run_status(conn);
    if (status == nullptr) return;
    
    if (type == "consume") CONSUME   = mpd_status_get_consume_state(status) == MPD_CONSUME_ON;
    
    if (type == "update")  UPDATE_ID = mpd_status_get_update_id(status);
    
    mpd_status_free(status);
}

void mpdIdleLoop(struct mpd_connection* conn) {
    if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        throw std::invalid_argument("Invalid or broken MPD connection passed to watcher.");
    }

    // Define the bitmask of subsystems we want to listen to
    const unsigned int interest_mask = MPD_IDLE_PLAYER 
                                     | MPD_IDLE_PLAYLIST 
                                     | MPD_IDLE_MIXER 
                                     | MPD_IDLE_UPDATE;
    
    while (true) {
        // Blocks until one (or more) of our masked events triggers
        enum mpd_idle events = mpd_run_idle_mask(conn, static_cast<enum mpd_idle>(interest_mask));

        // Handled connection drop or errors
        if (events == 0) {
            enum mpd_error err = mpd_connection_get_error(conn);
            if (err != MPD_ERROR_SUCCESS) {
                std::cerr << "MPD Connection Error: " << mpd_connection_get_error_message(conn) << "\n";
            }
            break; 
        }

        if (events & MPD_IDLE_PLAYER) {
            for (const std::string f : {"radio", "skip", "cdstart"}) {
                if (fs::exists(DIR_SHM + f)) break;
                
                runCommand("status-push.sh");
            }
        }
        
        if (events & MPD_IDLE_PLAYLIST) {
            if (!fs::exists(DIR_SHM +"pushplaylist")) {
                mpdStatus(conn, "consume");
                if (CONSUME) runCommand("cmd.sh playlistpush");
            }
        }
        
        if (events & MPD_IDLE_MIXER) {
            std::ifstream f(DIR_SHM +"player");
            std::string player;
            std::getline(f, player);
            if (player == "upnp") runCommand("cmd.sh pushVolume");
        }
        
        if (events & MPD_IDLE_UPDATE) {
            mpdStatus(conn, "update");
            if (UPDATE_ID == 0 && PREV_ID > 0) { // update done
                if (!fs::exists(DIR_DATA +"/mpd/listing")) runCommand("cmd-list.sh");
            } 
            PREV_ID = UPDATE_ID;
        }
    }
}

int main() {
    struct mpd_connection* conn = mpd_connection_new(nullptr, 0, 0);
    
    if (conn == nullptr) {
        std::cerr << "Out of memory trying to create MPD connection structure.\n";
        return 1;
    }

    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        std::cerr << "Connection failed: " << mpd_connection_get_error_message(conn) << "\n";
        mpd_connection_free(conn);
        return 1;
    }

    try {
        mpdIdleLoop(conn);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    mpd_connection_free(conn);
    return 0;
}
