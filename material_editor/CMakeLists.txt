cmake_minimum_required(VERSION 3.21)
project(JkGfxModMaterialEditor LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# Pin source releases and link all non-system GUI dependencies statically.
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b-docking
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(editor_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(editor_imgui PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(editor_imgui PUBLIC glfw)
target_compile_definitions(editor_imgui PUBLIC GLFW_INCLUDE_NONE)
find_package(OpenGL REQUIRED)
add_executable(jkgm-material-editor WIN32
  main.cpp
  preview.cpp
  preview.hpp
  editor_image.cpp
  editor_image.hpp
  native_mat.cpp
  native_mat.hpp
  ../base/md5.cpp
  source_pack.hpp
  editor_ini_settings.hpp
  ../glad/glad.cpp)
# Embed compact 512x256 PNG previews, NOT their 1024x512 HDR sources.
# ImageMagick runs only at build time; the Windows EXE stays standalone.
option(JKGM_REQUIRE_PRESET_HDRI "Require three embedded photographic previews" OFF)
if(WIN32)
  enable_language(RC)
  find_program(_magick_exe NAMES magick)
  set(_preset_count 0)
  set(_preset_rc "#include <windows.h>\n")
  set(_preset_names studio_small_02 hilly_terrain_01 vignaioli_night)
  set(_preset_id 401)
  foreach(_name IN LISTS _preset_names)
    set(_png "${CMAKE_CURRENT_BINARY_DIR}/${_name}_512.png")
    if(NOT EXISTS "${_png}" AND _magick_exe)
      set(_hdr "${CMAKE_CURRENT_BINARY_DIR}/${_name}_1k.hdr")
      if(NOT EXISTS "${_hdr}")
        file(DOWNLOAD
          "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/${_name}_1k.hdr"
          "${_hdr}" STATUS _download_result TIMEOUT 35 INACTIVITY_TIMEOUT 15
          TLS_VERIFY ON)
        list(GET _download_result 0 _download_code)
        if(NOT _download_code EQUAL 0)
          message(WARNING "Unable to fetch ${_name}: ${_download_result}")
          file(REMOVE "${_hdr}")
        endif()
      endif()
      if(EXISTS "${_hdr}")
        # Tone-map and quantize before embedding. The HDR never enters the EXE.
        set(_exposure 0.65)
        if(_preset_id EQUAL 402)
          set(_exposure 0.40)
        elseif(_preset_id EQUAL 403)
          set(_exposure 1.65)
        endif()
        execute_process(
          COMMAND "${_magick_exe}" "${_hdr}" -resize "512x256!"
            -evaluate Multiply "${_exposure}" -evaluate Pow 0.454545
            -clamp -colorspace sRGB -strip -depth 8
            "PNG24:${_png}"
          RESULT_VARIABLE _convert_status
          OUTPUT_QUIET ERROR_VARIABLE _convert_message)
        if(NOT _convert_status EQUAL 0)
          message(WARNING "Could not resize ${_name}: ${_convert_message}")
          file(REMOVE "${_png}")
        endif()
        if(EXISTS "${_png}")
          file(REMOVE "${_hdr}")
        endif()
      endif()
    endif()
    if(EXISTS "${_png}")
      file(SIZE "${_png}" _png_size)
      if(_png_size GREATER 128)
        file(TO_CMAKE_PATH "${_png}" _rc_path)
        string(APPEND _preset_rc "${_preset_id} RCDATA \"${_rc_path}\"\n")
        math(EXPR _preset_count "${_preset_count}+1")
        message(STATUS "Bundling ${_name} 512x256 PNG (${_png_size} bytes)")
      else()
        file(REMOVE "${_png}")
      endif()
    endif()
    math(EXPR _preset_id "${_preset_id}+1")
  endforeach()
  if(JKGM_REQUIRE_PRESET_HDRI AND NOT _preset_count EQUAL 3)
    message(FATAL_ERROR
      "All three compact environment PNGs are required (${_preset_count}/3). "
      "ImageMagick must be available on PATH for first-time builds.")
  endif()
  set(_preset_resource "${CMAKE_CURRENT_BINARY_DIR}/preview_environment.rc")
  file(WRITE "${_preset_resource}" "${_preset_rc}")
  target_sources(jkgm-material-editor PRIVATE "${_preset_resource}")
endif()

target_include_directories(jkgm-material-editor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../glad)
target_link_libraries(jkgm-material-editor PRIVATE
  editor_imgui glfw OpenGL::GL comdlg32 shell32 ole32)
target_compile_definitions(jkgm-material-editor PRIVATE
  GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(jkgm-material-editor PRIVATE /W3 /EHsc /utf-8)
  target_link_options(jkgm-material-editor PRIVATE /INCREMENTAL:NO)
endif()

# Pure JSON semantics; no GLFW/OpenGL or game DLL dependency.
add_executable(editor-source-pack-tests source_pack_tests.cpp source_pack.hpp)
target_include_directories(editor-source-pack-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
  target_compile_options(editor-source-pack-tests PRIVATE /W3 /EHsc /utf-8)
endif()

# Exercise portable ImGui settings and multi-folder MAT discovery without
# starting a GLFW window or OpenGL renderer.
add_executable(editor-settings-tests
  editor_settings_tests.cpp
  editor_ini_settings.hpp
  native_mat.cpp
  native_mat.hpp
  ../base/md5.cpp)
target_include_directories(editor-settings-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(editor-settings-tests PRIVATE editor_imgui)
if(MSVC)
  target_compile_options(editor-settings-tests PRIVATE /W3 /EHsc /utf-8)
endif()

# Test compact PNG encodings and compiler-compatible linear-space PMA.
add_executable(editor-image-tests
  editor_image_tests.cpp editor_image.cpp editor_image.hpp)
target_include_directories(editor-image-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
  target_compile_options(editor-image-tests PRIVATE /W3 /EHsc /utf-8)
endif()

# Pure C++ tests for renderer-parity scroll wrapping and pulse waveforms.
add_executable(editor-animation-tests
  editor_animation_tests.cpp editor_animation.hpp)
target_include_directories(editor-animation-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
  target_compile_options(editor-animation-tests PRIVATE /W3 /EHsc /utf-8)
endif()

# Headless policy regression: a material, preset, or custom image may be
# selected for reflections regardless of the separate illumination preset.
add_executable(editor-environment-tests
  preview_environment_tests.cpp preview_environment.hpp)
target_include_directories(editor-environment-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
  target_compile_options(editor-environment-tests PRIVATE /W3 /EHsc /utf-8)
endif()
