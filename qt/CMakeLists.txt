find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup(REQUIRES 6.8)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
qt_add_library(player_sdk_qt STATIC
    qt_player.cpp
    video_widget.cpp
    include/player_sdk_qt/qt_player.h
    include/player_sdk_qt/video_widget.h
)
add_library(PlayerSDK::QtWidgets ALIAS player_sdk_qt)
target_compile_features(player_sdk_qt PUBLIC cxx_std_20)
target_include_directories(player_sdk_qt
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${FFMPEG_INCLUDE_DIRS}
)
target_link_libraries(player_sdk_qt
    PUBLIC PlayerSDK::Core Qt6::Core Qt6::Gui Qt6::Widgets
    PRIVATE ${FFMPEG_LIBRARIES} PkgConfig::LIBASS
)
if(MSVC)
    target_compile_options(player_sdk_qt PRIVATE /utf-8 /Zc:__cplusplus)
endif()
