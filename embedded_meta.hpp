#pragma once

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================
struct AudioEmbedded {
    bool hasArt            = false;
    bool hasLyrics         = false;
    size_t artOffset       = 0;
    size_t artSize         = 0;
    std::string lyricsText = "";
    std::string mimeType   = "";
};

inline uint32_t readSynchsafeInt32(const uint8_t* b) noexcept {
    return (static_cast<uint32_t>(b[0] & 0x7F) << 21) |
           (static_cast<uint32_t>(b[1] & 0x7F) << 14) |
           (static_cast<uint32_t>(b[2] & 0x7F) << 7)  |
           static_cast<uint32_t>(b[3] & 0x7F);
}

// ============================================================================
// STRING & DECODING HELPERS
// ============================================================================
std::vector<uint8_t> decodeBase64(const std::string& input) {
    const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<uint8_t>(b64[i])] = i;

    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (char c : input) {
        uint8_t idx = static_cast<uint8_t>(c);
        if (T[idx] == -1) continue;
        val = (val << 6) + T[idx];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Converts a little/big-endian UTF-16 byte buffer (optionally BOM-prefixed) to UTF-8.
// Shared by ID3v2 (encoding 0x01/0x02) and WMA (WM/Lyrics, which is always UTF-16LE).
std::string utf16ToUtf8(const uint8_t* textPtr, size_t payloadLen, bool forceBigEndian = false) {
    std::string converted;
    size_t i = 0;

    if (payloadLen >= 2) {
        if ((textPtr[0] == 0xFF && textPtr[1] == 0xFE) || (textPtr[0] == 0xFE && textPtr[1] == 0xFF)) {
            i += 2;
        }
    }

    bool isBigEndian = forceBigEndian || (payloadLen >= 2 && textPtr[0] == 0xFE && textPtr[1] == 0xFF);

    for (; i + 1 < payloadLen; i += 2) {
        uint16_t unicodeChar = isBigEndian ?
            static_cast<uint16_t>((textPtr[i] << 8) | textPtr[i + 1]) :
            static_cast<uint16_t>((textPtr[i + 1] << 8) | textPtr[i]);

        if (unicodeChar == 0) break;

        if (unicodeChar < 0x80) {
            converted += static_cast<char>(unicodeChar);
        } else if (unicodeChar < 0x800) {
            converted += static_cast<char>((unicodeChar >> 6) | 0xC0);
            converted += static_cast<char>((unicodeChar & 0x3F) | 0x80);
        } else {
            converted += static_cast<char>((unicodeChar >> 12) | 0xE0);
            converted += static_cast<char>(((unicodeChar >> 6) & 0x3F) | 0x80);
            converted += static_cast<char>((unicodeChar & 0x3F) | 0x80);
        }
    }
    return converted;
}

// Restricts a string to a safe filesystem-path component: alnum, '_', '-' only.
// Strips everything else (including '/', '\', '.', NUL) so it can never escape
// the directory it's written into or resolve to an unexpected path.
std::string sanitizeFilename(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        uint8_t uc = static_cast<uint8_t>(c);
        if (std::isalnum(uc) || c == '_' || c == '-') {
            out += c;
        }
    }
    return out;
}

void embeddedCoverart(const uint8_t* d, size_t size, AudioEmbedded& AE, size_t absoluteOffset) {
    if (size < 32) return;
    uint32_t mimeLen = readUint32BE(d + 4);
    // Bounds check must use <=: we are about to read 4 bytes at offset (8 + mimeLen).
    if (8 + mimeLen + 4 <= size) {
        AE.mimeType = std::string(reinterpret_cast<const char*>(d + 8), mimeLen);
        uint32_t descLen = readUint32BE(d + 8 + mimeLen);
        size_t cur = 8 + mimeLen + 4 + descLen + 16;
        if (cur + 4 <= size) {
            uint32_t imgSize = readUint32BE(d + cur);
            // Guard against a declared image size that would run past the buffer.
            if (cur + 4 + static_cast<size_t>(imgSize) <= size || absoluteOffset != 0) {
                AE.hasArt = true;
                AE.artOffset = absoluteOffset + cur + 4;
                AE.artSize = imgSize;
            }
        }
    }
}

