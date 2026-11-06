if(PLAYERDEMO_MAP_QT_DEBUG_TO_RELEASE)
    message(STATUS "Mapping Qt debug config to release")
    set_target_properties(Qt6::Core Qt6::Gui Qt6::Widgets PROPERTIES
        MAP_IMPORTED_CONFIG_DEBUG "Release"
    )
endif()

set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/about.h
    ${CMAKE_CURRENT_SOURCE_DIR}/app_preferences.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ctrlbar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CustomSlider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/globalhelper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/latest_video_frame_mailbox.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mainwid.h
    ${CMAKE_CURRENT_SOURCE_DIR}/media_format_registry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/media_location_privacy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/medialist.h
    ${CMAKE_CURRENT_SOURCE_DIR}/playlist.h
    ${CMAKE_CURRENT_SOURCE_DIR}/playlistfile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/settingwid.h
    ${CMAKE_CURRENT_SOURCE_DIR}/show.h
    ${CMAKE_CURRENT_SOURCE_DIR}/subtitle_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/title.h
    ${CMAKE_CURRENT_SOURCE_DIR}/playback_runtime_bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/playback_benchmark.h
)

set(SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/about.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_preferences.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ctrlbar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CustomSlider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/globalhelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/latest_video_frame_mailbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mainwid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/media_format_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/media_location_privacy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/medialist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/playlist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/playlistfile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settingwid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/show.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/subtitle_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/title.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/playback_runtime_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/playback_status_presenter.cpp
)

set(UI_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/about.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ctrlbar.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/form.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/mainwid.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/playlist.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/settingwid.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/show.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/title.ui
)

set(QRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/mainwid.qrc)

if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/myplayer_version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/myplayer_version.rc"
        @ONLY
    )
    set(RC_FILES "${CMAKE_CURRENT_BINARY_DIR}/myplayer_version.rc")
endif()

qt_add_executable(myplayer WIN32
    ${SOURCE_FILES}
    ${HEADER_FILES}
    ${UI_FILES}
    ${QRC_FILES}
    ${RC_FILES}
    playback_benchmark.cpp
)

target_include_directories(myplayer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(myplayer PRIVATE
    play_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::LIBASS
)

set_target_properties(myplayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLAYERDEMO_OUTPUT_DIR}"
    OUTPUT_NAME_DEBUG "myplayer_debug"
    OUTPUT_NAME_RELEASE "myplayer"
)

myplayer_deploy_runtime_dependencies(myplayer)
myplayer_enable_warnings(myplayer)
myplayer_enable_sanitizers(myplayer)
myplayer_install_release_tree(myplayer)

# The existing main-window regression target reuses the application sources.
foreach(source_list IN ITEMS SOURCE_FILES HEADER_FILES UI_FILES QRC_FILES)
    set(${source_list} "${${source_list}}" PARENT_SCOPE)
endforeach()
