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

## Parameter and environment diagnostics

Reflection strength is normally adjusted with a 0..1 slider, while the
adjacent numeric field accepts any finite, nonnegative value (the same range
as the existing compiler). Roughness, metalness, clearcoat channels and
MatCap roughness are always 0..1. Normal strengths are 0..4; per-channel
MatCap factors are 0..4. All saved values use the existing renderer metadata.

The viewer reports which EnvMap loaded or whether an image was missing and
the built-in preset was substituted. Studio mode keeps the renderer's
Fresnel-scaled EnvMap contribution; "EnvMap reflection only" removes the
added studio direct highlight and diffuse; "Raw EnvMap" displays the texture
unweighted to diagnose source images, independent of material reflectivity.
The two diagnostic display modes do not alter saved metadata. Unicode
Windows filesystem paths are supported for image loading.

## Preview EnvMap brightness

The default preview boost is 3x to make reflected environments legible,
especially on a plane viewed nearly head-on. The 1x (parity) button restores
the unboosted renderer-style EnvMap radiance. Gain multiplies only the EnvMap
reflection layers **after** Fresnel/roughness and does not modify direct
specular highlights, raw EnvMap diagnostics, or saved material properties.

## Undo and redo

Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z and the Edit menu undo/redo edits to the
compiled metadata. Slider and color drags are grouped into one undo action,
not one per frame. The Materials pane also has Undo and Redo buttons. History
is bounded to 128 actions, spans materials, restores the edited material
selection, and clears redo when a new change is made. Saving establishes a
new clean baseline, so undoing a saved change correctly restores the unsaved
indicator. Undo/redo history is cleared when another pack is opened.
