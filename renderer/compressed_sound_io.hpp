#pragma once
// JKGM sound-file bridge. Converts OGG/FLAC/MP3 to a virtual PCM WAV through
// the game's original HostServices filesystem (including GOB archives).
// The native Sith sound loader and DirectSound3D backend remain unchanged.
namespace jkgm {
bool compressed_sound_executable_supported();
// Temporary native sound diagnostics: inspect the live HostServices callbacks,
// probe JK's sound/voice resource search paths, and trace file-open requests.
void diagnose_compressed_sound_file_services(char const* filename);
void begin_compressed_sound_open_trace();
void end_compressed_sound_open_trace();
bool install_compressed_sound_file_services();
void uninstall_compressed_sound_file_services();
}
