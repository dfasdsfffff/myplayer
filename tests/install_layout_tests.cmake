if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(stage_dir "${BINARY_DIR}/install-layout-test")
file(REMOVE_RECURSE "${stage_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --config Release --prefix "${stage_dir}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Release install failed: ${install_result}")
endif()

foreach(required_path IN ITEMS
        "bin/myplayer.exe"
        "bin/avdevice-63.dll"
        "bin/Qt6Widgets.dll"
        "bin/platforms/qwindows.dll"
        "LICENSE"
        "NOTICE"
        "licenses")
    if(NOT EXISTS "${stage_dir}/${required_path}")
        message(FATAL_ERROR "missing staged release content: ${required_path}")
    endif()
endforeach()

file(GLOB_RECURSE debug_dlls "${stage_dir}/*d.dll")
if(debug_dlls)
    message(FATAL_ERROR "staged Release tree contains debug DLLs: ${debug_dlls}")
endif()
