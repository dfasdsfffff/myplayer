cmake_minimum_required(VERSION 3.20)

project(playerdemo VERSION 1.0 LANGUAGES CXX)
 
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_DIR [=[C:\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6]=])
set(Qt6_DIR [=[C:\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6]=])
set(sdl2_DIR [=[D:\libraries\SDL2-2.32.10\cmake]=])


# 针对 MSVC 添加 /Zc:__cplusplus 选项
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

find_package(QT NAMES Qt6 COMPONENTS Widgets REQUIRED)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

find_package(sdl2 REQUIRED)


include_directories(${CMAKE_SOURCE_DIR}/lib/ffmpeg/include ${CMAKE_SOURCE_DIR}/lib/SDL2/include)
link_directories(
    ${CMAKE_SOURCE_DIR}/lib/ffmpeg/bin
    ${CMAKE_SOURCE_DIR}/lib/SDL2/lib/x64
    )

find_package(sdl2 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets LinguistTools)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/lib/ffmpeg/include
    ${CMAKE_SOURCE_DIR}/lib/SDL2/include
    ${CMAKE_SOURCE_DIR}/lib/soundtouch-2.3.3/include
    ${CMAKE_SOURCE_DIR}/src
)

# 链接目录
link_directories(
    ${CMAKE_SOURCE_DIR}/lib/ffmpeg/bin
    ${CMAKE_SOURCE_DIR}/lib/SDL2/lib/x64
    ${CMAKE_SOURCE_DIR}/lib/soundtouch-2.3.3/lib
)

# 源文件
set(SOURCE_FILES
    src/main.cpp
    src/about.cpp
    src/ctrlbar.cpp
    src/CustomSlider.cpp
    src/customthread.cpp
    src/globalhelper.cpp
    src/mainwid.cpp
    src/medialist.cpp
    src/playlist.cpp
    src/settingwid.cpp
    src/show.cpp
    src/title.cpp
    src/videoctl.cpp
    xlogger.cpp
)

# 头文件
set(HEADER_FILES
    enums.h
    src/about.h
    src/ctrlbar.h
    src/CustomSlider.h
    src/customthread.h
    src/datactl.h
    src/globalhelper.h
    src/resource.h
    src/videoctl.h
    src/title.h
    src/show.h
    src/settingwid.h
    src/playlist.h
    src/medialist.h
    src/mainwid.h
    xlogger.h
)

# UI 文件
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

# 资源文件
set(QRC_FILES
    src/mainwid.qrc
)

# Windows 资源文件
if(WIN32)
    set(RC_FILES src/playerdemo.rc)
endif()

# 创建可执行文件
add_executable(${PROJECT_NAME} WIN32
    ${SOURCE_FILES}
    ${HEADER_FILES}
    ${UI_FILES}
    ${QRC_FILES}
    ${RC_FILES}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
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

# # Debug 配置下额外链接 SoundTouch
# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     target_link_libraries(${PROJECT_NAME} PRIVATE
#         SoundTouchDLL_x64
#     )
# endif()

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}
)

# Windows 特定设置
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WINDOWS
        UNICODE
        _UNICODE
    )
endif()