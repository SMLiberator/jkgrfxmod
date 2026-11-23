# JkGfxMod Material Editor (Windows)

A standalone Windows material editor using **Dear ImGui + OpenGL 3.3**.
There are no Qt libraries, external GUI DLLs, game EXE, renderer DLL,
or in-game hot-reload dependencies.

## Revert commands

**File > Revert** reloads the open `script.json` or `metadata.json` from
its current disk path. If the document has unsaved edits, a confirmation
explains that all unsaved pack/material changes will be lost and the
document's Undo/Redo history cleared. Loading happens into a temporary
JSON document first, so missing or malformed disk content leaves the
working document untouched. The selected material is retained by name
where possible, otherwise by index. The same command also refreshes
externally edited files when no unsaved changes exist.

**Edit > Revert Material** restores the selected raw material object from
the editor's most recent saved/loaded document state, without changing
other materials or pack-wide defaults. It is available only when the
selected material differs from that saved state. This action is a
single Undo/Redo step and resolves source-pack inheritance normally.
Neither revert command deletes or rewrites image files on disk.

## Texture alpha conventions

Data maps (normal, surface R/G/B, displacement R, clearcoat and MatCap
surface masks) do not use their alpha channels in the current preview
and game shaders. The editor writes new data imports without alpha.
The standalone pack compiler still emits RGBA for those maps; compact
compiled output from script.json will require the same change there.

Emissive images are **not** equivalent to data maps: the existing
compiler converts their sRGB to linear light, multiplies RGB by input
alpha, and encodes back to sRGB. The shader then uses the stored RGB.
Consequently, emissive alpha provides an indirect intensity mask.
Automatically discarding it from existing images would alter their
appearance. Image conversion/loading is a separate feature from these
Revert commands.

## Deferred texture loading and compact PNG saving

