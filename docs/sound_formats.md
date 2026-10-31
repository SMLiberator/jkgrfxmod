# Compressed audio support (JKGM renderer)

JKGM supports Ogg Vorbis (`.ogg`), FLAC (`.flac`) and MP3 (`.mp3`)
through the native Sith sound loader and DirectSound playback system.
The codecs are built into renderer.dll; external decoders are not needed.

## Using compressed sounds

Reference the *actual* filename and extension in COG or sound lists:

```c
sound = LoadSound("sugar.ogg");
PlaySoundLocal(sound, 1.0, 0.0, 0x0);
```

JK's search order is `sound/`, then `voice/`, then `music/`,
including resources stored in GOB/GOO archives. An explicit
`LoadSound("sugar.ogg")` never substitutes a different extension.
The native WAV path remains unchanged. Both the initial sound-metadata
lookup and the later PCM-buffer load use the extended directory list.

## Implementation

A guarded detour of `sithSound_Load` ensures that JKGM's HostServices
file callbacks are active before loading sounds. JK can reset those
callbacks during resource initialization, so the bridge verifies and
reattaches only its own original callbacks when necessary.

Compressed sources are opened through the original JK resource manager
and read sequentially, in bounded chunks, until the reader returns
zero bytes. This works for loose files and GOB-backed handles; it avoids
the differing `fseek` success conventions and the unsafe native
`fileSize` callback. The decoders turn the input into 16-bit
interleaved mono/stereo PCM exposed as a virtual RIFF/WAVE file.

The existing sound loader then creates normal DirectSound buffers:
there is no replacement mixer or change to the COG sound verbs.
Looping and pitch shifting have been verified in-game. OGG, FLAC, MP3
and OGG in `music/` inside a GOB have been verified in-game.

Limits: compressed input up to 64 MiB, decoded PCM up to 192 MiB,
and an eviction-bounded decoded-data cache of 128 MiB. Stereo
and mono sources are supported; multichannel sources are rejected.
This is load-time decoding, not incremental streaming or a replacement
for the CD music subsystem. MP3 padding may complicate seamless loops;
lossless FLAC or properly authored OGG are preferable for those.

## Compatibility and tests

The native loader and WAV parser addresses, HostServices pointers, and
two search-list instruction operands are guarded for the audited
`jkgm.exe` build. Unsupported builds retain original sound behavior.

Windows CI builds the renderer and runs regression tests for codec decoding
and forward-only resource reads, including partial reads, empty streams,
and the 64 MiB encoded-input limit.

For troubleshooting, the log retains warnings about unsupported
executables, callback mismatches, failed decoding and virtual-handle
exhaustion. Successful decoding is logged once per uncached source;
the temporary per-open and live-HostServices diagnostics have been removed.
