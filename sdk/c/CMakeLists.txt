add_library(player_sdk_c SHARED src/player_c.cpp)
add_library(PlayerSDK::C ALIAS player_sdk_c)
set_target_properties(player_sdk_c PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    VERSION 1.0.0 SOVERSION 1
)
target_compile_features(player_sdk_c PRIVATE cxx_std_20)
target_compile_definitions(player_sdk_c PRIVATE PLAYER_SDK_C_BUILD)
target_include_directories(player_sdk_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(player_sdk_c PRIVATE PlayerSDK::Core)
myplayer_enable_warnings(player_sdk_c)
myplayer_enable_sanitizers(player_sdk_c)
