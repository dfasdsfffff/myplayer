add_library(play_core STATIC
    clock.cpp
    decoder.cpp
    frame_queue.cpp
    media_session.cpp
    packet_queue.cpp
    playback_controller.cpp
    playback_controller_videoctl.cpp
    renderer_dispatcher.cpp
    videoctl.cpp
    soundtouch_wrap.cpp
    xlogger.cpp
)

function(playerdemo_import_library target_name library_name)
    if(TARGET ${target_name})
        return()
    endif()

    find_library(${target_name}_LIBRARY
        NAMES ${library_name}
        HINTS
            "${PLAYERDEMO_FFMPEG_ROOT}/bin"
            "${PLAYERDEMO_FFMPEG_ROOT}/lib"
            "${PLAYERDEMO_SOUNDTOUCH_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(NOT ${target_name}_LIBRARY)
        find_library(${target_name}_LIBRARY NAMES ${library_name})
    endif()

    if(NOT ${target_name}_LIBRARY)
        message(FATAL_ERROR "Could not find ${library_name}. Set PLAYERDEMO_DEPS_ROOT or the matching dependency root cache variable.")
    endif()

    add_library(${target_name} UNKNOWN IMPORTED)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${${target_name}_LIBRARY}"
    )
endfunction()

playerdemo_import_library(FFmpeg::avcodec avcodec)
playerdemo_import_library(FFmpeg::avdevice avdevice)
playerdemo_import_library(FFmpeg::avfilter avfilter)
playerdemo_import_library(FFmpeg::avformat avformat)
playerdemo_import_library(FFmpeg::avutil avutil)
playerdemo_import_library(FFmpeg::swresample swresample)
playerdemo_import_library(FFmpeg::swscale swscale)
playerdemo_import_library(SoundTouch::SoundTouch SoundTouchDLL_x64)

target_include_directories(play_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PLAYERDEMO_FFMPEG_ROOT}/include
    ${PLAYERDEMO_SDL2_ROOT}/include
    ${PLAYERDEMO_SOUNDTOUCH_ROOT}/lib
    ${PLAYERDEMO_SIGSLOT_ROOT}/include
)

target_link_libraries(play_core PUBLIC
    SDL2::SDL2
    FFmpeg::avcodec
    FFmpeg::avdevice
    FFmpeg::avfilter
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
    SoundTouch::SoundTouch
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
