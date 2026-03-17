cmake_minimum_required(VERSION 3.20)

project(player-demo)
 
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

file(GLOB SOURCE_FILES src/*.ui src/*.h src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2 Qt6::Core Qt6::Widgets )
target_link_libraries(${PROJECT_NAME} PRIVATE
libavcodec libavformat libavutil libswscale libswresample libavfilter libavdevice)
