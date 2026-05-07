cmake_minimum_required(VERSION 3.20)

project(playerdemo VERSION 1.0 LANGUAGES CXX)

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

if(PLAYERDEMO_MAP_QT_DEBUG_TO_RELEASE)
    set_target_properties(Qt6::Core Qt6::Gui Qt6::Widgets PROPERTIES
        MAP_IMPORTED_CONFIG_DEBUG "Release"
    )
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(HEADER_FILES
    src/about.h
    src/ctrlbar.h
    src/CustomSlider.h
    src/globalhelper.h
    src/mainwid.h
    src/medialist.h
    src/playlist.h
    src/resource.h
    src/settingwid.h
    src/show.h
    src/title.h
    src/videoctl_bridge.h
)

set(SOURCE_FILES
    src/main.cpp
    src/about.cpp
    src/ctrlbar.cpp
    src/CustomSlider.cpp
    src/globalhelper.cpp
    src/mainwid.cpp
    src/medialist.cpp
    src/playlist.cpp
    src/settingwid.cpp
    src/show.cpp
    src/title.cpp
    src/videoctl_bridge.cpp
)

set(UI_FILES
    src/about.ui
    src/ctrlbar.ui
    src/form.ui
    src/mainwid.ui
    src/playlist.ui
    src/settingwid.ui
    src/show.ui
    src/title.ui
)

set(QRC_FILES
    src/mainwid.qrc
)

if(WIN32)
    set(RC_FILES src/playerdemo.rc)
endif()

add_executable(${PROJECT_NAME} WIN32
    ${SOURCE_FILES}
    ${HEADER_FILES}
    ${UI_FILES}
    ${QRC_FILES}
    ${RC_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
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
    OUTPUT_NAME_DEBUG "${PROJECT_NAME}_debug"
    OUTPUT_NAME_RELEASE "${PROJECT_NAME}"
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
    endforeach()
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
