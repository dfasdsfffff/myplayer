add_executable(player_sdk_runtime_tests runtime_coexistence_tests.cpp)
target_link_libraries(player_sdk_runtime_tests PRIVATE PlayerSDK::Core SDL2::SDL2)
add_test(NAME player_sdk_runtime_tests COMMAND player_sdk_runtime_tests)
set_tests_properties(player_sdk_runtime_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "SDL_AUDIODRIVER=dummy")

add_executable(player_sdk_tests
    player_sdk_tests.cpp
    ../media_fixture_builder.cpp
)
target_link_libraries(player_sdk_tests PRIVATE PlayerSDK::Core)
target_include_directories(player_sdk_tests PRIVATE .. ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(player_sdk_tests PRIVATE ${FFMPEG_LIBRARIES})
if(MSVC)
    target_compile_options(player_sdk_tests PRIVATE /utf-8)
endif()
if(WIN32)
    add_custom_command(TARGET player_sdk_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:player_sdk_tests> $<TARGET_FILE_DIR:player_sdk_tests>
        COMMAND_EXPAND_LISTS
    )
endif()
add_test(NAME player_sdk_tests COMMAND player_sdk_tests)
set_tests_properties(player_sdk_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
if(TARGET PlayerSDK::QtWidgets)
    qt_add_executable(player_sdk_qt_tests qt_player_tests.cpp ../media_fixture_builder.cpp)
    target_include_directories(player_sdk_qt_tests PRIVATE .. ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(player_sdk_qt_tests PRIVATE PlayerSDK::QtWidgets SDL2::SDL2 ${FFMPEG_LIBRARIES})
    if(MSVC)
        target_compile_options(player_sdk_qt_tests PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
    if(WIN32)
        get_target_property(qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(qt_bin "${qmake}" DIRECTORY)
        add_custom_command(TARGET player_sdk_qt_tests POST_BUILD
            COMMAND "${qt_bin}/windeployqt.exe" "$<TARGET_FILE:player_sdk_qt_tests>" --no-translations --no-compiler-runtime
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${qt_bin}/../plugins/platforms" "$<TARGET_FILE_DIR:player_sdk_qt_tests>/platforms"
        )
    endif()
    add_test(NAME player_sdk_qt_tests COMMAND player_sdk_qt_tests)
    set_tests_properties(player_sdk_qt_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")
endif()


if(TARGET PlayerSDK::C)
    enable_language(C)
    add_executable(player_sdk_c_smoke player_c_smoke.c)
    set_target_properties(player_sdk_c_smoke PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        C_STANDARD 11 C_STANDARD_REQUIRED YES
    )
    target_link_libraries(player_sdk_c_smoke PRIVATE PlayerSDK::C)
    add_executable(player_sdk_c_tests player_c_tests.cpp ../media_fixture_builder.cpp)
    target_compile_features(player_sdk_c_tests PRIVATE cxx_std_20)
    target_include_directories(player_sdk_c_tests PRIVATE .. ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(player_sdk_c_tests PRIVATE PlayerSDK::C ${FFMPEG_LIBRARIES})
    if(MSVC)
        target_compile_options(player_sdk_c_tests PRIVATE /utf-8)
    endif()
    foreach(target player_sdk_c_smoke player_sdk_c_tests)
        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
                COMMAND_EXPAND_LISTS
            )
        endif()
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_AUDIODRIVER=dummy")
    endforeach()
endif()
