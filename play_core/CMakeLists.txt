set(PLAY_CORE_SOURCES
    player.cpp
    audio_render_queue.cpp
    audio_output.cpp
    clock.cpp
    decoder.cpp
    decoder_workers.cpp
    filter_configurator.cpp
    frame_queue.cpp
    hardware_decode.cpp
    media_sync.cpp
    media_session.cpp
    network_input.cpp
    packet_queue.cpp
    playback_command_mailbox.cpp
    reconnect_controller.cpp
    renderer_dispatcher.cpp
    stream_reader.cpp
    subtitle_dispatcher.cpp
    video_frame_converter.cpp
    videoctl.cpp
    soundtouch_wrap.cpp
    xlogger.cpp
)

list(TRANSFORM PLAY_CORE_SOURCES PREPEND "src/")
add_library(play_core STATIC ${PLAY_CORE_SOURCES})
# Legacy entry points exist only to keep the original white-box regressions.
# They are not compiled into an embedded/source-release SDK.
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING AND PROJECT_NAME STREQUAL "myplayer")
    target_sources(play_core PRIVATE src/playback_runtime.cpp src/playback_controller.cpp src/playback_controller_videoctl.cpp)
endif()
set_target_properties(play_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_library(PlayerSDK::Core ALIAS play_core)

target_compile_features(play_core PUBLIC cxx_std_20)

target_include_directories(play_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FFMPEG_INCLUDE_DIRS}
        ${PLAYERDEMO_SIGSLOT_ROOT}/include
)

target_link_libraries(play_core PRIVATE
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

myplayer_enable_warnings(play_core)
myplayer_enable_sanitizers(play_core)
