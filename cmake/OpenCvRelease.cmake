# Official prebuilt OpenCV Windows package for the OCR test.
#
# The release archive contains all OpenCV modules. The OCR test uses only
# core/imgproc APIs, while the official Windows package exposes them through
# the combined opencv_world library.

set(TALKINPUT_OPENCV_VERSION "5.0.0" CACHE STRING "OpenCV version")
set(TALKINPUT_OPENCV_ROOT "" CACHE PATH
    "OpenCV installation root containing build/include/opencv2")
set(TALKINPUT_OPENCV_PACKAGE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_parties/opencv")
set(TALKINPUT_OPENCV_PACKAGE
    "${TALKINPUT_OPENCV_PACKAGE_DIR}/opencv-${TALKINPUT_OPENCV_VERSION}-windows.exe")
set(TALKINPUT_OPENCV_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/third_parties/opencv")

if(NOT TALKINPUT_OPENCV_ROOT)
    set(TALKINPUT_OPENCV_ROOT
        "${TALKINPUT_OPENCV_EXTRACT_DIR}/opencv/opencv/build")
endif()

if(NOT EXISTS "${TALKINPUT_OPENCV_ROOT}/include/opencv2/core.hpp")
    if(NOT EXISTS "${TALKINPUT_OPENCV_PACKAGE}")
        file(MAKE_DIRECTORY "${TALKINPUT_OPENCV_PACKAGE_DIR}")
        set(TALKINPUT_OPENCV_URL
            "https://github.com/opencv/opencv/releases/download/${TALKINPUT_OPENCV_VERSION}/opencv-${TALKINPUT_OPENCV_VERSION}-windows.exe")
        message(STATUS "Downloading OpenCV ${TALKINPUT_OPENCV_VERSION} from ${TALKINPUT_OPENCV_URL}")
        file(DOWNLOAD "${TALKINPUT_OPENCV_URL}" "${TALKINPUT_OPENCV_PACKAGE}"
             SHOW_PROGRESS STATUS TALKINPUT_OPENCV_DOWNLOAD_STATUS)
        list(GET TALKINPUT_OPENCV_DOWNLOAD_STATUS 0 TALKINPUT_OPENCV_DOWNLOAD_CODE)
        if(NOT TALKINPUT_OPENCV_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "OpenCV download failed: ${TALKINPUT_OPENCV_DOWNLOAD_STATUS}")
        endif()
    endif()

    find_program(TALKINPUT_7Z_EXECUTABLE NAMES 7z 7za)
    if(NOT TALKINPUT_7Z_EXECUTABLE)
        message(FATAL_ERROR
            "OpenCV prebuilt package needs 7z for extraction. "
            "Install 7-Zip or set TALKINPUT_OPENCV_ROOT to an extracted package.")
    endif()

    file(MAKE_DIRECTORY "${TALKINPUT_OPENCV_EXTRACT_DIR}")
    execute_process(
        COMMAND "${TALKINPUT_7Z_EXECUTABLE}" x "${TALKINPUT_OPENCV_PACKAGE}"
                "-o${TALKINPUT_OPENCV_EXTRACT_DIR}/opencv" -y
        RESULT_VARIABLE TALKINPUT_OPENCV_EXTRACT_RESULT
        OUTPUT_QUIET
        ERROR_VARIABLE TALKINPUT_OPENCV_EXTRACT_ERROR)
    if(NOT TALKINPUT_OPENCV_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenCV extraction failed: ${TALKINPUT_OPENCV_EXTRACT_ERROR}")
    endif()
endif()

set(TALKINPUT_OPENCV_INCLUDE_DIR "${TALKINPUT_OPENCV_ROOT}/include")
set(TALKINPUT_OPENCV_LIB_DIR "${TALKINPUT_OPENCV_ROOT}/x64/vc17/lib")
set(TALKINPUT_OPENCV_BIN_DIR "${TALKINPUT_OPENCV_ROOT}/x64/vc17/bin")
if(NOT EXISTS "${TALKINPUT_OPENCV_LIB_DIR}/opencv_world500.lib")
    set(TALKINPUT_OPENCV_LIB_DIR "${TALKINPUT_OPENCV_ROOT}/x64/vc16/lib")
    set(TALKINPUT_OPENCV_BIN_DIR "${TALKINPUT_OPENCV_ROOT}/x64/vc16/bin")
endif()

set(TALKINPUT_OPENCV_WORLD_LIB "${TALKINPUT_OPENCV_LIB_DIR}/opencv_world500.lib")
set(TALKINPUT_OPENCV_WORLD_DLL "${TALKINPUT_OPENCV_BIN_DIR}/opencv_world500.dll")

if(EXISTS "${TALKINPUT_OPENCV_WORLD_LIB}" AND
   EXISTS "${TALKINPUT_OPENCV_WORLD_DLL}")
    add_library(talkinput_opencv_world SHARED IMPORTED GLOBAL)
    set_target_properties(talkinput_opencv_world PROPERTIES
        IMPORTED_IMPLIB "${TALKINPUT_OPENCV_WORLD_LIB}"
        IMPORTED_LOCATION "${TALKINPUT_OPENCV_WORLD_DLL}"
        INTERFACE_INCLUDE_DIRECTORIES "${TALKINPUT_OPENCV_INCLUDE_DIR}")

    set(TALKINPUT_OPENCV_FOUND TRUE)
    message(STATUS "OpenCV ${TALKINPUT_OPENCV_VERSION} ready: ${TALKINPUT_OPENCV_ROOT}")
else()
    message(WARNING "OpenCV world library not found under ${TALKINPUT_OPENCV_ROOT}")
endif()
