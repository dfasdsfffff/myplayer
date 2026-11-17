set(REVIEW_WORKSPACE_SOURCES
    review_timeline.cpp
    review_timeline.h
    review_playback_controller.cpp
    review_playback_controller.h
    review_settings.cpp
    review_settings.h
)

qt_add_executable(myplayer_review WIN32
    ${REVIEW_WORKSPACE_SOURCES}
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
    qt_add_executable(review_workspace_tests
        review_workspace_tests.cpp
        ${REVIEW_WORKSPACE_SOURCES}
        review_document.cpp
    )
    target_link_libraries(review_workspace_tests PRIVATE PlayerSDK::QtWidgets)
    myplayer_enable_warnings(review_workspace_tests)
    add_test(NAME review_workspace_tests COMMAND review_workspace_tests)
    set_tests_properties(review_workspace_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy"
    )
    qt_add_executable(review_document_tests
        review_document_tests.cpp
        review_document.cpp
        review_document.h
    )

    target_link_libraries(review_document_tests PRIVATE Qt6::Core)
    myplayer_enable_warnings(review_document_tests)
    add_test(NAME review_document_tests COMMAND review_document_tests)

    qt_add_executable(review_window_tests
        ${REVIEW_WORKSPACE_SOURCES}
        review_window_tests.cpp
        review_window.cpp
        review_window.h
        review_document.cpp
        ../../sdk/tests/media_fixture_builder.cpp
    )
    target_include_directories(review_window_tests PRIVATE
        ../../sdk/tests
        ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(review_window_tests PRIVATE PlayerSDK::QtWidgets ${FFMPEG_LIBRARIES})
    myplayer_enable_warnings(review_window_tests)
    add_test(NAME review_window_tests COMMAND review_window_tests)
    set_tests_properties(review_window_tests PROPERTIES
        TIMEOUT 45
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy"
    )
endif()

if(WIN32)
    get_target_property(qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin "${qt_qmake}" DIRECTORY)
    if(TARGET review_window_tests)
        add_custom_command(TARGET review_window_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:review_window_tests>
                $<TARGET_FILE_DIR:review_window_tests>
            COMMAND "${qt_bin}/windeployqt.exe" "$<TARGET_FILE:review_window_tests>" --no-translations --no-compiler-runtime
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${qt_bin}/../plugins/platforms" "$<TARGET_FILE_DIR:review_window_tests>/platforms"
            COMMAND_EXPAND_LISTS
        )
    endif()
    add_custom_command(TARGET myplayer_review POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:myplayer_review>
            $<TARGET_FILE_DIR:myplayer_review>
        COMMAND "${qt_bin}/windeployqt.exe" "$<TARGET_FILE:myplayer_review>" --no-translations --no-compiler-runtime
        COMMAND_EXPAND_LISTS
    )
endif()
