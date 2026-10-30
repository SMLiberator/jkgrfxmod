#pragma once
// Compressed JK sound files are decoded to ordinary PCM16 RIFF/WAVE bytes.
// Playback remains entirely inside JK's native DirectSound/A3D pipeline.
#include <cstdint>
#include <string_view>
#include <vector>

namespace jkgm {
bool decode_compressed_sound_to_wave(
    std::string_view extension,
    std::vector<uint8_t> const& compressed,
    std::vector<uint8_t>& wave);
}
