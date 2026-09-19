cmake_minimum_required(VERSION 3.25)

project(myplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MYPLAYER_BUILD_QT_APP "Build the Qt Widgets player" ON)
include(CTest)
option(MYPLAYER_ENABLE_WARNINGS "Enable project warning flags" ON)
option(MYPLAYER_ENABLE_SANITIZERS "Enable supported sanitizers" OFF)

include(cmake/MyPlayerWarnings.cmake)
include(cmake/MyPlayerInstall.cmake)

option(PLAYERDEMO_MAP_QT_DEBUG_TO_RELEASE "Use release Qt libraries for Debug builds" OFF)

set(PLAYERDEMO_DEPS_ROOT "${PROJECT_SOURCE_DIR}/lib" CACHE PATH "Repository third-party source root")
set(PLAYERDEMO_SIGSLOT_ROOT "${PLAYERDEMO_DEPS_ROOT}/sigslot-1.2.3" CACHE PATH "sigslot header-only dependency root")
set(PLAYERDEMO_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin" CACHE PATH "Runtime output directory")
set(PLAYERDEMO_QT_ROOT "" CACHE PATH "Optional Qt installation prefix, for example C:/Qt/6.9.3/msvc2022_64")
set(PLAYERDEMO_QT_APP_DIR "${PROJECT_SOURCE_DIR}/apps/qt_player")

if(PLAYERDEMO_QT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${PLAYERDEMO_QT_ROOT}")
endif()


if(MSVC)
    add_compile_options(/Zc:__cplusplus /MP /utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(FFMPEG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SoundTouch CONFIG REQUIRED)
if(MYPLAYER_BUILD_QT_APP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()

add_subdirectory(play_core)

if(MYPLAYER_BUILD_QT_APP)
    if(PLAYERDEMO_MAP_QT_DEBUG_TO_RELEASE)
        message(STATUS "Mapping Qt debug config to release")
        set_target_properties(Qt6::Core Qt6::Gui Qt6::Widgets PROPERTIES
            MAP_IMPORTED_CONFIG_DEBUG "Release"
        )
    endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(HEADER_FILES
    ${PLAYERDEMO_QT_APP_DIR}/about.h
    ${PLAYERDEMO_QT_APP_DIR}/app_preferences.h
    ${PLAYERDEMO_QT_APP_DIR}/ctrlbar.h
    ${PLAYERDEMO_QT_APP_DIR}/CustomSlider.h
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.h
    ${PLAYERDEMO_QT_APP_DIR}/latest_video_frame_mailbox.h
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.h
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.h
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.h
    ${PLAYERDEMO_QT_APP_DIR}/medialist.h
    ${PLAYERDEMO_QT_APP_DIR}/playlist.h
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.h
    ${PLAYERDEMO_QT_APP_DIR}/resource.h
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.h
    ${PLAYERDEMO_QT_APP_DIR}/show.h
    ${PLAYERDEMO_QT_APP_DIR}/subtitle_renderer.h
    ${PLAYERDEMO_QT_APP_DIR}/title.h
    ${PLAYERDEMO_QT_APP_DIR}/playback_runtime_bridge.h
)

set(SOURCE_FILES
    ${PLAYERDEMO_QT_APP_DIR}/main.cpp
    ${PLAYERDEMO_QT_APP_DIR}/about.cpp
    ${PLAYERDEMO_QT_APP_DIR}/app_preferences.cpp
    ${PLAYERDEMO_QT_APP_DIR}/ctrlbar.cpp
    ${PLAYERDEMO_QT_APP_DIR}/CustomSlider.cpp
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.cpp
    ${PLAYERDEMO_QT_APP_DIR}/latest_video_frame_mailbox.cpp
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
    ${PLAYERDEMO_QT_APP_DIR}/medialist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.cpp
    ${PLAYERDEMO_QT_APP_DIR}/show.cpp
    ${PLAYERDEMO_QT_APP_DIR}/subtitle_renderer.cpp
    ${PLAYERDEMO_QT_APP_DIR}/title.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playback_runtime_bridge.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playback_status_presenter.cpp
)

set(UI_FILES
    ${PLAYERDEMO_QT_APP_DIR}/about.ui
    ${PLAYERDEMO_QT_APP_DIR}/ctrlbar.ui
    ${PLAYERDEMO_QT_APP_DIR}/form.ui
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.ui
    ${PLAYERDEMO_QT_APP_DIR}/playlist.ui
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.ui
    ${PLAYERDEMO_QT_APP_DIR}/show.ui
    ${PLAYERDEMO_QT_APP_DIR}/title.ui
)

set(QRC_FILES
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.qrc
)

if(WIN32)
    set(RC_FILES ${PLAYERDEMO_QT_APP_DIR}/myplayer.rc)
endif()

add_executable(${PROJECT_NAME} WIN32
    ${SOURCE_FILES}
    ${HEADER_FILES}
    ${UI_FILES}
    ${QRC_FILES}
    ${RC_FILES}
)



target_include_directories(${PROJECT_NAME} PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    play_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::LIBASS
)

if(WIN32)
    get_filename_component(MYPLAYER_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
endif()

function(myplayer_deploy_runtime_dependencies target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MYPLAYER_QT_ROOT}/plugins/platforms"
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLAYERDEMO_OUTPUT_DIR}"
    OUTPUT_NAME_DEBUG "myplayer_debug"
    OUTPUT_NAME_RELEASE "myplayer"
)

myplayer_deploy_runtime_dependencies(${PROJECT_NAME})

myplayer_enable_warnings(${PROJECT_NAME})
myplayer_enable_sanitizers(${PROJECT_NAME})

myplayer_install_release_tree(${PROJECT_NAME})
endif()

if(BUILD_TESTING)
function(myplayer_set_test_runtime_dir target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/${target}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests/Debug/${target}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests/Release/${target}"
    )
endfunction()

    if(MYPLAYER_BUILD_QT_APP)
        add_test(NAME myplayer_smoke_test COMMAND $<TARGET_FILE:${PROJECT_NAME}> --smoke-test)
        set_tests_properties(myplayer_smoke_test PROPERTIES
            TIMEOUT 5
            ENVIRONMENT "QT_QPA_PLATFORM=windows;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_test(NAME cmake_configuration_tests
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}/cmake-configuration-contract
            -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -P ${PROJECT_SOURCE_DIR}/tests/cmake_configuration_tests.cmake
    )

    if(MYPLAYER_BUILD_QT_APP)
        add_test(NAME install_layout_tests
            COMMAND ${CMAKE_COMMAND}
                -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -P ${PROJECT_SOURCE_DIR}/tests/install_layout_tests.cmake
        )
    endif()

add_executable(playback_settings_tests
    tests/playback_settings_tests.cpp
)

target_include_directories(playback_settings_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/play_core
)

add_test(NAME playback_settings_tests COMMAND playback_settings_tests)

if(MYPLAYER_BUILD_QT_APP)
add_executable(media_location_privacy_tests
    tests/media_location_privacy_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
)

target_include_directories(media_location_privacy_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(media_location_privacy_tests PRIVATE Qt6::Core)
add_test(NAME media_location_privacy_tests COMMAND media_location_privacy_tests)

add_executable(globalhelper_privacy_tests
    tests/globalhelper_privacy_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
)

target_include_directories(globalhelper_privacy_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)

target_link_libraries(globalhelper_privacy_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
myplayer_deploy_runtime_dependencies(globalhelper_privacy_tests)
add_test(NAME globalhelper_privacy_tests COMMAND globalhelper_privacy_tests)
set_tests_properties(globalhelper_privacy_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(playlist_privacy_tests
    tests/playlist_privacy_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.cpp
    ${PLAYERDEMO_QT_APP_DIR}/medialist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.ui
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
)

target_include_directories(playlist_privacy_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)

target_link_libraries(playlist_privacy_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
myplayer_set_test_runtime_dir(playlist_privacy_tests)
if(WIN32)
    add_custom_command(TARGET playlist_privacy_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:playlist_privacy_tests>
            $<TARGET_FILE_DIR:playlist_privacy_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MYPLAYER_QT_ROOT}/plugins/platforms"
            "$<TARGET_FILE_DIR:playlist_privacy_tests>/platforms"
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME playlist_privacy_tests COMMAND playlist_privacy_tests)
set_tests_properties(playlist_privacy_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(playlist_navigation_tests
    tests/playlist_navigation_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.cpp
    ${PLAYERDEMO_QT_APP_DIR}/medialist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.ui
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
)

target_include_directories(playlist_navigation_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)

target_link_libraries(playlist_navigation_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
myplayer_set_test_runtime_dir(playlist_navigation_tests)
if(WIN32)
    add_custom_command(TARGET playlist_navigation_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:playlist_navigation_tests>
            $<TARGET_FILE_DIR:playlist_navigation_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MYPLAYER_QT_ROOT}/plugins/platforms"
            "$<TARGET_FILE_DIR:playlist_navigation_tests>/platforms"
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME playlist_navigation_tests COMMAND playlist_navigation_tests)
set_tests_properties(playlist_navigation_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(app_preferences_tests
    tests/app_preferences_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/app_preferences.cpp
)

target_include_directories(app_preferences_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(app_preferences_tests PRIVATE
    play_core
    Qt6::Core
)

add_test(NAME app_preferences_tests COMMAND app_preferences_tests)

add_executable(setting_widget_tests
    tests/setting_widget_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/app_preferences.cpp
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.cpp
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.ui
)

target_include_directories(setting_widget_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(setting_widget_tests PRIVATE
    play_core
    Qt6::Core
    Qt6::Widgets
)
myplayer_set_test_runtime_dir(setting_widget_tests)

if(WIN32)
    add_custom_command(TARGET setting_widget_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:setting_widget_tests>
            $<TARGET_FILE_DIR:setting_widget_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MYPLAYER_QT_ROOT}/plugins/platforms"
            "$<TARGET_FILE_DIR:setting_widget_tests>/platforms"
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME setting_widget_tests COMMAND setting_widget_tests)
set_tests_properties(setting_widget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(playback_status_presenter_tests
    tests/playback_status_presenter_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playback_status_presenter.cpp
)

target_include_directories(playback_status_presenter_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(playback_status_presenter_tests PRIVATE
    play_core
    Qt6::Core
)

add_test(NAME playback_status_presenter_tests COMMAND playback_status_presenter_tests)

add_executable(media_format_registry_tests
    tests/media_format_registry_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.cpp
)

target_include_directories(media_format_registry_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(media_format_registry_tests PRIVATE Qt6::Core)
if(WIN32)
    add_custom_command(TARGET media_format_registry_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:media_format_registry_tests>
            $<TARGET_FILE_DIR:media_format_registry_tests>
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME media_format_registry_tests COMMAND media_format_registry_tests)

add_executable(playlistfile_tests
    tests/playlistfile_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_format_registry.cpp
    ${PLAYERDEMO_QT_APP_DIR}/media_location_privacy.cpp
)

target_include_directories(playlistfile_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
)

target_link_libraries(playlistfile_tests PRIVATE
    Qt6::Core
)

if(WIN32)
    add_custom_command(TARGET playlistfile_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:playlistfile_tests>
            $<TARGET_FILE_DIR:playlistfile_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME playlistfile_tests COMMAND playlistfile_tests)
endif()

add_executable(play_core_refactor_tests
    tests/play_core_refactor_tests.cpp
)

target_link_libraries(play_core_refactor_tests PRIVATE
    play_core
)


add_test(NAME play_core_refactor_tests COMMAND play_core_refactor_tests)

add_executable(playback_command_mailbox_tests
    tests/playback_command_mailbox_tests.cpp
)

target_link_libraries(playback_command_mailbox_tests PRIVATE
    play_core
)

add_test(NAME playback_command_mailbox_tests COMMAND playback_command_mailbox_tests)

add_executable(clock_concurrency_tests
    tests/clock_concurrency_tests.cpp
)

target_link_libraries(clock_concurrency_tests PRIVATE
    play_core
)

add_test(NAME clock_concurrency_tests COMMAND clock_concurrency_tests)

add_executable(track_switching_tests
    tests/track_switching_tests.cpp
)

target_link_libraries(track_switching_tests PRIVATE
    play_core
)

add_test(NAME track_switching_tests COMMAND track_switching_tests)

add_executable(media_info_tests
    tests/media_info_tests.cpp
)

target_link_libraries(media_info_tests PRIVATE
    play_core
)

add_test(NAME media_info_tests COMMAND media_info_tests)

add_executable(video_geometry_tests
    tests/video_geometry_tests.cpp
)

target_link_libraries(video_geometry_tests PRIVATE
    play_core
)

add_test(NAME video_geometry_tests COMMAND video_geometry_tests)

add_executable(video_frame_format_tests tests/video_frame_format_tests.cpp)
target_link_libraries(video_frame_format_tests PRIVATE play_core)
add_test(NAME video_frame_format_tests COMMAND video_frame_format_tests)

add_executable(resume_policy_tests
    tests/resume_policy_tests.cpp
)

target_link_libraries(resume_policy_tests PRIVATE
    play_core
)

add_test(NAME resume_policy_tests COMMAND resume_policy_tests)

add_executable(subtitle_dispatcher_tests
    tests/subtitle_dispatcher_tests.cpp
)

target_link_libraries(subtitle_dispatcher_tests PRIVATE
    play_core
)

add_test(NAME subtitle_dispatcher_tests COMMAND subtitle_dispatcher_tests)

add_executable(subtitle_renderer_tests
    tests/subtitle_renderer_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/subtitle_renderer.cpp
)

target_include_directories(subtitle_renderer_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)

target_link_libraries(subtitle_renderer_tests PRIVATE
    Qt6::Core
    SDL2::SDL2
    PkgConfig::LIBASS
)

add_test(NAME subtitle_renderer_tests COMMAND subtitle_renderer_tests)
set_tests_properties(subtitle_renderer_tests PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy"
)

add_executable(latest_video_frame_mailbox_tests
    tests/latest_video_frame_mailbox_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/latest_video_frame_mailbox.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playback_runtime_bridge.cpp
)

target_include_directories(latest_video_frame_mailbox_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)

target_link_libraries(latest_video_frame_mailbox_tests PRIVATE play_core Qt6::Core)
add_test(NAME latest_video_frame_mailbox_tests COMMAND latest_video_frame_mailbox_tests)

add_executable(playback_lifecycle_stress_tests
    tests/playback_lifecycle_stress_tests.cpp
)

target_link_libraries(playback_lifecycle_stress_tests PRIVATE
    play_core
)

add_test(NAME playback_lifecycle_stress_tests COMMAND playback_lifecycle_stress_tests)
set_tests_properties(playback_lifecycle_stress_tests PROPERTIES LABELS STRESS_TESTS)

add_executable(network_playback_tests
    tests/network_playback_tests.cpp
)

target_link_libraries(network_playback_tests PRIVATE
    play_core
)


add_test(NAME network_playback_tests COMMAND network_playback_tests)

add_executable(reconnect_controller_tests
    tests/reconnect_controller_tests.cpp
)

target_link_libraries(reconnect_controller_tests PRIVATE
    play_core
)

add_test(NAME reconnect_controller_tests COMMAND reconnect_controller_tests)

add_executable(filter_configurator_tests
    tests/filter_configurator_tests.cpp
)

target_link_libraries(filter_configurator_tests PRIVATE
    play_core
)

add_test(NAME filter_configurator_tests COMMAND filter_configurator_tests)

add_executable(decoder_workers_tests
    tests/decoder_workers_tests.cpp
)

target_link_libraries(decoder_workers_tests PRIVATE
    play_core
)

if(WIN32)
    add_custom_command(TARGET decoder_workers_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:decoder_workers_tests>
            $<TARGET_FILE_DIR:decoder_workers_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME decoder_workers_tests COMMAND decoder_workers_tests)
set_tests_properties(decoder_workers_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:decoder_workers_tests>"
)

add_executable(stream_reader_tests
    tests/stream_reader_tests.cpp
)

target_link_libraries(stream_reader_tests PRIVATE
    play_core
)

if(WIN32)
    add_custom_command(TARGET stream_reader_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:stream_reader_tests>
            $<TARGET_FILE_DIR:stream_reader_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME stream_reader_tests COMMAND stream_reader_tests)
set_tests_properties(stream_reader_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:stream_reader_tests>"
)

add_executable(audio_output_tests
    tests/audio_output_tests.cpp
)

target_link_libraries(audio_output_tests PRIVATE
    play_core
)

if(WIN32)
    add_custom_command(TARGET audio_output_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:audio_output_tests>
            $<TARGET_FILE_DIR:audio_output_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME audio_output_tests COMMAND audio_output_tests)
set_tests_properties(audio_output_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:audio_output_tests>"
)

add_executable(audio_render_queue_tests
    tests/audio_render_queue_tests.cpp
)

target_link_libraries(audio_render_queue_tests PRIVATE
    play_core
)

add_test(NAME audio_render_queue_tests COMMAND audio_render_queue_tests)

add_executable(hardware_decode_policy_tests
    tests/hardware_decode_policy_tests.cpp
)

target_link_libraries(hardware_decode_policy_tests PRIVATE
    play_core
)

add_test(NAME hardware_decode_policy_tests COMMAND hardware_decode_policy_tests)

add_executable(playback_integration_tests
    tests/playback_integration_tests.cpp
    tests/media_fixture_builder.cpp
)

target_include_directories(playback_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(playback_integration_tests PRIVATE
    play_core
)

add_test(NAME playback_integration_tests COMMAND playback_integration_tests)
set_tests_properties(playback_integration_tests PROPERTIES
    LABELS INTEGRATION
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)

add_executable(network_integration_tests
    tests/network_integration_tests.cpp
    tests/media_fixture_builder.cpp
)
target_include_directories(network_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(network_integration_tests PRIVATE
    play_core
    ws2_32
)
add_test(NAME network_integration_tests COMMAND network_integration_tests)
set_tests_properties(network_integration_tests PROPERTIES LABELS INTEGRATION)

add_executable(soundtouch_wrap_tests
    tests/soundtouch_wrap_tests.cpp
)

target_link_libraries(soundtouch_wrap_tests PRIVATE
    play_core
)

add_test(NAME soundtouch_wrap_tests COMMAND soundtouch_wrap_tests)

add_executable(show_widget_tests
    tests/show_widget_tests.cpp
)

add_test(NAME show_widget_tests COMMAND show_widget_tests)
set_tests_properties(show_widget_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

set(MAIN_WINDOW_TEST_SOURCES ${SOURCE_FILES})
list(REMOVE_ITEM MAIN_WINDOW_TEST_SOURCES ${PLAYERDEMO_QT_APP_DIR}/main.cpp)
add_executable(main_window_behavior_tests
    tests/main_window_behavior_tests.cpp
    ${MAIN_WINDOW_TEST_SOURCES}
    ${HEADER_FILES}
    ${UI_FILES}
    ${QRC_FILES}
)
target_include_directories(main_window_behavior_tests PRIVATE
    ${PLAYERDEMO_QT_APP_DIR}
    ${PROJECT_SOURCE_DIR}/play_core
)
target_link_libraries(main_window_behavior_tests PRIVATE
    play_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::LIBASS
)
myplayer_set_test_runtime_dir(main_window_behavior_tests)
if(WIN32)
    add_custom_command(TARGET main_window_behavior_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:main_window_behavior_tests>
            $<TARGET_FILE_DIR:main_window_behavior_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MYPLAYER_QT_ROOT}/plugins/platforms"
            "$<TARGET_FILE_DIR:main_window_behavior_tests>/platforms"
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME main_window_behavior_tests COMMAND main_window_behavior_tests)
set_tests_properties(main_window_behavior_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)

foreach(test_target IN ITEMS
        playback_settings_tests
        media_location_privacy_tests
        globalhelper_privacy_tests
        playlist_privacy_tests
        app_preferences_tests
        setting_widget_tests
        playback_status_presenter_tests
        media_format_registry_tests
        playlistfile_tests
        video_geometry_tests
        resume_policy_tests
        play_core_refactor_tests
        network_playback_tests
        reconnect_controller_tests
        filter_configurator_tests
        decoder_workers_tests
        stream_reader_tests
        audio_output_tests
        show_widget_tests
        subtitle_dispatcher_tests
        subtitle_renderer_tests
        latest_video_frame_mailbox_tests
        main_window_behavior_tests)
    if(TARGET ${test_target})
        myplayer_enable_warnings(${test_target})
        myplayer_enable_sanitizers(${test_target})
    endif()
endforeach()
endif()
