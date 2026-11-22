# JkGfxMod Material Editor (Windows)

A standalone Windows material editor using **Dear ImGui + OpenGL 3.3**.
There are no Qt libraries, external GUI DLLs, game EXE, renderer DLL,
or in-game hot-reload dependencies.

## Preview-first milestone

- Open a compiled pack's metadata.json, browse and search materials.
- Rotate a 3D sphere with the mouse or select a plane for surface editing.
- Preview a material-defined or pack-wide equirectangular EnvMap, or a
  generated studio, outdoor or night fallback. Optionally choose a separate
  preview-only environment image.
- Edit albedo factor, base reflection strength/roughness/metalness, normal
  strengths, MatCap factor/roughness, emissive factor and clearcoat properties.
- Read compiled albedo, surface (R=strength, G=roughness, B=metalness),
  normal, emissive, MatCap and clearcoat maps. Live values update the viewport.
- The preview approximates the renderer's important layering conventions:
  sRGB color maps vs linear data maps, Schlick Fresnel, GGX-like direct
  highlights, MatCap as artistic color, clearcoat Fresnel/transmission,
  and roughness-based EnvMap mip selection.
- Save edited material properties into metadata.json with Ctrl+S, preserving
  unknown fields, MAT replacements, material names and signature hashes.

**Preview fidelity:** this does not yet reproduce in-game sector lighting,
shadows, fog, HDR auto-exposure, parallax, animation or alpha composition.
It uses one equirectangular EnvMap for ALL reflection modes, including game
modes that normally capture live scenes. These modes are not equivalent
in-game: the studio preview is a deliberate approximation for editing.
Renderer parity should eventually be checked using captured reference images.

**Compiler status:** This milestone cannot yet import/convert new image files
or generate MAT replacement signatures. Continue using the existing
command-line compiler for initial pack compilation; then edit the compiled
metadata in the GUI. Incremental compilation remains the next development phase.

## Portable Windows build

Requires Visual Studio 2022, CMake >=3.21, and build-time access to the
pinned Dear ImGui v1.92.9b-docking and GLFW 3.4 source repositories. It also
uses the existing repository GLAD and stb_image code.

    cmake -S material_editor -B build/material-editor -G "Visual Studio 17 2022" -A x64
    cmake --build build/material-editor --config Release --target jkgm-material-editor

Dear ImGui and GLFW are compiled into static libraries, and MSVC uses the
static /MT runtime. The CI artifact contains ONLY jkgm-material-editor.exe.
Standard Windows system DLLs and a working OpenGL 3.3 driver remain required.

The editor has its own CMake target and GitHub Actions workflow. It does not
build jkgfxmod.sln, the renderer, or any renderer regression tests.

Dear ImGui, GLFW, GLAD and stb_image are third-party components under their
respective licenses; see the repository's third-party attribution and upstream
source distributions for notices.

## Parameter and environment preview

Reflection strength is normally adjusted with a 0..1 slider, while the
adjacent numeric field accepts any finite, nonnegative value (the same range
as the existing compiler). Roughness, metalness, clearcoat channels and
MatCap roughness are always 0..1. Normal strengths are 0..4; per-channel
MatCap factors are 0..4. All saved values use the existing renderer metadata.

The preview renders the selected material using an environment image and
studio-style lighting, without the former three-way shading diagnostics.
Unicode Windows filesystem paths are supported for image loading.

## Preview Fresnel toggle

The compact "Fresnel" checkbox defaults to off, showing a stronger EnvMap
reflection on surfaces viewed nearly head-on. Enabling it previews the
renderer-style Fresnel response. The toggle affects only environment-map
reflection terms and does not change direct specular highlights, clearcoat
transmission/attenuation, or saved material properties.

## Property panel layout

The properties are ordered Albedo, Emissive, Reflections, Normal maps,
Clearcoat, MatCap, and other settings. Color fields use their own full-width
row with the label above. Referenced texture paths appear below the color
field, matching the Emissive section rather than wrapping in a narrow
right-hand column.

## Undo and redo

Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z and the Edit menu undo/redo edits to the
compiled metadata. Slider and color drags are grouped into one undo action,
not one per frame. The Materials pane also has Undo and Redo buttons. History
is bounded to 128 actions, spans materials, restores the edited material
selection, and clears redo when a new change is made. Saving establishes a
new clean baseline, so undoing a saved change correctly restores the unsaved
indicator. Undo/redo history is cleared when another pack is opened.

## Preview usability and native JK texture support

- Press **Space** to reset the current sphere or plane orientation. The preview
  also has a Reset view button.
- **Fresnel** is a compact preview-only checkbox, disabled by default.
  Enable it when comparing the material's actual Fresnel-based EnvMap response.
  Preview source diagnostics are hidden to leave more room for the model.
- The three panels are docked to the main ImGui viewport, and resize along
  with the application window. Their divisions remain adjustable.
- Alpha is previewed against a checkerboard (rather than black), with
  smooth blend, alpha-test mask with cutoff, and ordered dither modes.
  Compiled color PNGs are correctly un-premultiplied in linear space before
  their final alpha coverage is composited. Basic additive and multiply
  blend-mode behavior is supported in the preview.
- **Keep Albedo** material previews can now load an original MAT texture.
  Select the game's assets directory under Material Properties > Albedo:
  the editor indexes loose MATs and CMPs and the entries inside GOB/GOO
  archives. The first replacement record's MAT cel is used for the preview.
  A 16-bit native MAT can load without a palette; 8-bit MATs need a matching
  CMP palette. The editor attempts the standard dflt.cmp first and offers
  a separate CMP picker for other game/level colormaps. This uses an
  editor-side reader based on the compiler's MAT/CMP layouts, and never
  modifies the original MAT, material pack, or renderer.
- If the original game assets have not been selected, native MAT pixels
  cannot be recovered from compiled metadata alone: the material preview
  uses an untextured fallback and displays instructions in the Albedo panel.

All game-asset source selections, shader diagnostics, Fresnel state and
rotation are **preview-only**; saving continues to write only material
property changes into metadata.json.
