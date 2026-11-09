qt_add_executable(myplayer_review WIN32
    main.cpp
    review_window.cpp
    review_window.h
    review_document.cpp
    review_document.h
)

target_link_libraries(myplayer_review PRIVATE
    PlayerSDK::QtWidgets
)

set_target_properties(myplayer_review PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PLAYERDEMO_OUTPUT_DIR}/$<0:>"
    OUTPUT_NAME_DEBUG "myplayer_review_debug"
    OUTPUT_NAME_RELEASE "myplayer_review"
)
myplayer_enable_warnings(myplayer_review)
myplayer_enable_sanitizers(myplayer_review)

if(BUILD_TESTING)
    qt_add_executable(review_document_tests
        review_document_tests.cpp
        review_document.cpp
        review_document.h
    )

    target_link_libraries(review_document_tests PRIVATE Qt6::Core)
    myplayer_enable_warnings(review_document_tests)
    add_test(NAME review_document_tests COMMAND review_document_tests)
endif()

if(WIN32)
    get_target_property(qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin "${qt_qmake}" DIRECTORY)
    add_custom_command(TARGET myplayer_review POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:myplayer_review>
            $<TARGET_FILE_DIR:myplayer_review>
        COMMAND "${qt_bin}/windeployqt.exe" "$<TARGET_FILE:myplayer_review>" --no-translations --no-compiler-runtime
        COMMAND_EXPAND_LISTS
    )
endif()
