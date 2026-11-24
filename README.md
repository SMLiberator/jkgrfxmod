# JkGRfxMod Material Editor

This `material-editor` branch contains the **standalone Windows material editor** for JkGRfxMod/JkGfxMod material packs. It is intentionally separate from the full game renderer and loader.

For setup, features, pack formats, and usage instructions, see the **[Material Editor guide](material_editor/README.md)**.

## Build

Requirements: Windows, Visual Studio 2022 (MSVC), CMake 3.21+, internet access for first-time GLFW/ImGui FetchContent setup, and an OpenGL 3.3-capable driver.

```powershell
cmake -S material_editor -B build/material-editor -G "Visual Studio 17 2022" -A x64
cmake --build build/material-editor --config Release --target jkgm-material-editor
```

The executable is `build/material-editor/Release/jkgm-material-editor.exe`. The build produces a standalone portable Windows EXE; Windows and OpenGL system libraries are still required.

For the three bundled 512×256 photographic environment previews, install ImageMagick (`magick`) and configure with `-DJKGM_REQUIRE_PRESET_HDRI=ON`. An offline build may use the generated environment fallback instead.

Run regression tests with the `editor-*-tests` targets, or use the **Material Editor** GitHub Actions workflow.

## Scope

The editor can open and save source `script.json` and compiled `metadata.json` material packs, edit textures and material properties, preview animations, and regenerate changed compiled MAT replacement signatures using configured Jedi Knight/MotS game assets. It **does not bundle the game**, the renderer, or the separate source-pack compiler.

The full JkGRfxMod engine, command-line compiler, and renderer development live on the **[main project branch](https://github.com/SMLiberator/jkgrfxmod/tree/master)**. This branch is a self-contained editor distribution; merging it back into the full engine branch without reconciling the tree would also remove unrelated engine files.

## Licensing

See [LICENSE.md](LICENSE.md) and [THIRDPARTY.md](THIRDPARTY.md). Dependencies downloaded at build time retain their upstream licenses. The photographic environment presets are from Poly Haven under CC0.
