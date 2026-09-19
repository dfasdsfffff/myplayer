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
    -DBUILD_TESTING=OFF
    -DVCPKG_MANIFEST_MODE=OFF
    "-DVCPKG_INSTALLED_DIR=${BINARY_DIR}/../vcpkg_installed")

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

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
foreach(required_runtime_contract IN ITEMS
        "get_property(myplayer_build_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)"
        "if(test_target MATCHES \"_tests$\")"
        "myplayer_set_test_runtime_dir(\${test_target})"
        "if(\"\${test_link_libraries}\" MATCHES \"Qt6::\")"
        "myplayer_deploy_runtime_dependencies(\${test_target})")
    string(FIND "${root_cmake}" "${required_runtime_contract}" runtime_contract_index)
    if(runtime_contract_index EQUAL -1)
        message(FATAL_ERROR "missing isolated test runtime contract: ${required_runtime_contract}")
    endif()
endforeach()

set(app_runtime_deploy_call_text [=[myplayer_deploy_runtime_dependencies(${PROJECT_NAME})]=])
string(FIND "${root_cmake}" "${app_runtime_deploy_call_text}" app_runtime_deploy_call)
if(app_runtime_deploy_call EQUAL -1)
    message(FATAL_ERROR "application target does not deploy its runtime dependencies")
endif()

string(FIND "${root_cmake}" "myplayer_deploy_runtime_dependencies(globalhelper_privacy_tests)" globalhelper_runtime_deploy_call)
if(globalhelper_runtime_deploy_call EQUAL -1)
    message(FATAL_ERROR "globalhelper privacy test does not deploy its runtime dependencies")
endif()
