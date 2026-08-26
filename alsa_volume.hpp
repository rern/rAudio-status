#pragma once

#include <alsa/asoundlib.h>

// Replicates the cubic-root volume scale algorithm used by `amixer -M`.
inline int mapPercent(long current_db, long min_db, long max_db) {
    if (max_db <= min_db) return 0;

    // Linear mapping for narrow ranges (<= 24 dB span)
    if (max_db - min_db <= 2400) {
        double ratio = static_cast<double>(current_db - min_db) / (max_db - min_db);
        return static_cast<int>(std::round(ratio * 100.0));
    }

    // Logarithmic (perceptual) mapping for wider ranges
    double normalized = std::pow(10.0, static_cast<double>(current_db - max_db) / 6000.0);
    double min_normalized = std::pow(10.0, static_cast<double>(min_db - max_db) / 6000.0);
    if (1.0 - min_normalized != 0.0) {
        normalized = (normalized - min_normalized) / (1.0 - min_normalized);
    }

    int pct = static_cast<int>(std::round(normalized * 100.0));
    return (pct < 0) ? 0 : (pct > 100) ? 100 : pct;
}

// Optimized: direct lookup using the known mixer control name.
// device defaults to ALSA's "default" ctl (see /etc/asound.conf) when omitted.
inline int getVolume(const std::string& CONTROL, const std::string& device = "default") {
    int percent = 0;
    snd_mixer_t *handle = nullptr;
    snd_mixer_elem_t *elem = nullptr;
    snd_mixer_selem_id_t *sid = nullptr;

    // 1. Core ALSA boilerplate
    if (snd_mixer_open(&handle, 0) < 0) return 0;
    if (snd_mixer_attach(handle, device.c_str()) < 0) { snd_mixer_close(handle); return 0; }
    if (snd_mixer_selem_register(handle, nullptr, nullptr) < 0) { snd_mixer_close(handle); return 0; }
    if (snd_mixer_load(handle) < 0) { snd_mixer_close(handle); return 0; }

    // 2. Set up the identification token using the known name
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0); // default index 0
    snd_mixer_selem_id_set_name(sid, CONTROL.c_str());

    // 3. Direct lookup instead of a sequential loop
    elem = snd_mixer_find_selem(handle, sid);

    // 4. Verify the target control exists and tracks playback volume
    if (elem && snd_mixer_selem_has_playback_volume(elem)) {
        long min_db = 0, max_db = 0, current_db = 0;
        if (snd_mixer_selem_get_playback_dB_range(elem, &min_db, &max_db) >= 0 &&
            snd_mixer_selem_get_playback_dB(elem, SND_MIXER_SCHN_FRONT_LEFT, &current_db) >= 0) {
            percent = mapPercent(current_db, min_db, max_db);
        }
    }

    snd_mixer_close(handle);
    return percent;
}
