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
  native_mat.cpp
  native_mat.hpp
  source_pack.hpp
  ../glad/glad.cpp)
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
