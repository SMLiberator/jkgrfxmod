# JkGfxMod Material Editor

Windows-focused material authoring and **live in-editor visualizer**. This project
is fully independent of the game renderer; there is no in-game hot reload.

## First prototype

- Open an existing compiled material pack's metadata.json.
- Inspect the pack's material list.
- Preview selected material on a rotatable sphere or plane.
- Use an existing material-specific equirectangular environment map, falling
  back to the pack default. When neither exists, choose a generated studio,
  outdoor, or night environment. A separate custom image can temporarily
  override the preview environment.
- View albedo and packed surface textures (R=reflectivity, G=roughness,
  B=metalness), with approximate reflection and clearcoat lighting.
- Edit albedo factor, reflection strength, roughness, metalness, clearcoat
  strength, clearcoat roughness, and clearcoat transmission. Values update
  the preview immediately.
- Save JSON atomically without regenerating MAT signatures, while preserving
  other metadata entries, material names, original replacements and signatures.

**Current preview is a visual prototype, not exact renderer parity.** It does
not yet include normal maps, MatCap, emissive lighting, alpha modes or
parallax; all reflection modes use the same environment image in the viewer.
The generated environment presets are deliberately simple. A physically
accurate preview will require later editor-side implementation and comparison.

This prototype does not yet create packs, import textures, generate MAT
signatures, or replace compile.exe. Those operations will be integrated after
extracting the existing compiler routines into a reusable incremental core.
For now, compile new materials using the existing compiler, then open the
resulting metadata.json in the editor.

## Windows build

Requires Visual Studio 2022, CMake >=3.21, and Qt >=6.5 with the
Widgets, OpenGL and OpenGLWidgets modules.

Example (PowerShell):

    cmake -S material_editor -B build/material-editor -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64"
    cmake --build build/material-editor --config Release --target jkgm-material-editor

The editor has its own CMake project rather than being included in the
renderer solution. The Material Editor GitHub Actions workflow builds only
this target and deploys its runtime Qt libraries into an artifact.

The editor intentionally does not modify the renderer or any renderer tests.
