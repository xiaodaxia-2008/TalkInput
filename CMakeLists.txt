cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

project(TalkInput VERSION 0.1.0 DESCRIPTION "Voice input method")

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Core Gui Multimedia Network Svg Sql LinguistTools)
find_package(LibArchive REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(QHotkey
    URL https://github.com/Skycoder42/QHotkey/archive/refs/tags/1.5.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt version to use (6)" FORCE)
set(QHOTKEY_INSTALL OFF CACHE BOOL "Disable install" FORCE)
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
FetchContent_MakeAvailable(QHotkey)

include(cmake/QtDeploy.cmake)
include(cmake/SherpaOnnxRelease.cmake)
include(cmake/GitInfo.cmake)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(src)

include(InstallRequiredSystemLibraries)

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "TalkInput")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_STRIP_FILES TRUE)

set(CPACK_NSIS_LANGUAGE "SimpChinese")
set(CPACK_NSIS_DISPLAY_NAME "TalkInput 语音输入法")
set(CPACK_NSIS_PACKAGE_NAME "TalkInput 语音输入法")
set(CPACK_NSIS_CONTACT "support@talkinput.local")
set(CPACK_NSIS_MODIFY_PATH ON)

set(CPACK_NSIS_MENU_LINKS
    "bin/TalkInput.exe" "TalkInput 语音输入法"
)

set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP\\\\TalkInput 语音输入法.lnk' '$INSTDIR\\\\bin\\\\TalkInput.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$DESKTOP\\\\TalkInput 语音输入法.lnk'"
)

include(CPack)
