# play_core — 播放器核心模块（无 Qt 依赖）
# 编译为静态库，供 UI 层链接

add_library(play_core STATIC
    clock.cpp
    decoder.cpp
    frame_queue.cpp
    packet_queue.cpp
    videoctl.cpp
    soundtouch_wrap.cpp
    xlogger.cpp
)

target_include_directories(play_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}                              # play_core/*.h
    ${CMAKE_SOURCE_DIR}/lib/ffmpeg/include
    ${CMAKE_SOURCE_DIR}/lib/SDL2/include
    ${CMAKE_SOURCE_DIR}/lib/soundtouch-2.3.3/lib
    ${CMAKE_SOURCE_DIR}/lib/sigslot-1.2.3/include
)

# 链接目录（FFmpeg 和 SoundTouch 使用预编译库）
target_link_directories(play_core PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/ffmpeg/bin
    ${CMAKE_SOURCE_DIR}/lib/SDL2/lib/x64
    ${CMAKE_SOURCE_DIR}/lib/soundtouch-2.3.3/lib
)

target_link_libraries(play_core PUBLIC
    SDL2::SDL2
    avcodec
    avdevice
    avfilter
    avformat
    avutil
    swresample
    swscale
    SoundTouchDLL_x64
)

target_compile_definitions(play_core PRIVATE
    _WINDOWS
    UNICODE
    _UNICODE
)

# MSVC: 启用 UTF-8 源文件编码（解决 C4819 警告）
if(MSVC)
    target_compile_options(play_core PRIVATE /utf-8)
endif()
