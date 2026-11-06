add_executable(player_sdk_console core/main.cpp)
target_link_libraries(player_sdk_console PRIVATE PlayerSDK::Core)
if(TARGET PlayerSDK::QtWidgets)
    qt_add_executable(player_sdk_widgets widgets/main.cpp)
    target_link_libraries(player_sdk_widgets PRIVATE PlayerSDK::QtWidgets)
endif()
if(TARGET PlayerSDK::C)
    enable_language(C)
    add_executable(player_sdk_c_console c/main.c)
    set_target_properties(player_sdk_c_console PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(player_sdk_c_console PRIVATE PlayerSDK::C)
    if(WIN32)
        add_custom_command(TARGET player_sdk_c_console POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:player_sdk_c_console> $<TARGET_FILE_DIR:player_sdk_c_console>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
