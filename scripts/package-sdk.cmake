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
file(COPY "${root}/sdk/" DESTINATION "${OUTPUT_DIR}/PlayerSDK"
    PATTERN ".git" EXCLUDE PATTERN "build" EXCLUDE PATTERN "CMakeUserPresets.json" EXCLUDE)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT_DIR}/PlayerSDK-1.0.0-source.zip" --format=zip PlayerSDK
    WORKING_DIRECTORY "${OUTPUT_DIR}" COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Source SDK: ${OUTPUT_DIR}/PlayerSDK-1.0.0-source.zip")
