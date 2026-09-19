if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(version_resource "${BINARY_DIR}/apps/qt_player/myplayer_version.rc")
if(NOT EXISTS "${version_resource}")
    message(FATAL_ERROR "generated Windows version resource is missing: ${version_resource}")
endif()

file(READ "${version_resource}" resource_contents)
foreach(required_value IN ITEMS
        "VS_VERSION_INFO"
        "FILEVERSION 1,0,0,0"
        "PRODUCTVERSION 1,0,0,0"
        "VALUE \"FileDescription\", \"MyPlayer\\0\""
        "VALUE \"FileVersion\", \"1.0.0\\0\"")
    string(FIND "${resource_contents}" "${required_value}" value_index)
    if(value_index EQUAL -1)
        message(FATAL_ERROR "version resource lacks required metadata: ${required_value}")
    endif()
endforeach()
