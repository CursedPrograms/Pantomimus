cmake_minimum_required(VERSION 3.20)
project(Pantomimus VERSION 1.0.0 LANGUAGES C CXX)
# Pantomimus, Atelier's webcam motion capture: full-body poses from a webcam (MediaPipe BlazePose through ONNX Runtime),
# solved onto a humanoid skeleton, recorded, cleaned and exported (.bvh, .fbx, .glb) or streamed live.
#   Pantomimus   the window (SDL2, OpenGL 3.3, Dear ImGui's docking branch); --selftest and --script for the tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(EXT ${CMAKE_SOURCE_DIR}/external)
include(FetchContent)
# Dear ImGui's docking branch: the panels resize, move and tab together
if(EXISTS ${EXT}/imgui-1.90.4-docking/imgui.h)
    set(FETCHCONTENT_SOURCE_DIR_IMGUI ${EXT}/imgui-1.90.4-docking)
endif()
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4-docking.tar.gz)
FetchContent_MakeAvailable(imgui)
if(WIN32 AND NOT SDL2_DIR AND EXISTS ${EXT}/SDL2-2.30.9/cmake)
    set(SDL2_DIR ${EXT}/SDL2-2.30.9/cmake)
endif()
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
set(STB_DIR ${EXT}/stb)
foreach(f stb_image.h stb_image_write.h)
    if(NOT EXISTS ${STB_DIR}/${f})
        file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/${f} ${STB_DIR}/${f})
    endif()
endforeach()
# ONNX Runtime's C API: the header here, the library loaded at run time (onnxruntime.dll / libonnxruntime.so)
set(ORT_DIR ${EXT}/onnxruntime)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# the parts without a window (tracking, solving, clips, exports, links): the app and the tests use them
add_library(panto_core STATIC
    src/pose.cpp src/filter.cpp src/skeleton.cpp src/solver.cpp src/clip.cpp src/export.cpp src/link.cpp
    src/source.cpp src/download.cpp src/util.cpp src/image.cpp src/hands.cpp src/face.cpp src/markers.cpp)
target_include_directories(panto_core PUBLIC src ${ORT_DIR} ${STB_DIR})
if(WIN32)
    target_link_libraries(panto_core PUBLIC ws2_32 mfplat mfreadwrite mf mfuuid ole32 shlwapi winhttp comdlg32 shell32)
else()
    target_link_libraries(panto_core PUBLIC dl pthread)
endif()
if(MSVC)
    target_compile_options(panto_core PRIVATE /W3 /O2 /fp:fast /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()

set(SOURCES src/main.cpp src/gui.cpp src/view3d.cpp src/lang.cpp src/splash.cpp src/selftest.cpp src/script.cpp)
if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/src/app.rc)
    list(APPEND SOURCES src/app.rc)
endif()
add_executable(Pantomimus WIN32 ${SOURCES})
target_link_libraries(Pantomimus PRIVATE panto_core imgui)
target_compile_definitions(Pantomimus PRIVATE PANTOMIMUS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(panto_core PUBLIC PANTOMIMUS_VERSION="${PROJECT_VERSION}")
if(TARGET SDL2::SDL2main)
    target_link_libraries(Pantomimus PRIVATE SDL2::SDL2main)
endif()
if(MSVC)
    target_compile_options(Pantomimus PRIVATE /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS)
endif()
add_custom_command(TARGET Pantomimus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Pantomimus>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:Pantomimus>/lang
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:Pantomimus>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/models $<TARGET_FILE_DIR:Pantomimus>/models
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/blender $<TARGET_FILE_DIR:Pantomimus>/blender
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests $<TARGET_FILE_DIR:Pantomimus>/tests)
if(WIN32)
    add_custom_command(TARGET Pantomimus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:Pantomimus>)
    if(EXISTS ${ORT_DIR}/onnxruntime.dll)
        add_custom_command(TARGET Pantomimus POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_DIR}/onnxruntime.dll $<TARGET_FILE_DIR:Pantomimus>)
    endif()
endif()
