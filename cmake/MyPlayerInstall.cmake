include(GNUInstallDirs)

function(myplayer_install_release_tree target)
    install(TARGETS ${target}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(WIN32)
        install(FILES $<TARGET_RUNTIME_DLLS:${target}>
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES "${MYPLAYER_QT_ROOT}/plugins/platforms/qwindows.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/platforms"
        )
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/NOTICE"
        "${PROJECT_SOURCE_DIR}/README.md"
        "${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        DESTINATION "."
    )
    install(FILES "${PROJECT_SOURCE_DIR}/lib/sigslot-1.2.3/LICENSE"
        DESTINATION "licenses"
        RENAME "sigslot-LICENSE"
    )
endfunction()