std::string stripTimeSync(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '[') {
            size_t closeBracket = input.find(']', i);
            if (closeBracket != std::string::npos) {
                bool isTimestamp = false;
                size_t insideLen = closeBracket - i - 1;

                if (insideLen >= 4) {
                    size_t colonPos = input.find(':', i);
                    if (colonPos != std::string::npos && colonPos < closeBracket) {
                        if (colonPos > i && colonPos + 1 < closeBracket &&
                            std::isdigit(static_cast<uint8_t>(input[colonPos - 1])) &&
                            std::isdigit(static_cast<uint8_t>(input[colonPos + 1]))) {
                            isTimestamp = true;
                        }
                    }
                }

                if (isTimestamp) {
                    i = closeBracket + 1;
                    while (i < input.size() && (input[i] == ' ' || input[i] == '\t')) {
                        i++;
                    }
                    continue;
                }
            }
        }
        result += input[i];
        i++;
    }

    size_t startPos = 0;
    while (startPos < result.size() && (result[startPos] == '\r' || result[startPos] == '\n')) {
        startPos++;
    }

    return (startPos > 0) ? result.substr(startPos) : result;
}

// ============================================================================
// CORE SUB-PARSERS
// ============================================================================
AudioEmbedded embeddedID3v2(AudioData& AD, size_t startOffset = 0) {
    AudioEmbedded AE;
    uint8_t header[10];
    AD.file.seekg(startOffset, std::ios::beg);
    AD.file.read(reinterpret_cast<char*>(header), 10);
    if (static_cast<size_t>(AD.file.gcount()) < 10 || std::memcmp(header, "ID3", 3) != 0) return AE;

    uint32_t tagSize = readSynchsafeInt32(header + 6);
    size_t tagDataStart = startOffset + 10;
    std::vector<uint8_t> tagData(tagSize);
    AD.file.read(reinterpret_cast<char*>(tagData.data()), tagSize);
    size_t bytesRead = static_cast<size_t>(AD.file.gcount());

    size_t offset = 0;
    while (offset + 10 < bytesRead) {
        if (tagData[offset] == 0) break;

        uint32_t frameSize = readUint32BE(tagData.data() + offset + 4);
        if (offset + 10 + frameSize > bytesRead) break;

        if (std::memcmp(tagData.data() + offset, "APIC", 4) == 0) {
            size_t cur = offset + 10;
            size_t frameEnd = offset + 10 + frameSize;

            if (cur < frameEnd) cur += 1; // Skip text encoding descriptor byte
            std::string mime = "";
            while (cur < bytesRead && cur < frameEnd && tagData[cur] != 0) {
                mime += static_cast<char>(tagData[cur]);
                cur++;
            }
            if (cur < frameEnd) cur += 1; // Skip null string terminator
            if (cur < frameEnd) cur += 1; // Skip picture type descriptor index byte
            while (cur < bytesRead && cur < frameEnd && tagData[cur] != 0) cur++;
            if (cur < frameEnd) cur += 1; // Skip description string null terminator

            size_t consumed = cur - (offset + 10);
            if (consumed <= frameSize) {
                AE.hasArt = true;
                AE.mimeType = mime;
                AE.artOffset = tagDataStart + cur;
                AE.artSize = frameSize - consumed;
            }
        }
        else if (std::memcmp(tagData.data() + offset, "USLT", 4) == 0) {
            size_t cur = offset + 10;
            size_t frameEnd = offset + 10 + frameSize;
            if (cur >= frameEnd) { offset += 10 + frameSize; continue; }

            uint8_t encoding = tagData[cur];
            cur += 1;
            cur += 3; // Skip ISO-639 language code (3 bytes)
            if (cur > frameEnd) { offset += 10 + frameSize; continue; }

            if (encoding == 0x00 || encoding == 0x03) {
                while (cur < bytesRead && cur < frameEnd && tagData[cur] != 0) cur++;
                if (cur < frameEnd) cur += 1;
            } else {
                while (cur + 1 < bytesRead && cur + 1 < frameEnd &&
                       !(tagData[cur] == 0 && tagData[cur + 1] == 0)) {
                    cur += 2;
                }
                cur += 2;
            }

            size_t consumed = cur - (offset + 10);
            if (consumed <= frameSize) {
                size_t payloadLen = frameSize - consumed;
                if (cur + payloadLen <= bytesRead && payloadLen > 0) {
                    AE.hasLyrics = true;
                    const uint8_t* textPtr = tagData.data() + cur;

                    if (encoding == 0x00 || encoding == 0x03) {
                        AE.lyricsText = std::string(reinterpret_cast<const char*>(textPtr), payloadLen);
                    } else if (encoding == 0x01 || encoding == 0x02) {
                        AE.lyricsText = utf16ToUtf8(textPtr, payloadLen, encoding == 0x02);
                    }
                }
            }
        }

        offset += 10 + frameSize;
    }
    return AE;
}

