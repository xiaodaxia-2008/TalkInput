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

include(cmake/QtDeploy.cmake)
include(cmake/SherpaOnnxRelease.cmake)
include(cmake/GitInfo.cmake)

add_subdirectory(Src)

target_compile_definitions(TalkInput PRIVATE
    GIT_COMMIT_ID="${GIT_COMMIT_ID}"
    GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
)

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
