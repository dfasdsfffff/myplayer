if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

file(REMOVE_RECURSE "${BINARY_DIR}")

set(configure_command
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BINARY_DIR}"
    -DMYPLAYER_BUILD_QT_APP=OFF
    -DBUILD_TESTING=OFF)

if(DEFINED TOOLCHAIN_FILE)
    list(APPEND configure_command "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE result
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "core-only configure failed: ${result}")
endif()

file(READ "${BINARY_DIR}/CMakeCache.txt" cache_contents)
foreach(expected_cache_entry IN ITEMS
        "MYPLAYER_BUILD_QT_APP:BOOL=OFF"
        "BUILD_TESTING:BOOL=OFF")
    string(FIND "${cache_contents}" "${expected_cache_entry}" cache_entry_index)
    if(cache_entry_index EQUAL -1)
        message(FATAL_ERROR "missing cache entry: ${expected_cache_entry}")
    endif()
endforeach()