Material images can be loaded and previewed **entirely in memory**.
Texture controls are deliberately compact: a thumbnail and **Load** /
**Remove** buttons. **Load** stages a converted image for saving in both
source and compiled packs. **Remove** clears the local material override
(or removes a compiled material's assignment). Staging creates no disk
file and does not alter MAT replacement signatures.

Click the thumbnail to inspect its original filename, dimensions, file
size and RGBA/R/G/B/A channels. Right-click a thumbnail in source mode
to **Reference existing file...** without importing or to edit the raw
source filename. Pack-default textures offer the same compact controls.
Source maps inherited from pack defaults have no local override to remove.

The editor uses the pack compiler's linear-space alpha premultiplication
for sRGB color textures. **Albedo** retains alpha if any pixel is
transparent; otherwise it saves as RGB. **Emissive**, **MatCap**, and
**environment** color images bake their alpha into RGB, then discard
the unused alpha channel. **Surface, normal, clearcoat and MatCap mask**
textures preserve RGB as data without gamma conversion or alpha;
**displacement** saves only the red channel as an 8-bit grayscale PNG.
On source packs, imported albedo remains straight-alpha so the regular
pack compiler doesn't premultiply it twice. All imported images require
power-of-two dimensions to remain compatible with the compiler.

The preview uploads staged and saved image pixels directly to OpenGL,
using matching RGB8/sRGB8/R8 formats where applicable instead of forcing
all image buffers to RGBA8. The R/G/B/A channel inspector can inspect
staged pixels too. The standalone game renderer's texture upload path
is separate and may require changes to realize equivalent GPU savings.

**Save** writes only staged images still referenced by the document,
using a unique filename and PNG encoding. PNGs are prepared before
writing JSON, and metadata is replaced only after the images have been
successfully committed. Write failures roll back newly created PNGs;
existing files are not overwritten. **Undo, Revert Material, and Cancel**
change image references without deleting files. Drafts never saved are
discarded when the pack is closed or File > Revert is used. Images that
have already been saved are kept available for Undo/Redo without being
written twice.

Texture details appear **on demand** inside the thumbnail popup, rather
than occupying multiple lines in every material section. Packed surface
maps expose their reflection/roughness/metalness channels there; clearcoat
masks expose coverage/roughness/transmission. Channel views are generated
lazily and cached until a texture is reloaded. Missing or unsupported
images retain their filename without cluttering the panel.

Normal maps are organized as two distinct groups: **Normal map 1** image
followed immediately by **Normal map 1 strength**, then **Normal map 2**
image and **Normal map 2 strength**. The old duplicate **Compiled textures**
section and stale read-only warnings have been removed.

Material feature headers now start open only if active: albedo is always
open; emissive is active for map/color/pulse; reflections for a surface map
or positive strength; normal/parallax/MatCap for their texture maps; clearcoat
for an active layer or map. Inherited source-pack properties count as active.
Per-material expansion state remains under user control, and adding a new
feature opens its section without auto-collapsing other sections.

## MAT replacements and original albedo

**MAT replacements** is open by default in Material Properties. In a
source `script.json`, each entry exposes the in-game MAT resource path and
cel index, with Add/Remove controls. Editing a bare filename keeps its
short-string form (cel 0); changing the cel serializes a
`["mat/filename.mat", cel]` tuple. Multiple replacement entries remain
independent, and the path may refer to a MAT inside a GOB/GOO archive rather
than to a loose file. Empty paths are rejected on Save. These edits participate
in normal Undo/Redo, Save, Revert Material and File > Revert.

In compiled `metadata.json`, MAT paths and cel indices are **editable**.
When a replacement changes, Save regenerates that material's
`replaces_signatures` using the compiler-compatible MD5 algorithm and
the configured JK/MotS game assets. Each 16-bit MAT mipmap is hashed with
its dimensions; indexed 8-bit textures are hashed under **every indexed CMP
palette**, matching the pack compiler. The operation fails without the
necessary MAT or CMP files rather than writing stale signatures.
Unchanged MAT mappings keep their existing signatures and can be saved
without game assets. Changes are staged and committed with the JSON
atomically. All asset roots and colormap files need to correspond to the
same game resources the original pack compiler would use.

The **Materials** tab has **Add**, **Duplicate**, and **Remove** actions
(with deletion confirmation). **Duplicate** (also **Edit > Duplicate Material**
or **Ctrl+D**) makes an independent copy
of the selected material's settings, image references, and MAT targets
(including existing compiled signatures), assigns a unique `(copy)` name,
and selects the copy. Change its MAT replacements when making a new
variant for another game texture. Both source and compiled materials can
be renamed in Material Properties. New materials start with a unique name
and an empty MAT replacement that must be completed before saving.
Structural Add/Duplicate/Remove edits use a single Undo/Redo step and
restore selection correctly. A current material is not silently reverted
to a different saved material after a structural list change.

**Keep original MAT albedo** is available for source and compiled materials.
It uses the first MAT replacement's original image (and cel) for preview,
when corresponding game assets are configured. It is mutually exclusive
with an assigned `albedo_map`: selecting Keep removes a local image
assignment; importing/referencing an image explicitly disables Keep.
Source-pack defaults expose this option immediately beside Default Albedo.
A material-level Keep override can suppress an inherited pack albedo map,
without flattening defaults. Neither option changes the MAT replacement or
regenerates compiled signatures. If no game assets are available, the
original albedo preview remains unavailable until they are configured.

The Live Material Preview toolbar arranges Shape, Environment, and
Animation & View controls into responsive columns, with compact
pause/resume and restart controls.

## Material animations

The Albedo, Emissive and Normal maps sections expose the engine's existing
animation properties. **Scroll (U,V) / sec** is a signed pair of texture repeats
per second; Normal map 1 and Normal map 2 have separate speeds. Emissive can
also use a pulse with **Sine**, **Square**, **Flicker** or **Random** waveform,
cycle **rate** in seconds, **minimum/maximum multipliers** and a **phase**
offset (cycles). Pulse affects emissive factor even without an emissive image.

Source-pack **Animation defaults** can be inherited and independently
overridden by materials. The **x** control removes a local scroll override,
restoring the pack default where applicable. **Remove pulse** does the same
for a local pulse. A material that inherits a pulse can select
**Neutralize inherited pulse** to set a 1.0 multiplier without modifying
the pack default. Edits participate in Undo/Redo and ordinary Save/Revert.

The live preview applies the same scroll offsets and pulse waveform math as
the game. **Animate** pauses/resumes the preview clock and **Restart
animation** resets it to zero; neither changes pack metadata. Preview random
waveforms use the selected material's stable list index as a seed (the game
uses a runtime instance ID), so exact random values can differ in-game.
All other surface masks continue to use the original UV coordinates.