AudioEmbedded embeddedAIFF(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(0, std::ios::beg);

    uint8_t formHeader[12];
    AD.file.read(reinterpret_cast<char*>(formHeader), 12);
    if (static_cast<size_t>(AD.file.gcount()) < 12 || std::memcmp(formHeader, "FORM", 4) != 0) return AE;
    if (std::memcmp(formHeader + 8, "AIFF", 4) != 0 && std::memcmp(formHeader + 8, "AIFC", 4) != 0) return AE;

    while (AD.file.good()) {
        uint8_t chunkHeader[8];
        AD.file.read(reinterpret_cast<char*>(chunkHeader), 8);
        if (AD.file.gcount() < 8) break;

        uint32_t chunkSize = readUint32BE(chunkHeader + 4);
        std::streampos chunkDataPos = AD.file.tellg();
        std::streamoff paddedSize = chunkSize + (chunkSize % 2);
        std::streampos nextChunkPos = chunkDataPos + paddedSize;

        if (std::memcmp(chunkHeader, "ID3 ", 4) == 0) {
            AE = embeddedID3v2(AD, static_cast<size_t>(chunkDataPos));
            break;
        }

        AD.file.seekg(nextChunkPos, std::ios::beg);
    }
    return AE;
}

AudioEmbedded embeddedAPE(AudioData& AD) {
    AudioEmbedded AE = embeddedID3v2(AD, 0);
    if (AE.hasLyrics && AE.hasArt) return AE;

    AD.file.seekg(0, std::ios::end);
    std::streampos fileSize = AD.file.tellg();
    if (fileSize < 32) return AE;

    AD.file.seekg(-32, std::ios::end);
    uint8_t footer[32];
    AD.file.read(reinterpret_cast<char*>(footer), 32);

    if (std::memcmp(footer, "APETAGEX", 8) != 0) return AE;

    uint32_t tagSize = readUint32LE(footer + 12);
    uint32_t itemCount = readUint32LE(footer + 16);

    std::streamoff tagOffsetAdjustment = (footer[23] & 0x80) ? 0 : 32;
    AD.file.seekg(fileSize - std::streamoff(tagSize) - tagOffsetAdjustment, std::ios::beg);

    if (tagOffsetAdjustment == 32) {
        uint8_t headerCheck[32];
        AD.file.read(reinterpret_cast<char*>(headerCheck), 32);
        if (std::memcmp(headerCheck, "APETAGEX", 8) != 0) return AE;
    }

    for (uint32_t i = 0; i < itemCount; ++i) {
        if (!AD.file.good()) break;

        uint8_t lenBuf[4];
        AD.file.read(reinterpret_cast<char*>(lenBuf), 4);
        if (AD.file.gcount() < 4) break;
        uint32_t valueLength = readUint32LE(lenBuf);

        AD.file.seekg(4, std::ios::cur); // Skip itemFlags segment

        std::string key = "";
        char ch;
        while (AD.file.get(ch) && ch != '\0') {
            key += ch;
        }
        if (!AD.file.good()) break;

        std::streampos itemPayloadStart = AD.file.tellg();
        std::streampos nextItemPos = itemPayloadStart + std::streamoff(valueLength);

        if ((key == "Lyrics" || key == "LYRICS") && !AE.hasLyrics) {
            std::vector<char> valBuf(valueLength);
            AD.file.read(valBuf.data(), valueLength);
            AE.hasLyrics = true;
            AE.lyricsText = std::string(valBuf.data(), valueLength);
        }
        else if (key == "Cover Art (Front)" && !AE.hasArt) {
            std::vector<char> nameBuf(valueLength);
            AD.file.read(nameBuf.data(), valueLength);

            // filename is a NUL-terminated string followed by the raw image bytes
            size_t nameLen = 0;
            while (nameLen < valueLength && nameBuf[nameLen] != '\0') nameLen++;
            nameLen += 1; // include the NUL terminator

            if (nameLen < valueLength) {
                std::string filename(nameBuf.data(), nameLen - 1);
                std::string ext;
                size_t dot = filename.find_last_of('.');
                if (dot != std::string::npos) ext = filename.substr(dot + 1);
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<uint8_t>(c)));

                AE.hasArt = true;
                AE.artOffset = static_cast<size_t>(itemPayloadStart) + nameLen;
                AE.artSize = valueLength - nameLen;
                AE.mimeType = (ext == "png") ? "image/png" : "image/jpeg";
            }
        }

        AD.file.seekg(nextItemPos, std::ios::beg);
    }
    return AE;
}

