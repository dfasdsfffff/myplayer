cmake_minimum_required(VERSION 3.25)

project(myplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(play_core)

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
    ${PLAYERDEMO_QT_APP_DIR}/ctrlbar.h
    ${PLAYERDEMO_QT_APP_DIR}/CustomSlider.h
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.h
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.h
    ${PLAYERDEMO_QT_APP_DIR}/medialist.h
    ${PLAYERDEMO_QT_APP_DIR}/playlist.h
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.h
    ${PLAYERDEMO_QT_APP_DIR}/resource.h
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.h
    ${PLAYERDEMO_QT_APP_DIR}/show.h
    ${PLAYERDEMO_QT_APP_DIR}/title.h
    ${PLAYERDEMO_QT_APP_DIR}/playback_runtime_bridge.h
)

set(SOURCE_FILES
    ${PLAYERDEMO_QT_APP_DIR}/main.cpp
    ${PLAYERDEMO_QT_APP_DIR}/about.cpp
    ${PLAYERDEMO_QT_APP_DIR}/ctrlbar.cpp
    ${PLAYERDEMO_QT_APP_DIR}/CustomSlider.cpp
    ${PLAYERDEMO_QT_APP_DIR}/globalhelper.cpp
    ${PLAYERDEMO_QT_APP_DIR}/mainwid.cpp
    ${PLAYERDEMO_QT_APP_DIR}/medialist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlist.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
    ${PLAYERDEMO_QT_APP_DIR}/settingwid.cpp
    ${PLAYERDEMO_QT_APP_DIR}/show.cpp
    ${PLAYERDEMO_QT_APP_DIR}/title.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playback_runtime_bridge.cpp
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
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLAYERDEMO_OUTPUT_DIR}"
    OUTPUT_NAME_DEBUG "myplayer_debug"
    OUTPUT_NAME_RELEASE "myplayer"
)



install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

enable_testing()

add_executable(playback_settings_tests
    tests/playback_settings_tests.cpp
)

target_include_directories(playback_settings_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/play_core
)

add_test(NAME playback_settings_tests COMMAND playback_settings_tests)

add_executable(playlistfile_tests
    tests/playlistfile_tests.cpp
    ${PLAYERDEMO_QT_APP_DIR}/playlistfile.cpp
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

add_executable(play_core_refactor_tests
    tests/play_core_refactor_tests.cpp
)

target_link_libraries(play_core_refactor_tests PRIVATE
    play_core
)


add_test(NAME play_core_refactor_tests COMMAND play_core_refactor_tests)

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

add_executable(show_widget_tests
    tests/show_widget_tests.cpp
)

add_test(NAME show_widget_tests COMMAND show_widget_tests)
set_tests_properties(show_widget_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
