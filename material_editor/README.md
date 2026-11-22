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
shadows, fog, HDR auto-exposure, animation or the full in-game alpha composition.
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

## Preview navigation

The preview now orbits an upright camera around the stationary material,
rather than applying quaternion rotation to the object. With either left
or middle mouse drag, horizontal motion changes yaw around world +Y, and
vertical motion changes the camera's pitch (drag up to orbit downward). Up remains up, there is no
unintended camera roll, and pitch stops just before the poles. Each drag
starts from the mouse-down view. Space resets the view.

The environment map, direct studio light, normal mapping, and surface UVs
remain in world/object space while the camera orbits; MatCap is evaluated
in the updated camera space. This avoids rotating the environment and
lighting together with the material, keeping camera movement predictable.

## Emissive-only compositing and plane proportions

Normal, Additive and Fluid emission now contributes independently of the
base albedo alpha, following the game rendering path. An emissive-only
material with albedo factor [0,0,0,0] still displays its emissive color.
Masked/dithered pixels suppress both albedo and emission when discarded;
Multiply mode biases its multiplier instead of adding emission. The preview
does not replicate HDR bloom or exposure, so very bright emission may still
appear different from the final game image.

Plane mode scales the preview surface according to the actual loaded albedo
image dimensions, including original JK MAT albedo. If no usable albedo
texture is loaded, it uses the emissive texture dimensions instead. If
neither texture exists the plane remains square. UV coverage stays 0..1,
and the longer dimension stays at the normal preview plane size.

## Parallax occlusion preview

The Parallax mapping properties section exposes `displacement_map` and the
signed `displacement_factor` with an unrestricted numeric/drag input.
The preview matches the 3DO renderer's tangent-space parallax occlusion
sampling (32 to 128 layers, red height channel and linear interpolation).
Displaced UVs are used by albedo, normal, emissive, surface, clearcoat and
other texture layers; an absent map or a factor of zero disables the effect.
This change remains editor-only, with no new material metadata keys.

## Unsaved changes and Save As

The File menu includes Save As (Ctrl+Shift+S). Exiting through the window
close button or File > Exit with unsaved edits opens an ImGui confirmation
with **Save As...**, **Close without saving**, and **Cancel**. The same
confirmation is used before replacing a dirty pack (with **Open without
saving** in place of the close action). Save As defaults to a `-copy.json`
sibling file and uses a native Windows save picker with an overwrite prompt.
If the picker is cancelled or the write fails, the confirmation stays open
and no changes are discarded. Saving a copy preserves all existing JSON
fields, including replacement signatures, and adopts the chosen path as the
current document. Save As writes the JSON only; it does not duplicate the
compiled image files, so keep copies alongside the original assets when
possible.

## Pack metadata editing

The **Materials** window has **Materials** and **Pack Properties** tabs.
The Pack Properties tab, to the right of the Materials tab, edits the
compiled pack's top-level `name`, `description`, optional `comment`,
and optional `environment_map`. The **Material Properties** window is now
reserved for the selected material. The optional compiled `images` registry
can be expanded to edit, add, or delete `{id, file}`
records, with the same unique 0–65535 ID and filename rules used by the
runtime. The editor updates the preview when the pack EnvMap changes.

Pack metadata changes share Undo/Redo, dirty-state tracking, Save, Save As,
and the unsaved-changes dialog with material changes. The editor preserves
all unrecognized metadata fields, existing materials, and replacement
signatures. Editing registry paths does not copy image files into the pack;
missing files are indicated in the UI.

## Preview shapes and reset behavior

The editor offers Sphere, Plane and Cube preview geometry. Cube uses six
flat-shaded faces with separate outward normals and full 0–1 UV coverage,
so each material texture is shown once per face. Switching shapes resets
the camera to a useful starting angle: Plane face-on, Sphere at its familiar
three-quarter view, and Cube angled to show three sides. Press Space or use
Reset view to restore the current shape's starting view at any time.

Pack Description and Comment inputs now enable Dear ImGui's native multiline
word-wrapping flag. Lines wrap to the field width when typing and when the
Materials window is resized, without inserting newline characters into
stored metadata.
