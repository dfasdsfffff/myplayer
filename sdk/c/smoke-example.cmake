cmake_minimum_required(VERSION 3.25)
project(PlayerSDKSmoke LANGUAGES C)
find_package(PlayerSDK CONFIG REQUIRED)
add_executable(player_sdk_smoke player_c_smoke.c)
set_target_properties(player_sdk_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_link_libraries(player_sdk_smoke PRIVATE PlayerSDK::C)
add_custom_command(TARGET player_sdk_smoke POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "$<TARGET_FILE_DIR:PlayerSDK::C>" "$<TARGET_FILE_DIR:player_sdk_smoke>"
    VERBATIM
)
