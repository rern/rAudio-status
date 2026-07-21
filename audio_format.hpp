#pragma once

// BITWISE UTILITIES ----------------------------------------
inline uint16_t readUint16LE(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

inline uint32_t readUint32LE(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0])        |
           (static_cast<uint32_t>(bytes[1]) << 8)  |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

inline uint32_t readUint32BE(const uint8_t* bytes) noexcept {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8)  |
           static_cast<uint32_t>(bytes[3]);
}

inline uint64_t readUint64LE(const uint8_t* bytes) noexcept {
    return static_cast<uint64_t>(bytes[0])        |
           (static_cast<uint64_t>(bytes[1]) << 8)  |
           (static_cast<uint64_t>(bytes[2]) << 16) |
           (static_cast<uint64_t>(bytes[3]) << 24) |
           (static_cast<uint64_t>(bytes[4]) << 32) |
           (static_cast<uint64_t>(bytes[5]) << 40) |
           (static_cast<uint64_t>(bytes[6]) << 48) |
           (static_cast<uint64_t>(bytes[7]) << 56);
}

inline uint64_t readUint64BE(const uint8_t* bytes) noexcept {
    return (static_cast<uint64_t>(bytes[0]) << 56) |
           (static_cast<uint64_t>(bytes[1]) << 48) |
           (static_cast<uint64_t>(bytes[2]) << 40) |
           (static_cast<uint64_t>(bytes[3]) << 32) |
           (static_cast<uint64_t>(bytes[4]) << 24) |
           (static_cast<uint64_t>(bytes[5]) << 16) |
           (static_cast<uint64_t>(bytes[6]) << 8)  |
           static_cast<uint64_t>(bytes[7]);
}
// ----------------------------------------------------------
enum class AF {
    aiff, ape, dsf, dff, flac, m4a, mp3, ogg, wav, wma,
    na
};
struct AudioData {
    AF format   = AF::na;   // default-initialized: never left indeterminate,
                             // safe to read even if readFile() bails out early
    std::ifstream file;
    std::vector<uint8_t> buffer; // owns the raw header bytes; AD.h points into this
    bool error  = true;
    uint8_t* h  = 0;
    size_t size = 0;
};
namespace Utils {
    // A lightweight, zero-cost compile-time string comparator.
    // Replaces std::memcmp safely for constexpr contexts.
    // Bounds-checked against dataSize so it is safe to call even if a future
    // caller forgets (or gets wrong) an external size guard.
    constexpr bool matchMagic(const uint8_t* data, size_t dataSize, std::string_view magic, size_t offset = 0) noexcept {
        if (offset > dataSize || magic.size() > dataSize - offset) return false;
        for (size_t i = 0; i < magic.size(); ++i) {
            if (data[offset + i] != static_cast<uint8_t>(magic[i])) return false;
        }
        return true;
    }

    /**
     * @brief Detects the audio format from a raw header byte buffer.
     * Fully optimized for runtime execution and 100% compliant with compile-time constexpr evaluation.
     */
    constexpr AF audioFormat(const uint8_t* h, size_t size) noexcept {
        if (!h || size == 0) return AF::na;

        // --- MP3 ---
        // Matches ID3v2 tag ("ID3") OR MPEG Audio Frame Sync (0xFFE0 mask)
        if (matchMagic(h, size, "ID3") ||
            (size >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0)) {
            return AF::mp3;
        }
        // --- FLAC ---
        if (matchMagic(h, size, "fLaC")) return AF::flac;
        // --- WAV (RIFF Container) ---
        if (matchMagic(h, size, "RIFF") && matchMagic(h, size, "WAVE", 8)) return AF::wav;
        // --- M4A / AAC (MP4 Container) ---
        if (matchMagic(h, size, "ftyp", 4)) return AF::m4a;
        // --- AIFF / AIFC (IFF Container) ---
        if (matchMagic(h, size, "FORM")) {
            if (matchMagic(h, size, "AIFF", 8) || matchMagic(h, size, "AIFC", 8)) return AF::aiff;
        }
        // --- DSF (DSD Stream File) ---
        if (matchMagic(h, size, "DSD ")) return AF::dsf;
        // --- DFF (DSDIFF Container) ---
        if (matchMagic(h, size, "FRM8")) return AF::dff;
        // --- APE (Monkey's Audio) ---
        if (matchMagic(h, size, "MAC ")) return AF::ape;
        // --- OGG (Vorbis / Opus Container) ---
        if (matchMagic(h, size, "OggS")) return AF::ogg;
        // --- WMA (ASF Container GUID Object) ---
        if (size >= 16 &&
            h[0] == 0x30 && h[1] == 0x26 && h[2] == 0xB2 && h[3] == 0x75 &&
            h[4] == 0x8E && h[5] == 0x66 && h[6] == 0xCF && h[7] == 0x11) {
            return AF::wma;
        }

        return AF::na;
    }

    AudioData readFile(const std::string& FILE_SOURCE, const bool return_file) {
        AudioData AD;
        std::ifstream file(FILE_SOURCE, std::ios::binary);
        if (!file) {
            std::cerr << "Error: AudioData readFile - " << FILE_SOURCE << '\n';
        } else {
            AD.buffer.resize(4096);
            file.read(reinterpret_cast<char*>(AD.buffer.data()), AD.buffer.size());
            AD.size = static_cast<size_t>(file.gcount());
            if (AD.size < 16) {
                std::cerr << "Error: AD.size < 16\n";
            } else {
                AD.error = false;
                AD.h = AD.buffer.data(); // points into AD's own buffer, valid for AD's lifetime
                AD.format = audioFormat(AD.h, AD.size);
                if (return_file) AD.file = std::move(file); // for process file
            }
        }
        return AD;
    }
} // namespace Utils