## Slider and numeric input behavior

Parameter names and their numeric values appear **inside** standard slider
and drag controls. Emissive pulse **Wave**, **Rate**, **Minimum**, **Maximum**
and **Phase** now remain legible even in the narrower inspector panes;
U/V scrolling and RGB fields show their individual channel names.
**Drag** with the left mouse button for continuous adjustments, or
**double-click** a numeric slider/drag to type an exact value (also
accessible with Ctrl+click). An ordinary left click continues to adjust
the slider; it does not unexpectedly erase the value.

Click **Reset** beside a parameter to restore the corresponding default.
On source materials, resetting a local numeric override reveals the
pack-inherited value. Pulse parameters have individual Reset buttons
to restore their canonical defaults while preserving other pulse settings.
The live preview's **Lighting > Reset** button restores **1.00×** without
changing any material or pack JSON. A separate exact-value box remains for
reflection strength, since the game permits values above its 0–1 slider.
All material resets participate in Undo/Redo.

## Source and compiled pack workflows

The editor now supports both pack document modes:

- **Source**: Open a `script.json` (or a JSON file containing source-format
  MAT replacements). Change pack name, description, comment and environment
  map; edit numeric/material properties, material names and relative source
  texture filenames; manage **Source material defaults** in Pack Properties.
  The preview resolves inherited defaults without writing them into each
  material. Any material edit becomes an explicit per-material override;
  removing an override returns it to the source pack's inherited value.
  Source MAT replacements stay as strings or `[filename, cel]` tuples.
- **Compiled**: Open `metadata.json` or a pack with compiled replacement
  records. Edit material names and MAT replacements as well as properties;
  changed mappings generate fresh signatures when saving. Existing
  image paths and unrelated metadata are preserved.

The Materials window shows the active **SOURCE PACK** or **COMPILED PACK**
mode. **File > Save** writes the currently opened JSON type in place,
without flattening inherited source defaults. **Save As** defaults to a
sibling `-copy.json` file. To prevent breaking compiler-relative image
paths, source Save As currently requires a destination in the same folder
as the original script. The saved source remains usable as the input to
the existing pack compiler; compilation from within the editor is a later
milestone.

Source images are loaded directly from the script's folder. Referenced
source albedo remains straight-alpha; compiled albedo is premultiplied.
Image paths can be typed into source material slots or chosen through
the file picker. **Import PNG (on Save)...** stages a converted image
without writing to disk until Save. The previous eager Import Copy
behavior has been replaced with this deferred workflow.
Integrated source-pack compilation remains future work; compiled MAT
signature regeneration is implemented. Staged imports are now converted
and saved as PNGs next to JSON;
existing source files referenced through Reference... are never converted
in place.

Pack defaults follow the compiler's existing inheritance logic,
including `keep_albedo` versus `albedo_map` and the inherited alpha-cutoff
rule. Undo/Redo tracks pack defaults and material overrides independently.
The Windows CI now runs source inheritance/serialization tests, including
the repository's real `assets/sample_matpack/script.json` fixture.

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
- Save material changes with Ctrl+S, preserving unknown fields and
  recalculating compiled MAT signatures only when mappings change.

**Preview fidelity:** this does not yet reproduce in-game sector lighting,
shadows, fog, HDR auto-exposure, or the full in-game alpha composition.
It uses one equirectangular EnvMap for ALL reflection modes, including game
modes that normally capture live scenes. These modes are not equivalent
in-game: the studio preview is a deliberate approximation for editing.
Renderer parity should eventually be checked using captured reference images.