AudioEmbedded embeddedDFF(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(AD.file.tellg());
    if (fileSize < 1024) return AE;

    size_t checkOffset = fileSize - 1024;
    AD.file.seekg(checkOffset, std::ios::beg);
    std::vector<char> tailBuffer(1024);
    AD.file.read(tailBuffer.data(), 1024);
    std::string tailStr(tailBuffer.data(), 1024);

    size_t id3Pos = tailStr.find("ID3");
    if (id3Pos != std::string::npos) {
        AE = embeddedID3v2(AD, checkOffset + id3Pos);
    }
    return AE;
}

AudioEmbedded embeddedDSF(AudioData& AD) {
    AudioEmbedded AE;
    uint8_t dsdChunk[28];
    AD.file.seekg(0, std::ios::beg);
    AD.file.read(reinterpret_cast<char*>(dsdChunk), 28);
    if (AD.file.gcount() < 28) return AE;

    uint64_t id3Pointer = readUint64LE(dsdChunk + 20);
    if (id3Pointer > 0) {
        AE = embeddedID3v2(AD, static_cast<size_t>(id3Pointer));
    }
    return AE;
}

AudioEmbedded embeddedFLAC(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(4, std::ios::beg);

    bool isLast = false;
    while (!isLast) {
        uint8_t blockHeader[4];
        AD.file.read(reinterpret_cast<char*>(blockHeader), 4);
        if (AD.file.gcount() < 4) break;

        isLast = (blockHeader[0] & 0x80) != 0;
        int type = blockHeader[0] & 0x7F;
        uint32_t size = (static_cast<uint32_t>(blockHeader[1]) << 16) |
                        (static_cast<uint32_t>(blockHeader[2]) << 8)  |
                        static_cast<uint32_t>(blockHeader[3]);

        size_t absolutePayloadPos = static_cast<size_t>(AD.file.tellg());
        std::streampos nextBlockPos = AD.file.tellg() + std::streamoff(size);

        if (type == 6) { // Metadata block type PICTURE
            std::vector<uint8_t> blockData(size);
            AD.file.read(reinterpret_cast<char*>(blockData.data()), size);
            embeddedCoverart(blockData.data(), size, AE, absolutePayloadPos);
        }
        else if (type == 4) { // Metadata block type VORBIS_COMMENT
            std::vector<char> commentData(size);
            AD.file.read(commentData.data(), size);
            std::string comments(commentData.data(), size);

            size_t lyrPos = comments.find("LYRICS=");
            if (lyrPos == std::string::npos) lyrPos = comments.find("UNSYNCEDLYRICS=");
            if (lyrPos != std::string::npos) {
                AE.hasLyrics = true;
                size_t start = comments.find("=", lyrPos) + 1;
                size_t length = 0;
                while (start + length < comments.size() && comments[start + length] != '\n' && comments[start + length] != '\0') {
                    length++;
                }
                AE.lyricsText = comments.substr(start, length);
            }

            size_t b64ArtPos = comments.find("METADATA_BLOCK_PICTURE=");
            if (b64ArtPos != std::string::npos) {
                size_t start = b64ArtPos + 23;
                std::string b64Str = "";
                while (start < comments.size() && (std::isalnum(static_cast<uint8_t>(comments[start])) || comments[start] == '+' || comments[start] == '/' || comments[start] == '=')) {
                    b64Str += comments[start++];
                }
                std::vector<uint8_t> rawPicBlock = decodeBase64(b64Str);
                embeddedCoverart(rawPicBlock.data(), rawPicBlock.size(), AE, 0);
                if (AE.artSize > 0) {
                    AE.artOffset = 0;
                    AE.lyricsText += "\n[BUFFERED_ART_PAYLOAD:" + b64Str + "]";
                }
            }
        }

        AD.file.seekg(nextBlockPos, std::ios::beg);
    }
    return AE;
}

