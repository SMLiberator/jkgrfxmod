# Compressed audio support (JKGM renderer)

Development builds can load Ogg Vorbis (`.ogg`), FLAC (`.flac`), and
MP3 (`.mp3`) as ordinary Sith sound resources through the original
DirectSound / DirectSound3D audio system. This does not replace JK's mixer,
COG verbs, SoundClass behavior, pitch/panning, or spatial sound playback.

## Filename rules

Use the **real filename and extension** in COG and sound resource lists:

```c
sound = LoadSound("music.ogg");
PlaySoundLocal(sound, 1.0, 0.0, 0x0);
```

`LoadSound("music.wav")` only opens `music.wav`. It will **not** search
for `music.ogg`, `music.flac`, or `music.mp3`. Likewise, a request for
`music.ogg` looks for exactly that file. Ordinary WAV requests continue
using JK's original native I/O and decoder without modification.

The native sound loader now searches three directories, in this order:
`sound/`, `voice/`, then **`music/`**. This applies to WAV and
compressed sounds, including resources in the game's usual GOB/GOO archives.
Keep using the actual compressed extension in COG; there is no implicit
extension fallback. The search-path expansion patches two audited operands
in this specific `jkgm.exe`, for both the initial metadata lookup and the
later lazy PCM read. The original addresses are restored when the DLL unloads.

Format selection uses the extension; renaming a WAV file to `.ogg`
does not convert its contents into Ogg Vorbis.

## Implementation

The verified JKGM sound loader is detoured to install virtual HostServices
read/seek/size callbacks before invoking the native loader. When its file
open callback receives `.ogg`, `.flac`, or `.mp3`, JKGM opens exactly
the requested file through the game's original GOB/loose-file I/O, decodes
it to 16-bit interleaved PCM, and exposes the result as a virtual RIFF/WAVE
stream. Native WAV parsing and DirectSound playback are otherwise unchanged.

The decoders are compiled into renderer.dll: stb_vorbis, dr_flac, and
dr_mp3. No external codec DLL or FFmpeg executable is required. Only mono
and stereo streams are supported. Source files are limited to 64 MiB and
decoded PCM to 192 MiB; a bounded 128 MiB cache avoids repeated decoding.
This is load-time decoding, not streaming playback or JK's CD-music subsystem.

The compatibility guard is specific to the audited `jkgm.exe`: loader at
`0x004EF440`, WAV parser at `0x00437A30`, and HostServices globals at
`0x0082F0EC` / `0x00866BC0`. Unknown executable builds remain unmodified.

## Diagnosing silent sounds

Start with a short known-working WAV in the same sound directory to
confirm the existing JK sound call and audio device work. Replace it with
a *genuinely encoded* `.ogg` or `.mp3` and reference that exact extension.
Use the log to trace the following sequence:

1. `JKGM compressed sound native load requested:` — the native Sith
   loader detour was reached. If this never appears, the request did not
   enter that loader (or used another filename/entry path).
2. `JKGM compressed sound open requested:` — the HostServices bridge
   received an explicit compressed file-open request. If missing despite
   step 1, investigate the native loader's path or early rejection.
3. `JKGM compressed sound decoded:` — the encoded bytes were successfully
   converted into a virtual PCM WAV. `file not found` and `decode failed`
   warnings distinguish missing resources from invalid/unsupported data.
4. `JKGM compressed sound virtual WAV opened:` — JKGM returned a virtual
   file handle to the engine.
5. `JKGM compressed PCM first read:` — the native WAV parser actually
   requested bytes from that handle.
6. `JKGM compressed sound native load returned:` — the native sound loader
   completed, with its resulting sound pointer in the log.

If `JKGM sound HostServices live table` reports `bridge_active=0`,
the adapter was not installed at the moment of the check. The bridge now
verifies its original callback pointers and rebinds if JK resets them.
Unlike the earlier diagnostic build, it never invokes JK's original
fileOpen separately to probe resources; only the native sound loader
performs these file-opening requests.

A successful decoder test alone cannot prove the native Sith/DirectSound
integration works in the game. The final check remains actual playback,
volume, pitch, looping and spatialization in JKGM under Wine and Windows.

MP3 may add encoder padding. For short seamless loops, consider OGG or
FLAC. For positional 3D sounds, mono is generally preferred.