**Compiler status:** The editor can now decode, process, preview and
write compiler-compatible source/compiled PNG image data. Compiled
replacement signatures are updated on Save when required, using configured
game assets and all indexed colormaps. The editor does not compile entire
source packs into game-ready output; use the command-line pack compiler
for `script.json`.
Integrated compilation remains a separate development milestone.

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
preset-dependent ambient and directional lighting, without the former three-way shading diagnostics.
Unicode Windows filesystem paths are supported for image loading.

## Lighting intensity and embedded environment images

The preview has three lighting presets. **Studio** retains the original
neutral fill and direct-specular levels; **Outdoor** adds brighter, warm
sunlight and cool ambient fill; **Night** uses weak, cool-toned illumination
so emissive signs and lamps are easier to inspect. The preview-only
**Intensity** slider (0.0–2.0x, default 1.0) multiplies ambient/directional
diffuse and direct-specular illumination, but **never changes the material's
emissive factor, MatCap contribution, or environment reflections**. Its value
does not enter script.json or metadata.json.

**EnvMap** chooses the image used for reflections, independently from
**Preset**, which sets the current lighting profile. The source menu has
three choices: **Material / pack** (use the authored image, if assigned),
**Preset** (force the Studio, Outdoor or Night panorama even if a material
EnvMap exists), and **Custom image** (enabled after using **Load custom...**).
A missing material/custom image falls back to the current preset panorama.
Switching sources does not overwrite any pack property or discard a loaded
custom image. This is useful when an in-game global EnvMap changes at
runtime: the material's authored image is not necessarily what the game
will reflect. The preview never updates the in-game material.

To minimize the EXE size as well as preview memory, the build downloads
**1K (1024×512) HDR source panoramas**, converts and shrinks them to
**512×256 8-bit RGB PNGs**, and embeds **only those compact PNGs** in
the EXE. The HDR inputs remain build-time intermediates and are not
shipped or decoded at runtime.

Instead of the old flat-gradient reflection environments, the Windows build
bundles three reduced, tone-mapped 512×256 photographic panoramas into
the EXE as Win32 resources:

- **Studio:** [Studio Small 02](https://polyhaven.com/a/studio_small_02)
- **Outdoor:** [Hilly Terrain 01](https://polyhaven.com/a/hilly_terrain_01)
- **Night:** [Vignaioli Night](https://polyhaven.com/a/vignaioli_night)

The photographs are by Poly Haven contributors, published under
[CC0](https://polyhaven.com/license). The source HDRs are downloaded
from Poly Haven's asset CDN at build time; ImageMagick converts them
to the embedded PNGs during CMake configuration. No additional images
are needed at runtime. If a local/offline build cannot fetch or convert
an image, the editor can use its generated fallback. Windows CI requires
all three converted PNGs before publishing the executable. These are tone-mapped LDR reflection
images in the preview, **not** physically integrated HDR image-based lighting;
the simple diffuse/directional shader still supplies its approximate lighting.

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
  Configure the game's assets directory and colormap under **File > JK / MotS
  game assets**. The editor indexes loose MATs/CMPs and entries in GOB/GOO
  archives. The first replacement record's MAT cel is used for the preview.
  A 16-bit native MAT can load without a palette; 8-bit MATs need a matching
  CMP palette. The reader attempts the standard dflt.cmp first and lets you
  choose a separate CMP for other game/level colormaps. This uses an
  editor-side reader based on the compiler's MAT/CMP layouts, and never
  modifies the original MAT, material pack, or renderer.
- If the original game assets have not been selected, native MAT pixels
  cannot be recovered from compiled metadata alone. The material preview
  uses an untextured fallback and points to the File menu.

**Portable game-asset settings:** Dear ImGui's existing `imgui.ini`
now contains a custom `[JkGfxModEditor][Config]` section for the ordered
list of JK/MotS asset folders and the selected CMP. The INI file is placed
beside `jkgm-material-editor.exe` instead of depending on the process's
working directory. With a writable EXE directory, moving the EXE together
with `imgui.ini` preserves both the docking layout and game-asset settings.
Paths within the executable's directory tree are stored relative to the
EXE, while external asset locations are saved as absolute paths.

Use **File > JK / MotS game assets > Add assets folder...** repeatedly to
configure multiple folders. Each folder indexes loose MAT/CMP resources and
GOB/GOO archives. Earlier folders take precedence for resource-name
collisions, and loose resources take precedence over archive entries within
the same folder. Open a folder's submenu to move it up/down or remove it.
The menu also supports clearing the list and selecting/clearing the CMP.

The previous `%APPDATA%\\JkGfxMod\\material-editor.json` settings file is
imported automatically on the first launch without an existing custom INI
section, then settings are written to `imgui.ini`. The old file is left
untouched for safety, but is not written again. Changing folder order or
the CMP immediately refreshes the native MAT preview, without dirtying
material pack JSON. Preview Fresnel, camera controls, and diagnostics
remain preview-only.

## Preview navigation

The preview now orbits an upright camera around the stationary material,
rather than applying quaternion rotation to the object. With either left
mouse drag, horizontal motion changes yaw around world +Y, and
vertical motion changes the camera's pitch (drag up to orbit downward).
**Middle mouse drag pans** instead of rotating. Up remains up, there is no
unintended camera roll, and pitch stops just before the poles. Each drag
starts from the mouse-down view. Space resets rotation, pan and zoom.

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
unrecognized metadata fields and signatures for unchanged mappings. Editing registry paths does not copy image files into the pack;
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

## Preview zoom and in-app Dear ImGui development tools

The preview shape controls are full-size, highlighted buttons in the order
**Sphere**, **Cube**, **Plane**, with a larger and more reliable clickable
rectangle than the former radio controls. Changing the shape resets the
camera. Scrolling the mouse wheel over the preview zooms its orbit camera,
without modifying the rendered material or zooming when the pointer is over
other panels. Zoom uses a bounded, multiplicative distance (1.35–16 units).
Space or Reset view restores each shape's default camera (4.4-unit distance
for Cube; 3.5 for Sphere and Plane).

**Tools** in the top menu exposes Dear ImGui's built-in **Demo**, **Metrics /
Debugger** and **Style Editor**. The demo showcases widgets with working
examples, Metrics helps inspect item bounding boxes/hover handling and
docking, and Style Editor allows experimenting with padding, colors and
spacing without recompilation. Runtime style edits are temporary unless
transcribed to source. This is intended to help direct UI customization; a
separate visual code generator such as ImRAD can be used for new layouts.

## Preview panning and cube framing

Left mouse drag rotates the view; **middle mouse drag moves (pans) the
view**, following the cursor in the camera's right/up plane. Pan is
measured from the start of each gesture and scales with current zoom and
viewport height. The camera target and view transform move together, so
normal/specular/parallax view vectors remain consistent with the camera.
Wheel zoom is unchanged.

The Cube preview resets to a distance of 4.4 instead of 3.5. Its default
target is now calculated from the perspective-projected eight cube corners
so the projected bounding rectangle is centered in the preview. The old
manual +Y offset was removed. Sphere and Plane retain their previous
starting distance and centering.
Space, Reset View and switching preview shapes restore each shape's
rotation, zoom and pan to their default values.

## Projection mode

The **Orthographic** preview checkbox switches between perspective
(default) and orthographic without editing material metadata. Orthographic
keeps edges parallel, uses the same wheel zoom and orbit/pan controls, and
matches perspective's apparent size at the camera target distance. Direct
lighting, reflections and parallax all use parallel view rays in
orthographic mode. Perspective remains the default for every preview shape.
Reset View recalculates the Cube's centered projected bounds for whichever
projection is active. Toggling projections on a Cube compensates for the
change in its optical center while preserving the user's manual pan,
rotation and zoom.
