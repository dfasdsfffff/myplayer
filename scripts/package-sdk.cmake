# Run: cmake -DOUTPUT_DIR=<new empty path> -P scripts/package-sdk.cmake
cmake_minimum_required(VERSION 3.25)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR must name a new directory (will not overwrite an existing package)")
endif()
get_filename_component(OUTPUT_DIR "${OUTPUT_DIR}" ABSOLUTE)
if(EXISTS "${OUTPUT_DIR}")
    message(FATAL_ERROR "Refusing to overwrite ${OUTPUT_DIR}")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}/PlayerSDK")
set(package "${OUTPUT_DIR}/PlayerSDK")
file(COPY "${root}/play_core" "${root}/qt" "${root}/examples" "${root}/licenses"
    "${root}/LICENSE" "${root}/NOTICE" "${root}/THIRD-PARTY-NOTICES.md" "${root}/SDK_README.md" "${root}/SDK_CHANGELOG.md"
    DESTINATION "${package}")
file(MAKE_DIRECTORY "${package}/cmake" "${package}/lib" "${package}/tests")
file(COPY "${root}/cmake/PlayerSDK.cmake" "${root}/cmake/MyPlayerWarnings.cmake" DESTINATION "${package}/cmake")
configure_file("${root}/cmake/PlayerSDKRoot.cmake.in" "${package}/CMakeLists.txt" COPYONLY)
file(COPY "${root}/lib/sigslot-1.2.3" DESTINATION "${package}/lib"
    PATTERN ".git" EXCLUDE PATTERN "build" EXCLUDE)
file(COPY "${root}/tests/sdk" "${root}/tests/consumer" "${root}/tests/media_fixture_builder.cpp"
    "${root}/tests/media_fixture_builder.h" DESTINATION "${package}/tests")
file(COPY "${root}/SDK_README.md" DESTINATION "${package}")
configure_file("${root}/SDK_README.md" "${package}/README.md" COPYONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT_DIR}/PlayerSDK-1.0.0-source.zip" --format=zip PlayerSDK
    WORKING_DIRECTORY "${OUTPUT_DIR}" COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Source SDK: ${OUTPUT_DIR}/PlayerSDK-1.0.0-source.zip")
