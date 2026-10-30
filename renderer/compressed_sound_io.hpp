#pragma once
// JKGM sound-file bridge. Converts OGG/FLAC/MP3 to a virtual PCM WAV through
// the game's original HostServices filesystem (including GOB archives).
// The native Sith sound loader and DirectSound3D backend remain unchanged.
namespace jkgm {
bool compressed_sound_executable_supported();
bool install_compressed_sound_file_services();
void uninstall_compressed_sound_file_services();
}
