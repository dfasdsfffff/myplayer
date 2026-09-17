add_library(play_core STATIC
    clock.cpp
    decoder.cpp
    frame_queue.cpp
    media_sync.cpp
    media_session.cpp
    network_input.cpp
    packet_queue.cpp
    playback_controller.cpp
    playback_controller_videoctl.cpp
    playback_runtime.cpp
    renderer_dispatcher.cpp
    videoctl.cpp
    soundtouch_wrap.cpp
    xlogger.cpp
)

target_include_directories(play_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
    ${PLAYERDEMO_SIGSLOT_ROOT}/include
)

target_link_libraries(play_core PUBLIC
    SDL2::SDL2
    ${FFMPEG_LIBRARIES}
    SoundTouch::SoundTouchDLL
)

target_compile_definitions(play_core PRIVATE
    UNICODE
    _UNICODE
)

if(WIN32)
    target_compile_definitions(play_core PRIVATE _WINDOWS)
endif()

if(MSVC)
    target_compile_options(play_core PRIVATE /utf-8)
endif()
