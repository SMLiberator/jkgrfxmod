#pragma once
// Compressed sound bridge: OGG/FLAC/MP3 decoded into virtual PCM WAV streams
// through JK's original HostServices file system, including GOB archives.
// Native Sith sound loading and DirectSound playback remain unchanged.
namespace jkgm {
bool compressed_sound_executable_supported();
bool install_compressed_sound_file_services();
void uninstall_compressed_sound_file_services();
}
