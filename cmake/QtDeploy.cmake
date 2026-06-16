macro(qt_deploy)
    set(oneValueArgs TARGET COMPONENT)
    cmake_parse_arguments(QT_DEPLOY "" "${oneValueArgs}" "" ${ARGN})

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${QT_BINARY_DIR})
    message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

    get_filename_component(QT_DEPLOY_BIN_DIR "${WINDEPLOYQT_EXECUTABLE}" DIRECTORY)
    # Copy Qt runtime DLLs and plugins to the target's build directory.
    add_custom_command(TARGET ${QT_DEPLOY_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${QT_DEPLOY_BIN_DIR}"
        "${WINDEPLOYQT_EXECUTABLE}" --verbose 0 --no-compiler-runtime "$<TARGET_FILE:${QT_DEPLOY_TARGET}>"
        COMMENT "Deploying Qt..."
    )

    # Install Qt runtime DLLs and plugins to the target's install directory.
    if(Qt6_FOUND)
        qt_generate_deploy_app_script(TARGET ${QT_DEPLOY_TARGET}
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
            NO_COMPILER_RUNTIME
        )
        install(SCRIPT ${deploy_script} COMPONENT ${QT_DEPLOY_COMPONENT})
    else()
        install(CODE "
        message(STATUS CMAKE_INSTALL_PREFIX: $<INSTALL_PREFIX>)
        message(STATUS CMAKE_INSTALL_BINDIR: ${CMAKE_INSTALL_BINDIR})
        set(target_path $<INSTALL_PREFIX>/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:${QT_DEPLOY_TARGET}>)
        message(STATUS target_path: \${target_path})
        message(STATUS WINDEPLOYQT_EXECUTABLE: ${WINDEPLOYQT_EXECUTABLE})
        execute_process(COMMAND ${WINDEPLOYQT_EXECUTABLE} --verbose 1 --no-compiler-runtime \${target_path}
            )
        "
            COMPONENT ${QT_DEPLOY_COMPONENT}
        )
    endif()
endmacro()
