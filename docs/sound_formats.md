# Compressed audio support (JKGM renderer)

The development build accepts Ogg Vorbis (`.ogg`), FLAC (`.flac`), and
MP3 (`.mp3`) for **ordinary Sith engine sounds**, without changing the
engine's DirectSound / DirectSound3D / A3D playback, mixing, listener
position, COG verbs or SoundClass definitions.

## Names and priority

A reference to `shot.wav` is resolved in the normal JK `sound/` and
`voice/` directories, including their existing GOB/GOO archives.

1. Try the original `shot.wav` via JK's original fileOpen.
2. Only when WAV is absent, try `shot.ogg`, then `shot.flac`,
   then `shot.mp3` through that same fileOpen.
3. The sound list may also explicitly name `shot.ogg`, `shot.flac`,
   or `shot.mp3`. Those files are decoded directly.

Native WAV takes precedence; existing mods need not change. Both
preloaded and on-demand sounds go through the same decoder bridge.

## Implementation

JKGM's verified sound loader calls the original HostServices callbacks.
On its first invocation, a narrowly gated detour installs a virtual
RIFF/WAVE file adapter on the shared file callbacks. It reads the selected
compressed file through JK's *existing* I/O functions, preserving loose
and GOB/GOO resources, and decodes it to interleaved **16-bit PCM**. The
native WAV parser and lazy DirectSound buffer creation then work unchanged.

Decoder code is compiled into renderer.dll: stb_vorbis, dr_flac, dr_mp3.
No standalone codec DLL, FFmpeg executable, or system OGG decoder is required.
Only mono and stereo files are accepted. An individual source file has a
64 MiB compressed-size ceiling; decoded PCM is limited to 192 MiB. A
bounded 128 MiB cache avoids repeating full decoding for the metadata
pass and the subsequent buffer load.

The ABI guard is specific to the audited `jkgm.exe`: sound loader at
`0x004EF440`, WAV parser at `0x00437A30`, and HostServices pointers at
`0x0082F0EC` / `0x00866BC0`. Unknown executable builds retain their
unmodified sound loading. The file callbacks are restored at DLL detach.

## Testing

Place a compressed sound as `sound/test.ogg`, `sound/test.flac`, or
`sound/test.mp3`, and reference `test.wav` or its actual extension in
the original JK sound system. For a clean fallback test, ensure the WAV is
not also present in a higher-priority loose directory or loaded archive.

Check the log for `JKGM compressed sound decoded:`. Test
`PlaySoundThing`, `PlaySoundPos`, loop/restart and 3D placement on the
same source with a known WAV, then replace it with a compressed version.
MP3 may introduce encoder padding, so short looping sounds generally
benefit from lossless FLAC or OGG with suitable loops.

**Known scope:** this is load-time decoding of short/ordinary sounds,
not a replacement for JK's background CD/streaming-music subsystem or a
streaming decoder. Multi-channel (>2) audio is rejected rather than
silently downmixed. Format and GOB support are implemented, but live
in-game testing is still needed for the specific JKGM ABI.