AudioEmbedded embeddedM4A(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(AD.file.tellg());
    AD.file.seekg(0, std::ios::beg);

    size_t sizeToRead = std::min(fileSize, static_cast<size_t>(1024000));
    std::vector<uint8_t> atomBuffer(sizeToRead);
    AD.file.read(reinterpret_cast<char*>(atomBuffer.data()), sizeToRead);
    std::string dataBlock(reinterpret_cast<const char*>(atomBuffer.data()), sizeToRead);

    size_t covrPos = dataBlock.find("covr");
    if (covrPos != std::string::npos) {
        size_t atomDataOffset = covrPos + 8;
        if (atomDataOffset + 16 < sizeToRead) {
            uint32_t dataAtomSize = readUint32BE(atomBuffer.data() + atomDataOffset);
            uint32_t dataType = readUint32BE(atomBuffer.data() + atomDataOffset + 8);
            if (dataAtomSize >= 16 && atomDataOffset + dataAtomSize <= sizeToRead) {
                AE.hasArt = true;
                AE.mimeType = (dataType == 14) ? "image/png" : "image/jpeg";
                AE.artOffset = atomDataOffset + 16;
                AE.artSize = dataAtomSize - 16;
            }
        }
    }

    size_t lyrPos = dataBlock.find("\xa9lyr");
    if (lyrPos != std::string::npos) {
        size_t atomDataOffset = lyrPos + 8;
        if (atomDataOffset + 16 < sizeToRead) {
            uint32_t dataAtomSize = readUint32BE(atomBuffer.data() + atomDataOffset);
            if (dataAtomSize >= 16 && atomDataOffset + dataAtomSize <= sizeToRead) {
                AE.hasLyrics = true;
                AE.lyricsText = std::string(reinterpret_cast<const char*>(atomBuffer.data() + atomDataOffset + 16), dataAtomSize - 16);
            }
        }
    }
    return AE;
}

AudioEmbedded embeddedOGG(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(0, std::ios::end);
    size_t sizeToRead = std::min(static_cast<size_t>(AD.file.tellg()), static_cast<size_t>(512000));
    AD.file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(sizeToRead);
    AD.file.read(reinterpret_cast<char*>(buffer.data()), sizeToRead);
    std::string dataBlock(reinterpret_cast<const char*>(buffer.data()), sizeToRead);

    size_t lyrPos = dataBlock.find("LYRICS=");
    if (lyrPos == std::string::npos) lyrPos = dataBlock.find("UNSYNCEDLYRICS=");
    if (lyrPos != std::string::npos) {
        AE.hasLyrics = true;
        size_t start = dataBlock.find("=", lyrPos) + 1;
        size_t length = 0;
        while (start + length < dataBlock.size() && dataBlock[start + length] != '\n' && dataBlock[start + length] != '\0') {
            if (start + length + 4 <= dataBlock.size() && dataBlock.compare(start + length, 4, "OggS") == 0) {
                break;
            }
            length++;
        }
        AE.lyricsText = dataBlock.substr(start, length);
    }

    size_t b64ArtPos = dataBlock.find("METADATA_BLOCK_PICTURE=");
    if (b64ArtPos != std::string::npos) {
        size_t start = b64ArtPos + 23;
        std::string b64Str = "";
        while (start < dataBlock.size() && (std::isalnum(static_cast<uint8_t>(dataBlock[start])) || dataBlock[start] == '+' || dataBlock[start] == '/' || dataBlock[start] == '=')) {
            b64Str += dataBlock[start++];
        }
        std::vector<uint8_t> rawPicBlock = decodeBase64(b64Str);
        embeddedCoverart(rawPicBlock.data(), rawPicBlock.size(), AE, 0);
        if (AE.artSize > 0) {
            AE.artOffset = 0;
            AE.lyricsText += "\n[BUFFERED_ART_PAYLOAD:" + b64Str + "]";
        }
    }
    return AE;
}

AudioEmbedded embeddedWAV(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(12, std::ios::beg);

    while (true) {
        uint8_t chunkHeader[8];
        AD.file.read(reinterpret_cast<char*>(chunkHeader), 8);
        if (AD.file.gcount() < 8) break;

        uint32_t chunkSize = readUint32LE(chunkHeader + 4);
        if (std::memcmp(chunkHeader, "id3 ", 4) == 0 || std::memcmp(chunkHeader, "ID3 ", 4) == 0) {
            size_t currentPos = static_cast<size_t>(AD.file.tellg());
            AE = embeddedID3v2(AD, currentPos);
            break;
        }
        if (chunkSize % 2 != 0) chunkSize++;
        AD.file.seekg(chunkSize, std::ios::cur);
    }
    return AE;
}

