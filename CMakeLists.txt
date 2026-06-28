cmake_minimum_required(VERSION 3.20)

project(myplayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLAYERDEMO_USE_BUNDLED_DEPS "Prefer third-party libraries from the repository lib directory" ON)
option(PLAYERDEMO_MAP_QT_DEBUG_TO_RELEASE "Use release Qt libraries for Debug builds" OFF)

set(PLAYERDEMO_DEPS_ROOT "${PROJECT_SOURCE_DIR}/lib" CACHE PATH "Third-party dependency root")
set(PLAYERDEMO_FFMPEG_ROOT "${PLAYERDEMO_DEPS_ROOT}/ffmpeg" CACHE PATH "FFmpeg root directory")
set(PLAYERDEMO_SDL2_ROOT "${PLAYERDEMO_DEPS_ROOT}/SDL2" CACHE PATH "SDL2 root directory")
set(PLAYERDEMO_SOUNDTOUCH_ROOT "${PLAYERDEMO_DEPS_ROOT}/soundtouch-2.3.3" CACHE PATH "SoundTouch root directory")
set(PLAYERDEMO_SIGSLOT_ROOT "${PLAYERDEMO_DEPS_ROOT}/sigslot-1.2.3" CACHE PATH "sigslot root directory")
set(PLAYERDEMO_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin" CACHE PATH "Runtime output directory")
set(PLAYERDEMO_QT_ROOT "" CACHE PATH "Optional Qt installation prefix, for example C:/Qt/6.9.3/msvc2022_64")
set(PLAYERDEMO_QT_APP_DIR "${PROJECT_SOURCE_DIR}/apps/qt_player")
set(PLAYERDEMO_IMGUI_APP_DIR "${PROJECT_SOURCE_DIR}/apps/imgui_player")

if(PLAYERDEMO_QT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${PLAYERDEMO_QT_ROOT}")
endif()

if(PLAYERDEMO_USE_BUNDLED_DEPS AND NOT DEFINED sdl2_DIR AND EXISTS "${PLAYERDEMO_SDL2_ROOT}/cmake/sdl2-config.cmake")
    set(sdl2_DIR "${PLAYERDEMO_SDL2_ROOT}/cmake" CACHE PATH "Location of sdl2-config.cmake")
endif()

if(MSVC)
    add_compile_options(/Zc:__cplusplus /MP /utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(sdl2 CONFIG REQUIRED)
endif()

add_subdirectory(play_core)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(imgui CONFIG REQUIRED)

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

add_executable(myplayer_imgui
    ${PLAYERDEMO_IMGUI_APP_DIR}/main.cpp
)

target_include_directories(myplayer_imgui PRIVATE
    ${PLAYERDEMO_IMGUI_APP_DIR}
)

target_link_libraries(myplayer_imgui PRIVATE
    play_core
    imgui::imgui
)

if(WIN32)
    target_link_libraries(myplayer_imgui PRIVATE
        d3d11
        dxgi
        comdlg32
    )
endif()

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

set_target_properties(myplayer_imgui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLAYERDEMO_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLAYERDEMO_OUTPUT_DIR}"
    OUTPUT_NAME_DEBUG "myplayer_imgui_debug"
    OUTPUT_NAME_RELEASE "myplayer_imgui"
)

if(WIN32)
    file(GLOB PLAYERDEMO_RUNTIME_DLLS CONFIGURE_DEPENDS
        "${PLAYERDEMO_FFMPEG_ROOT}/bin/*.dll"
        "${PLAYERDEMO_SDL2_ROOT}/lib/x64/SDL2.dll"
        "${PLAYERDEMO_SOUNDTOUCH_ROOT}/lib/SoundTouchDLL_x64.dll"
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WINDOWS
        UNICODE
        _UNICODE
    )

    foreach(PLAYERDEMO_RUNTIME_DLL IN LISTS PLAYERDEMO_RUNTIME_DLLS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLAYERDEMO_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
        add_custom_command(TARGET myplayer_imgui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLAYERDEMO_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:myplayer_imgui>"
        )
    endforeach()
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

enable_testing()

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

if(WIN32)
    add_custom_command(TARGET play_core_refactor_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PLAYERDEMO_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:play_core_refactor_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME play_core_refactor_tests COMMAND play_core_refactor_tests)

add_executable(network_playback_tests
    tests/network_playback_tests.cpp
)

target_link_libraries(network_playback_tests PRIVATE
    play_core
)

if(WIN32)
    add_custom_command(TARGET network_playback_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PLAYERDEMO_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:network_playback_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME network_playback_tests COMMAND network_playback_tests)

add_executable(show_widget_tests
    tests/show_widget_tests.cpp
)

add_test(NAME show_widget_tests COMMAND show_widget_tests)
set_tests_properties(show_widget_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
