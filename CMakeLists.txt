cmake_minimum_required(VERSION 3.20)

project(playerdemo VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置 Qt6 路径，允许用户通过 -DQt6_DIR=... 覆盖
# 默认路径为 C:\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6
if(NOT DEFINED Qt6_DIR AND EXISTS "C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6")
    set(Qt6_DIR "C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Location of Qt6Config.cmake")
endif()


if(NOT DEFINED sdl2_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/lib/SDL2/cmake/sdl2-config.cmake")
    set(sdl2_DIR "${CMAKE_SOURCE_DIR}/lib/SDL2/cmake" CACHE PATH "Location of sdl2-config.cmake")
endif()

# 针对 MSVC 添加编译选项
if(MSVC)
    add_compile_options(/Zc:__cplusplus /MP /utf-8)
    # Qt6 官方预编译库使用动态 CRT (/MD)，因此本项目也使用动态 CRT
    # SoundTouch_x64.lib 是 /MT 编译的静态库，需要忽略其默认库以避免冲突
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# 查找 SDL2（play_core 依赖）
find_package(sdl2 REQUIRED)

# ========== 播放器核心模块（无 Qt 依赖）==========
add_subdirectory(play_core)

# ========== Qt UI 模块 ==========
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# 强制所有配置使用 Qt6 的 Release 版本库（包括 Debug 构建）
set_target_properties(Qt6::Core Qt6::Gui Qt6::Widgets PROPERTIES
    MAP_IMPORTED_CONFIG_DEBUG "Release"
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# UI 头文件
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

# UI 源文件
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

# UI 的 include 路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# 链接 play_core 静态库 + Qt + SDL
target_link_libraries(${PROJECT_NAME} PRIVATE
    play_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)

# 设置可执行文件名称
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME_DEBUG "${PROJECT_NAME}_debug"
    OUTPUT_NAME_RELEASE "${PROJECT_NAME}"
)
# Windows 特定设置
if(WIN32)
    file(GLOB PLAYERDEMO_RUNTIME_DLLS CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/lib/ffmpeg/bin/*.dll"
        "${CMAKE_SOURCE_DIR}/lib/SDL2/lib/x64/SDL2.dll"
        "${CMAKE_SOURCE_DIR}/lib/soundtouch-2.3.3/lib/SoundTouchDLL_x64.dll"
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WINDOWS
        UNICODE
        _UNICODE
    )
    # 部署 SoundTouch DLL 到输出目录
    foreach(PLAYERDEMO_RUNTIME_DLL IN LISTS PLAYERDEMO_RUNTIME_DLLS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLAYERDEMO_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()
