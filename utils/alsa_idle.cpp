#include <iostream>
#include <vector>
#include <alsa/asoundlib.h>
#include <poll.h>

// Callback triggered whenever a volume or mute status change is detected
int mixer_element_callback(snd_mixer_elem_t *elem, unsigned int mask) {
    if (mask & SND_CTL_EVENT_MASK_VALUE) {
        long volume = 0;
        long min_vol = 0, max_vol = 0;

        // Get limits and current volume
        snd_mixer_selem_get_playback_volume_range(elem, &min_vol, &max_vol);
        snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);

        // Convert to percentage
        int percentage = 0;
        if (max_vol - min_vol > 0) {
            percentage = (100 * (volume - min_vol)) / (max_vol - min_vol);
        }

        std::cout << "[Event] Volume changed! Current volume: " << percentage << "%" << std::endl;
    }
    return 0;
}

int main() {
    snd_mixer_t *mixer_handle = nullptr;
    snd_mixer_elem_t *elem = nullptr;

    // 1. Open an empty mixer object
    if (snd_mixer_open(&mixer_handle, 0) < 0) {
        std::cerr << "Failed to open mixer." << std::endl;
        return 1;
    }

    // 2. Attach it to the default sound card (hw:0 or "default")
    if (snd_mixer_attach(mixer_handle, "default") < 0) {
        std::cerr << "Failed to attach mixer to default card." << std::endl;
        snd_mixer_close(mixer_handle);
        return 1;
    }

    // 3. Register the abstraction layer elements
    if (snd_mixer_selem_register(mixer_handle, nullptr, nullptr) < 0) {
        std::cerr << "Failed to register mixer elements." << std::endl;
        snd_mixer_close(mixer_handle);
        return 1;
    }

    // 4. Load the elements into memory
    if (snd_mixer_load(mixer_handle) < 0) {
        std::cerr << "Failed to load mixer elements." << std::endl;
        snd_mixer_close(mixer_handle);
        return 1;
    }

    // 5. Find the "Master" volume track
    snd_mixer_selem_id_t *sid = nullptr;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    elem = snd_mixer_find_selem(mixer_handle, sid);
    if (!elem) {
        std::cerr << "Cannot find 'Master' mixer element." << std::endl;
        snd_mixer_close(mixer_handle);
        return 1;
    }

    // 6. Assign our custom event callback to the Master element
    snd_mixer_elem_set_callback(elem, mixer_element_callback);

    // 7. Prepare the poll structures for an unblocking loop
    int count = snd_mixer_poll_descriptors_count(mixer_handle);
    if (count <= 0) {
        std::cerr << "Invalid poll descriptor count." << std::endl;
        snd_mixer_close(mixer_handle);
        return 1;
    }

    std::vector<struct pollfd> fds(count);
    snd_mixer_poll_descriptors(mixer_handle, fds.data(), count);

    std::cout << "Listening for volume changes... (Press Ctrl+C to exit)" << std::endl;

    // 8. The Event Loop
    while (true) {
        // Set an unblocking timeout (e.g., 100ms) so your thread can handle other application tasks
        int timeout_ms = 100; 
        int res = poll(fds.data(), count, timeout_ms);

        if (res < 0) {
            std::cerr << "Poll error occured." << std::endl;
            break;
        }

        // If res == 0, it timed out without blocking. Use this space to do UI ticks or other logic.
        if (res == 0) {
            // Main application logic / Game loop ticks can go here
            continue;
        }

        // If res > 0, an ALSA mixer event was triggered
        unsigned short revents;
        snd_mixer_poll_descriptors_revents(mixer_handle, fds.data(), count, &revents);

        if (revents & POLLIN) {
            // This natively dispatches events to `mixer_element_callback`
            snd_mixer_handle_events(mixer_handle); 
        }
    }

    // Clean up
    snd_mixer_close(mixer_handle);
    return 0;
}
