include("${CMAKE_CURRENT_LIST_DIR}/PlayerSDKBuildInfo.cmake")
if(NOT TARGET PlayerSDK::C)
    include("${CMAKE_CURRENT_LIST_DIR}/PlayerSDKTargets.cmake")
endif()