AudioEmbedded embeddedWMA(AudioData& AD) {
    AudioEmbedded AE;
    AD.file.seekg(0, std::ios::beg);

    const uint8_t asfHeaderGUID[16]    = {0x30,0x26,0xB2,0x75,0x8E,0x66,0xCF,0x11,0xA6,0xD9,0x00,0xAA,0x00,0x62,0xCE,0x6C};
    const uint8_t extContentGUID[16]   = {0x17,0x01,0x29,0xD2,0x5F,0x66,0xD3,0x11,0x96,0x78,0x00,0x60,0x08,0xC2,0xCB,0x9E};
    const uint8_t asfId3ObjectGUID[16] = {0x0F,0x90,0x05,0x33,0x51,0xAD,0x3E,0x40,0xA3,0x40,0x97,0xF1,0x0E,0x7E,0x03,0x41};

    uint8_t fileGUID[16];
    AD.file.read(reinterpret_cast<char*>(fileGUID), 16);
    if (AD.file.gcount() < 16 || std::memcmp(asfHeaderGUID, fileGUID, 16) != 0) return AE;

    AD.file.seekg(8, std::ios::cur);
    uint32_t subObjectCount = 0;
    AD.file.read(reinterpret_cast<char*>(&subObjectCount), 4);
    AD.file.seekg(2, std::ios::cur);

    for (uint32_t i = 0; i < subObjectCount; ++i) {
        uint8_t objGUID[16];
        AD.file.read(reinterpret_cast<char*>(objGUID), 16);
        if (AD.file.gcount() < 16) break;

        uint64_t objSize = 0;
        AD.file.read(reinterpret_cast<char*>(&objSize), 8);
        if (AD.file.gcount() < 8 || objSize < 24) break;

        std::streampos nextObjPos = AD.file.tellg() + std::streamoff(objSize - 24);

        if (std::memcmp(asfId3ObjectGUID, objGUID, 16) == 0) {
            AD.file.seekg(4, std::ios::cur);
            size_t id3AbsOffset = static_cast<size_t>(AD.file.tellg());

            AudioEmbedded id3data = embeddedID3v2(AD, id3AbsOffset);
            if (id3data.hasLyrics || id3data.hasArt) {
                return id3data;
            }
        }
        else if (std::memcmp(extContentGUID, objGUID, 16) == 0) {
            uint16_t descriptorsCount = 0;
            AD.file.read(reinterpret_cast<char*>(&descriptorsCount), 2);

            for (uint16_t j = 0; j < descriptorsCount; ++j) {
                uint16_t nameLen = 0; AD.file.read(reinterpret_cast<char*>(&nameLen), 2);
                std::vector<wchar_t> nameBuf(nameLen / 2);
                AD.file.read(reinterpret_cast<char*>(nameBuf.data()), nameLen);
                std::wstring wideName(nameBuf.data(), nameLen / 2);

                uint16_t dataType = 0; AD.file.read(reinterpret_cast<char*>(&dataType), 2);
                uint16_t valLen = 0; AD.file.read(reinterpret_cast<char*>(&valLen), 2);

                std::vector<uint8_t> valBuf(valLen);
                size_t descriptorPayloadOffset = static_cast<size_t>(AD.file.tellg());
                AD.file.read(reinterpret_cast<char*>(valBuf.data()), valLen);

                if (wideName == L"WM/Lyrics" && !AE.hasLyrics) {
                    // WM/Lyrics is stored as UTF-16LE; convert properly instead of
                    // truncating each 16-bit code unit to a single char.
                    AE.hasLyrics = true;
                    AE.lyricsText = utf16ToUtf8(valBuf.data(), valLen, /*forceBigEndian=*/false);
                } else if (wideName == L"WM/Picture" && !AE.hasArt) {
                    if (valLen > 5) {
                        uint8_t pictureType = valBuf[0];
                        uint32_t imgSize = readUint32LE(valBuf.data() + 1);
                        size_t cur = 5;

                        // MIME type is a NUL-terminated UTF-16LE string.
                        std::string mime;
                        size_t mimeStart = cur;
                        while (cur + 1 < valLen && !(valBuf[cur] == 0 && valBuf[cur + 1] == 0)) cur += 2;
                        if (cur + 1 < valLen) {
                            mime = utf16ToUtf8(valBuf.data() + mimeStart, cur - mimeStart, false);
                            cur += 2; // skip NUL terminator
                        }
                        // Description string, also UTF-16LE NUL-terminated.
                        while (cur + 1 < valLen && !(valBuf[cur] == 0 && valBuf[cur + 1] == 0)) cur += 2;
                        if (cur + 1 < valLen) cur += 2;

                        if (cur + imgSize <= valLen) {
                            AE.hasArt = true;
                            AE.artSize = imgSize;
                            AE.artOffset = descriptorPayloadOffset + cur;
                            AE.mimeType = mime.empty() ? "image/jpeg" : mime;
                        }
                        (void)pictureType;
                    }
                }
            }
        }
        AD.file.seekg(nextObjPos, std::ios::beg);
    }
    return AE;
}

