cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MyPlayerMonitor VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    get_filename_component(MYPLAYER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    option(MONITOR_USE_BUNDLED_DEPS "Use repository Windows x64 dependency backups" OFF)
    if(MSVC)
        add_compile_options(/utf-8 /Zc:__cplusplus)
    endif()
    if(MONITOR_USE_BUNDLED_DEPS)
        if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR "Bundled dependencies require Windows x64")
        endif()
        set(FFMPEG_INCLUDE_DIRS "${MYPLAYER_ROOT}/lib/ffmpeg-7.1.1/include")
        foreach(component avcodec avdevice avfilter avformat avutil swresample swscale)
            list(APPEND FFMPEG_LIBRARIES "${MYPLAYER_ROOT}/lib/ffmpeg-7.1.1/bin/${component}.lib")
        endforeach()
        find_package(SDL2 CONFIG REQUIRED PATHS "${MYPLAYER_ROOT}/lib/SDL2/cmake" NO_DEFAULT_PATH)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/compat/soundtouch")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compat/soundtouch/SoundTouchDLL.h"
            "#include \"${MYPLAYER_ROOT}/lib/soundtouch-2.3.3/lib/SoundTouchDLL.h\"\n")
        add_library(SoundTouch::SoundTouchDLL SHARED IMPORTED)
        set_target_properties(SoundTouch::SoundTouchDLL PROPERTIES
            IMPORTED_IMPLIB "${MYPLAYER_ROOT}/lib/soundtouch-2.3.3/lib/SoundTouchDLL_x64.lib"
            IMPORTED_LOCATION "${MYPLAYER_ROOT}/lib/soundtouch-2.3.3/lib/SoundTouchDLL_x64.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/compat")
    else()
        find_package(FFMPEG REQUIRED)
        find_package(SDL2 CONFIG REQUIRED)
        find_package(SoundTouch CONFIG REQUIRED)
    endif()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Xml)
    qt_standard_project_setup()
    set(PLAYERDEMO_SIGSLOT_ROOT "${MYPLAYER_ROOT}/sdk/third_party/sigslot")
    include("${MYPLAYER_ROOT}/sdk/cmake/MyPlayerWarnings.cmake")
    add_subdirectory("${MYPLAYER_ROOT}/sdk/core" "${CMAKE_CURRENT_BINARY_DIR}/play_core")
else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Xml)
    qt_standard_project_setup()
    set(MYPLAYER_ROOT "${PROJECT_SOURCE_DIR}")
endif()

if(NOT TARGET PlayerSDK::QtWidgets)
    add_subdirectory("${MYPLAYER_ROOT}/sdk/qt" "${CMAKE_CURRENT_BINARY_DIR}/sdk-qt")
endif()

set(PLAYERDEMO_OUTPUT_DIR "${MYPLAYER_ROOT}/bin" CACHE PATH "Runtime output directory")

qt_add_executable(myplayer_monitor
    main.cpp
    myplayer_monitor.rc
    xviewer.cpp xviewer.h xviewer.ui
    onvif_camera.cpp onvif_camera.h
    xcamera_config.cpp xcamera_config.h
    xcamera_widget.cpp xcamera_widget.h
    xcamera_record.cpp xcamera_record.h
    xcalendar.cpp xcalendar.h
    xplayvideo.cpp xplayvideo.h xplayvideo.ui
    core_video_widget.cpp core_video_widget.h
)
qt_add_resources(myplayer_monitor monitor_icons
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        img/pause.png img/play.png img/cam.png img/close.png
        img/logo.png img/logo_150_40.png img/max.png img/min.png
        img/normal.png img/xv.ico
)
target_link_libraries(myplayer_monitor PRIVATE PlayerSDK::QtWidgets Qt6::Network Qt6::Xml)
target_include_directories(myplayer_monitor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(myplayer_monitor PRIVATE ${FFMPEG_LIBRARIES})
set_target_properties(myplayer_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}/$<0:>"
    OUTPUT_NAME_DEBUG "myplayer_monitor_debug"
    OUTPUT_NAME_RELEASE "myplayer_monitor"
)
if(MSVC)
    target_compile_options(myplayer_monitor PRIVATE /utf-8)
