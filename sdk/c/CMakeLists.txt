add_library(player_sdk_c SHARED src/player_c.cpp)
add_library(PlayerSDK::C ALIAS player_sdk_c)
set_target_properties(player_sdk_c PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    VERSION 1.0.0 SOVERSION 1
)
target_compile_features(player_sdk_c PRIVATE cxx_std_20)
target_compile_definitions(player_sdk_c PRIVATE PLAYER_SDK_C_BUILD)
target_include_directories(player_sdk_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(player_sdk_c PRIVATE PlayerSDK::Core)
# vcpkg's static FFmpeg is built with --enable-pic. Its x86 assembly also
# requires symbolic binding when incorporated into a Linux shared library.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET
    AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/libavcodec.a"
    AND NOT EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/libavcodec.so")
    target_link_options(player_sdk_c PRIVATE "LINKER:-Bsymbolic")
endif()
myplayer_enable_warnings(player_sdk_c)
myplayer_enable_sanitizers(player_sdk_c)

if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_property(TARGET player_sdk_c PROPERTY EXPORT_NAME C)
    add_custom_command(TARGET player_sdk_c POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DLINKER=${CMAKE_LINKER}"
            "-DDLL=$<TARGET_FILE:player_sdk_c>"
            "-DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/exports-v1.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-exports.cmake"
        VERBATIM
    )
    set_property(TARGET player_sdk_c APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/exports-v1.txt")

    install(TARGETS player_sdk_c EXPORT PlayerSDKTargets
        RUNTIME_DEPENDENCY_SET player_sdk_c_runtime
        CONFIGURATIONS Release
        RUNTIME DESTINATION bin COMPONENT PlayerSDK
        ARCHIVE DESTINATION lib COMPONENT PlayerSDK
    )
    install(CODE [[
        if(POLICY CMP0207)
            cmake_policy(SET CMP0207 NEW)
        endif()
    ]] CONFIGURATIONS Release COMPONENT PlayerSDK)
    install(RUNTIME_DEPENDENCY_SET player_sdk_c_runtime
        DIRECTORIES "$<TARGET_FILE_DIR:player_sdk_c>"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        PRE_EXCLUDE_REGEXES "^(api-ms-win-|ext-ms-win-)"
        POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]"
        RUNTIME DESTINATION bin CONFIGURATIONS Release COMPONENT PlayerSDK
    )
    install(EXPORT PlayerSDKTargets NAMESPACE PlayerSDK::
        DESTINATION lib/cmake/PlayerSDK CONFIGURATIONS Release COMPONENT PlayerSDK)
    install(DIRECTORY include/ DESTINATION include CONFIGURATIONS Release COMPONENT PlayerSDK)
    install(FILES PlayerSDKConfig.cmake DESTINATION lib/cmake/PlayerSDK
        CONFIGURATIONS Release COMPONENT PlayerSDK)
    install(FILES README.md ../LICENSE ../NOTICE ../THIRD-PARTY-NOTICES.md
        DESTINATION . CONFIGURATIONS Release COMPONENT PlayerSDK)
    install(FILES ../tests/api/player_c_smoke.c ../tests/api/player_c_layout.h
        DESTINATION examples CONFIGURATIONS Release COMPONENT PlayerSDK)
    if(IS_DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/"
            DESTINATION licenses/vcpkg CONFIGURATIONS Release COMPONENT PlayerSDK
            FILES_MATCHING PATTERN copyright)
    endif()
    install(FILES smoke-example.cmake DESTINATION examples RENAME CMakeLists.txt
        CONFIGURATIONS Release COMPONENT PlayerSDK)
endif()