// ============================================================================
// EXPORT PROCESSING MANAGER
// ============================================================================
std::string extractEmbedded(AudioData& AD, const AudioEmbedded& AE, const bool& COVERART, const std::string& FILE_SOURCE, std::string& file_embedded) {
    if (COVERART) {
        if (!AE.hasArt || AE.artSize == 0) return {};

        // Sanitize the caller-supplied name so it cannot contain path separators
        // or traversal sequences ("..", "/") before it is used to build a path
        // under the web-served output directory.
        std::string safeBase = sanitizeFilename(!file_embedded.empty() ? file_embedded : FILE_SOURCE);
        if (safeBase.empty()) return {};

        std::string safeName = safeBase + (AE.mimeType.find("png") != std::string::npos ? ".png" : ".jpg");
        std::ofstream file_out("/srv/http/" + safeName, std::ios::binary);
        if (!file_out) return {};

        if (AE.artOffset == 0) {
            size_t marker = AE.lyricsText.find("[BUFFERED_ART_PAYLOAD:");
            if (marker != std::string::npos) {
                size_t start = marker + 22;
                size_t end = AE.lyricsText.find("]", start);
                if (end != std::string::npos) {
                    std::vector<uint8_t> decryptedRaw = decodeBase64(AE.lyricsText.substr(start, end - start));
                    if (decryptedRaw.size() >= 32) {
                        const uint8_t* rawPtr = decryptedRaw.data();
                        uint32_t mLen = readUint32BE(rawPtr + 4);
                        if (8 + mLen + 4 <= decryptedRaw.size()) {
                            uint32_t dLen = readUint32BE(rawPtr + 8 + mLen);
                            size_t payloadStart = 8 + mLen + 4 + dLen + 16 + 4;

                            if (payloadStart <= decryptedRaw.size() &&
                                AE.artSize <= decryptedRaw.size() - payloadStart) {
                                file_out.write(reinterpret_cast<const char*>(rawPtr + payloadStart), AE.artSize);
                                file_embedded = safeName;
                                return file_embedded;
                            }
                        }
                    }
                }
            }
            return {};
        } else {
            AD.file.seekg(AE.artOffset, std::ios::beg);
            std::vector<char> buffer(AE.artSize);
            AD.file.read(buffer.data(), AE.artSize);
            file_out.write(buffer.data(), AE.artSize);
            file_embedded = safeName;
            return file_embedded;
        }
    } else {
        if (!AE.hasLyrics || AE.lyricsText.empty()) return {};

        size_t marker = AE.lyricsText.find("\n[BUFFERED_ART_PAYLOAD:");
        std::string lyrics = (marker != std::string::npos) ? AE.lyricsText.substr(0, marker) : AE.lyricsText;
        lyrics = stripTimeSync(lyrics);
        return lyrics;
    }
}

AudioEmbedded getEmbeddedAudio(AudioData& AD) {
    switch (AD.format) {
        case AF::aiff: return embeddedAIFF(AD);
        case AF::ape:  return embeddedAPE(AD);
        case AF::dsf:  return embeddedDSF(AD);
        case AF::dff:  return embeddedDFF(AD);
        case AF::flac: return embeddedFLAC(AD);
        case AF::mp3:
        case AF::na:   return embeddedID3v2(AD);
        case AF::m4a:  return embeddedM4A(AD);
        case AF::ogg:  return embeddedOGG(AD);
        case AF::wav:  return embeddedWAV(AD);
        case AF::wma:  return embeddedWMA(AD);
        default:       return AudioEmbedded{}; // fallback
    }
}