endif()
option(MONITOR_BUILD_TESTS "Build multi-channel playback and recording integration tests" OFF)
if(MONITOR_BUILD_TESTS)
    enable_testing()
    qt_add_executable(monitor_startup_tests monitor_startup_tests.cpp core_video_widget.cpp core_video_widget.h)
    target_link_libraries(monitor_startup_tests PRIVATE PlayerSDK::QtWidgets)
    if(MSVC)
        target_compile_options(monitor_startup_tests PRIVATE /utf-8)
    endif()
    add_test(NAME monitor_startup_tests COMMAND monitor_startup_tests)
    set_tests_properties(monitor_startup_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=codex-invalid-audio-driver")
    qt_add_executable(monitor_integration_tests
        monitor_integration_tests.cpp
        core_video_widget.cpp core_video_widget.h
        xcamera_record.cpp xcamera_record.h
        "${MYPLAYER_ROOT}/sdk/tests/media_fixture_builder.cpp"
    )
    target_include_directories(monitor_integration_tests PRIVATE "${MYPLAYER_ROOT}/sdk/tests")
    target_link_libraries(monitor_integration_tests PRIVATE PlayerSDK::QtWidgets ${FFMPEG_LIBRARIES})
    target_include_directories(monitor_integration_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(monitor_integration_tests PRIVATE /utf-8)
    endif()
    qt_add_executable(monitor_recording_browser_tests
        recording_browser_tests.cpp
        xviewer.cpp xviewer.h xviewer.ui
        onvif_camera.cpp onvif_camera.h
        xcamera_config.cpp xcamera_config.h
        xcamera_widget.cpp xcamera_widget.h
        xcamera_record.cpp xcamera_record.h
        xcalendar.cpp xcalendar.h
        xplayvideo.cpp xplayvideo.h xplayvideo.ui
        core_video_widget.cpp core_video_widget.h
    )
    target_link_libraries(monitor_recording_browser_tests PRIVATE PlayerSDK::QtWidgets Qt6::Network Qt6::Xml ${FFMPEG_LIBRARIES})
    target_include_directories(monitor_recording_browser_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(monitor_recording_browser_tests PRIVATE /utf-8)
    endif()
    add_test(NAME monitor_recording_browser_tests COMMAND monitor_recording_browser_tests)
    set_tests_properties(monitor_recording_browser_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")
    add_test(NAME monitor_integration_tests COMMAND monitor_integration_tests)
    add_test(NAME monitor_window_smoke_test COMMAND myplayer_monitor --smoke-test --layout 16)
    set_tests_properties(monitor_window_smoke_test PROPERTIES TIMEOUT 15)
    set_tests_properties(monitor_integration_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()

if(WIN32)
    set(monitor_runtime_targets myplayer_monitor)
    if(TARGET monitor_integration_tests)
        list(APPEND monitor_runtime_targets monitor_integration_tests monitor_recording_browser_tests)
    endif()
    get_target_property(qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin "${qt_qmake}" DIRECTORY)
    foreach(runtime_target IN LISTS monitor_runtime_targets)
        add_custom_command(TARGET ${runtime_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${runtime_target}> $<TARGET_FILE_DIR:${runtime_target}>
            COMMAND_EXPAND_LISTS
        )
        if(MONITOR_USE_BUNDLED_DEPS)
            foreach(component avcodec-61 avdevice-61 avfilter-10 avformat-61 avutil-59 swresample-5 swscale-8 postproc-58)
                add_custom_command(TARGET ${runtime_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MYPLAYER_ROOT}/lib/ffmpeg-7.1.1/bin/${component}.dll"
                        "$<TARGET_FILE_DIR:${runtime_target}>"
                )
            endforeach()
        endif()
        add_custom_command(TARGET ${runtime_target} POST_BUILD
            COMMAND "${qt_bin}/windeployqt.exe" "$<TARGET_FILE:${runtime_target}>" --no-translations --no-compiler-runtime
        )
    endforeach()
endif()
